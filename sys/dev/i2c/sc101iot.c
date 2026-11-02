/*
 * SPDX-License-Identifier: BSD-2-Clause AND Apache-2.0
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * Driver for SmartSens SC101IOT DVP sensors. Its SCCB page-select and ID
 * protocol follows Espressif esp_cam_sensor v2.4.0,
 * esp-video-components commit 8fc931636ffa937c89cab53b53f4ce380190c1a4
 * (Apache-2.0).
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/gpio.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>
#include <dev/fdt/fdt_port.h>
#include <dev/i2c/i2cvar.h>
#include <dev/video_if.h>
#include <dev/video_sensor_if.h>

#include "camera_sensor_power.h"
#include "sc101iot_logic.h"
#include "sc101iot_mode.h"

struct sc101iot_softc {
	device_t sc_dev;
	i2c_tag_t sc_tag;
	i2c_addr_t sc_addr;
	int sc_phandle;
	struct fdt_device_ports sc_ports;
	struct video_sensor sc_sensor;
	struct video_format sc_format;
	struct camera_sensor_power sc_power;
	kmutex_t sc_lock;
	uint16_t sc_pid;
	bool sc_identified;
	bool sc_configured;
	bool sc_streaming;
};

static int sc101iot_match(device_t, cfdata_t, void *);
static void sc101iot_attach(device_t, device_t, void *);
static void sc101iot_init(device_t);
static int sc101iot_set_reset(void *, int);
static int sc101iot_set_powerdown(void *, int);
static void sc101iot_delay_us(void *, unsigned int);
static int sc101iot_read_reg(struct sc101iot_softc *, uint16_t, uint8_t *);
static int sc101iot_write_reg(struct sc101iot_softc *, uint16_t, uint8_t);
static int sc101iot_read_reg_cb(void *, uint16_t, uint8_t *);
static void *sc101iot_ep_get_data(device_t, struct fdt_endpoint *);
static int sc101iot_get_format(void *, struct video_format *);
static int sc101iot_try_format(void *, struct video_format *);
static int sc101iot_try_format_locked(void *, struct video_format *);
static int sc101iot_default_format(struct video_format *);
static int sc101iot_set_format(void *, const struct video_format *);
static int sc101iot_set_stream(void *, bool);
static int sc101iot_power_up(struct sc101iot_softc *);
static struct sc101iot_sequence_ops sc101iot_sequence_ops(
    struct sc101iot_softc *);
static int sc101iot_write_mode(struct sc101iot_softc *);
static int sc101iot_write_reg_locked(struct sc101iot_softc *, uint8_t,
    uint8_t);

CFATTACH_DECL_NEW(sc101iots, sizeof(struct sc101iot_softc), sc101iot_match,
	sc101iot_attach, NULL, NULL);

static const struct device_compatible_entry sc101iot_compat[] = {
	{ .compat = "smartsens,sc101iot" }, DEVICE_COMPAT_EOL
};

static const struct video_sensor_ops sc101iot_sensor_ops = {
	.get_format = sc101iot_get_format,
	.try_format = sc101iot_try_format,
	.set_format = sc101iot_set_format,
	.set_stream = sc101iot_set_stream,
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
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NONE);
	/* Publish mode metadata before deferred sensor identification. */
	if (sc101iot_default_format(&sc->sc_format) != 0) {
		aprint_error_dev(self, "cannot initialize default video mode\n");
		return;
	}
	sc->sc_sensor.ops = &sc101iot_sensor_ops;
	sc->sc_sensor.cookie = sc;
	sc->sc_ports.dp_ep_get_data = sc101iot_ep_get_data;
	fdt_ports_register(&sc->sc_ports, self, sc->sc_phandle,
	    EP_VIDEO_SENSOR);
	aprint_naive("\n");
	aprint_normal(": SC101IOT video sensor\n");
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
sc101iot_write_reg(struct sc101iot_softc *sc, uint16_t reg, uint8_t value)
{
	uint8_t page_command[2] = { 0xf0, reg >> 8 };
	int error;

	error = iic_acquire_bus(sc->sc_tag, 0);
	if (error != 0)
		return error;
	error = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
	    page_command, sizeof(page_command), NULL, 0, 0);
	if (error == 0)
		error = sc101iot_write_reg_locked(sc, reg & 0xff, value);
	iic_release_bus(sc->sc_tag, 0);
	return error;
}

static int
sc101iot_read_reg_cb(void *cookie, uint16_t reg, uint8_t *value)
{
	return sc101iot_read_reg(cookie, reg, value);
}

static int
sc101iot_write_reg_locked(struct sc101iot_softc *sc, uint8_t reg, uint8_t val)
{
	uint8_t command[2] = { reg, val };

	return iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
	    command, sizeof(command), NULL, 0, 0);
}

static int
sc101iot_power_up(struct sc101iot_softc *sc)
{
	struct sc101iot_sequence_ops ops = sc101iot_sequence_ops(sc);

	return sc101iot_power_on(&ops);
}

static struct sc101iot_sequence_ops
sc101iot_sequence_ops(struct sc101iot_softc *sc)
{
	return (struct sc101iot_sequence_ops) {
		.cookie = sc,
		.set_reset = sc101iot_set_reset,
		.set_powerdown = sc101iot_set_powerdown,
		.delay_us = sc101iot_delay_us,
		.read_reg = sc101iot_read_reg_cb,
	};
}

static int
sc101iot_write_mode(struct sc101iot_softc *sc)
{
	int error;

	error = iic_acquire_bus(sc->sc_tag, 0);
	if (error != 0)
		return error;
	for (size_t i = 0; i < __arraycount(sc101iot_mode_640x480_uyvy); i++) {
		const struct sc101iot_reginfo *ri =
		    &sc101iot_mode_640x480_uyvy[i];

		error = sc101iot_write_reg_locked(sc, ri->reg, ri->val);
		if (error != 0)
			break;
	}
	iic_release_bus(sc->sc_tag, 0);
	return error;
}

static int
sc101iot_get_format(void *cookie, struct video_format *format)
{
	struct sc101iot_softc *sc = cookie;

	mutex_enter(&sc->sc_lock);
	*format = sc->sc_format;
	mutex_exit(&sc->sc_lock);
	return 0;
}

static int
sc101iot_try_format(void *cookie, struct video_format *format)
{
	struct sc101iot_softc *sc = cookie;
	int error;

	mutex_enter(&sc->sc_lock);
	error = sc101iot_try_format_locked(cookie, format);
	mutex_exit(&sc->sc_lock);
	return error;
}

static int
sc101iot_try_format_locked(void *cookie, struct video_format *format)
{
	size_t stride, size;
	int error;

	(void)cookie;
	if (format->pixel_format != VIDEO_FORMAT_UYVY)
		return ENOTSUP;
	error = sc101iot_mode_layout(format->width, format->height,
	    &stride, &size);
	if (error != 0)
		return error;
	format->stride = stride;
	format->sample_size = size;
	format->priv = 0;
	return 0;
}

static int
sc101iot_default_format(struct video_format *format)
{
	*format = (struct video_format) {
		.pixel_format = VIDEO_FORMAT_UYVY,
		.width = 640,
		.height = 480,
	};
	return sc101iot_try_format_locked(NULL, format);
}

static int
sc101iot_set_format(void *cookie, const struct video_format *requested)
{
	struct sc101iot_softc *sc = cookie;
	struct video_format format = *requested;
	int error;

	mutex_enter(&sc->sc_lock);
	if (!sc->sc_identified)
		error = ENXIO;
	else if (sc->sc_streaming)
		error = EBUSY;
	else
		error = sc101iot_try_format_locked(sc, &format);
	if (error != 0) {
		mutex_exit(&sc->sc_lock);
		return error;
	}
	/* Any configuration attempt invalidates the previously programmed mode. */
	sc->sc_configured = false;
	error = sc101iot_power_up(sc);
	if (error != 0) {
		fdtbus_gpio_write(sc->sc_power.reset, 1);
		fdtbus_gpio_write(sc->sc_power.powerdown, 1);
		mutex_exit(&sc->sc_lock);
		return error;
	}
	error = sc101iot_write_mode(sc);
	if (error != 0) {
		fdtbus_gpio_write(sc->sc_power.reset, 1);
		fdtbus_gpio_write(sc->sc_power.powerdown, 1);
		mutex_exit(&sc->sc_lock);
		return error;
	}
	sc->sc_format = format;
	sc->sc_configured = true;
	mutex_exit(&sc->sc_lock);
	return 0;
}

static int
sc101iot_set_stream(void *cookie, bool enable)
{
	struct sc101iot_softc *sc = cookie;
	int error;

	mutex_enter(&sc->sc_lock);
	if (!sc->sc_configured)
		error = ENXIO;
	else if (sc->sc_streaming == enable)
		error = 0;
	else
		error = sc101iot_write_reg(sc, 0x3100, enable ? 0x01 : 0x00);
	if (error == 0 && sc->sc_configured)
		sc->sc_streaming = enable;
	mutex_exit(&sc->sc_lock);
	return error;
}

static void *
sc101iot_ep_get_data(device_t dev, struct fdt_endpoint *ep)
{
	struct sc101iot_softc *sc = device_private(dev);

	(void)ep;
	return &sc->sc_sensor;
}

static void
sc101iot_init(device_t self)
{
	struct sc101iot_softc *sc = device_private(self);
	struct sc101iot_sequence_ops ops = sc101iot_sequence_ops(sc);
	int error;

	error = camera_sensor_power_acquire(sc->sc_phandle, &sc->sc_power);
	if (error != 0) {
		aprint_error_dev(self, "cannot acquire sensor controls/supply (%d); "
		    "any acquired reset/power-down outputs remain asserted\n", error);
		return;
	}

	mutex_enter(&sc->sc_lock);
	error = sc101iot_identify(&ops, &sc->sc_pid);
	if (error == 0) {
		sc->sc_identified = true;
	}
	mutex_exit(&sc->sc_lock);
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
	aprint_normal_dev(self, "sensor PID 0x%04x identified; "
	    "640x480 UYVY mode available at 15 fps\n", sc->sc_pid);
}
