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
	uint8_t *sc_mode;
	bool sc_ready, sc_enabled, sc_suspended;
	int sc_tapping;
};

static int imt_match(device_t, cfdata_t, void *);
static void imt_attach(device_t, device_t, void *);
static int imt_detach(device_t, int);
static void imt_childdet(device_t, device_t);
static int imt_enable(void *);
static int imt_set_mode(struct imt_softc *, bool);
static bool imt_suspend(device_t, const pmf_qual_t *);
static bool imt_resume(device_t, const pmf_qual_t *);
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

	/* Resume can run with interrupts disabled. Allocate its buffer now. */
	sc->sc_mode = kmem_zalloc(sc->sc_config.config_size, KM_SLEEP);
	sc->sc_ready = true;
	mouse.accessops = &imt_accessops;
	mouse.accesscookie = sc;
	sc->sc_mouse = config_found(self, &mouse, wsmousedevprint, CFARGS_NONE);
	if (sc->sc_mouse == NULL)
		sc->sc_ready = false;
	if (!pmf_device_register(self, imt_suspend, imt_resume))
		aprint_error_dev(self, "could not register power handler\n");
}

static int
imt_enable(void *arg)
{
	struct imt_softc *sc = arg;
	int error;

	if (!sc->sc_ready)
		return ENXIO;
	if (sc->sc_enabled || sc->sc_suspended)
		return EBUSY;
	error = ihidev_open(&sc->sc_hdev);
	if (error != 0)
		return error;
	error = imt_set_mode(sc, false);
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

static int
imt_set_mode(struct imt_softc *sc, bool poll)
{
	struct hidmt_config *cfg = &sc->sc_config;
	uint8_t *mode = sc->sc_mode;
	unsigned i;
	int error;

	memset(mode, 0, cfg->config_size);
	error = ihidev_get_report_poll(sc->sc_hdev.sc_parent->sc_dev, hid_feature,
	    cfg->config_id, mode, cfg->config_size, poll);
	if (error == 0) {
		/* Input Mode 3 selects Precision Touchpad reports. */
		for (i = 0; i < cfg->mode.size; i++) {
			unsigned bit = cfg->mode.pos + i;
			mode[bit / 8] &= ~(1U << (bit % 8));
			mode[bit / 8] |= ((3U >> i) & 1U) << (bit % 8);
		}
		error = ihidev_set_report_poll(sc->sc_hdev.sc_parent->sc_dev,
		    hid_feature, cfg->config_id, mode, cfg->config_size, poll);
	}
	return error;
}

static bool
imt_suspend(device_t self, const pmf_qual_t *q)
{
	struct imt_softc *sc = device_private(self);
	int s;

	/* Serialize with the non-MPSAFE ihidev worker and wscons callbacks. */
	KERNEL_LOCK(1, NULL);
	sc->sc_suspended = true;
	memset(&sc->sc_pointer, 0, sizeof(sc->sc_pointer));
	if (sc->sc_enabled && sc->sc_mouse != NULL) {
		/* wsmouse has no suspend handler to release held buttons. */
		s = spltty();
		wsmouse_input(sc->sc_mouse, 0, 0, 0, 0, 0, WSMOUSE_INPUT_DELTA);
		splx(s);
	}
	KERNEL_UNLOCK_ONE(NULL);
	return true;
}

static bool
imt_resume(device_t self, const pmf_qual_t *q)
{
	struct imt_softc *sc = device_private(self);
	bool ok = true;

	KERNEL_LOCK(1, NULL);
	/* PMF resumes ihidev first. Its reset restores the mouse report mode. */
	sc->sc_suspended = true;
	memset(&sc->sc_pointer, 0, sizeof(sc->sc_pointer));
	if (sc->sc_enabled && imt_set_mode(sc, true) != 0) {
		aprint_error_dev(self, "could not restore Precision Touchpad mode\n");
		ok = false;
	}
	/* Retain the open reference even on failure, for retry or close. */
	if (ok)
		sc->sc_suspended = false;
	KERNEL_UNLOCK_ONE(NULL);
	return ok;
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

	if (!sc->sc_enabled || sc->sc_suspended || sc->sc_mouse == NULL)
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
	if (sc->sc_mode != NULL)
		kmem_free(sc->sc_mode, sc->sc_config.config_size);
	return 0;
}

static void
imt_childdet(device_t self, device_t child)
{
	struct imt_softc *sc = device_private(self);

	KASSERT(child == sc->sc_mouse);
	sc->sc_mouse = NULL;
}
