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
#include <sys/syslog.h>
#include <sys/time.h>
#include <dev/i2c/i2cvar.h>
#include <dev/i2c/ihidev.h>
#include <dev/hid/hidmtvar.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsmousevar.h>

/* Upper bound for hw.imt0.rawlog. */
#define IMT_RAWLOG_MAX		2000
/* Bytes of one raw report printed per log line. */
#define IMT_RAWLOG_BYTES	128
/*
 * Default scroll_scale: precision-scroll units for a swipe over the whole pad
 * height. wsmouse_precision_scroll() multiplies by 4096 / 12 (default scroll
 * distance), libopeninput turns one wheel click (v120 = 120) into 1920 of
 * those, i.e. 5.625 input units; 20 clicks per pad height is 112 units.
 */
#define IMT_SCROLL_SCALE	112
#define IMT_SCROLL_SCALE_MAX	100000

struct imt_softc {
	struct ihidev sc_hdev;
	device_t sc_mouse;
	struct hidmt_config sc_config;
	struct hidmt_pointer sc_pointer;
	struct hidmt_assembler sc_asm;
	struct sysctllog *sc_sysctllog;
	uint8_t *sc_mode;
	bool sc_ready, sc_enabled, sc_suspended;
	int sc_tapping;
	int sc_tapping_right;
	int sc_hybrid;		/* assemble hybrid frames from several reports */
	int sc_scroll;		/* two-finger scrolling */
	int sc_scroll_natural;
	int sc_scroll_scale;
	int sc_rawlog;		/* raw reports still to be logged */
	unsigned sc_drops;	/* frames rejected by hidmt_decode() */
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
static int imt_sysctl_dump_desc(SYSCTLFN_PROTO);
static int imt_sysctl_rawlog(SYSCTLFN_PROTO);
static int imt_sysctl_scale(SYSCTLFN_PROTO);
static int imt_sysctl_drops(SYSCTLFN_PROTO);

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

	/*
	 * One-finger tap synthesizes left click and can confuse Plasma on
	 * Nick (double left → black screen). Keep it off by default.
	 * Two-finger tap → right is safe and on by default.
	 */
	sc->sc_tapping = 0;
	sc->sc_tapping_right = 1;
	/* Everything below is off by default: the pre-existing behaviour. */
	sc->sc_hybrid = 0;
	sc->sc_scroll = 0;
	sc->sc_scroll_natural = 0;
	sc->sc_scroll_scale = IMT_SCROLL_SCALE;
	sc->sc_rawlog = 0;
	if (sysctl_createv(&sc->sc_sysctllog, 0, NULL, &node, 0,
	    CTLTYPE_NODE, device_xname(self),
	    SYSCTL_DESCR("Precision Touchpad settings"), NULL, 0, NULL, 0,
	    CTL_HW, CTL_CREATE, CTL_EOL) == 0) {
		sysctl_createv(&sc->sc_sysctllog, 0, &node, NULL,
		    CTLFLAG_READWRITE, CTLTYPE_INT, "tapping",
		    SYSCTL_DESCR("Enable one-finger tap-to-click (left button)"),
		    NULL, 0, &sc->sc_tapping, 0, CTL_CREATE, CTL_EOL);
		sysctl_createv(&sc->sc_sysctllog, 0, &node, NULL,
		    CTLFLAG_READWRITE, CTLTYPE_INT, "tapping_right",
		    SYSCTL_DESCR("Enable two-finger tap (right button)"),
		    NULL, 0, &sc->sc_tapping_right, 0, CTL_CREATE, CTL_EOL);
		sysctl_createv(&sc->sc_sysctllog, 0, &node, NULL,
		    CTLFLAG_READWRITE, CTLTYPE_INT, "hybrid",
		    SYSCTL_DESCR("Assemble frames from several hybrid reports"),
		    NULL, 0, &sc->sc_hybrid, 0, CTL_CREATE, CTL_EOL);
		sysctl_createv(&sc->sc_sysctllog, 0, &node, NULL,
		    CTLFLAG_READWRITE, CTLTYPE_INT, "scroll",
		    SYSCTL_DESCR("Two-finger scrolling"),
		    NULL, 0, &sc->sc_scroll, 0, CTL_CREATE, CTL_EOL);
		sysctl_createv(&sc->sc_sysctllog, 0, &node, NULL,
		    CTLFLAG_READWRITE, CTLTYPE_INT, "scroll_natural",
		    SYSCTL_DESCR("Natural (content follows fingers) scrolling"),
		    NULL, 0, &sc->sc_scroll_natural, 0, CTL_CREATE, CTL_EOL);
		sysctl_createv(&sc->sc_sysctllog, 0, &node, NULL,
		    CTLFLAG_READWRITE, CTLTYPE_INT, "scroll_scale",
		    SYSCTL_DESCR("Scroll units for a swipe over the pad height"),
		    imt_sysctl_scale, 0, (void *)sc, 0, CTL_CREATE, CTL_EOL);
		sysctl_createv(&sc->sc_sysctllog, 0, &node, NULL,
		    CTLFLAG_READWRITE, CTLTYPE_INT, "dump_desc",
		    SYSCTL_DESCR("Write 1 to log the HID descriptor and config"),
		    imt_sysctl_dump_desc, 0, (void *)sc, 0, CTL_CREATE, CTL_EOL);
		sysctl_createv(&sc->sc_sysctllog, 0, &node, NULL,
		    CTLFLAG_READWRITE, CTLTYPE_INT, "rawlog",
		    SYSCTL_DESCR("Log the next N raw input reports"),
		    imt_sysctl_rawlog, 0, (void *)sc, 0, CTL_CREATE, CTL_EOL);
		sysctl_createv(&sc->sc_sysctllog, 0, &node, NULL,
		    CTLFLAG_READONLY, CTLTYPE_INT, "drops",
		    SYSCTL_DESCR("Frames dropped as malformed or incomplete"),
		    imt_sysctl_drops, 0, (void *)sc, 0, CTL_CREATE, CTL_EOL);
	}

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
	memset(&sc->sc_asm, 0, sizeof(sc->sc_asm));
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
	if (cfg->config_mode_only) {
		/* No other feature values need preserving in this report. */
		for (i = 0; i < cfg->mode.size; i++) {
			unsigned bit = cfg->mode.pos + i;
			mode[bit / 8] |= ((3U >> i) & 1U) << (bit % 8);
		}
		return ihidev_set_report_poll(sc->sc_hdev.sc_parent->sc_dev,
		    hid_feature, cfg->config_id, mode, cfg->config_size, poll);
	}
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
imt_log_raw(struct imt_softc *sc, const uint8_t *buf, u_int len,
    const struct timespec *now)
{
	char hex[3 * IMT_RAWLOG_BYTES + 4];
	u_int i, n = len < IMT_RAWLOG_BYTES ? len : IMT_RAWLOG_BYTES;

	for (i = 0; i < n; i++)
		snprintf(hex + 3 * i, 4, "%02x ", buf[i]);
	if (n > 0)
		hex[3 * n - 1] = '\0';
	else
		hex[0] = '\0';
	log(LOG_INFO, "%s: raw t=%lld len=%u: %s%s\n",
	    device_xname(sc->sc_hdev.sc_idev),
	    (long long)now->tv_sec * 1000 + now->tv_nsec / 1000000, len, hex,
	    len > n ? " ..." : "");
}

static void
imt_intr(struct ihidev *hdev, void *buf, u_int len)
{
	struct imt_softc *sc = (struct imt_softc *)hdev;
	struct hidmt_frame frame;
	struct hidmt_motion motion;
	struct hidmt_scroll scroll;
	struct timespec now;
	uint64_t usec;
	int s, r;

	if (!sc->sc_enabled || sc->sc_suspended || sc->sc_mouse == NULL)
		return;
	getnanouptime(&now);
	usec = (uint64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
	if (sc->sc_rawlog > 0) {
		sc->sc_rawlog--;
		imt_log_raw(sc, buf, len, &now);
	}
	if (sc->sc_hybrid != 0) {
		r = hidmt_assemble(&sc->sc_config, &sc->sc_asm, buf, len, usec,
		    &frame);
		if (r == HIDMT_ASM_MORE)
			return;
		if (r != HIDMT_ASM_READY) {
			memset(&sc->sc_pointer, 0, sizeof(sc->sc_pointer));
			return;
		}
	} else if (hidmt_decode(&sc->sc_config, buf, len, &frame) != 0) {
		/* Reject malformed frames without retaining a possible tap. */
		sc->sc_drops++;
		memset(&sc->sc_pointer, 0, sizeof(sc->sc_pointer));
		return;
	}
	if (sc->sc_scroll != 0) {
		scroll.enable = true;
		scroll.natural = sc->sc_scroll_natural != 0;
		scroll.scale = sc->sc_scroll_scale;
		scroll.range = sc->sc_config.maxy - sc->sc_config.miny;
		hidmt_pointer_update_scroll(&sc->sc_pointer, &frame, usec,
		    (sc->sc_tapping != 0 ? HIDMT_TAP_LEFT : 0) |
		    (sc->sc_tapping_right != 0 ? HIDMT_TAP_RIGHT : 0),
		    &scroll, &motion);
	} else
		hidmt_pointer_update(&sc->sc_pointer, &frame, usec,
		    (sc->sc_tapping != 0 ? HIDMT_TAP_LEFT : 0) |
		    (sc->sc_tapping_right != 0 ? HIDMT_TAP_RIGHT : 0),
		    &motion);
	s = spltty();
	if (motion.scroll_x != 0 || motion.scroll_y != 0)
		wsmouse_precision_scroll(sc->sc_mouse, motion.scroll_x,
		    motion.scroll_y);
	wsmouse_input(sc->sc_mouse,
	    motion.buttons | (motion.tap ? motion.tap_button : 0),
	    motion.dx, motion.dy, 0, 0, WSMOUSE_INPUT_DELTA);
	if (motion.tap)
		wsmouse_input(sc->sc_mouse, motion.buttons, 0, 0, 0, 0,
		    WSMOUSE_INPUT_DELTA);
	splx(s);
}

static void
imt_log_loc(const char *name, const struct hid_location *l, const char *dev)
{
	log(LOG_INFO, "%s: cfg %s pos=%u size=%u count=%u\n", dev, name,
	    l->pos, l->size, l->count);
}

static int
imt_sysctl_dump_desc(SYSCTLFN_ARGS)
{
	struct sysctlnode node = *rnode;
	struct imt_softc *sc = node.sysctl_data;
	const struct hidmt_config *cfg = &sc->sc_config;
	const char *dev = device_xname(sc->sc_hdev.sc_idev);
	char lbl[16];
	char hex[3 * 16 + 1];
	const uint8_t *desc;
	void *d;
	int v = 0, len, error;
	unsigned off, i, n;

	node.sysctl_data = &v;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error || newp == NULL)
		return error;
	if (v != 1)
		return EINVAL;
	ihidev_get_report_desc(sc->sc_hdev.sc_parent, &d, &len);
	desc = d;
	log(LOG_INFO, "%s: desc length %d bytes\n", dev, len);
	for (off = 0; off < (unsigned)len; off += 16) {
		n = (unsigned)len - off < 16 ? (unsigned)len - off : 16;
		for (i = 0; i < n; i++)
			snprintf(hex + 3 * i, 4, "%02x ", desc[off + i]);
		hex[3 * n - 1] = '\0';
		log(LOG_INFO, "%s: desc %04x: %s\n", dev, off, hex);
	}
	log(LOG_INFO, "%s: cfg input_id=%d input_size=%u nslots=%u "
	    "config_id=%d config_size=%u config_mode_only=%d\n", dev,
	    cfg->input_id, cfg->input_size, cfg->nslots, cfg->config_id,
	    cfg->config_size, cfg->config_mode_only);
	log(LOG_INFO, "%s: cfg minx=%d maxx=%d miny=%d maxy=%d\n", dev,
	    cfg->minx, cfg->maxx, cfg->miny, cfg->maxy);
	imt_log_loc("count", &cfg->count, dev);
	imt_log_loc("button", &cfg->button, dev);
	imt_log_loc("mode", &cfg->mode, dev);
	imt_log_loc("scantime", &cfg->scantime, dev);
	for (i = 0; i < cfg->nslots; i++) {
		const struct hidmt_slot *sl = &cfg->slots[i];

		snprintf(lbl, sizeof(lbl), "slot%u.x", i);
		imt_log_loc(lbl, &sl->x, dev);
		snprintf(lbl, sizeof(lbl), "slot%u.y", i);
		imt_log_loc(lbl, &sl->y, dev);
		snprintf(lbl, sizeof(lbl), "slot%u.tip", i);
		imt_log_loc(lbl, &sl->tip, dev);
		snprintf(lbl, sizeof(lbl), "slot%u.conf", i);
		imt_log_loc(lbl, &sl->confidence, dev);
		snprintf(lbl, sizeof(lbl), "slot%u.id", i);
		imt_log_loc(lbl, &sl->id, dev);
	}
	return 0;
}

static int
imt_sysctl_rawlog(SYSCTLFN_ARGS)
{
	struct sysctlnode node = *rnode;
	struct imt_softc *sc = node.sysctl_data;
	int v = sc->sc_rawlog, error;

	node.sysctl_data = &v;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error || newp == NULL)
		return error;
	if (v < 0 || v > IMT_RAWLOG_MAX)
		return EINVAL;
	sc->sc_rawlog = v;
	return 0;
}

static int
imt_sysctl_scale(SYSCTLFN_ARGS)
{
	struct sysctlnode node = *rnode;
	struct imt_softc *sc = node.sysctl_data;
	int v = sc->sc_scroll_scale, error;

	node.sysctl_data = &v;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error || newp == NULL)
		return error;
	if (v < 1 || v > IMT_SCROLL_SCALE_MAX)
		return EINVAL;
	sc->sc_scroll_scale = v;
	return 0;
}

static int
imt_sysctl_drops(SYSCTLFN_ARGS)
{
	struct sysctlnode node = *rnode;
	struct imt_softc *sc = node.sysctl_data;
	int v = (int)(sc->sc_drops + sc->sc_asm.drops);

	node.sysctl_data = &v;
	return sysctl_lookup(SYSCTLFN_CALL(&node));
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
