/* SPDX-License-Identifier: ISC */
/*
 * Windows Precision Touchpad I2C transport.
 * The HID report selection follows OpenBSD imt(4), by Joshua Stein.
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kmem.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <dev/i2c/i2cvar.h>
#include <dev/i2c/ihidev.h>
#include <dev/hid/hidmtvar.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsmousevar.h>

struct imt_softc {
	struct ihidev sc_hdev;
	device_t sc_mouse;
	struct hidmt_config sc_config;
	struct hidmt_pointer sc_pointer;
	struct sysctllog *sc_sysctllog;
	bool sc_ready, sc_enabled;
	int sc_tapping;
};

static int imt_match(device_t, cfdata_t, void *);
static void imt_attach(device_t, device_t, void *);
static int imt_detach(device_t, int);
static void imt_childdet(device_t, device_t);
static int imt_enable(void *);
static void imt_disable(void *);
static int imt_ioctl(void *, u_long, void *, int, struct lwp *);
static void imt_intr(struct ihidev *, void *, u_int);

CFATTACH_DECL2_NEW(imt, sizeof(struct imt_softc), imt_match, imt_attach,
    imt_detach, NULL, NULL, imt_childdet);

static const struct wsmouse_accessops imt_accessops = {
	imt_enable, imt_ioctl, imt_disable,
};

static int
imt_match(device_t parent, cfdata_t cf, void *aux)
{
	struct ihidev_attach_arg *iha = aux;
	struct hidmt_config config;
	void *desc;
	int len;

	ihidev_get_report_desc(iha->parent, &desc, &len);
	if (iha->reportid == IHIDEV_CLAIM_ALLREPORTID ||
	    hidmt_parse(desc, len, &config) != 0 ||
	    iha->reportid != config.input_id)
		return IMATCH_NONE;
	return IMATCH_IFACECLASS;
}

static void
imt_attach(device_t parent, device_t self, void *aux)
{
	struct imt_softc *sc = device_private(self);
	struct ihidev_attach_arg *iha = aux;
	struct wsmousedev_attach_args mouse;
	const struct sysctlnode *node;
	void *desc;
	int len;

	sc->sc_hdev.sc_idev = self;
	sc->sc_hdev.sc_parent = iha->parent;
	sc->sc_hdev.sc_report_id = iha->reportid;
	sc->sc_hdev.sc_intr = imt_intr;
	ihidev_get_report_desc(iha->parent, &desc, &len);
	if (hidmt_parse(desc, len, &sc->sc_config) != 0) {
		aprint_error(": invalid Precision Touchpad descriptor\n");
		return;
	}
	sc->sc_hdev.sc_isize = sc->sc_config.input_size;
	/* No feature report belongs to the input report ID. */
	sc->sc_hdev.sc_fsize = hid_report_size(desc, len, hid_feature,
	    iha->reportid);
	sc->sc_hdev.sc_osize = hid_report_size(desc, len, hid_output,
	    iha->reportid);
	aprint_normal(": Precision Touchpad, %u parallel contacts\n",
	    sc->sc_config.nslots);

	if (sysctl_createv(&sc->sc_sysctllog, 0, NULL, &node, 0,
	    CTLTYPE_NODE, device_xname(self),
	    SYSCTL_DESCR("Precision Touchpad settings"), NULL, 0, NULL, 0,
	    CTL_HW, CTL_CREATE, CTL_EOL) == 0)
		sysctl_createv(&sc->sc_sysctllog, 0, &node, NULL, CTLFLAG_READWRITE,
		    CTLTYPE_INT, "tapping", SYSCTL_DESCR("Enable single finger tap"),
		    NULL, 0, &sc->sc_tapping, 0, CTL_CREATE, CTL_EOL);

	sc->sc_ready = true;
	mouse.accessops = &imt_accessops;
	mouse.accesscookie = sc;
	sc->sc_mouse = config_found(self, &mouse, wsmousedevprint, CFARGS_NONE);
	if (sc->sc_mouse == NULL)
		sc->sc_ready = false;
	if (!pmf_device_register(self, NULL, NULL))
		aprint_error_dev(self, "could not register power handler\n");
}

static int
imt_enable(void *arg)
{
	struct imt_softc *sc = arg;
	struct hidmt_config *cfg = &sc->sc_config;
	uint8_t *mode;
	unsigned i;
	int error;

	if (!sc->sc_ready)
		return ENXIO;
	if (sc->sc_enabled)
		return EBUSY;
	error = ihidev_open(&sc->sc_hdev);
	if (error != 0)
		return error;
	mode = kmem_zalloc(cfg->config_size, KM_SLEEP);
	error = ihidev_get_report(sc->sc_hdev.sc_parent->sc_dev, hid_feature,
	    cfg->config_id, mode, cfg->config_size);
	if (error == 0) {
		/* Input Mode 3 selects Precision Touchpad reports. */
		for (i = 0; i < cfg->mode.size; i++) {
			unsigned bit = cfg->mode.pos + i;
			mode[bit / 8] &= ~(1U << (bit % 8));
			mode[bit / 8] |= ((3U >> i) & 1U) << (bit % 8);
		}
		error = ihidev_set_report(sc->sc_hdev.sc_parent->sc_dev,
		    hid_feature, cfg->config_id, mode, cfg->config_size);
	}
	kmem_free(mode, cfg->config_size);
	if (error != 0) {
		ihidev_close(&sc->sc_hdev);
		aprint_error_dev(sc->sc_hdev.sc_idev,
		    "could not enable Precision Touchpad mode\n");
		return EIO;
	}
	memset(&sc->sc_pointer, 0, sizeof(sc->sc_pointer));
	sc->sc_enabled = true;
	return 0;
}

static void
imt_disable(void *arg)
{
	struct imt_softc *sc = arg;

	if (sc->sc_enabled) {
		sc->sc_enabled = false;
		ihidev_close(&sc->sc_hdev);
	}
	memset(&sc->sc_pointer, 0, sizeof(sc->sc_pointer));
}

static void
imt_intr(struct ihidev *hdev, void *buf, u_int len)
{
	struct imt_softc *sc = (struct imt_softc *)hdev;
	struct hidmt_frame frame;
	struct hidmt_motion motion;
	struct timespec now;
	int s;

	if (!sc->sc_enabled || sc->sc_mouse == NULL)
		return;
	if (hidmt_decode(&sc->sc_config, buf, len, &frame) != 0) {
		/* Reject malformed frames without retaining a possible tap. */
		memset(&sc->sc_pointer, 0, sizeof(sc->sc_pointer));
		return;
	}
	getnanouptime(&now);
	hidmt_pointer_update(&sc->sc_pointer, &frame,
	    (uint64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000,
	    sc->sc_tapping != 0, &motion);
	s = spltty();
	wsmouse_input(sc->sc_mouse, motion.buttons | (motion.tap ? 1 : 0),
	    motion.dx, motion.dy, 0, 0, WSMOUSE_INPUT_DELTA);
	if (motion.tap)
		wsmouse_input(sc->sc_mouse, motion.buttons, 0, 0, 0, 0,
		    WSMOUSE_INPUT_DELTA);
	splx(s);
}

static int
imt_ioctl(void *arg, u_long cmd, void *data, int flag, struct lwp *l)
{
	switch (cmd) {
	case WSMOUSEIO_GTYPE:
		/* NetBSD wscons consumes pointer events from the contact policy. */
		*(u_int *)data = WSMOUSE_TYPE_USB;
		return 0;
	default:
		return EPASSTHROUGH;
	}
}

static int
imt_detach(device_t self, int flags)
{
	struct imt_softc *sc = device_private(self);
	int error;

	error = config_detach_children(self, flags);
	if (error != 0)
		return error;
	sysctl_teardown(&sc->sc_sysctllog);
	pmf_device_deregister(self);
	return 0;
}

static void
imt_childdet(device_t self, device_t child)
{
	struct imt_softc *sc = device_private(self);

	KASSERT(child == sc->sc_mouse);
	sc->sc_mouse = NULL;
}
