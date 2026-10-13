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
 * Allwinner A733 thermal sensor controller.
 *
 * Five sensors behind one converter, which samples them on its own; the
 * driver only reads the latest codes. The register layout is the one the
 * H6 introduced. Calibration codes come from the EFUSE: each is what the
 * sensor returned at the factory test temperature.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <dev/sysmon/sysmonvar.h>

#include <dev/fdt/fdtvar.h>

#include <arm/sunxi/sunxi_sid.h>

#define	THS_CTRL		0x00
#define	 THS_CTRL_FS_DIV	__BITS(31,16)
#define	 THS_CTRL_T_ACQ		__BITS(15,0)
#define	THS_EN			0x04
#define	THS_PER			0x08
#define	 THS_PER_THERMAL	__BITS(31,12)
#define	THS_DATA_INTS		0x20
#define	THS_MFC			0x30
#define	 THS_MFC_FILTER_EN	__BIT(2)
#define	 THS_MFC_FILTER_TYPE	__BITS(1,0)
#define	THS_CDATA(n)		(0xa0 + 4 * ((n) / 2))
#define	 THS_CDATA_SHIFT(n)	(16 * ((n) % 2))
#define	THS_DATA(n)		(0xc0 + 4 * (n))
#define	 THS_CODE_MASK		0xfff

/* The timing the board vendor's kernel uses with the 24 MHz clock. */
#define	THS_FS_DIV		479
#define	THS_T_ACQ		47
#define	THS_PERIOD		28
#define	THS_FILTER_TYPE		1

#define	THS_NSENSORS		5
#define	THS_CDATA_DEFAULT	0x800

/*
 * Millidegrees Celsius from a code: two straight lines that meet near
 * 65 degC. A larger code is a lower temperature.
 */
#define	THS_CODE_KNEE		1769
#define	THS_COLD_OFFSET		2822
#define	THS_COLD_SCALE		62
#define	THS_HOT_OFFSET		2835
#define	THS_HOT_SCALE		59

/*
 * EFUSE, from byte 0x44: the factory test temperature in tenths of a
 * degree in the low 12 bits, then one 12-bit code per sensor.
 */
#define	THS_EFUSE_OFFSET	0x44
#define	THS_EFUSE_WORDS		3

/* The vendor's trip points for this chip. */
#define	THS_WARN_MC		90000
#define	THS_CRIT_MC		110000
/*
 * Between these two the processor clocks are held down: slowing starts
 * before the warning limit and ends once the chip has cooled off.
 */
#define	THS_THROTTLE_ON_MC	85000
#define	THS_THROTTLE_OFF_MC	75000

#define	THS_MC_TO_UK(mc)	((mc) * 1000 + 273150000)

static const char * const sun60i_a733_ths_names[THS_NSENSORS] = {
	"big cores",
	"memory",
	"NPU",
	"little cores",
	"GPU",
};

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "allwinner,sun60i-a733-ths" },
	DEVICE_COMPAT_EOL
};

struct sun60i_a733_ths_softc {
	device_t		sc_dev;
	bus_space_tag_t		sc_bst;
	bus_space_handle_t	sc_bsh;

	struct sysmon_envsys	*sc_sme;
	envsys_data_t		sc_data[THS_NSENSORS];

	callout_t		sc_tick;
	bool			sc_throttled;
	bool			sc_calibrated;
};

#define	RD4(sc, reg)		\
	bus_space_read_4((sc)->sc_bst, (sc)->sc_bsh, (reg))
#define	WR4(sc, reg, val)	\
	bus_space_write_4((sc)->sc_bst, (sc)->sc_bsh, (reg), (val))

static int
sun60i_a733_ths_to_mc(uint32_t code)
{
	if (code > THS_CODE_KNEE)
		return (THS_COLD_OFFSET - (int)code) * THS_COLD_SCALE;

	return (THS_HOT_OFFSET - (int)code) * THS_HOT_SCALE;
}

/*
 * Shift each sensor's code so that the code recorded at the factory
 * reads as the factory temperature. Returns the number of sensors
 * calibrated.
 */
static u_int
sun60i_a733_ths_calibrate(struct sun60i_a733_ths_softc *sc)
{
	uint32_t efuse[THS_EFUSE_WORDS], val;
	u_int n, done = 0;

	if (sunxi_sid_read(THS_EFUSE_OFFSET, efuse, THS_EFUSE_WORDS) != 0)
		return 0;

	const int test_mc = (efuse[0] & THS_CODE_MASK) * 100;
	if (test_mc == 0)
		return 0;

	for (n = 0; n < THS_NSENSORS; n++) {
		const u_int bit = 12 * (n + 1);
		uint32_t code = efuse[bit / 32] >> (bit % 32);

		if (bit % 32 > 20)
			code |= efuse[bit / 32 + 1] << (32 - bit % 32);
		code &= THS_CODE_MASK;

		const int cdata = THS_CDATA_DEFAULT -
		    (sun60i_a733_ths_to_mc(code) - test_mc) / THS_COLD_SCALE;
		if (cdata < 0 || cdata > THS_CODE_MASK)
			continue;

		val = RD4(sc, THS_CDATA(n));
		val &= ~(THS_CODE_MASK << THS_CDATA_SHIFT(n));
		val |= (uint32_t)cdata << THS_CDATA_SHIFT(n);
		WR4(sc, THS_CDATA(n), val);
		done++;
	}

	return done;
}

/* Index 5 is the hottest of all five sensors in this local binding. */
static int
sun60i_a733_ths_get_temp(device_t dev, const void *data, size_t len, int *mc)
{
	struct sun60i_a733_ths_softc * const sc = device_private(dev);
	const uint32_t *spec = data;
	uint32_t index, code;
	u_int first, last, n;
	int hottest = INT_MIN;

	if (len != sizeof(*spec) || data == NULL || mc == NULL)
		return EINVAL;
	index = be32toh(spec[0]);
	if (index > THS_NSENSORS)
		return EINVAL;
	if (!sc->sc_calibrated)
		return EIO;
	first = index == THS_NSENSORS ? 0 : index;
	last = index == THS_NSENSORS ? THS_NSENSORS : index + 1;
	for (n = first; n < last; n++) {
		code = RD4(sc, THS_DATA(n)) & THS_CODE_MASK;
		if (code == 0 || code == THS_CODE_MASK)
			return EIO;
		hottest = MAX(hottest, sun60i_a733_ths_to_mc(code));
	}
	*mc = hottest;
	return 0;
}

static const struct fdtbus_thermal_controller_func sun60i_a733_ths_funcs = {
	.get_temp = sun60i_a733_ths_get_temp,
};

static void
sun60i_a733_ths_refresh(struct sysmon_envsys *sme, envsys_data_t *edata)
{
	struct sun60i_a733_ths_softc * const sc = sme->sme_cookie;

	const uint32_t code = RD4(sc, THS_DATA(edata->private)) &
	    THS_CODE_MASK;
	if (code == 0) {
		/* No conversion has finished yet. */
		edata->state = ENVSYS_SINVALID;
		return;
	}

	edata->value_cur = THS_MC_TO_UK(sun60i_a733_ths_to_mc(code));
	edata->state = ENVSYS_SVALID;
}

/* Once a second: ask for slower clocks while the hottest sensor is hot. */
static void
sun60i_a733_ths_tick(void *arg)
{
	struct sun60i_a733_ths_softc * const sc = arg;
	int hottest = INT_MIN;
	u_int n;

	for (n = 0; n < THS_NSENSORS; n++) {
		const uint32_t code = RD4(sc, THS_DATA(n)) & THS_CODE_MASK;

		if (code != 0)
			hottest = MAX(hottest, sun60i_a733_ths_to_mc(code));
	}
	if (!sc->sc_throttled && hottest >= THS_THROTTLE_ON_MC) {
		sc->sc_throttled = true;
		pmf_event_inject(NULL, PMFE_THROTTLE_ENABLE);
	} else if (sc->sc_throttled && hottest != INT_MIN &&
	    hottest <= THS_THROTTLE_OFF_MC) {
		sc->sc_throttled = false;
		pmf_event_inject(NULL, PMFE_THROTTLE_DISABLE);
	}
	callout_schedule(&sc->sc_tick, hz);
}

static void
sun60i_a733_ths_get_limits(struct sysmon_envsys *sme, envsys_data_t *edata,
    sysmon_envsys_lim_t *lim, uint32_t *props)
{
	lim->sel_warnmax = THS_MC_TO_UK(THS_WARN_MC);
	lim->sel_critmax = THS_MC_TO_UK(THS_CRIT_MC);
	*props = PROP_WARNMAX | PROP_CRITMAX;
}

static int
sun60i_a733_ths_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
sun60i_a733_ths_attach(device_t parent, device_t self, void *aux)
{
	struct sun60i_a733_ths_softc * const sc = device_private(self);
	struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;
	struct fdtbus_reset *rst;
	struct clk *clk;
	bus_addr_t addr;
	bus_size_t size;
	u_int n;

	if (fdtbus_get_reg(phandle, 0, &addr, &size) != 0) {
		aprint_error(": couldn't get registers\n");
		return;
	}

	sc->sc_dev = self;
	sc->sc_bst = faa->faa_bst;
	if (bus_space_map(sc->sc_bst, addr, size, 0, &sc->sc_bsh) != 0) {
		aprint_error(": couldn't map registers\n");
		return;
	}

	for (n = 0; (clk = fdtbus_clock_get_index(phandle, n)) != NULL; n++) {
		if (clk_enable(clk) != 0) {
			aprint_error(": couldn't enable clock #%u\n", n);
			return;
		}
	}
	rst = fdtbus_reset_get_index(phandle, 0);
	if (rst == NULL || fdtbus_reset_deassert(rst) != 0) {
		aprint_error(": couldn't de-assert reset\n");
		return;
	}

	aprint_naive("\n");
	aprint_normal(": Thermal sensor controller\n");

	const u_int calibrated = sun60i_a733_ths_calibrate(sc);
	sc->sc_calibrated = calibrated == THS_NSENSORS;
	if (calibrated != THS_NSENSORS)
		aprint_error_dev(self,
		    "%u of %u sensors have no factory calibration\n",
		    THS_NSENSORS - calibrated, THS_NSENSORS);

	WR4(sc, THS_CTRL, __SHIFTIN(THS_FS_DIV, THS_CTRL_FS_DIV) |
	    __SHIFTIN(THS_T_ACQ, THS_CTRL_T_ACQ));
	WR4(sc, THS_MFC, THS_MFC_FILTER_EN |
	    __SHIFTIN(THS_FILTER_TYPE, THS_MFC_FILTER_TYPE));
	WR4(sc, THS_PER, __SHIFTIN(THS_PERIOD, THS_PER_THERMAL));
	WR4(sc, THS_DATA_INTS, __BITS(THS_NSENSORS - 1, 0));
	WR4(sc, THS_EN, __BITS(THS_NSENSORS - 1, 0));

	sc->sc_sme = sysmon_envsys_create();
	sc->sc_sme->sme_name = device_xname(self);
	sc->sc_sme->sme_cookie = sc;
	sc->sc_sme->sme_refresh = sun60i_a733_ths_refresh;
	sc->sc_sme->sme_get_limits = sun60i_a733_ths_get_limits;
	for (n = 0; n < THS_NSENSORS; n++) {
		sc->sc_data[n].private = n;
		sc->sc_data[n].units = ENVSYS_STEMP;
		sc->sc_data[n].state = ENVSYS_SINVALID;
		sc->sc_data[n].flags = ENVSYS_FMONLIMITS;
		strlcpy(sc->sc_data[n].desc, sun60i_a733_ths_names[n],
		    sizeof(sc->sc_data[n].desc));
		if (sysmon_envsys_sensor_attach(sc->sc_sme,
		    &sc->sc_data[n]) != 0)
			goto fail;
	}
	if (sysmon_envsys_register(sc->sc_sme) != 0)
		goto fail;

	if (fdtbus_register_thermal_controller(self, phandle,
	    &sun60i_a733_ths_funcs) != 0)
		aprint_error_dev(self, "couldn't register thermal provider\n");

	callout_init(&sc->sc_tick, CALLOUT_MPSAFE);
	callout_setfunc(&sc->sc_tick, sun60i_a733_ths_tick, sc);
	callout_schedule(&sc->sc_tick, hz);

	return;

fail:
	aprint_error_dev(self, "couldn't register sensors\n");
	sysmon_envsys_destroy(sc->sc_sme);
}

CFATTACH_DECL_NEW(sun60i_a733_ths, sizeof(struct sun60i_a733_ths_softc),
    sun60i_a733_ths_match, sun60i_a733_ths_attach, NULL, NULL);
