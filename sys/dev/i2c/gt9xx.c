/* Origin: EmberBSD - expose Goodix GT9xx touchscreens as a wscons pointer. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/bus.h>
#include <sys/kmem.h>

#include <dev/i2c/i2cvar.h>
#ifndef _MODULE
#include "opt_fdt.h"
#endif

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsmousevar.h>
#include "gt9xx_frame.h"

#ifdef FDT
#include "gt9xx_fdt.h"
#endif

struct gt9xx_softc {
	device_t sc_dev, sc_mouse;
	i2c_tag_t sc_tag;
	i2c_addr_t sc_addr;
	lwp_t *sc_thread;
	bool sc_ready, sc_enabled, sc_dying, sc_pressed;
	uint16_t sc_maxx, sc_maxy, sc_x, sc_y;
	unsigned int sc_contacts;
};

static int gt9xx_match(device_t, cfdata_t, void *);
static void gt9xx_attach(device_t, device_t, void *);
static int gt9xx_detach(device_t, int);
static void gt9xx_childdet(device_t, device_t);
static int gt9xx_enable(void *);
static void gt9xx_disable(void *);
static int gt9xx_ioctl(void *, u_long, void *, int, struct lwp *);
static void gt9xx_poll(void *);

CFATTACH_DECL2_NEW(gt9xx, sizeof(struct gt9xx_softc), gt9xx_match,
    gt9xx_attach, gt9xx_detach, NULL, NULL, gt9xx_childdet);

static const struct device_compatible_entry gt9110_acpi[] = {
	{ .compat = "GDIX1002" },
	DEVICE_COMPAT_EOL
};
static const struct wsmouse_accessops gt9xx_accessops = {
	.enable = gt9xx_enable,
	.ioctl = gt9xx_ioctl,
	.disable = gt9xx_disable,
};

static int
gt9xx_read(struct gt9xx_softc *sc, uint16_t reg, void *data, size_t len)
{
	const uint8_t cmd[2] = {reg >> 8, reg & 255};
	int error;

	error = iic_acquire_bus(sc->sc_tag, 0);
	if (error != 0)
		return (error);
	error = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    cmd, sizeof(cmd), data, len, 0);
	iic_release_bus(sc->sc_tag, 0);
	return (error);
}

static int
gt9xx_ack(struct gt9xx_softc *sc)
{
	const uint8_t cmd[2] = {0x81, 0x4e}, zero = 0;
	int error;

	error = iic_acquire_bus(sc->sc_tag, 0);
	if (error != 0)
		return (error);
	error = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
	    cmd, sizeof(cmd), __UNCONST(&zero), 1, 0);
	iic_release_bus(sc->sc_tag, 0);
	return (error);
}

static void
gt9xx_report(struct gt9xx_softc *sc, bool pressed, uint16_t x, uint16_t y)
{
	int s;

	KASSERT(KERNEL_LOCKED_P());
	if (sc->sc_mouse == NULL || !sc->sc_enabled)
		return;
	if (!pressed) {
		x = sc->sc_x;
		y = sc->sc_y;
	}
	s = spltty();
	wsmouse_input(sc->sc_mouse, pressed ? 1 : 0, x, y, 0, 0,
	    WSMOUSE_INPUT_ABSOLUTE_X | WSMOUSE_INPUT_ABSOLUTE_Y);
	splx(s);
	sc->sc_pressed = pressed;
	sc->sc_x = x;
	sc->sc_y = y;
}

static void
gt9xx_poll(void *arg)
{
	struct gt9xx_softc *sc = arg;
	struct gt9xx_contact point;
	uint8_t data[81];
	unsigned int count;
	size_t frame_size;
	int error, decoded;

	/* A non-MPSAFE thread serializes wscons operations with the kernel lock. */
	KASSERT(KERNEL_LOCKED_P());
	while (!sc->sc_dying) {
		if (!sc->sc_enabled || sc->sc_mouse == NULL)
			goto wait;
		error = gt9xx_read(sc, 0x814e, data, 1);
		if (error != 0)
			goto failed;
		if ((data[0] & 0x80) == 0)
			goto wait;
		count = data[0] & 15;
		frame_size = gt9xx_report_size(count, sc->sc_contacts);
		if (frame_size > 1)
			error = gt9xx_read(sc, 0x814f, data + 1, frame_size - 1);
		decoded = error == 0 ? gt9xx_decode_frame(data,
		    frame_size,
		    sc->sc_maxx, sc->sc_maxy, &point) : -1;
		/* Acknowledge malformed ready frames as well, to unblock the sensor. */
		error = gt9xx_ack(sc);
		if (error != 0 || decoded != 1)
			goto failed;
		if (!sc->sc_dying && sc->sc_enabled)
			gt9xx_report(sc, point.pressed, point.x, point.y);
		goto wait;
failed:
		if (!sc->sc_dying && sc->sc_pressed)
			gt9xx_report(sc, false, sc->sc_x, sc->sc_y);
wait:
		kpause("gt9poll", false, MAX(1, mstohz(20)), NULL);
	}
	kthread_exit(0);
}

static int
gt9xx_match(device_t parent, cfdata_t cf, void *aux)
{
	struct i2c_attach_args *ia = aux;
	int result;

	if (ia->ia_cookietype == I2C_COOKIE_ACPI &&
	    (ia->ia_addr == 0x14 || ia->ia_addr == 0x5d) &&
	    iic_use_direct_match(ia, cf, gt9110_acpi, &result))
		return (result);
#ifdef FDT
	result = gt9xx_fdt_match(ia, cf);
	if (result != 0)
		return (result);
#endif
	return (0);
}

static void
gt9xx_attach(device_t parent, device_t self, void *aux)
{
	struct gt9xx_softc *sc = device_private(self);
	struct i2c_attach_args *ia = aux;
	struct wsmousedev_attach_args mouse;
	uint8_t id[10], cfg[7];
	int error;

	sc->sc_dev = self;
	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;
	aprint_naive("\n");
	aprint_normal(": Goodix touchscreen\n");
#ifdef FDT
	if (ia->ia_cookietype == I2C_COOKIE_OF) {
		error = gt9xx_fdt_prepare(ia);
		if (error != 0 || gt9xx_read(sc, 0x8140, id, sizeof(id)) != 0 ||
		    memcmp(id, "9271", 4) != 0) {
			aprint_error_dev(self, "sensor reset or identification failed\n");
			return;
		}
		sc->sc_maxx = id[6] | id[7] << 8;
		sc->sc_maxy = id[8] | id[9] << 8;
		sc->sc_contacts = 5;
	} else
#endif
	if (ia->ia_cookietype == I2C_COOKIE_ACPI) {
		if (gt9xx_read(sc, 0x8140, id, sizeof(id)) != 0 ||
		    memcmp(id, "9110", 4) != 0 ||
		    gt9xx_read(sc, 0x8047, cfg, sizeof(cfg)) != 0) {
			aprint_error_dev(self, "sensor identification or config failed\n");
			return;
		}
		if (gt9110_decode_config(cfg, sizeof(cfg), &sc->sc_maxx,
		    &sc->sc_maxy, &sc->sc_contacts) != 0) {
			aprint_error_dev(self, "invalid sensor config\n");
			return;
		}
	} else
		return;
	if (sc->sc_maxx == 0 || sc->sc_maxy == 0 ||
	    sc->sc_maxx > 8192 || sc->sc_maxy > 8192) {
		aprint_error_dev(self, "invalid configured sensor dimensions\n");
		return;
	}
	aprint_normal_dev(self, "GT%.4s firmware %04x, %ux%u, single pointer, "
	    "20 ms polling interval\n", (char *)id, id[4] | id[5] << 8,
	    sc->sc_maxx, sc->sc_maxy);
	error = kthread_create(PRI_NONE, KTHREAD_MUSTJOIN, NULL,
	    gt9xx_poll, sc, &sc->sc_thread, "%s", device_xname(self));
	if (error != 0) {
		aprint_error_dev(self, "cannot create sensor poller\n");
		return;
	}
	/* An already-open mux can call enable from inside config_found. */
	sc->sc_ready = true;
	mouse.accessops = &gt9xx_accessops;
	mouse.accesscookie = sc;
	sc->sc_mouse = config_found(self, &mouse, wsmousedevprint, CFARGS_NONE);
	if (sc->sc_mouse == NULL) {
		sc->sc_ready = false;
		sc->sc_dying = true;
		(void)kthread_join(sc->sc_thread);
		sc->sc_thread = NULL;
	}
}

static int
gt9xx_enable(void *arg)
{
	struct gt9xx_softc *sc = arg;

	KASSERT(KERNEL_LOCKED_P());
	if (!sc->sc_ready || sc->sc_dying)
		return ENXIO;
	if (sc->sc_enabled)
		return EBUSY;
	sc->sc_enabled = true;
	sc->sc_pressed = false;
	return 0;
}

static void
gt9xx_disable(void *arg)
{
	struct gt9xx_softc *sc = arg;

	KASSERT(KERNEL_LOCKED_P());
	sc->sc_enabled = false;
	sc->sc_pressed = false;
}

static int
gt9xx_ioctl(void *arg, u_long cmd, void *data, int flag, struct lwp *l)
{
	struct gt9xx_softc *sc = arg;
	struct wsmouse_calibcoords *cal;

	switch (cmd) {
	case WSMOUSEIO_GTYPE:
		*(u_int *)data = WSMOUSE_TYPE_TPANEL;
		return 0;
	case WSMOUSEIO_GCALIBCOORDS:
		cal = data;
		memset(cal, 0, sizeof(*cal));
		cal->maxx = sc->sc_maxx;
		cal->maxy = sc->sc_maxy;
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
gt9xx_detach(device_t self, int flags)
{
	struct gt9xx_softc *sc = device_private(self);
	int error;

	KASSERT(KERNEL_LOCKED_P());
	error = config_detach_children(self, flags);
	if (error != 0)
		return error;
	sc->sc_dying = true;
	sc->sc_ready = false;
	if (sc->sc_thread != NULL)
		return kthread_join(sc->sc_thread);
	return 0;
}

static void
gt9xx_childdet(device_t self, device_t child)
{
	struct gt9xx_softc *sc = device_private(self);

	KASSERT(KERNEL_LOCKED_P());
	KASSERT(child == sc->sc_mouse);
	sc->sc_mouse = NULL;
	sc->sc_enabled = false;
}

MODULE(MODULE_CLASS_DRIVER, gt9xx, "iic");
#ifdef _MODULE
#include "ioconf.c"
#endif
static int
gt9xx_modcmd(modcmd_t cmd, void *arg)
{
	switch (cmd) {
	case MODULE_CMD_INIT:
#ifdef _MODULE
		return config_init_component(cfdriver_ioconf_gt9xx,
		    cfattach_ioconf_gt9xx, cfdata_ioconf_gt9xx);
#else
		return 0;
#endif
	case MODULE_CMD_FINI:
#ifdef _MODULE
		return config_fini_component(cfdriver_ioconf_gt9xx,
		    cfattach_ioconf_gt9xx, cfdata_ioconf_gt9xx);
#else
		return 0;
#endif
	default:
		return ENOTTY;
	}
}
