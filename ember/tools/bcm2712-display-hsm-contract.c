/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual HSM core and native provider. Only kernel/firmware services are fake. */
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
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
#include "display.h"
#include "hsm-types.h"
#include "properties.h"
#include "hsm.h"

static unsigned int checks;
#define CHECK(e) do { checks++; if (!(e)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); exit(1); } } while (0)

struct firmware {
	uint32_t state, rate, maximum;
	unsigned int calls, sets, enables, disables;
	uint32_t tags[64];
	unsigned int fail_call, bad_call;
	int bad_word;
	uint32_t corrupt;
	bool ignore_rate, clamp, round_up;
};
static struct firmware fw;
static int device_token;
static unsigned int allocated, freed, resource_calls;
static int resource_error;
static _Thread_local bool in_irq, in_softirq;
typedef void *device_t;
typedef void *ACPI_HANDLE;
typedef pthread_mutex_t kmutex_t;
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define KM_SLEEP 0
#define BCMMBOX_CHANARM2VC 8
#define ASSERT_SLEEPABLE() assert(!in_irq && !in_softirq)
#define cpu_intr_p() in_irq
#define cpu_softintr_p() in_softirq
static void mutex_init(kmutex_t *m, int type, int ipl)
{ assert(type == 0 && ipl == 0); assert(pthread_mutex_init(m, NULL) == 0); }
static void mutex_enter(kmutex_t *m) { assert(pthread_mutex_lock(m) == 0); }
static void mutex_exit(kmutex_t *m) { assert(pthread_mutex_unlock(m) == 0); }
struct once { pthread_mutex_t mutex; bool done; int error; };
#define ONCE_DECL(o) struct once o = { PTHREAD_MUTEX_INITIALIZER, false, 0 }
static int run_once(struct once *o, int (*fn)(void))
{
	int error;
	assert(pthread_mutex_lock(&o->mutex) == 0);
	if (!o->done) { o->error = fn(); o->done = true; }
	error = o->error;
	assert(pthread_mutex_unlock(&o->mutex) == 0);
	return error;
}
#define RUN_ONCE(o,fn) run_once(o,fn)
static void *kmem_zalloc(size_t size, int flag)
{ void *p; assert(flag == 0); p = calloc(1, size); assert(p); allocated++; return p; }
static void kmem_free(void *p, size_t size)
{ assert(p != NULL && size != 0); freed++; free(p); }
static int bcm2712_display_acpi_resources(device_t d, ACPI_HANDLE h,
    struct bcm2712_display_contract *out)
{
	assert(d == (void *)&device_token && out != NULL);
	resource_calls++;
	if (h == NULL) return EINVAL;
	memset(out, 0, sizeof(*out));
	return resource_error;
}
static int exchange(void *, uint32_t *, size_t);
static int bcmmbox_request(unsigned int chan, void *p, size_t n, uint32_t *response)
{
	assert(chan == 8 && response != NULL);
	*response = 0x1000;
	return exchange(&fw, p, n);
}
#include "hsm_acpi.h"

static void
reset(uint32_t state, uint32_t rate)
{
	assert(bcm2712_hsm_owner == NULL);
	memset(&fw, 0, sizeof(fw));
	fw.state = state; fw.rate = rate; fw.maximum = 600000000;
	fw.bad_word = -1;
	resource_error = 0; resource_calls = 0;
}

static int
exchange(void *arg, uint32_t *p, size_t size)
{
	struct firmware *f = arg;
	uint32_t tag = le32toh(p[2]), value = le32toh(p[6]);
	bool set_rate = tag == VCPROPTAG_SET_CLOCKRATE;
	size_t words = set_rate ? 9 : 8;
	unsigned int call = ++f->calls;

	assert(((uintptr_t)p & 15) == 0);
	assert(size == words * 4 && le32toh(p[0]) == size);
	assert(p[1] == 0 && p[4] == 0 && p[words - 1] == 0);
	assert(le32toh(p[3]) == (set_rate ? 12 : 8));
	assert(le32toh(p[5]) == 13);
	if (set_rate) {
		assert(le32toh(p[7]) == 1);
		f->sets++;
	} else if (tag == VCPROPTAG_SET_CLOCKSTATE) {
		f->sets++;
	}
	if (call <= 64) f->tags[call - 1] = tag;
	if (f->fail_call == call) return ETIMEDOUT;
	switch (tag) {
	case VCPROPTAG_GET_CLOCKSTATE: value = f->state; break;
	case VCPROPTAG_GET_CLOCKRATE: value = f->rate; break;
	case VCPROPTAG_GET_MAX_CLOCKRATE: value = f->maximum; break;
	case VCPROPTAG_SET_CLOCKRATE:
		if (!f->ignore_rate) f->rate = f->clamp ? value - 1 : value;
		if (f->round_up) f->rate += 1000;
		if (!f->ignore_rate) value = f->rate;
		break;
	case VCPROPTAG_SET_CLOCKSTATE:
		assert(value <= 1);
		/* The clock rate must be verified before an enable request. */
		if (value == 1) { assert(f->rate >= 120000000); f->enables++; }
		else f->disables++;
		f->state = value;
		break;
	default: abort();
	}
	p[1] = htole32(VCPROP_REQ_SUCCESS);
	p[4] = htole32(VCPROPTAG_RESPONSE | 8);
	p[6] = htole32(value);
	if (f->bad_call == call) {
		assert(f->bad_word >= 0 && (size_t)f->bad_word < words);
		p[f->bad_word] = htole32(f->corrupt);
	}
	return 0;
}

static void
core_tests(void)
{
	struct bcm2712_hsm c, saved;
	unsigned int i, calls;

	reset(0, 0); memset(&c, 0xa5, sizeof(c)); saved = c;
	CHECK(bcm2712_hsm_init(NULL, exchange, &fw) == EINVAL);
	CHECK(bcm2712_hsm_init(&c, NULL, &fw) == EINVAL);
	for (i = 1; i <= 3; i++) {
		reset(0, 0); fw.fail_call = i;
		CHECK(bcm2712_hsm_init(&c, exchange, &fw) == ETIMEDOUT);
		CHECK(memcmp(&c, &saved, sizeof(c)) == 0 && fw.sets == 0);
	}
	reset(2, 0);
	CHECK(bcm2712_hsm_init(&c, exchange, &fw) == ENODEV);
	reset(4, 0);
	CHECK(bcm2712_hsm_init(&c, exchange, &fw) == EIO);
	reset(0, 0); fw.maximum = 119999999;
	CHECK(bcm2712_hsm_init(&c, exchange, &fw) == ERANGE && fw.sets == 0);
	reset(0, 0);
	CHECK(bcm2712_hsm_init(&c, exchange, &fw) == 0 && fw.sets == 0);
	CHECK(bcm2712_hsm_acquire(&c, 2, 1) == EINVAL);
	CHECK(bcm2712_hsm_acquire(&c, 0, 0) == EINVAL);
	CHECK(bcm2712_hsm_update(&c, 0, 120000000) == ENOENT);
	CHECK(bcm2712_hsm_release(&c, 0) == ENOENT);
	CHECK(bcm2712_hsm_acquire(&c, 0, UINT32_MAX) == ERANGE && fw.sets == 0);
	CHECK(c.status.leases == 0 && c.status.fault == 0);
	CHECK(bcm2712_hsm_acquire(&c, 0, 1) == 0);
	CHECK(fw.rate == 120000000 && fw.state == 1 && fw.sets == 2);
	CHECK(c.status.leases == 1 && c.status.minimum[0] == 1);
	CHECK(bcm2712_hsm_acquire(&c, 0, 120000000) == EBUSY);
	CHECK(bcm2712_hsm_fini(&c) == EBUSY);
	CHECK(bcm2712_hsm_acquire(&c, 1, 240000000) == 0);
	CHECK(fw.rate == 240000000 && c.status.leases == 3);
	CHECK(bcm2712_hsm_update(&c, 0, 360000000) == 0 && fw.rate == 360000000);
	CHECK(bcm2712_hsm_release(&c, 0) == 0 && fw.rate == 240000000);
	CHECK(c.status.leases == 2 && fw.disables == 0);
	CHECK(bcm2712_hsm_release(&c, 1) == 0 && fw.rate == 120000000);
	CHECK(bcm2712_hsm_fini(&c) == 0 && fw.disables == 1 && fw.rate != 0);
	CHECK(bcm2712_hsm_fini(&c) == ENXIO);

	reset(1, 200000000);
	CHECK(bcm2712_hsm_init(&c, exchange, &fw) == 0);
	CHECK(bcm2712_hsm_acquire(&c, 0, 300000000) == 0);
	CHECK(bcm2712_hsm_release(&c, 0) == 0 && fw.rate == 200000000);
	CHECK(bcm2712_hsm_fini(&c) == 0 && fw.disables == 0);

	reset(0, 0); fw.round_up = true;
	CHECK(bcm2712_hsm_init(&c, exchange, &fw) == 0);
	CHECK(bcm2712_hsm_acquire(&c, 0, 120000000) == 0 && fw.rate == 120001000);
	CHECK(bcm2712_hsm_release(&c, 0) == 0);
	CHECK(bcm2712_hsm_fini(&c) == 0);

	/* Every request in the first acquire fails once. No optimistic lease. */
	for (i = 1; i <= 8; i++) {
		reset(0, 0);
		CHECK(bcm2712_hsm_init(&c, exchange, &fw) == 0);
		fw.fail_call = fw.calls + i;
		CHECK(bcm2712_hsm_acquire(&c, 0, 120000000) == ETIMEDOUT);
		CHECK(c.status.leases == 0);
		if (i <= 3) {
			CHECK(c.status.fault == 0 && !c.status.mutation_attempted && fw.sets == 0);
			CHECK(bcm2712_hsm_fini(&c) == 0);
		} else {
			CHECK(c.status.fault == ETIMEDOUT && c.status.mutation_attempted);
			calls = fw.calls;
			CHECK(bcm2712_hsm_acquire(&c, 1, 120000000) == ETIMEDOUT);
			CHECK(bcm2712_hsm_fini(&c) == ETIMEDOUT && fw.calls == calls);
		}
	}
	/* An already enabled inherited floor needs no write, even on failure. */
	for (i = 1; i <= 5; i++) {
		reset(1, 240000000);
		CHECK(bcm2712_hsm_init(&c, exchange, &fw) == 0);
		fw.fail_call = fw.calls + i;
		CHECK(bcm2712_hsm_acquire(&c, 0, 120000000) == ETIMEDOUT);
		CHECK(c.status.leases == 0 && c.status.fault == 0 && fw.sets == 0);
		fw.fail_call = 0;
		CHECK(bcm2712_hsm_acquire(&c, 0, 120000000) == 0);
		CHECK(bcm2712_hsm_release(&c, 0) == 0);
		CHECK(bcm2712_hsm_fini(&c) == 0 && fw.sets == 0);
	}
	reset(0, 0);
	CHECK(bcm2712_hsm_init(&c, exchange, &fw) == 0);
	fw.maximum = 119999999;
	CHECK(bcm2712_hsm_acquire(&c, 0, 120000000) == ERANGE);
	CHECK(fw.sets == 0 && c.status.fault == 0 && c.status.leases == 0);
	CHECK(bcm2712_hsm_fini(&c) == 0);
	/* Invalid state replies never publish a lease or permit a retry. */
	for (i = 0; i <= 4; i += 2) {
		reset(0, 0);
		CHECK(bcm2712_hsm_init(&c, exchange, &fw) == 0);
		fw.bad_call = fw.calls + 6; fw.bad_word = 6; fw.corrupt = i;
		CHECK(bcm2712_hsm_acquire(&c, 0, 120000000) == (i == 2 ? ENODEV : EIO));
		CHECK(c.status.leases == 0 && c.status.fault != 0);
	}
	/* A failed read before release preserves the existing two leases. */
	reset(1, 240000000);
	CHECK(bcm2712_hsm_init(&c, exchange, &fw) == 0);
	CHECK(bcm2712_hsm_acquire(&c, 0, 120000000) == 0);
	CHECK(bcm2712_hsm_acquire(&c, 1, 240000000) == 0);
	fw.fail_call = fw.calls + 1;
	CHECK(bcm2712_hsm_release(&c, 0) == ETIMEDOUT);
	CHECK(c.status.leases == 3 && c.status.fault == 0 && fw.sets == 0);
	fw.fail_call = 0;
	CHECK(bcm2712_hsm_release(&c, 0) == 0);
	CHECK(bcm2712_hsm_release(&c, 1) == 0);
	CHECK(bcm2712_hsm_fini(&c) == 0 && fw.disables == 0);
	/* A well-formed SET response does not prove the clock rate was applied. */
	reset(0, 0); fw.ignore_rate = true;
	CHECK(bcm2712_hsm_init(&c, exchange, &fw) == 0);
	CHECK(bcm2712_hsm_acquire(&c, 0, 120000000) == EIO);
	CHECK(c.status.fault == EIO && fw.enables == 0 && c.status.leases == 0);
	reset(0, 0); fw.clamp = true;
	CHECK(bcm2712_hsm_init(&c, exchange, &fw) == 0);
	CHECK(bcm2712_hsm_acquire(&c, 0, 120000000) == ERANGE && fw.enables == 0);

	/* Each response framing field must match, including the echoed ID. */
	for (i = 0; i < 9; i++) {
		if (i == 6 || i == 7) continue;
		reset(0, 0);
		CHECK(bcm2712_hsm_init(&c, exchange, &fw) == 0);
		fw.bad_call = fw.calls + 4; fw.bad_word = (int)i; fw.corrupt = 99;
		CHECK(bcm2712_hsm_acquire(&c, 0, 120000000) == EIO);
		CHECK(c.status.fault == EIO && c.status.leases == 0);
	}
	reset(0, 0);
	CHECK(bcm2712_hsm_init(&c, exchange, &fw) == 0);
	CHECK(bcm2712_hsm_acquire(&c, 0, 240000000) == 0);
	fw.fail_call = fw.calls + 4; /* Release SET_RATE fails: lease stays owned. */
	CHECK(bcm2712_hsm_release(&c, 0) == ETIMEDOUT && c.status.leases == 1);
	calls = fw.calls;
	CHECK(bcm2712_hsm_fini(&c) == ETIMEDOUT && fw.calls == calls);
	for (i = 1; i <= 2; i++) {
		reset(0, 0);
		CHECK(bcm2712_hsm_init(&c, exchange, &fw) == 0);
		CHECK(bcm2712_hsm_acquire(&c, 0, 120000000) == 0);
		CHECK(bcm2712_hsm_release(&c, 0) == 0);
		fw.fail_call = fw.calls + i;
		CHECK(bcm2712_hsm_fini(&c) == ETIMEDOUT && c.initialized);
		calls = fw.calls;
		CHECK(bcm2712_hsm_fini(&c) == ETIMEDOUT && fw.calls == calls);
	}
}

struct worker { struct bcm2712_hsm_provider *p; unsigned int port; int result; };
static void *acquire_worker(void *arg)
{
	struct worker *w = arg;
	w->result = bcm2712_display_hsm_acquire(w->p, w->port, (w->port + 1) * 120000000);
	return NULL;
}

static void
provider_tests(void)
{
	struct bcm2712_hsm_provider untouched;
	struct bcm2712_hsm_provider *p = &untouched, *other = NULL;
	struct bcm2712_hsm_status status;
	struct worker w[2];
	pthread_t threads[2];
	unsigned int calls;

	reset(0, 0);
	in_irq = true;
	CHECK(bcm2712_display_hsm_create((void *)&device_token, (void *)&device_token, &p) == EWOULDBLOCK);
	in_irq = false; in_softirq = true;
	CHECK(bcm2712_display_hsm_create((void *)&device_token, (void *)&device_token, &p) == EWOULDBLOCK);
	in_softirq = false;
	CHECK(p == &untouched && resource_calls == 0 && fw.calls == 0);
	resource_error = EOPNOTSUPP;
	CHECK(bcm2712_display_hsm_create((void *)&device_token, (void *)&device_token, &p) == EOPNOTSUPP);
	CHECK(p == &untouched && fw.calls == 0);
	resource_error = 0; fw.fail_call = 1;
	CHECK(bcm2712_display_hsm_create((void *)&device_token, (void *)&device_token, &p) == ETIMEDOUT);
	CHECK(allocated == freed && p == &untouched);
	reset(0, 0);
	CHECK(bcm2712_display_hsm_create((void *)&device_token, (void *)&device_token, &p) == 0);
	calls = fw.calls;
	CHECK(bcm2712_display_hsm_create((void *)&device_token, (void *)&device_token, &other) == EBUSY);
	CHECK(other == NULL && fw.calls == calls);
	CHECK(bcm2712_display_hsm_acquire(other, 0, 120000000) == ENXIO);
	for (unsigned int i = 0; i < 2; i++) {
		w[i] = (struct worker){ p, i, -1 };
		CHECK(pthread_create(&threads[i], NULL, acquire_worker, &w[i]) == 0);
	}
	for (unsigned int i = 0; i < 2; i++) {
		CHECK(pthread_join(threads[i], NULL) == 0);
		CHECK(w[i].result == 0);
	}
	CHECK(bcm2712_display_hsm_status(p, &status) == 0);
	CHECK(status.leases == 3 && status.rate == 240000000);
	CHECK(bcm2712_display_hsm_destroy(p) == EBUSY);
	in_irq = true;
	CHECK(bcm2712_display_hsm_release(p, 0) == EWOULDBLOCK);
	in_irq = false;
	CHECK(bcm2712_display_hsm_update(p, 0, 360000000) == 0);
	CHECK(bcm2712_display_hsm_release(p, 0) == 0);
	CHECK(bcm2712_display_hsm_status(p, &status) == 0 && status.rate == 240000000);
	CHECK(bcm2712_display_hsm_release(p, 1) == 0);
	CHECK(bcm2712_display_hsm_destroy(p) == 0 && allocated == freed);
	CHECK(bcm2712_display_hsm_status(NULL, &status) == ENXIO);
	reset(0, 0);
	CHECK(bcm2712_display_hsm_create((void *)&device_token, (void *)&device_token, &p) == 0);
	fw.fail_call = fw.calls + 4;
	CHECK(bcm2712_display_hsm_acquire(p, 0, 120000000) == ETIMEDOUT);
	CHECK(bcm2712_display_hsm_status(p, &status) == 0 && status.fault == ETIMEDOUT);
	calls = fw.calls;
	CHECK(bcm2712_display_hsm_destroy(p) == ETIMEDOUT);
	CHECK(bcm2712_display_hsm_create((void *)&device_token, (void *)&device_token, &other) == EBUSY);
	CHECK(fw.calls == calls && allocated == freed + 1);
	/* Test teardown only: production retains this owner until reboot. */
	kmem_free(p, sizeof(*p)); bcm2712_hsm_owner = NULL;
	CHECK(allocated == freed);
}

int
main(void)
{
	core_tests();
	provider_tests();
	printf("HSM contract: %u checks PASS; source/host only, no display MMIO\n", checks);
	return 0;
}
