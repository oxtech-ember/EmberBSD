/*
 * Copyright (c) 2026 The EmberBSD Project.
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: BSD-2-Clause AND Apache-2.0
 *
 * CST9220 protocol based on esp_lcd_touch_cst9220 v0.1.1:
 * espressif/esp-iot-solution commit 8eee17c20da66048faa20c683ffeb80bfa85d723.
 * Apache-2.0 terms are in LICENSE.cst9220. wscons model follows gt9xx.c.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/proc.h>
#include <sys/bus.h>
#include <dev/i2c/i2cvar.h>
#include <dev/fdt/fdtvar.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsmousevar.h>
#include "cst9220_frame.h"

#define CST_REPORT 0xd000
#define CST_INFO_MODE 0xd101
#define CST_NORMAL_MODE 0xd109
#define CST_PROTOCOL_PROBE 0xd11e
#define CST_MODE_STATUS 0x0002
#define CST_RESOLUTION 0xd1f8
#define CST_PROJECT_ID 0xd204
#define CST_INFO_CHIP_ID 0xd224
#define CST_INFO_CHECKCODE 0xd228
#define CST_LEGACY_PROJECT 0x542f
#define CST_CHIP_ID 0x9220
#define CST_ACK 0xab

struct cst9220_softc {
	device_t dev, mouse;
	i2c_tag_t tag;
	i2c_addr_t addr;
	int phandle;
	struct fdtbus_regulator *supply;
	lwp_t *thread;
	bool ready, enabled, dying, pressed, legacy;
	uint16_t width, height, x, y;
};
static int cst_match(device_t, cfdata_t, void *);
static void cst_attach(device_t, device_t, void *);
static void cst_init(device_t);
static int cst_detach(device_t, int);
static void cst_childdet(device_t, device_t);
static int cst_enable(void *);
static void cst_disable(void *);
static int cst_ioctl(void *, u_long, void *, int, struct lwp *);
static void cst_poll(void *);

CFATTACH_DECL2_NEW(cst92xx, sizeof(struct cst9220_softc), cst_match, cst_attach,
                   cst_detach, NULL, NULL, cst_childdet);
static const struct device_compatible_entry cst_compat[] = {
    {.compat = "hynitron,cst9220"}, DEVICE_COMPAT_EOL};
static const struct wsmouse_accessops cst_accessops = {
    .enable = cst_enable,
    .ioctl = cst_ioctl,
    .disable = cst_disable,
};

static int
cst_io(struct cst9220_softc *sc, bool read, uint16_t reg, void *buf, size_t len)
{
	uint8_t cmd[2] = {reg >> 8, reg & 0xff};
	int error = iic_acquire_bus(sc->tag, 0);

	if (error != 0)
		return error;
	error = iic_exec(sc->tag,
	                 read ? I2C_OP_READ_WITH_STOP : I2C_OP_WRITE_WITH_STOP,
	                 sc->addr, cmd, sizeof(cmd), buf, len, 0);
	iic_release_bus(sc->tag, 0);
	return error;
}
static int
cst_command(struct cst9220_softc *sc, uint16_t reg)
{
	return cst_io(sc, false, reg, NULL, 0);
}
static uint16_t
cst_le16(const uint8_t *p)
{
	return p[0] | (p[1] << 8);
}
static int
cst_identify(struct cst9220_softc *sc, uint16_t *project)
{
	uint8_t mode[2], res[4], id[4], part[8], chip[4], check[4];
	bool command_mode = false;
	int error;

	for (unsigned int i = 0; i < 3; i++) {
		if (cst_command(sc, CST_PROTOCOL_PROBE) == 0) {
			kpause("cstmode", false, cst9220_settle_ticks(1, hz), NULL);
			if (cst_io(sc, true, CST_MODE_STATUS, mode,
			           sizeof(mode)) == 0 &&
			    mode[1] == 0x1e) {
				command_mode = true;
				break;
			}
		}
		kpause("cstmode", false, cst9220_settle_ticks(1, hz), NULL);
	}
	if (command_mode && (error = cst_command(sc, CST_NORMAL_MODE)) != 0)
		return error;
	if (command_mode)
		kpause("cstmode", false, cst9220_settle_ticks(1, hz), NULL);
	error = cst_command(sc, CST_INFO_MODE);
	if (error != 0)
		goto fail;
	kpause("cstinfo", false, cst9220_settle_ticks(10, hz), NULL);
	/* D224 is optional on older firmware; do not infer identity from a
	 * failed probe. */
	if (command_mode &&
	    cst_io(sc, true, CST_PROJECT_ID, part, sizeof(part)) == 0 &&
	    cst_io(sc, true, CST_INFO_CHECKCODE, check, sizeof(check)) == 0 &&
	    cst_io(sc, true, CST_INFO_CHIP_ID, chip, sizeof(chip)) == 0 &&
	    (part[3] != chip[1] || cst_le16(chip + 2) != 0xcaca))
		aprint_verbose_dev(sc->dev,
		                   "optional image-info marker unavailable\n");
	error = cst_io(sc, true, CST_RESOLUTION, res, sizeof(res));
	if (error == 0)
		error = cst_io(sc, true, CST_PROJECT_ID, id, sizeof(id));
	if (error != 0)
		goto fail;
	sc->width = cst_le16(res);
	sc->height = cst_le16(res + 2);
	if (!sc->width || !sc->height || sc->width > 8192 ||
	    sc->height > 8192 || cst_le16(id + 2) != CST_CHIP_ID) {
		error = ENXIO;
		goto fail;
	}
	*project = cst_le16(id);
	sc->legacy = *project == CST_LEGACY_PROJECT;
	if (sc->legacy)
		return cst_command(sc, CST_NORMAL_MODE);
	return 0;
fail:
	(void)cst_command(sc, CST_NORMAL_MODE);
	return error;
}
static int
cst_match(device_t parent, cfdata_t match, void *aux)
{
	const struct i2c_attach_args *ia = aux;
	return iic_compatible_match(ia, cst_compat);
}
static void
cst_report(struct cst9220_softc *sc, bool pressed, uint16_t x, uint16_t y)
{
	int s;
	if (!sc->enabled || sc->mouse == NULL)
		return;
	if (!pressed) {
		x = sc->x;
		y = sc->y;
	}
	s = spltty();
	wsmouse_input(sc->mouse, pressed ? 1 : 0, x, y, 0, 0,
	              WSMOUSE_INPUT_ABSOLUTE_X | WSMOUSE_INPUT_ABSOLUTE_Y);
	splx(s);
	sc->pressed = pressed;
	sc->x = x;
	sc->y = y;
}
static void
cst_poll(void *arg)
{
	struct cst9220_softc *sc = arg;
	KASSERT(KERNEL_LOCKED_P());
	while (!sc->dying) {
		uint8_t report[15], ack = CST_ACK;
		struct cst9220_point point;
		int error, decoded;
		if (!sc->enabled || sc->mouse == NULL)
			goto wait;
		error = cst_io(sc, true, CST_REPORT, report, sizeof(report));
		if (!error && !sc->legacy)
			error = cst_io(sc, false, CST_REPORT, &ack, 1);
		decoded = error ? -1
		                : cst9220_decode(report, sizeof(report),
		                                 sc->width, sc->height, &point);
		if (decoded == 1)
			cst_report(sc, true, point.x, point.y);
		else if (sc->pressed)
			cst_report(sc, false, sc->x, sc->y);
	wait:
		kpause("cstpoll", false, MAX(1, mstohz(20)), NULL);
	}
	kthread_exit(0);
}
static void
cst_attach(device_t parent, device_t self, void *aux)
{
	struct cst9220_softc *sc = device_private(self);
	struct i2c_attach_args *ia = aux;

	sc->dev = self;
	sc->tag = ia->ia_tag;
	sc->addr = ia->ia_addr;
	sc->phandle = ia->ia_cookie;
	aprint_naive("\n");
	aprint_normal(": CST9220 touch controller\n");
	config_interrupts(self, cst_init);
}

static void
cst_init(device_t self)
{
	struct cst9220_softc *sc = device_private(self);
	struct wsmousedev_attach_args mouse;
	uint16_t project;
	int error;

	if (of_hasprop(sc->phandle, "power-supply")) {
		sc->supply =
		    fdtbus_regulator_acquire(sc->phandle, "power-supply");
		if (sc->supply == NULL) {
			aprint_error_dev(self, "cannot acquire power supply\n");
			return;
		}
		error = fdtbus_regulator_enable(sc->supply);
		if (error != 0) {
			aprint_error_dev(
			    self, "cannot enable power supply (%d)\n", error);
			fdtbus_regulator_release(sc->supply);
			sc->supply = NULL;
			return;
		}
	}
	error = cst_identify(sc, &project);
	if (error != 0) {
		aprint_error_dev(self, "CST9220 identification failed (%d)\n",
		                 error);
		goto fail;
	}
	aprint_normal(": CST9220 project %04x, %ux%u, 20ms polling\n", project,
	              sc->width, sc->height);
	error = kthread_create(PRI_NONE, KTHREAD_MUSTJOIN, NULL, cst_poll, sc,
	                       &sc->thread, "%s", device_xname(self));
	if (error != 0) {
		aprint_error_dev(self, "cannot create poller\n");
		goto fail;
	}
	sc->ready = true;
	mouse.accessops = &cst_accessops;
	mouse.accesscookie = sc;
	sc->mouse = config_found(self, &mouse, wsmousedevprint, CFARGS_NONE);
	if (sc->mouse == NULL) {
		sc->ready = false;
		sc->dying = true;
		(void)kthread_join(sc->thread);
		sc->thread = NULL;
		goto fail;
	}
	return;
fail:
	if (sc->supply != NULL) {
		fdtbus_regulator_release(sc->supply);
		sc->supply = NULL;
	}
}
static int
cst_enable(void *arg)
{
	struct cst9220_softc *sc = arg;
	KASSERT(KERNEL_LOCKED_P());
	if (!sc->ready || sc->dying)
		return ENXIO;
	if (sc->enabled)
		return EBUSY;
	sc->enabled = true;
	sc->pressed = false;
	return 0;
}
static void
cst_disable(void *arg)
{
	struct cst9220_softc *sc = arg;
	KASSERT(KERNEL_LOCKED_P());
	sc->enabled = false;
	sc->pressed = false;
}
static int
cst_ioctl(void *arg, u_long cmd, void *data, int flags, struct lwp *l)
{
	struct cst9220_softc *sc = arg;
	struct wsmouse_calibcoords *cal;
	switch (cmd) {
	case WSMOUSEIO_GTYPE:
		*(u_int *)data = WSMOUSE_TYPE_TPANEL;
		return 0;
	case WSMOUSEIO_GCALIBCOORDS:
		cal = data;
		memset(cal, 0, sizeof(*cal));
		cal->maxx = sc->width;
		cal->maxy = sc->height;
		cal->samplelen = WSMOUSE_CALIBCOORDS_RESET;
		return 0;
	case WSMOUSEIO_SCALIBCOORDS:
		cal = data;
		return cal->samplelen == WSMOUSE_CALIBCOORDS_RESET ? 0 : EINVAL;
	default:
		return EPASSTHROUGH;
	}
}
static int
cst_detach(device_t self, int flags)
{
	struct cst9220_softc *sc = device_private(self);
	int error = config_detach_children(self, flags);
	if (error != 0)
		return error;
	sc->dying = true;
	sc->ready = false;
	if (sc->thread != NULL) {
		error = kthread_join(sc->thread);
		if (error != 0)
			return error;
	}
	if (sc->supply != NULL) {
		fdtbus_regulator_release(sc->supply);
		sc->supply = NULL;
	}
	return 0;
}
static void
cst_childdet(device_t self, device_t child)
{
	struct cst9220_softc *sc = device_private(self);
	KASSERT(child == sc->mouse);
	sc->mouse = NULL;
}
