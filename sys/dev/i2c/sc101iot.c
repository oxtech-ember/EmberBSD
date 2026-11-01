/*
 * SPDX-License-Identifier: BSD-2-Clause AND Apache-2.0
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * Identification-only driver for SmartSens SC101IOT DVP sensors. Its SCCB
 * page-select and ID protocol follows Espressif esp_cam_sensor v2.4.0,
 * esp-video-components commit 8fc931636ffa937c89cab53b53f4ce380190c1a4
 * (Apache-2.0).
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/gpio.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>
#include <dev/i2c/i2cvar.h>

#include "camera_sensor_power.h"
#include "sc101iot_logic.h"

struct sc101iot_softc {
	device_t sc_dev;
	i2c_tag_t sc_tag;
	i2c_addr_t sc_addr;
	int sc_phandle;
	struct camera_sensor_power sc_power;
	uint16_t sc_pid;
	bool sc_identified;
};

static int sc101iot_match(device_t, cfdata_t, void *);
static void sc101iot_attach(device_t, device_t, void *);
static void sc101iot_init(device_t);
static int sc101iot_set_reset(void *, int);
static int sc101iot_set_powerdown(void *, int);
static void sc101iot_delay_us(void *, unsigned int);
static int sc101iot_read_reg(struct sc101iot_softc *, uint16_t, uint8_t *);
static int sc101iot_read_reg_cb(void *, uint16_t, uint8_t *);

CFATTACH_DECL_NEW(sc101iots, sizeof(struct sc101iot_softc), sc101iot_match,
	sc101iot_attach, NULL, NULL);

static const struct device_compatible_entry sc101iot_compat[] = {
	{ .compat = "smartsens,sc101iot" }, DEVICE_COMPAT_EOL
};

static int
sc101iot_match(device_t parent, cfdata_t match, void *aux)
{
	const struct i2c_attach_args *ia = aux;

	return iic_compatible_match(ia, sc101iot_compat);
}

static void
sc101iot_attach(device_t parent, device_t self, void *aux)
{
	struct sc101iot_softc *sc = device_private(self);
	const struct i2c_attach_args *ia = aux;

	sc->sc_dev = self;
	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;
	sc->sc_phandle = ia->ia_cookie;
	aprint_naive("\n");
	aprint_normal(": SC101IOT sensor identification\n");
	config_interrupts(self, sc101iot_init);
}

static int
sc101iot_set_reset(void *cookie, int asserted)
{
	struct sc101iot_softc *sc = cookie;

	/* The FDT GPIO layer applies active-low polarity to logical values. */
	fdtbus_gpio_write(sc->sc_power.reset, asserted);
	return 0;
}

static int
sc101iot_set_powerdown(void *cookie, int asserted)
{
	struct sc101iot_softc *sc = cookie;

	fdtbus_gpio_write(sc->sc_power.powerdown, asserted);
	return 0;
}

static void
sc101iot_delay_us(void *cookie, unsigned int usec)
{
	(void)cookie;
	delay(usec);
}

static int
sc101iot_read_reg(struct sc101iot_softc *sc, uint16_t reg, uint8_t *value)
{
	uint8_t page_command[2] = { 0xf0, reg >> 8 };
	uint8_t register_command = reg & 0xff;
	int error;

	error = iic_acquire_bus(sc->sc_tag, 0);
	if (error != 0)
		return error;
	error = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
	    page_command, sizeof(page_command), NULL, 0, 0);
	if (error == 0) {
		error = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
		    &register_command, sizeof(register_command), value,
		    sizeof(*value), 0);
	}
	iic_release_bus(sc->sc_tag, 0);
	return error;
}

static int
sc101iot_read_reg_cb(void *cookie, uint16_t reg, uint8_t *value)
{
	return sc101iot_read_reg(cookie, reg, value);
}

static void
sc101iot_init(device_t self)
{
	struct sc101iot_softc *sc = device_private(self);
	struct sc101iot_sequence_ops ops = {
		.cookie = sc,
		.set_reset = sc101iot_set_reset,
		.set_powerdown = sc101iot_set_powerdown,
		.delay_us = sc101iot_delay_us,
		.read_reg = sc101iot_read_reg_cb,
	};
	int error;

	error = camera_sensor_power_acquire(sc->sc_phandle, &sc->sc_power);
	if (error != 0) {
		aprint_error_dev(self, "cannot acquire sensor controls/supply (%d); "
		    "any acquired reset/power-down outputs remain asserted\n", error);
		return;
	}

	error = sc101iot_identify(&ops, &sc->sc_pid);
	if (error != 0) {
		if (error == ENODEV) {
			aprint_error_dev(self, "unrecognized sensor PID 0x%04x; "
			    "reset asserted and sensor in power-down\n", sc->sc_pid);
		} else {
			aprint_error_dev(self, "sensor identification failed (%d); "
			    "reset asserted and sensor in power-down\n", error);
		}
		camera_sensor_power_release_flash(&sc->sc_power);
		return;
	}
	sc->sc_identified = true;
	aprint_normal_dev(self, "sensor PID 0x%04x identified; "
	    "DVP capture is not implemented\n", sc->sc_pid);
}
