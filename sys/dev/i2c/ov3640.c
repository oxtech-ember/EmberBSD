/*
 * SPDX-License-Identifier: BSD-2-Clause AND Apache-2.0
 *
 * OV3640 startup sequence adapted from Espressif esp_cam_sensor v2.4.0
 * (Apache-2.0), repository commit 8fc931636ffa937c89cab53b53f4ce380190c1a4.
 * Registry archive SHA256:
 * b345bc4361b8106f05fe4c56ceb907600555b3f4de98e1983ec365733c45b1a6.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/gpio.h>
#include <sys/systm.h>

#include <dev/clk/clk.h>
#include <dev/fdt/fdtvar.h>
#include <dev/i2c/i2cvar.h>

#include "camera_sensor_power.h"
#include "ov3640_logic.h"

struct ov3640_softc {
	device_t sc_dev;
	i2c_tag_t sc_tag;
	i2c_addr_t sc_addr;
	int sc_phandle;
	struct camera_sensor_power sc_power;
	uint16_t sc_pid;
	bool sc_identified;
};

static int ov3640_match(device_t, cfdata_t, void *);
static void ov3640_attach(device_t, device_t, void *);
static void ov3640_init(device_t);
static int ov3640_read_reg(struct ov3640_softc *, uint16_t, uint8_t *);
static int ov3640_set_reset(void *, int);
static int ov3640_set_powerdown(void *, int);
static void ov3640_delay_us(void *, unsigned int);
static int ov3640_read_reg_cb(void *, uint16_t, uint8_t *);

CFATTACH_DECL_NEW(ov3640s, sizeof(struct ov3640_softc), ov3640_match,
	ov3640_attach, NULL, NULL);

static const struct device_compatible_entry ov3640_compat[] = {
	{ .compat = "ovti,ov3640" }, DEVICE_COMPAT_EOL
};

static int
ov3640_match(device_t parent, cfdata_t match, void *aux)
{
	const struct i2c_attach_args *ia = aux;

	return iic_compatible_match(ia, ov3640_compat);
}

static void
ov3640_attach(device_t parent, device_t self, void *aux)
{
	struct ov3640_softc *sc = device_private(self);
	const struct i2c_attach_args *ia = aux;

	sc->sc_dev = self;
	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;
	sc->sc_phandle = ia->ia_cookie;
	aprint_naive("\n");
	aprint_normal(": OV3640 sensor identification\n");
	config_interrupts(self, ov3640_init);
}

static int
ov3640_set_reset(void *cookie, int asserted)
{
	struct ov3640_softc *sc = cookie;

	/* FDT GPIO writes are logical; the GPIO provider applies ACTIVE_LOW. */
	fdtbus_gpio_write(sc->sc_power.reset, asserted);
	return 0;
}

static int
ov3640_set_powerdown(void *cookie, int asserted)
{
	struct ov3640_softc *sc = cookie;

	fdtbus_gpio_write(sc->sc_power.powerdown, asserted);
	return 0;
}

static void
ov3640_delay_us(void *cookie, unsigned int usec)
{
	delay(usec);
}

static int
ov3640_read_reg(struct ov3640_softc *sc, uint16_t reg, uint8_t *value)
{
	uint8_t command[2] = { reg >> 8, reg & 0xff };
	int error;

	error = iic_acquire_bus(sc->sc_tag, 0);
	if (error != 0)
		return error;
	error = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    command, sizeof(command), value, sizeof(*value), 0);
	iic_release_bus(sc->sc_tag, 0);
	return error;
}

static int
ov3640_read_reg_cb(void *cookie, uint16_t reg, uint8_t *value)
{
	return ov3640_read_reg(cookie, reg, value);
}

static void
ov3640_init(device_t self)
{
	struct ov3640_softc *sc = device_private(self);
	struct ov3640_sequence_ops ops = {
		.cookie = sc,
		.set_reset = ov3640_set_reset,
		.set_powerdown = ov3640_set_powerdown,
		.delay_us = ov3640_delay_us,
		.read_reg = ov3640_read_reg_cb,
	};
	struct clk *xclk;
	u_int xclk_rate;
	int error;

	/* Acquire shared controls and enable, but never cycle, the shared rail. */
	error = camera_sensor_power_acquire(sc->sc_phandle, &sc->sc_power);
	if (error != 0) {
		aprint_error_dev(self, "cannot acquire sensor controls/supply (%d); "
		    "any acquired reset/power-down outputs remain asserted\n", error);
		return;
	}
	/* This is an on-module always-on oscillator; the OS does not gate it. */
	xclk = fdtbus_clock_get(sc->sc_phandle, "xclk");
	if (xclk == NULL) {
		aprint_error_dev(self, "cannot acquire module XCLK description\n");
		return;
	}
	xclk_rate = clk_get_rate(xclk);
	clk_put(xclk);
	if (xclk_rate != 24000000) {
		aprint_error_dev(self, "unexpected module XCLK rate %u Hz\n",
		    xclk_rate);
		return;
	}

	error = ov3640_identify(&ops, &sc->sc_pid);
	if (error != 0) {
		if (error == ENODEV) {
			aprint_error_dev(self, "unrecognized sensor PID 0x%04x; "
			    "reset asserted and sensor in power-down\n", sc->sc_pid);
		} else {
			aprint_error_dev(self, "sensor identification failed (%d); "
			    "reset asserted and sensor in power-down\n", error);
		}
		return;
	}
	sc->sc_identified = true;
	aprint_normal_dev(self,
	    "sensor PID 0x%04x identified; DVP capture is not implemented\n",
	    sc->sc_pid);
}
