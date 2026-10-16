/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual resource decoder, DVP core and native adapter; modeled MMIO/kernel. */
#ifdef NDEBUG
#error This contract requires assertions
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
#else
#include <sys/endian.h>
#endif
#ifndef __aligned
#define __aligned(n) __attribute__((aligned(n)))
#endif
typedef void *device_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
#define _KERNEL 1
#include "display.h"
#include "properties.h"
#include "display-core.h"
#include "dvp.h"
#include "dvp-core.h"

static unsigned int checks;
#define CHECK(e) do { checks++; if (!(e)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); exit(1); } } while (0)
struct hardware {
	uint32_t gates, resets, pending, xor_write, reset_on_write;
	unsigned int calls, reads, writes, barriers, fail_call;
	bool waiting, ignore_write;
};
static struct hardware hw;
static struct bcm2712_display_contract fixture_contract;
static int device_token, bus_token;
static _Thread_local bool in_irq, in_softirq;
static _Thread_local unsigned int held;
static unsigned int allocated, freed;
typedef pthread_mutex_t kmutex_t;
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define KM_SLEEP 0
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
#define cpu_intr_p() in_irq
#define cpu_softintr_p() in_softirq
#define ASSERT_SLEEPABLE() assert(!in_irq && !in_softirq)
static void mutex_init(kmutex_t *m, int type, int ipl)
{ assert(type == 0 && ipl == 0); assert(pthread_mutex_init(m, NULL) == 0); }
static void mutex_enter(kmutex_t *m)
{ assert(held == 0); assert(pthread_mutex_lock(m) == 0); held++; }
static void mutex_exit(kmutex_t *m)
{ assert(held == 1); held--; assert(pthread_mutex_unlock(m) == 0); }
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
static void *kmem_zalloc(size_t size, int flags)
{ void *p; assert(held == 1 && flags == 0); p = calloc(1, size); assert(p); allocated++; return p; }
static void kmem_free(void *p, size_t size)
{ assert(held == 1 && size != 0); freed++; free(p); }

static int
read_register(void *arg, unsigned int offset, uint32_t *out)
{
	struct hardware *h = arg;
	assert(!h->waiting);
	assert(offset == 0x04 || offset == 0x08);
	h->reads++;
	*out = offset == 0x04 ? h->resets : h->gates;
	if (++h->calls == h->fail_call) {
		*out = UINT32_MAX;
		return EIO;
	}
	return 0;
}

static int
write_register(void *arg, unsigned int offset, uint32_t value)
{
	struct hardware *h = arg;
	assert(offset == 0x08 && !h->waiting); /* Never write SW_INIT. */
	h->writes++;
	/* Even a failed write can reach hardware. */
	if (++h->calls == h->fail_call) {
		h->gates = value;
		return EIO;
	}
	h->pending = h->ignore_write ? h->gates : value ^ h->xor_write;
	h->waiting = true;
	return 0;
}

static void
register_barrier(void *arg)
{
	struct hardware *h = arg;
	assert(h->waiting);
	h->barriers++;
	h->gates = h->pending;
	h->resets |= h->reset_on_write;
	h->waiting = false;
}

static uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle, unsigned int off)
{
	uint32_t value;
	assert(held == 1 && tag == &bus_token && handle == 17);
	assert(read_register(&hw, off, &value) == 0);
	return value;
}
static void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle,
    unsigned int off, uint32_t value)
{
	assert(held == 1 && tag == &bus_token && handle == 17);
	assert(write_register(&hw, off, value) == 0);
}
static void
bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t handle,
    unsigned int offset, unsigned int length, unsigned int flags)
{
	assert(held == 1 && tag == &bus_token && handle == 17);
	assert(offset == 0 && length == 0x10 && flags == 3);
	register_barrier(&hw);
}
#include "dvp-native.h"

static struct bcm2712_dvp_io fixture_io = {
	.read = read_register, .write = write_register,
	.barrier = register_barrier, .arg = &hw
};

static void
reset_model(uint32_t gates, uint32_t resets)
{
	unsigned int i;
	assert(bcm2712_dvp_owner == NULL);
	memset(&hw, 0, sizeof(hw)); hw.gates = gates; hw.resets = resets;
	memset(&fixture_contract, 0, sizeof(fixture_contract)); fixture_contract.version = 1;
	for (i = 0; i < BCM2712_DISPLAY_NMEM; i++)
		fixture_contract.memory[i] = bcm2712_display_resources[i].memory;
	for (i = 0; i < BCM2712_DISPLAY_NIRQ; i++)
		fixture_contract.irq[i] = bcm2712_display_irqs[i].gsi;
}

static int
init_core(struct bcm2712_dvp *c)
{
	return bcm2712_dvp_init(c, &fixture_contract, &fixture_contract.memory[10], &fixture_io);
}

static void
core_tests(void)
{
	struct bcm2712_dvp c, saved;
	struct bcm2712_display_memory mapped;
	struct bcm2712_dvp_io invalid;
	unsigned int i, port, pattern, calls;

	reset_model(0x18, 0); memset(&c, 0xa5, sizeof(c)); saved = c;
	CHECK(bcm2712_dvp_init(NULL, &fixture_contract, &fixture_contract.memory[10], &fixture_io) == EINVAL);
	CHECK(bcm2712_dvp_init(&c, NULL, &fixture_contract.memory[10], &fixture_io) == EINVAL);
	CHECK(bcm2712_dvp_init(&c, &fixture_contract, NULL, &fixture_io) == EINVAL);
	CHECK(bcm2712_dvp_init(&c, &fixture_contract, &fixture_contract.memory[10], NULL) == EINVAL);
	for (i = 0; i < 3; i++) {
		invalid = fixture_io;
		if (i == 0) invalid.read = NULL;
		if (i == 1) invalid.write = NULL;
		if (i == 2) invalid.barrier = NULL;
		CHECK(bcm2712_dvp_init(&c, &fixture_contract, &fixture_contract.memory[10], &invalid) == EINVAL);
	}
	fixture_contract.version = 2; CHECK(init_core(&c) == EOPNOTSUPP);
	fixture_contract.version = 1; fixture_contract.memory[10].length = 8; CHECK(init_core(&c) == EINVAL);
	fixture_contract.memory[10] = bcm2712_display_resources[10].memory;
	fixture_contract.irq[5]++; CHECK(init_core(&c) == EINVAL); fixture_contract.irq[5]--;
	mapped = fixture_contract.memory[11];
	CHECK(bcm2712_dvp_init(&c, &fixture_contract, &mapped, &fixture_io) == EINVAL);
	CHECK(hw.calls == 0 && memcmp(&c, &saved, sizeof(c)) == 0);
	for (i = 1; i <= 2; i++) {
		reset_model(0x18, 0); hw.fail_call = i;
		CHECK(init_core(&c) == EIO && hw.writes == 0);
		CHECK(memcmp(&c, &saved, sizeof(c)) == 0);
	}
	/* Exercise every foreign bit, both ports, and all inherited gate states. */
	for (i = 0; i < 32; i++) for (pattern = 0; pattern < 4; pattern++)
	    for (port = 0; port < 2; port++) {
		const uint32_t foreign = (UINT32_C(1) << i) & ~UINT32_C(0x18);
		const uint32_t original = foreign | ((pattern & 1) ? 8 : 0) | ((pattern & 2) ? 16 : 0);
		const uint32_t bit = 1U << (port + 3);
		reset_model(original, UINT32_C(0xa5000000));
		CHECK(init_core(&c) == 0 && hw.writes == 0);
		CHECK(bcm2712_dvp_acquire(&c, port) == 0);
		CHECK(hw.gates == (original & ~bit) && hw.resets == UINT32_C(0xa5000000));
		CHECK(c.status.leases == (1U << port));
		CHECK(bcm2712_dvp_acquire(&c, port) == EBUSY);
		CHECK(bcm2712_dvp_fini(&c) == EBUSY);
		CHECK(bcm2712_dvp_release(&c, port) == 0 && hw.gates == original);
		CHECK(hw.writes == ((original & bit) ? 2U : 0U));
		CHECK(c.status.leases == 0 && c.restore == 0);
		CHECK(bcm2712_dvp_fini(&c) == 0);
		CHECK(bcm2712_dvp_fini(&c) == ENXIO);
	}
	reset_model(0x18, 0); CHECK(init_core(&c) == 0);
	CHECK(bcm2712_dvp_acquire(NULL, 0) == EINVAL);
	CHECK(bcm2712_dvp_acquire(&c, 2) == EINVAL);
	CHECK(bcm2712_dvp_release(&c, 2) == EINVAL);
	CHECK(bcm2712_dvp_release(&c, 0) == ENOENT);
	CHECK(bcm2712_dvp_acquire(&c, 0) == 0 && bcm2712_dvp_acquire(&c, 1) == 0);
	CHECK(hw.gates == 0 && c.status.leases == 3);
	CHECK(bcm2712_dvp_release(&c, 0) == 0 && hw.gates == 8 && c.status.leases == 2);
	CHECK(bcm2712_dvp_release(&c, 1) == 0 && hw.gates == 0x18);
	CHECK(bcm2712_dvp_fini(&c) == 0);
	for (port = 0; port < 2; port++) {
		reset_model(0x18, 1U << (port + 1)); CHECK(init_core(&c) == 0);
		CHECK(bcm2712_dvp_acquire(&c, port) == EBUSY && hw.writes == 0 && c.status.leases == 0);
		CHECK(bcm2712_dvp_acquire(&c, port ^ 1) == 0);
		CHECK(bcm2712_dvp_release(&c, port ^ 1) == 0);
		CHECK(bcm2712_dvp_fini(&c) == 0);
	}
	/* Fail each read/write in acquire and release, including after applied write. */
	for (pattern = 0; pattern < 2; pattern++) for (i = 1; i <= 5; i++) {
		reset_model(0x18, 0); CHECK(init_core(&c) == 0);
		if (pattern) CHECK(bcm2712_dvp_acquire(&c, 0) == 0);
		saved = c; hw.fail_call = hw.calls + i;
		CHECK((pattern ? bcm2712_dvp_release(&c, 0) : bcm2712_dvp_acquire(&c, 0)) == EIO);
		CHECK(c.status.leases == saved.status.leases);
		CHECK(c.status.gates == saved.status.gates && c.status.resets == saved.status.resets);
		if (i <= 2) {
			CHECK(memcmp(&c, &saved, sizeof(c)) == 0);
			hw.fail_call = 0;
			CHECK((pattern ? bcm2712_dvp_release(&c, 0) : bcm2712_dvp_acquire(&c, 0)) == 0);
			if (!pattern) CHECK(bcm2712_dvp_release(&c, 0) == 0);
			CHECK(bcm2712_dvp_fini(&c) == 0);
		} else {
			CHECK(c.status.fault == EIO && c.status.mutation_attempted);
			calls = hw.calls;
			CHECK(bcm2712_dvp_acquire(&c, 1) == EIO);
			CHECK(bcm2712_dvp_release(&c, 0) == EIO);
			CHECK(bcm2712_dvp_fini(&c) == EIO && hw.calls == calls);
		}
	}
	/* Ignored writes, foreign-bit corruption, and reset changes after a write. */
	for (pattern = 0; pattern < 2; pattern++) for (i = 0; i < 4; i++) {
		reset_model(0x18, 0); CHECK(init_core(&c) == 0);
		CHECK(bcm2712_dvp_acquire(&c, 1) == 0);
		if (pattern) CHECK(bcm2712_dvp_acquire(&c, 0) == 0);
		if (i == 0) hw.ignore_write = true;
		if (i == 1) hw.xor_write = UINT32_C(0x80000000);
		if (i == 2) hw.reset_on_write = 2;
		if (i == 3) hw.reset_on_write = 4;
		saved = c;
		CHECK((pattern ? bcm2712_dvp_release(&c, 0) : bcm2712_dvp_acquire(&c, 0)) == EIO);
		CHECK(c.status.leases == saved.status.leases && c.status.fault == EIO);
		CHECK(bcm2712_dvp_fini(&c) == EIO);
	}
	for (i = 0; i < 2; i++) {
		reset_model(0x18, 0); CHECK(init_core(&c) == 0);
		CHECK(bcm2712_dvp_acquire(&c, 0) == 0);
		if (i == 0) hw.resets |= 2; else hw.gates |= 8;
		calls = hw.writes;
		CHECK(bcm2712_dvp_acquire(&c, 1) == EIO);
		CHECK(c.status.leases == 1 && hw.writes == calls);
		CHECK(bcm2712_dvp_fini(&c) == EIO);
	}
}

struct worker_arg { struct bcm2712_dvp_provider *sc; unsigned int port; };
static void *
worker(void *arg)
{
	struct worker_arg *w = arg;
	struct bcm2712_dvp_status status;
	unsigned int i;
	for (i = 0; i < 1000; i++) {
		assert(bcm2712_display_dvp_acquire(w->sc, w->port) == 0);
		assert(bcm2712_display_dvp_status(w->sc, &status) == 0);
		assert((status.leases & (1U << w->port)) != 0 && status.fault == 0);
		assert(bcm2712_display_dvp_release(w->sc, w->port) == 0);
	}
	return NULL;
}

static int
create(struct bcm2712_dvp_provider **out)
{
	return bcm2712_display_dvp_create(&device_token, &fixture_contract,
	    &fixture_contract.memory[10], &bus_token, 17, out);
}

static void
native_tests(void)
{
	struct bcm2712_dvp_provider *sc = NULL, *second = NULL;
	struct bcm2712_dvp_status status, saved;
	struct worker_arg args[2];
	pthread_t threads[2];
	unsigned int calls, i;

	reset_model(0x18, 0);
	CHECK(bcm2712_display_dvp_create(NULL, &fixture_contract, &fixture_contract.memory[10], &bus_token, 17, &sc) == EINVAL);
	CHECK(create(NULL) == EINVAL);
	CHECK(bcm2712_display_dvp_create(&device_token, &fixture_contract, &fixture_contract.memory[10], NULL, 17, &sc) == EINVAL);
	in_irq = true; CHECK(create(&sc) == EWOULDBLOCK); in_irq = false;
	in_softirq = true; CHECK(create(&sc) == EWOULDBLOCK); in_softirq = false;
	fixture_contract.version = 2; CHECK(create(&sc) == EOPNOTSUPP); fixture_contract.version = 1;
	CHECK(sc == NULL && hw.calls == 0 && allocated == freed);
	CHECK(create(&sc) == 0 && sc != NULL);
	calls = hw.calls;
	CHECK(create(&second) == EBUSY && second == NULL && hw.calls == calls);
	CHECK(bcm2712_display_dvp_status(sc, &status) == 0 && hw.calls == calls);
	CHECK(status.gates == 0x18 && status.leases == 0 && !status.mutation_attempted);
	CHECK(bcm2712_display_dvp_status(sc, NULL) == EINVAL);
	CHECK(bcm2712_display_dvp_acquire(NULL, 0) == ENXIO);
	in_irq = true; CHECK(bcm2712_display_dvp_acquire(sc, 0) == EWOULDBLOCK); in_irq = false;
	CHECK(hw.calls == calls);
	CHECK(bcm2712_display_dvp_acquire(sc, 0) == 0);
	CHECK(bcm2712_display_dvp_destroy(sc) == EBUSY && bcm2712_dvp_owner == sc);
	CHECK(bcm2712_display_dvp_release(sc, 0) == 0);
	for (i = 0; i < 2; i++) {
		args[i] = (struct worker_arg) { sc, i };
		CHECK(pthread_create(&threads[i], NULL, worker, &args[i]) == 0);
	}
	for (i = 0; i < 2; i++) CHECK(pthread_join(threads[i], NULL) == 0);
	CHECK(hw.gates == 0x18 && hw.resets == 0);
	CHECK(bcm2712_display_dvp_destroy(sc) == 0 && allocated == freed);
	CHECK(bcm2712_display_dvp_destroy(NULL) == ENXIO);
	reset_model(0x18, 0); CHECK(create(&sc) == 0);
	CHECK(bcm2712_display_dvp_acquire(sc, 1) == 0);
	CHECK(bcm2712_display_dvp_status(sc, &saved) == 0);
	hw.ignore_write = true;
	CHECK(bcm2712_display_dvp_acquire(sc, 0) == EIO);
	CHECK(bcm2712_display_dvp_status(sc, &status) == 0 && status.fault == EIO);
	CHECK(status.leases == 2 && status.gates == saved.gates);
	calls = hw.calls;
	CHECK(bcm2712_display_dvp_destroy(sc) == EIO && hw.calls == calls);
	CHECK(bcm2712_display_dvp_release(sc, 1) == EIO);
	CHECK(create(&second) == EBUSY && allocated == freed + 1);
	/* Simulated reboot ends the intentionally retained faulted test lifetime. */
	free(sc); freed++; bcm2712_dvp_owner = NULL;
	CHECK(allocated == freed);
}

int
main(void)
{
	core_tests();
	native_tests();
	printf("DVP contract: %u checks; two concurrent ports, 2000 lease cycles PASS\n", checks);
	return 0;
}
