/* SPDX-License-Identifier: BSD-2-Clause */
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
struct sysctlnode;
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
static unsigned events;
static unsigned last_buttons;
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
static void
getnanouptime(struct timespec *t)
{
	*t = (struct timespec){1, 0};
}
static void
wsmouse_input(device_t d, unsigned b, int x, int y, int z, int w, int f)
{
	events++;
	last_buttons = b;
}
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
	*c = (struct hidmt_config){.config_size = 1, .config_id = 3, .input_id = 4, .mode = {.size = 2, .pos = 0}};
	return 0;
}
int
hidmt_decode(const struct hidmt_config *c, const uint8_t *b, unsigned n, struct hidmt_frame *f)
{
	*f = (struct hidmt_frame){0};
	return 0;
}
void
hidmt_pointer_update(struct hidmt_pointer *p, const struct hidmt_frame *f, uint64_t t, bool tap, struct hidmt_motion *m)
{
	*m = (struct hidmt_motion){.tap = p->tap_candidate};
}
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
	*(uint8_t *)b = 0xa0;
	return get_error;
}
static int
ihidev_set_report_poll(device_t d, int t, int id, void *b, int n, bool poll)
{
	assert(poll == expect_poll);
	assert(*(uint8_t *)b == 0xa3);
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
#include "adapter.c"
int
main(void)
{
	struct imt_softc sc = {0};
	struct device child = {.private = &sc}, mouse = {0}, parent = {0};
	struct ihidev_softc bus = {.sc_dev = &parent};
	struct ihidev_attach_arg aux = {.parent = &bus, .reportid = 4};
	parent.private = &bus;
	test_mouse = &mouse;
	imt_attach(&parent, &child, &aux);
	assert(suspend_cb && resume_cb);
	assert(imt_enable(&sc) == 0 && bus.refs == 1 && bus.mode == 3);
	assert(imt_enable(&sc) == EBUSY && bus.refs == 1);
	sc.sc_pointer.tap_candidate = true;
	assert(suspend_cb(&child, NULL));
	assert(!sc.sc_pointer.tap_candidate && last_buttons == 0);
	assert(imt_enable(&sc) == EBUSY && bus.refs == 1);
	unsigned before = events;
	imt_intr(&sc.sc_hdev, NULL, 0);
	assert(events == before);
	bus.mode = 0;		/* Parent resumes first and resets the device
				 * to mouse mode. */
	expect_poll = true;
	assert(resume_cb(&child, NULL));
	assert(bus.mode == 3 && bus.refs == 1);
	imt_intr(&sc.sc_hdev, NULL, 0);
	assert(events == before + 1 && last_buttons == 0);
	assert(suspend_cb(&child, NULL));
	bus.mode = 0;
	set_error = EIO;
	assert(!resume_cb(&child, NULL) && bus.mode == 0 && bus.refs == 1);
	before = events;
	imt_intr(&sc.sc_hdev, NULL, 0);
	assert(events == before);
	set_error = 0;
	get_error = EIO;
	assert(!resume_cb(&child, NULL) && bus.refs == 1);
	get_error = 0;
	sc.sc_pointer.tap_candidate = true;
	assert(resume_cb(&child, NULL) && bus.mode == 3 && bus.refs == 1);
	assert(!sc.sc_pointer.tap_candidate);
	imt_disable(&sc);
	assert(bus.refs == 0);
	assert(suspend_cb(&child, NULL));
	bus.mode = 0;
	assert(resume_cb(&child, NULL) && bus.mode == 0 && bus.refs == 0);
	expect_poll = false;
	get_error = EIO;
	assert(imt_enable(&sc) == EIO && bus.refs == 0);
	get_error = 0;
	assert(imt_enable(&sc) == 0 && bus.refs == 1);
	assert(suspend_cb(&child, NULL));
	imt_disable(&sc);
	assert(bus.refs == 0);
	expect_poll = true;
	bus.mode = 0;
	assert(resume_cb(&child, NULL) && bus.mode == 0 && bus.refs == 0);
	assert(imt_detach(&child, 0) == 0);
	assert(hidmt_test_allocations == 0);
	puts("imt adapter suspend/resume contract: PASS");
	return 0;
}
