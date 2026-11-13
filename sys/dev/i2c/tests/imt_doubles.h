/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Userland doubles for the imt adapter (kernel include boundary).  Shared by
 * t_imt.c (hidmt replaced by stubs) and t_imt_gesture.c (-DIMT_REAL_HIDMT:
 * the real hid.c/hidmt.c parse and decode real ELAN reports).
 */
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dev/hid/hidmtvar.h>
#include <sys/kmem.h>

#ifndef IMT_DOUBLES_H
#define IMT_DOUBLES_H
static void
test_noop(void)
{
}

size_t hidmt_test_allocations;
struct device {
	void *private;
};
typedef struct device *device_t;
typedef void *cfdata_t;
typedef int pmf_qual_t;
struct lwp;
struct sysctllog;
struct sysctlnode {
	void *sysctl_data;
};
#define SYSCTLFN_PROTO const int *, u_int, void *, size_t *, const void *, \
    size_t, const int *, struct lwp *, const struct sysctlnode *
#define SYSCTLFN_ARGS const int *name, u_int namelen, void *oldp, \
    size_t *oldlenp, const void *newp, size_t newlen, const int *oname, \
    struct lwp *l, const struct sysctlnode *rnode
#define SYSCTLFN_CALL(node) name, namelen, oldp, oldlenp, newp, newlen, \
    oname, l, node
#define CTLFLAG_READONLY 0
#define LOG_INFO 6
#define log test_log
struct ihidev_softc {
	device_t sc_dev;
	unsigned refs, mode;
};
struct ihidev {
	device_t sc_idev;
	struct ihidev_softc *sc_parent;
	unsigned sc_report_id, sc_isize, sc_fsize, sc_osize;
	void (*sc_intr)(struct ihidev *, void *, unsigned);
};
struct ihidev_attach_arg {
	struct ihidev_softc *parent;
	uint8_t reportid;
};
struct wsmouse_accessops {
	int (*enable)(void *);
	int (*ioctl)(void *, unsigned long, void *, int, struct lwp *);
	void (*disable)(void *);
};
struct wsmousedev_attach_args {
	const struct wsmouse_accessops *accessops;
	void *accesscookie;
};
#define CFATTACH_DECL2_NEW(n, size, match, attach, detach, activate, rescan, childdet) \
 static void *references[] __attribute__((used)) = { (void *)match, (void *)attach, (void *)detach, (void *)childdet }
#define IHIDEV_CLAIM_ALLREPORTID 255
#define IMATCH_NONE 0
#define IMATCH_IFACECLASS 1
#define CTLTYPE_NODE 0
#define CTLTYPE_INT 0
#define SYSCTL_DESCR(x) x
#define CTL_HW 0
#define CTL_CREATE 0
#define CTL_EOL 0
#define CTLFLAG_READWRITE 0
#define CFARGS_NONE 0
#define WSMOUSE_INPUT_DELTA 0
#define WSMOUSEIO_GTYPE 1
#define WSMOUSEIO_GGESTURE 2
#define WSMOUSEIO_GPINCH 3
struct wsmouse_gesture_info {
	unsigned wgi_supported, wgi_enabled;
};
#define WSMOUSE_GESTURE_BEGIN 0
#define WSMOUSE_GESTURE_UPDATE 1
#define WSMOUSE_GESTURE_END 2
#define WSMOUSE_GESTURE_CANCEL 3
#ifndef ENOSPC
#define ENOSPC 28
#endif
#define mstohz(ms) (ms)
struct callout {
	bool armed;
	void (*fn)(void *);
	void *arg;
};
static void callout_init(struct callout *c, unsigned f) { c->armed = false; }
static void callout_setfunc(struct callout *c, void (*fn)(void *), void *a) { c->fn = fn; c->arg = a; }
static void callout_schedule(struct callout *c, int t) { c->armed = true; }
static bool callout_stop(struct callout *c) { bool a = c->armed; c->armed = false; return a; }
static bool callout_halt(struct callout *c, void *l) { return callout_stop(c); }
static void callout_destroy(struct callout *c) { }
#define WSMOUSE_TYPE_USB 1
#define EPASSTHROUGH -2
#define KASSERT(x) assert(x)
#define KERNEL_LOCKED_P() true
#define KERNEL_LOCK(n, l) test_noop()
#define KERNEL_UNLOCK_ONE(l) test_noop()
#define aprint_error(...) test_noop()
#define aprint_normal(...) test_noop()
#define aprint_error_dev(...) test_noop()
#define device_private(d) ((d)->private)
#define device_xname(d) "imt0"
static device_t test_mouse;
static bool (*suspend_cb)(device_t, const pmf_qual_t *);
static bool (*resume_cb)(device_t, const pmf_qual_t *);
static int get_error, set_error, open_error;
static bool expect_poll;
static unsigned events, get_calls, set_calls;
static unsigned last_buttons;
static uint8_t last_set_mode;
static int wsmousedevprint;
static int
sysctl_createv(void *log, ...)
{
	return EIO;
}
static void
sysctl_teardown(void *log)
{
}
static device_t
config_found(device_t d, void *a, int p, int flags)
{
	return test_mouse;
}
static int
config_detach_children(device_t d, int f)
{
	return 0;
}
static bool
pmf_device_register(device_t d, bool (*s)(device_t, const pmf_qual_t *), bool (*r)(device_t, const pmf_qual_t *))
{
	suspend_cb = s;
	resume_cb = r;
	return true;
}
static void
pmf_device_deregister(device_t d)
{
}
static int
spltty(void)
{
	return 0;
}
static void
splx(int s)
{
}
static void __attribute__((format(printf, 2, 3)))
test_log(int pri, const char *fmt, ...)
{
}
static int
sysctl_lookup(SYSCTLFN_ARGS)
{
	if (newp != NULL)
		*(int *)rnode->sysctl_data = *(const int *)newp;
	return 0;
}
/* Gesture delivery log; a phase listed in gesture_fail fails with ENOSPC. */
static unsigned gesture_phases[256], ngesture;
static bool gesture_pinch[256];
static int gesture_args[256][3];
static unsigned gesture_fail;		/* bit mask of phases that fail */
static unsigned gesture_fail_count;	/* how many times it still fails */
static int
wsmouse_gesture(device_t d, int fingers, int phase, int dx, int dy)
{
	if (gesture_fail_count > 0 && (gesture_fail >> phase & 1)) {
		gesture_fail_count--;
		return ENOSPC;
	}
	assert(ngesture < 256);
	gesture_pinch[ngesture] = false;
	gesture_phases[ngesture] = (unsigned)phase;
	gesture_args[ngesture][0] = fingers;
	gesture_args[ngesture][1] = dx;
	gesture_args[ngesture][2] = dy;
	ngesture++;
	return 0;
}
static int
wsmouse_pinch(device_t d, int phase, int scale)
{
 int error = wsmouse_gesture(d, 2, phase, scale, 0);
 if (!error) gesture_pinch[ngesture - 1] = true;
 return error;
}
static void
wsmouse_precision_scroll(device_t d, int x, int y)
{
}
static struct timespec test_now = {1, 0};
static void
getnanouptime(struct timespec *t)
{
	*t = test_now;
}
static void
wsmouse_input(device_t d, unsigned b, int x, int y, int z, int w, int f)
{
	events++;
	last_buttons = b;
}
#ifdef IMT_REAL_HIDMT
static const void *test_desc;
static int test_desc_len;
static void
ihidev_get_report_desc(struct ihidev_softc *p, void **d, int *n)
{
	*d = (void *)(uintptr_t)test_desc;
	*n = test_desc_len;
}
#else
static void
ihidev_get_report_desc(struct ihidev_softc *p, void **d, int *n)
{
	*d = NULL;
	*n = 0;
}
int
hid_report_size(const void *d, int n, enum hid_kind k, uint8_t r)
{
	return 0;
}
int
hidmt_parse(const void *d, int n, struct hidmt_config *c)
{
	*c = (struct hidmt_config){.config_size = 1, .config_id = 3,
	    .input_id = 4, .mode = {.size = 2, .pos = 0},
	    .config_mode_only = true};
	return 0;
}
int
hidmt_decode(const struct hidmt_config *c, const uint8_t *b, unsigned n, struct hidmt_frame *f)
{
	*f = (struct hidmt_frame){0};
	return 0;
}
int
hidmt_assemble(const struct hidmt_config *c, struct hidmt_assembler *a,
    const uint8_t *b, unsigned n, uint64_t t, struct hidmt_frame *f)
{
	*f = (struct hidmt_frame){0};
	return HIDMT_ASM_READY;
}
void
hidmt_pointer_update_scroll(struct hidmt_pointer *p,
    const struct hidmt_frame *f, uint64_t t, unsigned tap_flags,
    const struct hidmt_scroll *sc, struct hidmt_motion *m)
{
	(void)tap_flags;
	*m = (struct hidmt_motion){.tap = p->tap_candidate};
}
void
hidmt_pointer_update(struct hidmt_pointer *p, const struct hidmt_frame *f, uint64_t t, unsigned tap_flags, struct hidmt_motion *m)
{
	(void)tap_flags;
	*m = (struct hidmt_motion){.tap = p->tap_candidate};
}
void
hidmt_gesture_update(struct hidmt_gstate *g, const struct hidmt_gconf *c,
    const struct hidmt_frame *f, struct hidmt_motion *m)
{
}
unsigned
hidmt_release(struct hidmt_pointer *p, struct hidmt_gstate *g)
{
	memset(p, 0, sizeof(*p));
	memset(g, 0, sizeof(*g));
	return 0;
}
unsigned
hidmt_reset(struct hidmt_pointer *p, struct hidmt_assembler *a,
    struct hidmt_gstate *g)
{
	memset(a, 0, sizeof(*a));
	return hidmt_release(p, g);
}
bool
hidmt_live(const struct hidmt_pointer *p, const struct hidmt_assembler *a,
    const struct hidmt_gstate *g)
{
	return false;
}
unsigned
hidmt_stall(struct hidmt_pointer *p, struct hidmt_assembler *a,
    struct hidmt_gstate *g, uint64_t now, uint64_t last, bool *released)
{
	*released = false;
	return 0;
}
#endif
static int
ihidev_open(struct ihidev *h)
{
	if (open_error)
		return open_error;
	h->sc_parent->refs++;
	return 0;
}
static void
ihidev_close(struct ihidev *h)
{
	assert(h->sc_parent->refs);
	h->sc_parent->refs--;
}
static int
ihidev_get_report_poll(device_t d, int t, int id, void *b, int n, bool poll)
{
	assert(poll == expect_poll);
	get_calls++;
	*(uint8_t *)b = 0xa0;
	return get_error;
}
static int
ihidev_set_report_poll(device_t d, int t, int id, void *b, int n, bool poll)
{
	assert(poll == expect_poll);
	set_calls++;
	last_set_mode = *(uint8_t *)b;
	if (!set_error)
		((struct ihidev_softc *)d->private)->mode = 3;
	return set_error;
}
static int
__attribute__((unused)) ihidev_get_report(device_t d, int t, int id, void *b, int n)
{
	return ihidev_get_report_poll(d, t, id, b, n, false);
}
static int
__attribute__((unused)) ihidev_set_report(device_t d, int t, int id, void *b, int n)
{
	return ihidev_set_report_poll(d, t, id, b, n, false);
}
#endif /* IMT_DOUBLES_H */
