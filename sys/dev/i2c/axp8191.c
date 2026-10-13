/*-
 * Copyright (c) 2026 Anton and EmberBSD contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * X-Powers AXP8191 power management chip: the power key.
 *
 * The chip switches the board on by itself when the key is pressed, and
 * the firmware switches it off through PSCI. What is left to the system
 * is to notice a short press while it runs and shut down in an orderly
 * way. The chip's interrupt line goes to a controller that has no
 * driver here, so the status is polled.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/callout.h>
#include <sys/device.h>
#include <sys/kernel.h>

#include <dev/i2c/i2cvar.h>

#include <dev/sysmon/sysmonvar.h>
#include <dev/sysmon/sysmon_taskq.h>

#include <dev/fdt/fdtvar.h>

#define	AXP8191_IC_TYPE		0x03
#define	AXP8191_CHIP_ID		0x0e
#define	AXP8191_CHIP_VER	0x0f
#define	AXP8191_IRQ_ENABLE2	0x41
#define	AXP8191_IRQ_STATUS2	0x49
#define	 AXP8191_IRQ2_POK_SHORT	__BIT(4)	/* the key, pressed briefly */

#define	AXP8191_POLL_MS		500

/*
 * Regulators. Nine switching converters, two switches behind the first
 * of them and 28 linear regulators. A converter's voltage code runs
 * through up to four ranges with their own steps.
 */
#define	AXP8191_DCDC_CTL1	0x10	/* enable bits of DCDC1-8 */
#define	AXP8191_DCDC_CTL2	0x11	/* DCDC9 and the switches */
#define	AXP8191_DCDC_VOL(n)	(0x12 + (n))
#define	AXP8191_LDO_CTL(n)	(0x20 + (n) / NBBY)
#define	AXP8191_LDO_VOL(n)	(0x24 + (n))
#define	AXP8191_NRANGES		4
/* How fast an output follows a new setting, and a margin on top. */
#define	AXP8191_RAMP_UV_PER_US	250
#define	AXP8191_RAMP_MARGIN_US	200

struct axp8191_range {
	u_int			r_uv;		/* at the first code */
	uint8_t			r_first;
	uint8_t			r_last;
	u_int			r_step;
};

struct axp8191_ctrl {
	const char		*c_name;
	uint8_t			c_enable_reg;
	uint8_t			c_enable_mask;
	uint8_t			c_vol_reg;	/* 0: a switch */
	uint8_t			c_vol_mask;
	struct axp8191_range	c_ranges[AXP8191_NRANGES];
};

#define	AXP8191_LOW_RANGES							{ 500000, 0x00, 0x46, 10000 }, { 1220000, 0x47, 0x57, 20000 }
#define	AXP8191_DCDC(_n, ...)							{ .c_name = "dcdc" #_n,							  .c_enable_reg = (_n) < 9 ? AXP8191_DCDC_CTL1 : AXP8191_DCDC_CTL2, 	  .c_enable_mask = __BIT(((_n) - 1) % NBBY),				  .c_vol_reg = AXP8191_DCDC_VOL((_n) - 1), .c_vol_mask = 0x7f,		  .c_ranges = { __VA_ARGS__ } }
#define	AXP8191_SWITCH(_name, _bit)						{ .c_name = (_name), .c_enable_reg = AXP8191_DCDC_CTL2,		  .c_enable_mask = __BIT(_bit) }
/* _k counts the linear regulators through all five groups. */
#define	AXP8191_LDO(_name, _k, _mask, _last, _step)				{ .c_name = (_name), .c_enable_reg = AXP8191_LDO_CTL(_k),		  .c_enable_mask = __BIT((_k) % NBBY),					  .c_vol_reg = AXP8191_LDO_VOL(_k), .c_vol_mask = (_mask),		  .c_ranges = { { 500000, 0x00, (_last), (_step) } } }
#define	AXP8191_LDO_100MV(_name, _k)						AXP8191_LDO(_name, _k, 0x1f, 0x1d, 100000)
#define	AXP8191_LDO_25MV(_name, _k)						AXP8191_LDO(_name, _k, 0x3f, 0x28, 25000)

static const struct axp8191_ctrl axp8191_ctrls[] = {
	AXP8191_DCDC(1, { 1000000, 0x00, 0x1c, 100000 }),
	AXP8191_DCDC(2, AXP8191_LOW_RANGES),
	AXP8191_DCDC(3, AXP8191_LOW_RANGES),
	AXP8191_DCDC(4, AXP8191_LOW_RANGES),
	AXP8191_DCDC(5, AXP8191_LOW_RANGES),
	AXP8191_DCDC(6, AXP8191_LOW_RANGES, { 1800000, 0x58, 0x67, 20000 },
	    { 2440000, 0x68, 0x70, 40000 }),
	AXP8191_DCDC(7, AXP8191_LOW_RANGES),
	AXP8191_DCDC(8, AXP8191_LOW_RANGES, { 1900000, 0x58, 0x67, 100000 }),
	AXP8191_DCDC(9, AXP8191_LOW_RANGES, { 1900000, 0x58, 0x67, 100000 }),
	AXP8191_SWITCH("dc1sw1", 3),
	AXP8191_SWITCH("dc1sw2", 4),
	AXP8191_LDO_100MV("aldo1", 0),
	AXP8191_LDO_100MV("aldo2", 1),
	AXP8191_LDO_100MV("aldo3", 2),
	AXP8191_LDO_100MV("aldo4", 3),
	AXP8191_LDO_100MV("aldo5", 4),
	AXP8191_LDO_100MV("aldo6", 5),
	AXP8191_LDO_100MV("bldo1", 6),
	AXP8191_LDO_100MV("bldo2", 7),
	AXP8191_LDO_100MV("bldo3", 8),
	AXP8191_LDO_100MV("bldo4", 9),
	AXP8191_LDO_100MV("bldo5", 10),
	AXP8191_LDO_100MV("cldo1", 11),
	AXP8191_LDO_100MV("cldo2", 12),
	AXP8191_LDO_100MV("cldo3", 13),
	AXP8191_LDO_100MV("cldo4", 14),
	AXP8191_LDO_100MV("cldo5", 15),
	AXP8191_LDO_100MV("dldo1", 16),
	AXP8191_LDO_100MV("dldo2", 17),
	AXP8191_LDO_100MV("dldo3", 18),
	AXP8191_LDO_100MV("dldo4", 19),
	AXP8191_LDO_100MV("dldo5", 20),
	AXP8191_LDO_100MV("dldo6", 21),
	AXP8191_LDO_25MV("eldo1", 22),
	AXP8191_LDO_25MV("eldo2", 23),
	AXP8191_LDO_25MV("eldo3", 24),
	AXP8191_LDO_25MV("eldo4", 25),
	AXP8191_LDO_25MV("eldo5", 26),
	AXP8191_LDO_25MV("eldo6", 27),
};

struct axp8191_softc {
	device_t		sc_dev;
	i2c_tag_t		sc_i2c;
	i2c_addr_t		sc_addr;

	struct sysmon_pswitch	sc_smpsw;
	callout_t		sc_tick;
};

struct axp8191reg_softc {
	device_t		sc_dev;
	struct axp8191_softc	*sc_pmic;
	const struct axp8191_ctrl *sc_ctrl;
	/* Limits from the device tree; zero when it gives none. */
	u_int			sc_min_uvol;
	u_int			sc_max_uvol;
};

struct axp8191reg_attach_args {
	const struct axp8191_ctrl *reg_ctrl;
	int			reg_phandle;
};

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "x-powers,axp8191" },
	DEVICE_COMPAT_EOL
};

static int
axp8191_read(struct axp8191_softc *sc, uint8_t reg, uint8_t *val)
{
	int error;

	if ((error = iic_acquire_bus(sc->sc_i2c, 0)) != 0)
		return error;
	error = iic_smbus_read_byte(sc->sc_i2c, sc->sc_addr, reg, val, 0);
	iic_release_bus(sc->sc_i2c, 0);

	return error;
}

static int
axp8191_write(struct axp8191_softc *sc, uint8_t reg, uint8_t val)
{
	int error;

	if ((error = iic_acquire_bus(sc->sc_i2c, 0)) != 0)
		return error;
	error = iic_smbus_write_byte(sc->sc_i2c, sc->sc_addr, reg, val, 0);
	iic_release_bus(sc->sc_i2c, 0);

	return error;
}

/* The bus sleeps, so the status is read from the sysmon task queue. */
static void
axp8191_poll(void *arg)
{
	struct axp8191_softc * const sc = arg;
	uint8_t status;

	if (axp8191_read(sc, AXP8191_IRQ_STATUS2, &status) == 0 &&
	    (status & AXP8191_IRQ2_POK_SHORT) != 0) {
		/* A status bit is cleared by writing it back. */
		(void)axp8191_write(sc, AXP8191_IRQ_STATUS2,
		    AXP8191_IRQ2_POK_SHORT);
		sysmon_pswitch_event(&sc->sc_smpsw, PSWITCH_EVENT_PRESSED);
	}

	callout_schedule(&sc->sc_tick, mstohz(AXP8191_POLL_MS));
}

static void
axp8191_tick(void *arg)
{
	sysmon_task_queue_sched(0, axp8191_poll, arg);
}

static int
axp8191reg_acquire(device_t dev)
{
	return 0;
}

static void
axp8191reg_release(device_t dev)
{
}

static int
axp8191reg_enable(device_t dev, bool enable)
{
	struct axp8191reg_softc * const sc = device_private(dev);
	const struct axp8191_ctrl *c = sc->sc_ctrl;
	uint8_t val;
	int error;

	if ((error = axp8191_read(sc->sc_pmic, c->c_enable_reg, &val)) != 0)
		return error;
	if (enable)
		val |= c->c_enable_mask;
	else
		val &= ~c->c_enable_mask;

	return axp8191_write(sc->sc_pmic, c->c_enable_reg, val);
}

/* Origin: EmberBSD; read the existing regulator enable bit without writes. */
static int
axp8191reg_is_enabled(device_t dev, bool *enabled)
{
	struct axp8191reg_softc * const sc = device_private(dev);
	const struct axp8191_ctrl *c = sc->sc_ctrl;
	uint8_t val;
	int error;

	error = axp8191_read(sc->sc_pmic, c->c_enable_reg, &val);
	if (error != 0)
		return error;

	*enabled = (val & c->c_enable_mask) != 0;
	return 0;
}

static int
axp8191reg_get_voltage(device_t dev, u_int *uvol)
{
	struct axp8191reg_softc * const sc = device_private(dev);
	const struct axp8191_ctrl *c = sc->sc_ctrl;
	uint8_t val;
	int error;
	u_int i;

	if (c->c_vol_reg == 0)
		return ENXIO;
	if ((error = axp8191_read(sc->sc_pmic, c->c_vol_reg, &val)) != 0)
		return error;
	val &= c->c_vol_mask;
	for (i = 0; i < AXP8191_NRANGES; i++) {
		const struct axp8191_range *r = &c->c_ranges[i];

		if (r->r_step != 0 && val >= r->r_first && val <= r->r_last) {
			*uvol = r->r_uv + (val - r->r_first) * r->r_step;
			return 0;
		}
	}

	return EIO;
}

/* The lowest voltage the regulator offers within the limits. */
static int
axp8191reg_set_voltage(device_t dev, u_int min_uvol, u_int max_uvol)
{
	struct axp8191reg_softc * const sc = device_private(dev);
	const struct axp8191_ctrl *c = sc->sc_ctrl;
	uint8_t val;
	int error;
	u_int i, old_uvol, new_uvol;

	if (c->c_vol_reg == 0)
		return ENXIO;
	if (sc->sc_max_uvol != 0 &&
	    (min_uvol < sc->sc_min_uvol || min_uvol > sc->sc_max_uvol))
		return ERANGE;
	if ((error = axp8191reg_get_voltage(dev, &old_uvol)) != 0)
		return error;
	for (i = 0; i < AXP8191_NRANGES; i++) {
		const struct axp8191_range *r = &c->c_ranges[i];
		const u_int last = r->r_uv +
		    (r->r_last - r->r_first) * r->r_step;
		u_int code = r->r_first;

		if (r->r_step == 0 || min_uvol > last)
			continue;
		if (min_uvol > r->r_uv)
			code += howmany(min_uvol - r->r_uv, r->r_step);
		new_uvol = r->r_uv + (code - r->r_first) * r->r_step;
		if (new_uvol > max_uvol)
			return ERANGE;

		error = axp8191_read(sc->sc_pmic, c->c_vol_reg, &val);
		if (error != 0)
			return error;
		val = (val & ~c->c_vol_mask) | code;
		error = axp8191_write(sc->sc_pmic, c->c_vol_reg, val);
		/* A consumer about to draw more must find the voltage up. */
		if (error == 0 && new_uvol > old_uvol)
			delay(howmany(new_uvol - old_uvol,
			    AXP8191_RAMP_UV_PER_US) + AXP8191_RAMP_MARGIN_US);
		return error;
	}

	return ERANGE;
}

static const struct fdtbus_regulator_controller_func axp8191reg_funcs = {
	.acquire = axp8191reg_acquire,
	.release = axp8191reg_release,
	.enable = axp8191reg_enable,
	.set_voltage = axp8191reg_set_voltage,
	.get_voltage = axp8191reg_get_voltage,
	.is_enabled = axp8191reg_is_enabled,
};

static int
axp8191reg_match(device_t parent, cfdata_t match, void *aux)
{
	return 1;
}

/*
 * A regulator is registered as the boot loader left it: nothing is
 * switched and no voltage is moved until a consumer asks.
 */
static void
axp8191reg_attach(device_t parent, device_t self, void *aux)
{
	struct axp8191reg_softc * const sc = device_private(self);
	struct axp8191reg_attach_args * const aaa = aux;
	const struct axp8191_ctrl *c = aaa->reg_ctrl;
	uint8_t val = 0;
	u_int uvol;

	sc->sc_dev = self;
	sc->sc_pmic = device_private(parent);
	sc->sc_ctrl = c;
	if (of_getprop_uint32(aaa->reg_phandle, "regulator-min-microvolt",
	    &sc->sc_min_uvol) != 0 ||
	    of_getprop_uint32(aaa->reg_phandle, "regulator-max-microvolt",
	    &sc->sc_max_uvol) != 0)
		sc->sc_min_uvol = sc->sc_max_uvol = 0;

	(void)axp8191_read(sc->sc_pmic, c->c_enable_reg, &val);
	const bool on = (val & c->c_enable_mask) != 0;

	aprint_naive("\n");
	if (axp8191reg_get_voltage(self, &uvol) == 0)
		aprint_normal(": %s, %u.%03u V, %s\n", c->c_name,
		    uvol / 1000000, uvol / 1000 % 1000, on ? "on" : "off");
	else
		aprint_normal(": %s, %s\n", c->c_name, on ? "on" : "off");

	fdtbus_register_regulator_controller(self, aaa->reg_phandle,
	    &axp8191reg_funcs);
}

static int
axp8191_match(device_t parent, cfdata_t match, void *aux)
{
	struct i2c_attach_args * const ia = aux;
	int match_result;

	if (iic_use_direct_match(ia, match, compat_data, &match_result))
		return match_result;

	return 0;
}

static void
axp8191_attach(device_t parent, device_t self, void *aux)
{
	struct axp8191_softc * const sc = device_private(self);
	struct i2c_attach_args * const ia = aux;
	struct axp8191reg_attach_args aaa;
	uint8_t type, id, ver, enable;
	int regulators, child;
	u_int i;

	sc->sc_dev = self;
	sc->sc_i2c = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	aprint_naive("\n");
	if (axp8191_read(sc, AXP8191_IC_TYPE, &type) != 0 ||
	    axp8191_read(sc, AXP8191_CHIP_ID, &id) != 0 ||
	    axp8191_read(sc, AXP8191_CHIP_VER, &ver) != 0) {
		aprint_error(": the chip does not answer\n");
		return;
	}
	aprint_normal(": AXP8191, type 0x%02x, ID 0x%02x, version 0x%02x\n",
	    type, id, ver);

	regulators = of_find_firstchild_byname(ia->ia_cookie, "regulators");
	for (i = 0; regulators > 0 && i < __arraycount(axp8191_ctrls); i++) {
		child = of_find_firstchild_byname(regulators,
		    axp8191_ctrls[i].c_name);
		if (child <= 0)
			continue;
		aaa.reg_ctrl = &axp8191_ctrls[i];
		aaa.reg_phandle = child;
		config_found(self, &aaa, NULL, CFARGS_NONE);
	}

	/* Let the chip record a short press and drop what is recorded. */
	if (axp8191_read(sc, AXP8191_IRQ_ENABLE2, &enable) != 0 ||
	    axp8191_write(sc, AXP8191_IRQ_ENABLE2,
	    enable | AXP8191_IRQ2_POK_SHORT) != 0 ||
	    axp8191_write(sc, AXP8191_IRQ_STATUS2,
	    AXP8191_IRQ2_POK_SHORT) != 0) {
		aprint_error_dev(self, "couldn't set up the power key\n");
		return;
	}

	sysmon_task_queue_init();
	sc->sc_smpsw.smpsw_name = device_xname(self);
	sc->sc_smpsw.smpsw_type = PSWITCH_TYPE_POWER;
	if (sysmon_pswitch_register(&sc->sc_smpsw) != 0) {
		aprint_error_dev(self, "couldn't register the power key\n");
		return;
	}

	callout_init(&sc->sc_tick, CALLOUT_MPSAFE);
	callout_setfunc(&sc->sc_tick, axp8191_tick, sc);
	callout_schedule(&sc->sc_tick, mstohz(AXP8191_POLL_MS));
}

CFATTACH_DECL_NEW(axp8191pm, sizeof(struct axp8191_softc),
    axp8191_match, axp8191_attach, NULL, NULL);

CFATTACH_DECL_NEW(axp8191reg, sizeof(struct axp8191reg_softc),
    axp8191reg_match, axp8191reg_attach, NULL, NULL);
