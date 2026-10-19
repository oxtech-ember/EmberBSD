/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual shared core, typed DISP adapter and hold probe; controlled services. */
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <libfdt.h>
#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define htole32 OSSwapHostToLittleInt32
#define le32toh OSSwapLittleToHostInt32
#elif defined(__linux__)
#include <endian.h>
#else
#include <sys/endian.h>
#endif
#ifndef __aligned
#define __aligned(n) __attribute__((__aligned__(n)))
#endif
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif
#ifndef __CTASSERT
#define __CTASSERT(e) _Static_assert(e, #e)
#endif
#ifndef EPROGMISMATCH
#define EPROGMISMATCH ENOEXEC
#endif
static atomic_uint checks;
#define CHECK(e) do { atomic_fetch_add(&checks, 1); if (!(e)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); } } while (0)
#include "display.h"
#include "clockvar.h"
#include "properties.h"
#include "clock-core.h"
/* Reuse only the existing contract's firmware service, not its test cases. */
#include "clock-firmware.h"
__CTASSERT(BCM2712_CLOCK_HSM == 0 && BCM2712_CLOCK_BVB == 1 && BCM2712_CLOCK_DISP == 2);
__CTASSERT(sizeof(struct bcm2712_clock_status) == 32 && sizeof(struct bcm2712_clock) == 64);

typedef void *device_t;
typedef void *ACPI_HANDLE;
typedef int modcmd_t;
typedef struct { pthread_mutex_t native; bool live; } kmutex_t;
typedef struct { unsigned int next; } deviter_t;
#define MODULE(a,b,c)
#define MODULE_CLASS_MISC 0
#define MODULE_CMD_INIT 1
#define MODULE_CMD_FINI 2
#define DEVITER_F_ROOT_FIRST 0
#define DRM_MINOR_RENDER 2
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define KM_SLEEP 0
#define BCMMBOX_CHANARM2VC 8
#define ACPI_FAILURE(e) ((e) != 0)
#define IS_ERR(p) ((intptr_t)(p) < 0)
static _Thread_local bool in_irq, in_softirq;
static _Thread_local unsigned int lock_depth;
#define cpu_intr_p() in_irq
#define cpu_softintr_p() in_softirq
#define ASSERT_SLEEPABLE() CHECK(!in_irq && !in_softirq)
#include "disp-types.h"
#include "hsm-types.h"
#include "bvb-types.h"
#include "core-types.h"
struct properties { const char *expected, *mode, *conflict; bool cold, bad_type; };
typedef struct properties *prop_dictionary_t;
struct drm_driver { const char *name; };
struct drm_minor;
struct drm_device { device_t dev; struct drm_driver *driver; bool registered; struct drm_minor *render; };
struct drm_minor { int type; struct drm_device *dev; };
struct acpi_devnode { device_t ad_device; };
const char buildinfo[] = "EmberBSD:accepted-disp-kernel\n";
static int device_token, fb_token;
static struct drm_driver driver;
static struct drm_device drm;
static struct drm_minor minor;
static struct acpi_devnode node;
static struct properties properties;
static struct firmware fw;
static unsigned int mutex_live, mutex_inits, mutex_destroys, resources, references;
static unsigned int allocated, freed;
static unsigned int unlock_after_error, interrupt_on_unlock;
static int resource_error;
static bool probe_test, find_device, right_driver, render, genfb, node_missing;
static uint64_t early_fb;
static char blob[4096] __aligned(8), log_text[16384];
static const void *tree;
static size_t log_size;
static atomic_uint transport_active, transport_max;
static atomic_bool threads_start;
static void destroyed_check(void);

static void
mutex_init(kmutex_t *m, int type, int ipl)
{
	CHECK(!m->live && type == MUTEX_DEFAULT && ipl == IPL_NONE);
	CHECK(pthread_mutex_init(&m->native, NULL) == 0);
	m->live = true; mutex_live++; mutex_inits++;
}
static void
mutex_enter(kmutex_t *m)
{
	CHECK(m->live && lock_depth == 0);
	CHECK(pthread_mutex_lock(&m->native) == 0);
	lock_depth++;
}
static void
mutex_exit(kmutex_t *m)
{
	CHECK(m->live && lock_depth == 1);
	lock_depth--;
	CHECK(pthread_mutex_unlock(&m->native) == 0);
	if (interrupt_on_unlock && fw.fail_call == fw.calls &&
	    ++unlock_after_error == interrupt_on_unlock)
		in_irq = true;
}
static void
mutex_destroy(kmutex_t *m)
{
	CHECK(m->live && lock_depth == 0 && mutex_live == 1);
	destroyed_check();
	CHECK(pthread_mutex_destroy(&m->native) == 0);
	m->live = false; mutex_live--; mutex_destroys++;
}
struct once { pthread_mutex_t mutex; bool done; int error; };
#define ONCE_DECL(o) struct once o = { PTHREAD_MUTEX_INITIALIZER, false, 0 }
static int
run_once(struct once *o, int (*fn)(void))
{
	int error;

	CHECK(pthread_mutex_lock(&o->mutex) == 0);
	if (!o->done) { o->error = fn(); o->done = true; }
	error = o->error;
	CHECK(pthread_mutex_unlock(&o->mutex) == 0);
	return error;
}
#define RUN_ONCE(o,fn) run_once(o,fn)
static void *
kmem_zalloc(size_t size, int flag)
{
	void *p;

	CHECK(flag == KM_SLEEP && lock_depth == 1);
	p = calloc(1, size); CHECK(p != NULL); allocated++;
	return p;
}
static void
kmem_free(void *p, size_t size)
{
	CHECK(p != NULL && size != 0); freed++; free(p);
}
static int
bcm2712_display_acpi_resources(device_t dev, ACPI_HANDLE handle,
    struct bcm2712_display_contract *out)
{
	CHECK(dev == &device_token && lock_depth == 1);
	CHECK(!probe_test || references == 1);
	resources++;
	if (resource_error) return resource_error;
	if (handle != dev) return EINVAL;
	memset(out, 0, sizeof(*out));
	return 0;
}
static int
bcmmbox_request(unsigned int channel, void *request, size_t length, uint32_t *reply)
{
	unsigned int active, previous;
	int error;

	CHECK(channel == 8 && lock_depth == 1 && fw.id == 16);
	if (le32toh(((uint32_t *)request)[2]) == VCPROPTAG_SET_CLOCKRATE)
		CHECK(le32toh(((uint32_t *)request)[6]) != 0);
	CHECK(!probe_test || references == 1);
	active = atomic_fetch_add(&transport_active, 1) + 1;
	CHECK(active == 1);
	previous = atomic_load(&transport_max);
	while (previous < active && !atomic_compare_exchange_weak(&transport_max, &previous, active))
		continue;
	sched_yield();
	error = exchange(&fw, request, length);
	atomic_fetch_sub(&transport_active, 1);
	*reply = 0;
	return error;
}
#include "disp-native.h"
static void
destroyed_check(void)
{
	CHECK(bcm2712_disp_owner == NULL && lock_depth == 0);
	CHECK(atomic_load(&transport_active) == 0);
}
static bool
prop_dictionary_get_bool(prop_dictionary_t p, const char *name, bool *out)
{ CHECK(strcmp(name, "cold-headless") == 0); *out = p->cold; return true; }
static bool
prop_dictionary_get_cstring_nocopy(prop_dictionary_t p, const char *name, const char **out)
{
	if (strcmp(name, "expected-buildinfo") == 0) { *out = p->expected; return *out != NULL; }
	CHECK(strcmp(name, "disp-mode") == 0); *out = p->mode;
	return *out != NULL && !p->bad_type;
}
static void *prop_dictionary_get(prop_dictionary_t p, const char *name)
{ return p->conflict && strcmp(p->conflict, name) == 0 ? p : NULL; }
static device_t device_find_by_xname(const char *name)
{ CHECK(strcmp(name, "bcmv3d0") == 0); return find_device ? &device_token : NULL; }
static bool
device_is_a(device_t dev, const char *name)
{
	return (dev == &device_token && right_driver && strcmp(name, "bcmv3d") == 0) ||
	    (dev == &fb_token && genfb && strcmp(name, "genfb") == 0);
}
static struct drm_minor *drm_minor_acquire(unsigned int id)
{ CHECK(id == 128 && references == 0); if (!render) return (void *)(intptr_t)-ENODEV; references++; return &minor; }
static void
drm_minor_release(struct drm_minor *value)
{
	CHECK(value == &minor && references == 1 && lock_depth == 0 && bcm2712_disp_owner == NULL);
	references--;
}
static int AcpiGetHandle(ACPI_HANDLE parent, const char *path, ACPI_HANDLE *out)
{ CHECK(parent == NULL && strcmp(path, "\\_SB.GPU0") == 0); *out = &device_token; return 0; }
static const struct acpi_devnode *acpi_match_node(ACPI_HANDLE handle)
{ CHECK(handle == &device_token); return node_missing ? NULL : &node; }
static void deviter_init(deviter_t *i, int flags)
{ CHECK(flags == DEVITER_F_ROOT_FIRST); i->next = 0; }
static device_t deviter_next(deviter_t *i)
{ return i->next++ == 0 && genfb ? &fb_token : NULL; }
static void deviter_release(deviter_t *i) { (void)i; }
static uint64_t arm_simplefb_physaddr(void) { return early_fb; }
static const void *fdtbus_get_data(void) { return tree; }
static void
capture(const char *format, va_list ap)
{
	int length = vsnprintf(log_text + log_size, sizeof(log_text) - log_size, format, ap);

	CHECK(length >= 0 && (size_t)length < sizeof(log_text) - log_size);
	log_size += (size_t)length;
}
static void aprint_normal(const char *f, ...)
{ va_list ap; va_start(ap, f); capture(f, ap); va_end(ap); }
static void aprint_error(const char *f, ...)
{ va_list ap; va_start(ap, f); capture(f, ap); va_end(ap); }
#include "disp-probe.h"

/* Harness-only reboot. It never represents successful native fault cleanup. */
static void
fixture_reboot(void)
{
	CHECK(lock_depth == 0 && atomic_load(&transport_active) == 0);
	if (bcm2712_disp_owner != NULL) {
		kmem_free(bcm2712_disp_owner, sizeof(*bcm2712_disp_owner));
		bcm2712_disp_owner = NULL;
	}
	CHECK(bcm2712_hsm_owner == NULL && bcm2712_bvb_owner == NULL && bcm2712_core_owner == NULL);
	if (bcm2712_clock_lock.live) mutex_destroy(&bcm2712_clock_lock);
	memset(&bcm2712_clock_lock, 0, sizeof(bcm2712_clock_lock));
	bcm2712_clock_once.done = false; bcm2712_clock_once.error = 0;
	CHECK(allocated == freed); allocated = freed = 0;
	memset(&disp_probe, 0, sizeof(disp_probe));
	mutex_live = mutex_inits = mutex_destroys = references = resources = 0;
	interrupt_on_unlock = unlock_after_error = 0;
	in_irq = in_softirq = false;
}
static void
reset(uint32_t state, uint32_t rate)
{
	fixture_reboot();
	fw = firmware_new(BCM2712_CLOCK_BVB, state, rate); fw.id = 16;
	resource_error = 0; probe_test = false;
	find_device = right_driver = render = true; genfb = node_missing = false; early_fb = 0;
	driver.name = "v3d"; drm = (struct drm_device){ &device_token, &driver, true, &minor };
	minor = (struct drm_minor){ DRM_MINOR_RENDER, &drm }; node.ad_device = &device_token;
	properties = (struct properties){ "EmberBSD:accepted-disp-kernel", "hold", NULL, true, false };
	CHECK(fdt_create_empty_tree(blob, sizeof(blob)) == 0); tree = blob;
	log_size = 0; log_text[0] = 0;
	atomic_store(&transport_active, 0); atomic_store(&transport_max, 0);
}
static struct bcm2712_disp_provider *
create(void)
{
	struct bcm2712_disp_provider *owner = NULL;

	CHECK(bcm2712_display_disp_create(&device_token, &device_token, &owner) == 0);
	CHECK(owner != NULL && fw.calls == 4 && fw.sets == 0);
	return owner;
}
static void
closed(void)
{
	CHECK(bcm2712_disp_owner == NULL && allocated == freed);
	CHECK(mutex_destroys == 0 && mutex_inits == mutex_live && references == 0);
}
static void
native_lifecycle(void)
{
	struct bcm2712_disp_provider unknown, *owner, *poison, *second;
	struct bcm2712_disp_status status, expected;
	unsigned int state, high, before, step, word;
	static const unsigned int fields[] = { 0, 1, 2, 3, 4, 5, 9 };

	memset(&unknown, 0xa5, sizeof(unknown));

	for (state = 0; state < 2; state++) for (high = 0; high < 2; high++) {
		reset(state, high ? 240000000 : 0); owner = create(); before = fw.calls;
		CHECK(bcm2712_disp_owner == owner && mutex_live == 1);
		second = (void *)(uintptr_t)0x1234;
		CHECK(bcm2712_display_disp_create(&device_token, &device_token, &second) == EBUSY);
		CHECK(second == (void *)(uintptr_t)0x1234 && fw.calls == before);
		memset(&status, 0xa5, sizeof(status)); memset(&expected, 0, sizeof(expected));
		expected.floor = fw.minimum; expected.state = state; expected.rate = fw.rate;
		CHECK(bcm2712_display_disp_status(owner, &status) == 0 && fw.calls == before);
		CHECK(memcmp(&status, &expected, sizeof(status)) == 0);
		CHECK(bcm2712_display_disp_acquire(owner, 0, fw.minimum) == 0);
		CHECK(fw.rate == fw.minimum && fw.state == 1);
		CHECK(bcm2712_display_disp_acquire(owner, 1, 100000000) == 0 && fw.rate == 100000000);
		CHECK(bcm2712_display_disp_update(owner, 0, 150000000) == 0 && fw.rate == 150000000);
		CHECK(bcm2712_display_disp_release(owner, 0) == 0 && fw.rate == 100000000);
		CHECK(bcm2712_display_disp_destroy(owner) == EBUSY);
		CHECK(bcm2712_display_disp_release(owner, 1) == 0 && fw.rate == fw.minimum);
		CHECK(bcm2712_display_disp_destroy(owner) == 0);
		CHECK(fw.state == state && fw.disables == (state == 0));
		closed();
	}
	reset(0, 0); owner = create(); before = fw.calls;
	CHECK(bcm2712_display_disp_destroy(owner) == 0 && fw.calls == before && fw.sets == 0);
	closed();
	for (step = 1; step <= 4; step++) for (word = 0; word <= __arraycount(fields); word++) {
		reset(0, 0);
		poison = (void *)(uintptr_t)0x1234;
		if (word == __arraycount(fields)) fw.fail_call = step;
		else { fw.bad_call = step; fw.bad_word = fields[word]; fw.bad_value = UINT32_MAX; }
		CHECK(bcm2712_display_disp_create(&device_token, &device_token, &poison) == EIO);
		CHECK(poison == (void *)(uintptr_t)0x1234 && fw.sets == 0 && bcm2712_disp_owner == NULL);
		closed();
	}
	reset(0, 0); fw.minimum = 0;
	poison = (void *)(uintptr_t)0x1234;
	CHECK(bcm2712_display_disp_create(&device_token, &device_token, &poison) == ERANGE);
	CHECK(fw.sets == 0 && poison == (void *)(uintptr_t)0x1234);
	closed();
	reset(0, 0); fw.minimum = fw.maximum + 1;
	CHECK(bcm2712_display_disp_create(&device_token, &device_token, &poison) == ERANGE && fw.sets == 0);
	closed();
	reset(0, 0); owner = create(); before = fw.calls;
	memset(&status, 0xa5, sizeof(status)); expected = status;
	CHECK(bcm2712_display_disp_status(&unknown, &status) == ENXIO);
	CHECK(memcmp(&status, &expected, sizeof(status)) == 0 && fw.calls == before);
	CHECK(bcm2712_display_disp_acquire((void *)(uintptr_t)1, 0, 1) == ENXIO);
	CHECK(bcm2712_display_disp_update((void *)(uintptr_t)1, 0, 1) == ENXIO);
	CHECK(bcm2712_display_disp_release((void *)(uintptr_t)1, 0) == ENXIO);
	CHECK(bcm2712_display_disp_destroy((void *)(uintptr_t)1) == ENXIO);
	in_irq = true; CHECK(bcm2712_display_disp_acquire(owner, 0, 1) == EWOULDBLOCK);
	in_irq = false; in_softirq = true; CHECK(bcm2712_display_disp_status(owner, &status) == EWOULDBLOCK);
	in_softirq = false; CHECK(fw.calls == before);
	CHECK(bcm2712_display_disp_destroy(owner) == 0); closed();
}
static void
native_faults(void)
{
	struct bcm2712_disp_provider *owner, *second;
	struct bcm2712_disp_status status;
	unsigned int step, before;

	for (step = 1; step <= 7; step++) {
		reset(0, 0); owner = create();
		CHECK(bcm2712_display_disp_acquire(owner, 0, 100000000) == 0);
		fw.fail_call = fw.calls + step; fw.fail_after_set = true;
		CHECK(bcm2712_display_disp_release(owner, 0) == EIO);
		CHECK(bcm2712_display_disp_status(owner, &status) == 0);
		CHECK(status.leases == 1 && status.minimum[0] == 100000000);
		CHECK(status.fault == (step <= 3 ? 0 : EIO));
		CHECK(bcm2712_disp_owner == owner && mutex_live == 1);
		if (step <= 3) {
			fw.fail_call = 0;
			CHECK(bcm2712_display_disp_release(owner, 0) == 0);
			CHECK(bcm2712_display_disp_destroy(owner) == 0);
			closed();
		} else {
			before = fw.calls;
			CHECK(bcm2712_display_disp_destroy(owner) == EIO);
			CHECK(bcm2712_display_disp_update(owner, 0, 1) == EIO);
			CHECK(bcm2712_display_disp_release(owner, 0) == EIO && fw.calls == before);
			second = (void *)(uintptr_t)0x1234;
			CHECK(bcm2712_display_disp_create(&device_token, &device_token, &second) == EBUSY);
			CHECK(second == (void *)(uintptr_t)0x1234 && bcm2712_disp_owner == owner);
		}
	}
	for (step = 1; step <= 2; step++) {
		reset(0, 0); owner = create();
		CHECK(bcm2712_display_disp_acquire(owner, 0, fw.minimum) == 0);
		CHECK(bcm2712_display_disp_release(owner, 0) == 0);
		fw.fail_call = fw.calls + step; fw.fail_after_set = true;
		CHECK(bcm2712_display_disp_destroy(owner) == EIO);
		CHECK(bcm2712_display_disp_status(owner, &status) == 0 && status.fault == EIO);
		before = fw.calls;
		CHECK(bcm2712_display_disp_destroy(owner) == EIO && fw.calls == before);
		CHECK(bcm2712_disp_owner == owner && mutex_live == 1);
	}
	reset(0, 0); owner = create(); fw.round = 1000;
	CHECK(bcm2712_display_disp_acquire(owner, 0, fw.minimum) == 0);
	CHECK(fw.rate == fw.minimum + 1000);
	CHECK(bcm2712_display_disp_release(owner, 0) == 0);
	CHECK(bcm2712_display_disp_destroy(owner) == 0); closed();
}

static int
probe_run(void)
{
	probe_test = true;
	return bcm2712_disp_probe_modcmd(MODULE_CMD_INIT, &properties);
}
static void
probe_tests(void)
{
	static const char * const conflicts[] = { "clock-mode", "dvp-mode", "hd-mode",
	    "hpd-mode", "ddc-mode", "phy-mode", "iommu-mode" };
	unsigned int i, before, variant;

	for (i = 0; i < __arraycount(conflicts); i++) {
		reset(0, 0); properties.conflict = conflicts[i]; CHECK(probe_run() == EINVAL);
		CHECK(fw.calls == 0 && resources == 0 && references == 0 && mutex_inits == 0);
	}
	reset(0, 0); properties.cold = false; CHECK(probe_run() == EINVAL && fw.calls == 0);
	reset(0, 0); properties.expected = "wrong"; CHECK(probe_run() == EPROGMISMATCH && fw.calls == 0);
	reset(0, 0); properties.mode = "other"; CHECK(probe_run() == EINVAL && fw.calls == 0);
	reset(0, 0); properties.bad_type = true; CHECK(probe_run() == EINVAL && fw.calls == 0);
	reset(0, 0); early_fb = 1; CHECK(probe_run() == EBUSY && fw.calls == 0);
	reset(0, 0); genfb = true; CHECK(probe_run() == EBUSY && fw.calls == 0);
	reset(0, 0); tree = NULL; CHECK(probe_run() == EIO && fw.calls == 0);
	reset(0, 0); find_device = false; CHECK(probe_run() == ENODEV && fw.calls == 0);
	reset(0, 0); node_missing = true; CHECK(probe_run() == ENODEV && fw.calls == 0);
	reset(0, 0); resource_error = EIO; CHECK(probe_run() == EIO); closed();
	for (i = 1; i <= 12; i++) {
		reset(0, 0); fw.fail_call = i; fw.fail_after_set = true;
		CHECK(probe_run() == (i < 8 ? EIO : 0));
		CHECK(fw.calls == i && strstr(log_text, "hold verified") == NULL);
		if (i < 8) {
			closed(); CHECK(mutex_destroys == 0 && fw.sets == 0);
			CHECK(bcm2712_disp_probe_modcmd(MODULE_CMD_FINI, NULL) == 0);
			fw.fail_call = 0;
			CHECK(probe_run() == 0 && mutex_inits == 1 && mutex_destroys == 0);
		} else {
			CHECK(disp_probe.held && references == 1 && mutex_live == 1 && bcm2712_disp_owner != NULL);
			CHECK(disp_probe.owner->core.status.fault == EIO && disp_probe.owner->core.status.leases == 0);
		}
		before = fw.calls;
		CHECK(bcm2712_disp_probe_modcmd(MODULE_CMD_FINI, NULL) == EBUSY);
		CHECK(probe_run() == EBUSY && fw.calls == before && mutex_live == 1);
	}
	reset(0, 0); fw.minimum = 0; CHECK(probe_run() == ERANGE); closed();
	CHECK(strstr(log_text, "hold verified") == NULL && fw.sets == 0);
	fw.minimum = 23000000; CHECK(probe_run() == 0 && references == 1 && mutex_live == 1);
	CHECK(strstr(log_text, "hold verified") != NULL && fw.state == 1 && fw.rate == fw.minimum);
	CHECK(disp_probe.owner->core.status.leases == 1);
	for (variant = 2; variant <= 3; variant++) {
		reset(0, 0); fw.fail_call = 5; interrupt_on_unlock = variant;
		if (variant == 2) {
			CHECK(probe_run() == 0 && disp_probe.held && references == 1 && mutex_live == 1);
			CHECK(strstr(log_text, "at destroy") != NULL);
			CHECK(bcm2712_disp_probe_modcmd(MODULE_CMD_FINI, NULL) == EBUSY);
		} else {
			CHECK(probe_run() == EIO); closed();
			CHECK(bcm2712_disp_probe_modcmd(MODULE_CMD_FINI, NULL) == 0);
		}
		CHECK(mutex_destroys == 0 && strstr(log_text, "hold verified") == NULL);
	}
}
static void *
worker(void *arg)
{
	unsigned int slot = (unsigned int)(uintptr_t)arg;

	while (!atomic_load(&threads_start)) sched_yield();
	CHECK(bcm2712_display_disp_acquire(bcm2712_disp_owner, slot, 50000000 + slot * 10000000) == 0);
	return NULL;
}
static void
concurrency(void)
{
	struct bcm2712_disp_provider *owner;
	pthread_t a, b;

	reset(0, 0); owner = create(); atomic_store(&threads_start, false);
	CHECK(pthread_create(&a, NULL, worker, (void *)(uintptr_t)0) == 0);
	CHECK(pthread_create(&b, NULL, worker, (void *)(uintptr_t)1) == 0);
	atomic_store(&threads_start, true);
	CHECK(pthread_join(a, NULL) == 0 && pthread_join(b, NULL) == 0);
	CHECK(atomic_load(&transport_max) == 1 && owner->core.status.leases == 3 && fw.rate == 60000000);
	CHECK(bcm2712_display_disp_release(owner, 0) == 0 && bcm2712_display_disp_release(owner, 1) == 0);
	CHECK(bcm2712_display_disp_destroy(owner) == 0); closed();
}
int
main(void)
{
	native_lifecycle(); native_faults(); probe_tests(); concurrency(); fixture_reboot();
	printf("DISP16 actual-source contract: %u checks PASS\n", atomic_load(&checks));
	return 0;
}
