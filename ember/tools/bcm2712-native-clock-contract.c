/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual native adapter/common core. Only kernel/ACPI/mailbox services fake. */
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
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif
#ifndef __CTASSERT
#define __CTASSERT(e) _Static_assert(e, #e)
#endif
static atomic_uint checks;
#define CHECK(e) do { atomic_fetch_add(&checks, 1); if (!(e)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); exit(1); } } while (0)
#include "display.h"
#include "clockvar.h"
#include "hsm-types.h"
#include "properties.h"
#include "clock-core.h"
/* Reuse the actual shared-core test's firmware service, not its test cases. */
#include "clock-firmware.h"

static struct firmware firmware[4];
static unsigned int allocated, freed, resource_calls, mailbox_calls, lock_calls;
static int resource_error, device_token;
static _Thread_local bool in_irq, in_softirq;
static _Thread_local unsigned int lock_depth;
static atomic_uint transport_active, transport_max;
static atomic_bool workers_start;
typedef void *device_t;
typedef void *ACPI_HANDLE;
typedef pthread_mutex_t kmutex_t;
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define KM_SLEEP 0
#define BCMMBOX_CHANARM2VC 8
#define ASSERT_SLEEPABLE() CHECK(!in_irq && !in_softirq)
#define cpu_intr_p() in_irq
#define cpu_softintr_p() in_softirq
#include "acpi-types.h"
#include "bvb-types.h"
#include "core-types.h"
#include "disp-types.h"

static void
mutex_init(kmutex_t *m, int type, int ipl)
{
	CHECK(type == MUTEX_DEFAULT && ipl == IPL_NONE);
	CHECK(pthread_mutex_init(m, NULL) == 0);
}
static void
mutex_enter(kmutex_t *m)
{
	CHECK(lock_depth == 0);
	CHECK(pthread_mutex_lock(m) == 0);
	lock_depth++;
	lock_calls++;
}
static void
mutex_exit(kmutex_t *m)
{
	CHECK(lock_depth == 1);
	lock_depth--;
	CHECK(pthread_mutex_unlock(m) == 0);
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
	p = calloc(1, size);
	CHECK(p != NULL);
	allocated++;
	return p;
}
static void
kmem_free(void *p, size_t size)
{
	CHECK(p != NULL && size != 0);
	freed++;
	free(p);
}
int
bcm2712_display_acpi_resources(device_t dev, ACPI_HANDLE handle,
    struct bcm2712_display_contract *out)
{
	CHECK(dev == &device_token && out != NULL && lock_depth == 1);
	resource_calls++;
	if (handle != &device_token)
		return EINVAL;
	if (resource_error != 0)
		return resource_error;
	memset(out, 0, sizeof(*out));
	return 0;
}
static int
bcmmbox_request(unsigned int channel, void *request, size_t length,
    uint32_t *response)
{
	uint32_t *packet = request;
	uint32_t id = le32toh(packet[5]);
	unsigned int active;
	int error;

	CHECK(channel == BCMMBOX_CHANARM2VC && response != NULL);
	CHECK(lock_depth == 1 && !in_irq && !in_softirq);
	CHECK(id == VCPROP_CLK_M2MC || id == VCPROP_CLK_PIXEL_BVB || id == VCPROP_CLK_CORE || id == VCPROP_CLK_DISP);
	active = atomic_fetch_add(&transport_active, 1) + 1;
	CHECK(active == 1);
	atomic_store(&transport_max, active);
	sched_yield();
	mailbox_calls++;
	*response = 0x1000;
	error = exchange(&firmware[id == VCPROP_CLK_M2MC ? 0 : id == VCPROP_CLK_CORE ? 2 : id == VCPROP_CLK_DISP ? 3 : 1],
	    packet, length);
	CHECK(atomic_fetch_sub(&transport_active, 1) == 1);
	return error;
}
#include "native-clock.h"

/* These are the accepted public HSM layout values, not a replacement type. */
_Static_assert(sizeof(struct bcm2712_hsm_status) == 28, "HSM status size");
_Static_assert(offsetof(struct bcm2712_hsm_status, minimum) == 0, "minimum");
_Static_assert(offsetof(struct bcm2712_hsm_status, rate) == 8, "rate");
_Static_assert(offsetof(struct bcm2712_hsm_status, state) == 12, "state");
_Static_assert(offsetof(struct bcm2712_hsm_status, leases) == 16, "leases");
_Static_assert(offsetof(struct bcm2712_hsm_status, fault) == 20, "fault");
_Static_assert(offsetof(struct bcm2712_hsm_status, mutation_attempted) == 24,
    "mutation");
#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(struct bcm2712_hsm) == 56, "64-bit HSM core size");
_Static_assert(offsetof(struct bcm2712_hsm, status) == 16, "core status");
_Static_assert(offsetof(struct bcm2712_hsm, floor) == 44, "core floor");
_Static_assert(offsetof(struct bcm2712_hsm, initial_state) == 48, "initial state");
_Static_assert(offsetof(struct bcm2712_hsm, initialized) == 52, "initialized");
_Static_assert(offsetof(struct bcm2712_hsm, rate_changed) == 53, "rate changed");
_Static_assert(offsetof(struct bcm2712_hsm, state_changed) == 54, "state changed");
#endif

static void
reset(void)
{
	CHECK(bcm2712_hsm_owner == NULL && bcm2712_bvb_owner == NULL && bcm2712_core_owner == NULL && bcm2712_disp_owner == NULL);
	CHECK(allocated == freed && atomic_load(&transport_active) == 0);
	firmware[0] = firmware_new(BCM2712_CLOCK_HSM, 0, 0);
	firmware[1] = firmware_new(BCM2712_CLOCK_BVB, 0, 0);
	firmware[2] = firmware_new(BCM2712_CLOCK_CORE, 0, 120000000);
	firmware[3] = firmware_new(BCM2712_CLOCK_DISP, 0, 0); firmware[3].id = 16;
	resource_error = 0;
	resource_calls = mailbox_calls = 0;
	atomic_store(&transport_max, 0);
}
/* Test reboot only. A production faulted slot is retained, never freed here. */
static void
reboot_fixture(void)
{
	if (bcm2712_hsm_owner != NULL) {
		kmem_free(bcm2712_hsm_owner, sizeof(*bcm2712_hsm_owner));
		bcm2712_hsm_owner = NULL;
	}
	if (bcm2712_bvb_owner != NULL) {
		kmem_free(bcm2712_bvb_owner, sizeof(*bcm2712_bvb_owner));
		bcm2712_bvb_owner = NULL;
	}
	if (bcm2712_core_owner != NULL) {
		kmem_free(bcm2712_core_owner, sizeof(*bcm2712_core_owner));
		bcm2712_core_owner = NULL;
	}
	if (bcm2712_disp_owner != NULL) {
		kmem_free(bcm2712_disp_owner, sizeof(*bcm2712_disp_owner));
		bcm2712_disp_owner = NULL;
	}
	CHECK(allocated == freed);
}

static void
refusal_tests(void)
{
	struct bcm2712_hsm_provider hsm_unknown, *hsm = &hsm_unknown;
	struct bcm2712_bvb_provider bvb_unknown, *bvb = &bvb_unknown;
	struct bcm2712_hsm_status hs, hs_saved;
	struct bcm2712_bvb_status bs, bs_saved;
	unsigned int context, locks;

	memset(&hsm_unknown, 0xa5, sizeof(hsm_unknown));
	memset(&bvb_unknown, 0xa5, sizeof(bvb_unknown));
	memset(&hs, 0xa5, sizeof(hs)); hs_saved = hs;
	memset(&bs, 0xa5, sizeof(bs)); bs_saved = bs;
	reset();
	CHECK(bcm2712_display_hsm_create(NULL, &device_token, &hsm) == EINVAL);
	CHECK(bcm2712_display_bvb_create(NULL, &device_token, &bvb) == EINVAL);
	CHECK(bcm2712_display_hsm_create(&device_token, &device_token, NULL) == EINVAL);
	CHECK(bcm2712_display_bvb_create(&device_token, &device_token, NULL) == EINVAL);
	for (context = 0; context < 3; context++) {
		in_irq = context == 1; in_softirq = context == 2; locks = lock_calls;
		CHECK(bcm2712_display_hsm_create(&device_token, &device_token, &hsm) == (context ? EWOULDBLOCK : 0));
		CHECK(bcm2712_display_bvb_create(&device_token, &device_token, &bvb) == (context ? EWOULDBLOCK : 0));
		if (context == 0) {
			CHECK(bcm2712_display_hsm_destroy(hsm) == 0);
			CHECK(bcm2712_display_bvb_destroy(bvb) == 0);
			reset();
		} else {
			CHECK(lock_calls == locks && resource_calls == 0 && mailbox_calls == 0);
		}
		CHECK(bcm2712_display_hsm_status(NULL, NULL) == EINVAL);
		CHECK(bcm2712_display_bvb_status(NULL, NULL) == EINVAL);
		CHECK(bcm2712_display_hsm_status(&hsm_unknown, &hs) == (context ? EWOULDBLOCK : ENXIO));
		CHECK(bcm2712_display_bvb_status(&bvb_unknown, &bs) == (context ? EWOULDBLOCK : ENXIO));
		CHECK(bcm2712_display_hsm_acquire(&hsm_unknown, 2, 0) == (context ? EWOULDBLOCK : ENXIO));
		CHECK(bcm2712_display_bvb_acquire(&bvb_unknown, 2, 0) == (context ? EWOULDBLOCK : ENXIO));
		CHECK(bcm2712_display_hsm_update(&hsm_unknown, 0, 1) == (context ? EWOULDBLOCK : ENXIO));
		CHECK(bcm2712_display_bvb_update(&bvb_unknown, 0, 1) == (context ? EWOULDBLOCK : ENXIO));
		CHECK(bcm2712_display_hsm_release(&hsm_unknown, 0) == (context ? EWOULDBLOCK : ENXIO));
		CHECK(bcm2712_display_bvb_release(&bvb_unknown, 0) == (context ? EWOULDBLOCK : ENXIO));
		CHECK(bcm2712_display_hsm_destroy(&hsm_unknown) == (context ? EWOULDBLOCK : ENXIO));
		CHECK(bcm2712_display_bvb_destroy(&bvb_unknown) == (context ? EWOULDBLOCK : ENXIO));
		CHECK(memcmp(&hs, &hs_saved, sizeof(hs)) == 0);
		CHECK(memcmp(&bs, &bs_saved, sizeof(bs)) == 0);
	}
	in_irq = in_softirq = false;
	CHECK(bcm2712_display_hsm_create(&device_token, NULL, &hsm) == EINVAL);
	CHECK(bcm2712_display_bvb_create(&device_token, NULL, &bvb) == EINVAL);
	resource_error = EOPNOTSUPP;
	hsm = &hsm_unknown; bvb = &bvb_unknown;
	CHECK(bcm2712_display_hsm_create(&device_token, &device_token, &hsm) == EOPNOTSUPP);
	CHECK(bcm2712_display_bvb_create(&device_token, &device_token, &bvb) == EOPNOTSUPP);
	CHECK(hsm == &hsm_unknown && bvb == &bvb_unknown && mailbox_calls == 0);
}

struct worker {
	struct bcm2712_hsm_provider *hsm;
	struct bcm2712_bvb_provider *bvb;
	unsigned int port;
	int error;
};
static void *
acquire_worker(void *arg)
{
	struct worker *worker = arg;

	while (!atomic_load(&workers_start)) sched_yield();
	worker->error = worker->hsm != NULL ?
	    bcm2712_display_hsm_acquire(worker->hsm, worker->port,
	    worker->port == 0 ? 150000000 : 240000000) :
	    bcm2712_display_bvb_acquire(worker->bvb, worker->port,
	    worker->port == 0 ? 70000000 : 110000000);
	return NULL;
}
static void
lifecycle_tests(void)
{
	struct bcm2712_hsm_provider *hsm, *hsm_other = NULL;
	struct bcm2712_bvb_provider *bvb, *bvb_other = NULL;
	struct bcm2712_hsm_status hs, hs_expected;
	struct bcm2712_bvb_status bs, bs_expected;
	struct worker worker[4];
	pthread_t threads[4];
	unsigned int calls, i;

	reset();
	CHECK(bcm2712_display_hsm_create(&device_token, &device_token, &hsm) == 0);
	CHECK(bcm2712_display_bvb_create(&device_token, &device_token, &bvb) == 0);
	CHECK(resource_calls == 2 && firmware[0].calls == 3 && firmware[1].calls == 4);
	CHECK(firmware[0].sets == 0 && firmware[1].sets == 0);
	CHECK(hsm->core.exchange == bcm2712_clock_exchange && hsm->core.arg == NULL);
	CHECK(bvb->core.exchange == bcm2712_clock_exchange && bvb->core.arg == NULL);
	calls = mailbox_calls;
	CHECK(bcm2712_display_hsm_create(&device_token, NULL, &hsm_other) == EBUSY);
	CHECK(bcm2712_display_bvb_create(&device_token, NULL, &bvb_other) == EBUSY);
	CHECK(hsm_other == NULL && bvb_other == NULL && mailbox_calls == calls && resource_calls == 2);
	memset(&hs, 0xa5, sizeof(hs)); memset(&hs_expected, 0, sizeof(hs_expected));
	memset(&bs, 0xa5, sizeof(bs)); memset(&bs_expected, 0, sizeof(bs_expected));
	bs_expected.floor = 23000000;
	CHECK(bcm2712_display_hsm_status(hsm, &hs) == 0);
	CHECK(bcm2712_display_bvb_status(bvb, &bs) == 0);
	CHECK(memcmp(&hs, &hs_expected, sizeof(hs)) == 0);
	CHECK(memcmp(&bs, &bs_expected, sizeof(bs)) == 0);
	CHECK(mailbox_calls == calls && hs.leases == 0 && bs.leases == 0);
	CHECK(hs.state == 0 && bs.state == 0 && bs.floor == 23000000);
	CHECK(bcm2712_display_hsm_acquire(hsm, 2, 1) == EINVAL);
	CHECK(bcm2712_display_bvb_acquire(bvb, 0, 0) == EINVAL);
	CHECK(bcm2712_display_hsm_update(hsm, 0, 1) == ENOENT);
	CHECK(bcm2712_display_bvb_release(bvb, 1) == ENOENT);
	CHECK(mailbox_calls == calls);
	atomic_store(&workers_start, false);
	for (i = 0; i < 4; i++) {
		worker[i] = (struct worker){ i < 2 ? hsm : NULL, i >= 2 ? bvb : NULL, i % 2, -1 };
		CHECK(pthread_create(&threads[i], NULL, acquire_worker, &worker[i]) == 0);
	}
	atomic_store(&workers_start, true);
	for (i = 0; i < 4; i++) {
		CHECK(pthread_join(threads[i], NULL) == 0);
		CHECK(worker[i].error == 0);
	}
	CHECK(atomic_load(&transport_max) == 1);
	calls = mailbox_calls;
	CHECK(bcm2712_display_hsm_status(hsm, &hs) == 0);
	CHECK(bcm2712_display_bvb_status(bvb, &bs) == 0);
	CHECK(mailbox_calls == calls && hs.leases == 3 && bs.leases == 3);
	CHECK(hs.rate == 240000000 && bs.rate == 110000000);
	CHECK(hs.minimum[0] == 150000000 && hs.minimum[1] == 240000000);
	CHECK(bs.minimum[0] == 70000000 && bs.minimum[1] == 110000000);
	CHECK(bcm2712_display_hsm_destroy(hsm) == EBUSY);
	CHECK(bcm2712_display_bvb_destroy(bvb) == EBUSY);
	CHECK(bcm2712_display_hsm_update(hsm, 0, 200000000) == 0);
	CHECK(bcm2712_display_bvb_update(bvb, 0, 90000000) == 0);
	CHECK(bcm2712_display_hsm_release(hsm, 1) == 0);
	CHECK(bcm2712_display_bvb_release(bvb, 1) == 0);
	CHECK(bcm2712_display_hsm_release(hsm, 0) == 0);
	CHECK(bcm2712_display_bvb_release(bvb, 0) == 0);
	CHECK(firmware[0].rate == 120000000 && firmware[1].rate == 23000000);
	CHECK(bcm2712_display_hsm_destroy(hsm) == 0);
	CHECK(bcm2712_display_bvb_destroy(bvb) == 0);
	CHECK(firmware[0].state == 0 && firmware[0].disables == 1);
	CHECK(firmware[1].state == 1 && firmware[1].disables == 0);
	CHECK(allocated == freed);
	reset();
	CHECK(bcm2712_display_bvb_create(&device_token, &device_token, &bvb) == 0);
	calls = mailbox_calls;
	CHECK(bcm2712_display_bvb_destroy(bvb) == 0);
	CHECK(mailbox_calls == calls && firmware[1].state == 0 && firmware[1].sets == 0);
	reset(); firmware[0].state = firmware[1].state = 1;
	firmware[0].rate = firmware[1].rate = 240000000;
	CHECK(bcm2712_display_hsm_create(&device_token, &device_token, &hsm) == 0);
	CHECK(bcm2712_display_bvb_create(&device_token, &device_token, &bvb) == 0);
	CHECK(bcm2712_display_hsm_acquire(hsm, 0, 75000000) == 0);
	CHECK(bcm2712_display_bvb_acquire(bvb, 0, 75000000) == 0);
	CHECK(firmware[0].rate == 240000000 && firmware[1].rate == 75000000);
	CHECK(bcm2712_display_hsm_release(hsm, 0) == 0);
	CHECK(bcm2712_display_bvb_release(bvb, 0) == 0);
	CHECK(bcm2712_display_hsm_destroy(hsm) == 0);
	CHECK(bcm2712_display_bvb_destroy(bvb) == 0);
	CHECK(firmware[0].disables == 0 && firmware[1].disables == 0);
}

static void
failure_tests(void)
{
	struct bcm2712_hsm_provider hsm_unknown, *hsm;
	struct bcm2712_bvb_provider bvb_unknown, *bvb;
	struct bcm2712_hsm_status hs;
	struct bcm2712_bvb_status bs;
	unsigned int profile, failure, calls;
	int error;

	for (profile = 0; profile < 2; profile++) {
		for (failure = 1; failure <= (profile == 0 ? 3U : 4U); failure++) {
			reset(); hsm = &hsm_unknown; bvb = &bvb_unknown;
			firmware[profile].fail_call = failure;
			error = profile == 0 ? bcm2712_display_hsm_create(&device_token, &device_token, &hsm) :
			    bcm2712_display_bvb_create(&device_token, &device_token, &bvb);
			CHECK(error == EIO && hsm == &hsm_unknown && bvb == &bvb_unknown);
			CHECK(allocated == freed && firmware[profile].sets == 0);
		}
		for (failure = 1; failure <= 8; failure++) {
			reset();
			CHECK(bcm2712_display_hsm_create(&device_token, &device_token, &hsm) == 0);
			CHECK(bcm2712_display_bvb_create(&device_token, &device_token, &bvb) == 0);
			firmware[profile].fail_call = firmware[profile].calls + failure;
			firmware[profile].fail_after_set = true;
			error = profile == 0 ? bcm2712_display_hsm_acquire(hsm, 0, 150000000) :
			    bcm2712_display_bvb_acquire(bvb, 0, 75000000);
			CHECK(error == EIO);
			calls = mailbox_calls;
			CHECK(bcm2712_display_hsm_status(hsm, &hs) == 0);
			CHECK(bcm2712_display_bvb_status(bvb, &bs) == 0);
			CHECK(mailbox_calls == calls && hs.leases == 0 && bs.leases == 0);
			CHECK((profile == 0 ? hs.fault : bs.fault) == (failure >= 4 ? EIO : 0));
			if (failure >= 4) {
				if (profile == 0) {
					CHECK(bcm2712_display_hsm_destroy(hsm) == EIO);
					CHECK(bcm2712_display_hsm_acquire(hsm, 1, 1) == EIO);
					CHECK(bcm2712_display_hsm_create(&device_token, &device_token, &hsm) == EBUSY);
				} else {
					CHECK(bcm2712_display_bvb_destroy(bvb) == EIO);
					CHECK(bcm2712_display_bvb_acquire(bvb, 1, 1) == EIO);
					CHECK(bcm2712_display_bvb_create(&device_token, &device_token, &bvb) == EBUSY);
				}
				CHECK(mailbox_calls == calls && allocated == freed + 2);
			}
			/* A faulted profile cannot poison the other fixed ID. */
			if (profile == 0) {
				CHECK(bcm2712_display_bvb_acquire(bvb, 0, 75000000) == 0);
				CHECK(bcm2712_display_bvb_release(bvb, 0) == 0);
				CHECK(bcm2712_display_bvb_destroy(bvb) == 0);
				if (failure < 4) CHECK(bcm2712_display_hsm_destroy(hsm) == 0);
			} else {
				CHECK(bcm2712_display_hsm_acquire(hsm, 0, 150000000) == 0);
				CHECK(bcm2712_display_hsm_release(hsm, 0) == 0);
				CHECK(bcm2712_display_hsm_destroy(hsm) == 0);
				if (failure < 4) CHECK(bcm2712_display_bvb_destroy(bvb) == 0);
			}
			reboot_fixture();
		}
	}
	for (failure = 1; failure <= 2; failure++) {
		reset();
		CHECK(bcm2712_display_hsm_create(&device_token, &device_token, &hsm) == 0);
		CHECK(bcm2712_display_hsm_acquire(hsm, 0, 150000000) == 0);
		CHECK(bcm2712_display_hsm_release(hsm, 0) == 0);
		firmware[0].fail_call = firmware[0].calls + failure;
		firmware[0].fail_after_set = true;
		CHECK(bcm2712_display_hsm_destroy(hsm) == EIO);
		calls = mailbox_calls;
		CHECK(bcm2712_display_hsm_status(hsm, &hs) == 0 && hs.fault == EIO);
		CHECK(bcm2712_display_hsm_destroy(hsm) == EIO && mailbox_calls == calls);
		reboot_fixture();
	}
	/* Floor-down SET/readback faults retain an existing native BVB lease. */
	for (failure = 4; failure <= 5; failure++) {
		reset();
		CHECK(bcm2712_display_hsm_create(&device_token, &device_token, &hsm) == 0);
		CHECK(bcm2712_display_bvb_create(&device_token, &device_token, &bvb) == 0);
		CHECK(bcm2712_display_bvb_acquire(bvb, 0, 75000000) == 0);
		firmware[1].fail_call = firmware[1].calls + failure;
		firmware[1].fail_after_set = true;
		CHECK(bcm2712_display_bvb_release(bvb, 0) == EIO);
		calls = mailbox_calls;
		CHECK(bcm2712_display_bvb_status(bvb, &bs) == 0);
		CHECK(bs.fault == EIO && bs.leases == 1 && bs.minimum[0] == 75000000);
		CHECK(bs.rate == 75000000 && bs.state == 1 && bs.mutation_attempted);
		CHECK(bcm2712_display_bvb_destroy(bvb) == EIO);
		CHECK(bcm2712_display_bvb_update(bvb, 0, 80000000) == EIO);
		CHECK(bcm2712_display_bvb_release(bvb, 0) == EIO);
		bvb = &bvb_unknown;
		CHECK(bcm2712_display_bvb_create(&device_token, &device_token, &bvb) == EBUSY);
		CHECK(bvb == &bvb_unknown && mailbox_calls == calls);
		CHECK(bcm2712_display_hsm_acquire(hsm, 0, 150000000) == 0);
		CHECK(bcm2712_display_hsm_release(hsm, 0) == 0);
		CHECK(bcm2712_display_hsm_destroy(hsm) == 0);
		reboot_fixture();
	}
	reset(); firmware[1].minimum = 0; bvb = &bvb_unknown;
	CHECK(bcm2712_display_bvb_create(&device_token, &device_token, &bvb) == ERANGE);
	CHECK(bvb == &bvb_unknown && firmware[1].sets == 0 && allocated == freed);
	reset(); firmware[1].bad_call = 4; firmware[1].bad_word = 5;
	firmware[1].bad_value = VCPROP_CLK_M2MC;
	CHECK(bcm2712_display_bvb_create(&device_token, &device_token, &bvb) == EIO);
	CHECK(bvb == &bvb_unknown && allocated == freed);
}

struct core4_worker {
	struct bcm2712_core_provider *owner;
	unsigned int slot;
	int error;
};
static void *
core4_acquire_worker(void *arg)
{
	struct core4_worker *w = arg;

	while (!atomic_load(&workers_start)) sched_yield();
	w->error = bcm2712_display_core_acquire(w->owner, w->slot, 1);
	return NULL;
}

static void
core4_tests(void)
{
	struct bcm2712_core_provider *core, *other, unknown;
	struct bcm2712_core_status status, saved, expected;
	struct bcm2712_hsm_provider *hsm;
	struct bcm2712_bvb_provider *bvb;
	struct core4_worker worker[2];
	pthread_t thread[2];
	unsigned int state, context, call, operation, step, before, i;

	memset(&unknown, 0xa5, sizeof(unknown));
	for (context = 0; context < 3; context++) {
		reset(); in_irq = context == 1; in_softirq = context == 2;
		memset(&status, 0xa5, sizeof(status)); memcpy(&saved, &status, sizeof(saved));
		CHECK(bcm2712_display_core_status(NULL, NULL) == EINVAL);
		CHECK(bcm2712_display_core_acquire(&unknown, 2, 0) == (context ? EWOULDBLOCK : ENXIO));
		CHECK(bcm2712_display_core_update(&unknown, 0, 1) == (context ? EWOULDBLOCK : ENXIO));
		CHECK(bcm2712_display_core_release(&unknown, 0) == (context ? EWOULDBLOCK : ENXIO));
		CHECK(bcm2712_display_core_destroy(&unknown) == (context ? EWOULDBLOCK : ENXIO));
		CHECK(bcm2712_display_core_status(&unknown, &status) == (context ? EWOULDBLOCK : ENXIO));
		CHECK(memcmp(&saved, &status, sizeof(status)) == 0 && mailbox_calls == 0);
		if (context) {
			core = &unknown;
			CHECK(bcm2712_display_core_create(&device_token, &device_token, &core) == EWOULDBLOCK);
			CHECK(core == &unknown && resource_calls == 0 && mailbox_calls == 0);
		}
		in_irq = in_softirq = false;
	}
	for (state = 0; state <= 1; state++) {
		reset(); firmware[2].state = state; firmware[2].rate = 240000000;
		CHECK(bcm2712_display_core_create(&device_token, &device_token, &core) == 0);
		CHECK(core->core.exchange == bcm2712_clock_exchange && core->core.arg == NULL);
		CHECK(firmware[2].calls == 4 && firmware[2].sets == 0);
		before = mailbox_calls; other = &unknown;
		CHECK(bcm2712_display_core_create(&device_token, NULL, &other) == EBUSY);
		CHECK(other == &unknown && mailbox_calls == before);
		memset(&expected, 0, sizeof(expected)); memset(&status, 0xa5, sizeof(status));
		expected.floor = expected.rate = 240000000; expected.state = state;
		CHECK(bcm2712_display_core_status(core, &status) == 0);
		CHECK(memcmp(&status, &expected, sizeof(status)) == 0 && mailbox_calls == before);
		CHECK(bcm2712_display_hsm_create(&device_token, &device_token, &hsm) == 0);
		CHECK(bcm2712_display_bvb_create(&device_token, &device_token, &bvb) == 0);
		atomic_store(&workers_start, false);
		for (i = 0; i < 2; i++) {
			worker[i].owner = core; worker[i].slot = i; worker[i].error = -1;
			CHECK(pthread_create(&thread[i], NULL, core4_acquire_worker, &worker[i]) == 0);
		}
		atomic_store(&workers_start, true);
		for (i = 0; i < 2; i++) CHECK(pthread_join(thread[i], NULL) == 0 && worker[i].error == 0);
		CHECK(atomic_load(&transport_max) == 1);
		CHECK(bcm2712_display_core_update(core, 0, 1) == 0);
		CHECK(bcm2712_display_core_destroy(core) == EBUSY);
		CHECK(bcm2712_display_core_release(core, 0) == 0);
		CHECK(bcm2712_display_core_release(core, 1) == 0);
		CHECK(firmware[2].rate == 240000000 && firmware[2].state == 1 && firmware[2].disables == 0);
		CHECK(bcm2712_display_core_destroy(core) == 0);
		CHECK(bcm2712_display_core_create(&device_token, &device_token, &core) == 0);
		CHECK(bcm2712_display_core_status(core, &status) == 0 && status.floor == 240000000);
		CHECK(bcm2712_display_core_destroy(core) == 0 && firmware[2].disables == 0);
		CHECK(bcm2712_display_hsm_destroy(hsm) == 0);
		CHECK(bcm2712_display_bvb_destroy(bvb) == 0);
	}
	for (call = 1; call <= 7; call++) {
		reset(); core = &unknown;
		if (call <= 4) firmware[2].fail_call = call;
		else if (call == 5) firmware[2].rate = 0;
		else if (call == 6) firmware[2].minimum = 0;
		else firmware[2].maximum = 100000000;
		CHECK(bcm2712_display_core_create(&device_token, &device_token, &core) == (call <= 4 ? EIO : ERANGE));
		CHECK(core == &unknown && allocated == freed && firmware[2].sets == 0);
	}
	for (operation = 0; operation < 3; operation++) {
		for (step = 1; step <= (operation == 0 ? 8U : 7U); step++) {
			reset();
			CHECK(bcm2712_display_core_create(&device_token, &device_token, &core) == 0);
			if (operation != 0)
				CHECK(bcm2712_display_core_acquire(core, 0, operation == 2 ? 240000000 : 150000000) == 0);
			CHECK(bcm2712_display_core_status(core, &saved) == 0);
			firmware[2].fail_call = firmware[2].calls + step; firmware[2].fail_after_set = true;
			CHECK((operation == 0 ? bcm2712_display_core_acquire(core, 0, 240000000) :
			    operation == 1 ? bcm2712_display_core_update(core, 0, 240000000) :
			    bcm2712_display_core_release(core, 0)) == EIO);
			before = mailbox_calls;
			CHECK(bcm2712_display_core_status(core, &status) == 0 && mailbox_calls == before);
			CHECK(status.leases == saved.leases && memcmp(status.minimum, saved.minimum, sizeof(saved.minimum)) == 0);
			CHECK(status.rate == saved.rate && status.floor == 120000000 && status.fault == (step > 3 ? EIO : 0));
			if (step > 3) {
				CHECK(bcm2712_display_core_destroy(core) == EIO);
				CHECK(bcm2712_display_core_update(core, 0, 1) == EIO);
				CHECK(bcm2712_display_core_release(core, 0) == EIO);
				other = &unknown;
				CHECK(bcm2712_display_core_create(&device_token, &device_token, &other) == EBUSY);
				CHECK(other == &unknown && mailbox_calls == before);
			}
			reboot_fixture(); /* Fresh simulated boot, never a production release. */
		}
	}
}

/* Four actual builtin typed slots share one callback and transport lock. */
static void
disp16_tests(void)
{
	struct bcm2712_hsm_provider *hsm;
	struct bcm2712_bvb_provider *bvb;
	struct bcm2712_core_provider *core;
	struct bcm2712_disp_provider *disp, *other, unknown;
	struct bcm2712_disp_status status, saved;
	unsigned int before, step, state;

	_Static_assert(sizeof(struct bcm2712_disp_status) == 32, "DISP status ABI");
	memset(&unknown, 0xa5, sizeof(unknown));
	for (state = 0; state < 2; state++) {
		reset(); firmware[3].state = state; firmware[3].rate = 240000000;
		CHECK(bcm2712_display_hsm_create(&device_token, &device_token, &hsm) == 0);
		CHECK(bcm2712_display_bvb_create(&device_token, &device_token, &bvb) == 0);
		CHECK(bcm2712_display_core_create(&device_token, &device_token, &core) == 0);
		CHECK(bcm2712_display_disp_create(&device_token, &device_token, &disp) == 0);
		CHECK(firmware[3].calls == 4 && firmware[3].sets == 0);
		CHECK(disp->core.exchange == hsm->core.exchange && disp->core.arg == NULL);
		before = mailbox_calls; other = &unknown;
		CHECK(bcm2712_display_disp_create(&device_token, NULL, &other) == EBUSY);
		CHECK(other == &unknown && mailbox_calls == before);
		memset(&status, 0xa5, sizeof(status)); saved = status;
		CHECK(bcm2712_display_disp_status(&unknown, &status) == ENXIO);
		CHECK(memcmp(&status, &saved, sizeof(status)) == 0 && mailbox_calls == before);
		in_irq = true;
		CHECK(bcm2712_display_disp_acquire(disp, 0, 240000000) == EWOULDBLOCK);
		in_irq = false; in_softirq = true;
		CHECK(bcm2712_display_disp_status(disp, &status) == EWOULDBLOCK);
		in_softirq = false; CHECK(mailbox_calls == before);
		CHECK(bcm2712_display_disp_acquire(disp, 0, 240000000) == 0);
		CHECK(bcm2712_display_disp_status(disp, &status) == 0);
		CHECK(status.floor == 23000000 && status.state == 1 && status.rate == 240000000);
		CHECK(status.leases == 1 && status.minimum[0] == 240000000 && status.fault == 0);
		CHECK(firmware[3].sets == (state == 0 ? 1U : 0U));
		CHECK(bcm2712_display_disp_destroy(disp) == EBUSY);
		CHECK(bcm2712_display_disp_release(disp, 0) == 0);
		CHECK(bcm2712_display_disp_destroy(disp) == 0);
		CHECK(firmware[3].state == state && firmware[3].disables == (state == 0));
		CHECK(bcm2712_display_hsm_destroy(hsm) == 0);
		CHECK(bcm2712_display_bvb_destroy(bvb) == 0);
		CHECK(bcm2712_display_core_destroy(core) == 0);
	}
	for (step = 1; step <= 8; step++) {
		reset();
		CHECK(bcm2712_display_disp_create(&device_token, &device_token, &disp) == 0);
		firmware[3].fail_call = firmware[3].calls + step; firmware[3].fail_after_set = true;
		CHECK(bcm2712_display_disp_acquire(disp, 0, 100000000) == EIO);
		CHECK(bcm2712_display_disp_status(disp, &status) == 0);
		CHECK(status.fault == (step <= 3 ? 0 : EIO) && status.leases == 0);
		before = mailbox_calls;
		if (step <= 3) {
			CHECK(bcm2712_display_disp_destroy(disp) == 0);
		} else {
			CHECK(bcm2712_display_disp_destroy(disp) == EIO);
			CHECK(bcm2712_display_disp_update(disp, 0, 1) == EIO);
			CHECK(bcm2712_display_disp_release(disp, 0) == EIO);
			other = &unknown;
			CHECK(bcm2712_display_disp_create(&device_token, &device_token, &other) == EBUSY);
			CHECK(other == &unknown && mailbox_calls == before);
			reboot_fixture();
		}
	}
}

int
main(void)
{
	if (getenv("CORE4_ONLY") == NULL) {
		refusal_tests();
		lifecycle_tests();
		failure_tests();
	}
	core4_tests();
	if (getenv("CORE4_ONLY") == NULL) disp16_tests();
	CHECK(allocated == freed);
	printf("Native display clocks: %u checks PASS; actual source, no hardware\n",
	    atomic_load(&checks));
	return 0;
}
