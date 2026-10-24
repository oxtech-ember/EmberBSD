/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD actual-source ADI AXI DMAC host regression. */
#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#include <sys/queue.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef void *device_t;
typedef struct {
	pthread_mutex_t native;
	unsigned held;
} kmutex_t;
typedef struct {
	pthread_cond_t native;
	bool initialized;
	bool waiting;
	bool signaled;
} kcondvar_t;
typedef void *bus_space_tag_t;
typedef void *bus_space_handle_t;
typedef void *bus_dma_tag_t;
typedef size_t bus_size_t;
typedef struct {
	uint64_t ds_addr;
	void *data;
} bus_dma_segment_t;
typedef struct test_dmamap {
	unsigned dm_nsegs;
	bus_dma_segment_t dm_segs[1];
	void *cpu_data;
	size_t sync_size;
	int sync_pre;
	bool dma_submitted;
	bool dma_drained;
	struct test_dmamap *next;
} *bus_dmamap_t;

#define CHECK(test) do { \
	if (!(test)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); \
		abort(); \
	} \
} while (0)
#define KASSERT CHECK
#define __BIT(n) (UINT32_C(1) << (n))
#undef __dead
#define __dead __attribute__((noreturn))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define KM_SLEEP 0
#define PAGE_SIZE 4096
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define IPL_BIO 1
#define BUS_DMA_WAITOK 1
#define BUS_DMA_COHERENT 2
#define BUS_DMA_READ 4
#define BUS_DMA_WRITE 8
#define BUS_DMASYNC_PREREAD 1
#define BUS_DMASYNC_POSTREAD 2
#define BUS_DMASYNC_PREWRITE 4
#define BUS_DMASYNC_POSTWRITE 8
#define BUS_SPACE_BARRIER_WRITE 1
#define SPINLOCK_BACKOFF_HOOK abort()
#define ONCE_DECL(name) bool name
#define RUN_ONCE run_once

enum irq_mode {
	IRQ_NONE,
	IRQ_MATCH,
	IRQ_WRONG_THEN_MATCH,
	IRQ_SOT,
	IRQ_DONE_ONLY,
	IRQ_BEFORE_WAIT,
	IRQ_SPURIOUS,
	IRQ_SPURIOUS_THEN_MATCH,
	IRQ_WRONG_ONLY,
	IRQ_TIMEOUT_MATCH,
	IRQ_EMPTY,
	IRQ_THREADED
};

static uint32_t registers[0x500 / 4];
static atomic_uint ticks;
static unsigned drain_calls, sync_calls, submits;
static enum irq_mode irq_mode;
static unsigned wait_calls, signal_calls, wait_delays[16];
static bool before_wait_delivered;
static kmutex_t *irq_lock;
static kcondvar_t *done_cv;
static unsigned alloc_step, alloc_fail_at;
static int drain_error, map_flags, load_flags, sync_flags[128];
static size_t sync_sizes[128];
static bus_dmamap_t dma_maps;
static pthread_mutex_t maps_lock = PTHREAD_MUTEX_INITIALIZER;
static bool drained, submitted;
static unsigned idle_ready_after, idle_reads, drain_elapsed;
static bool idle_pending;
static uint64_t physical_address;
static void *irq_cookie;
static int adiaxidmac_intr(void *);
static void deliver_irq(unsigned, uint32_t);

static pthread_mutex_t gate_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cv = PTHREAD_COND_INITIALIZER;
static unsigned thread_waits;
static kmutex_t *observed_lock;
static bool lock_blocked, copy_waiting, copy_release;
static void *copy_target;
static struct adi_axi_dmac_buffer *postdrain_buffer;
static kmutex_t *buffer_lock;
static bool postdrain_waiting, postdrain_release, postdrain_armed;
static bool pause_after_drain(const kmutex_t *);

static struct timespec
deadline(unsigned milliseconds)
{
	struct timespec ts;

	CHECK(clock_gettime(CLOCK_REALTIME, &ts) == 0);
	ts.tv_sec += milliseconds / 1000;
	ts.tv_nsec += (long)(milliseconds % 1000) * 1000000;
	if (ts.tv_nsec >= 1000000000) {
		ts.tv_sec++;
		ts.tv_nsec -= 1000000000;
	}
	return ts;
}

static void
wait_flag(const bool *flag)
{
	const struct timespec limit = deadline(5000);

	CHECK(pthread_mutex_lock(&gate_lock) == 0);
	while (!*flag)
		CHECK(pthread_cond_timedwait(&gate_cv, &gate_lock, &limit) == 0);
	CHECK(pthread_mutex_unlock(&gate_lock) == 0);
}

static void
wait_transfer(unsigned count)
{
	const struct timespec limit = deadline(5000);

	CHECK(pthread_mutex_lock(&gate_lock) == 0);
	while (thread_waits < count)
		CHECK(pthread_cond_timedwait(&gate_cv, &gate_lock, &limit) == 0);
	CHECK(pthread_mutex_unlock(&gate_lock) == 0);
}

static void
run_once(bool *done, int (*init)(void))
{
	if (!*done) {
		CHECK(init() == 0);
		*done = true;
	}
}

static void
mutex_init(kmutex_t *lock, int kind, int level)
{
	(void)kind;
	(void)level;
	CHECK(pthread_mutex_init(&lock->native, NULL) == 0);
	lock->held = 0;
}

static void
mutex_destroy(kmutex_t *lock)
{
	CHECK(lock->held == 0);
	CHECK(pthread_mutex_destroy(&lock->native) == 0);
}

static void
mutex_enter(kmutex_t *lock)
{
	int error;

	if (lock == observed_lock) {
		error = pthread_mutex_trylock(&lock->native);
		if (error == EBUSY) {
			CHECK(pthread_mutex_lock(&gate_lock) == 0);
			lock_blocked = true;
			CHECK(pthread_cond_broadcast(&gate_cv) == 0);
			CHECK(pthread_mutex_unlock(&gate_lock) == 0);
			error = pthread_mutex_lock(&lock->native);
		}
	} else {
		error = pthread_mutex_lock(&lock->native);
	}
	CHECK(error == 0 && lock->held == 0);
	lock->held = 1;
}

static void
mutex_exit(kmutex_t *lock)
{
	const bool pause = pause_after_drain(lock);

	CHECK(lock->held == 1);
	lock->held = 0;
	CHECK(pthread_mutex_unlock(&lock->native) == 0);
	if (pause) {
		CHECK(pthread_mutex_lock(&gate_lock) == 0);
		postdrain_waiting = true;
		CHECK(pthread_cond_broadcast(&gate_cv) == 0);
		while (!postdrain_release)
			CHECK(pthread_cond_wait(&gate_cv, &gate_lock) == 0);
		CHECK(pthread_mutex_unlock(&gate_lock) == 0);
	}
	/* Deliver an already pending EOT before the first predicate check. */
	if (irq_mode == IRQ_BEFORE_WAIT && lock == irq_lock && submitted &&
	    !before_wait_delivered) {
		before_wait_delivered = true;
		deliver_irq(2, __BIT(registers[0x404 / 4]));
	}
}

static void
cv_init(kcondvar_t *cv, const char *name)
{
	CHECK(name != NULL);
	memset(cv, 0, sizeof(*cv));
	CHECK(pthread_cond_init(&cv->native, NULL) == 0);
	cv->initialized = true;
}

static void
cv_destroy(kcondvar_t *cv)
{
	CHECK(cv->initialized && !cv->waiting);
	CHECK(pthread_cond_destroy(&cv->native) == 0);
	cv->initialized = false;
}

static void
cv_signal(kcondvar_t *cv)
{
	CHECK(cv == done_cv && cv->initialized && irq_lock->held == 1);
	signal_calls++;
	if (cv->waiting)
		cv->signaled = true;
	CHECK(pthread_cond_signal(&cv->native) == 0);
}
static bool
mutex_tryenter(kmutex_t *lock)
{
	int error = pthread_mutex_trylock(&lock->native);

	if (error == EBUSY)
		return false;
	CHECK(error == 0 && lock->held == 0);
	lock->held = 1;
	return true;
}
static void *kmem_zalloc(size_t n, int flags)
{
	void *p;
	(void)flags;
	p = calloc(1, n);
	CHECK(p != NULL);
	return p;
}
static void kmem_free(void *p, size_t n) { (void)n; free(p); }
static void device_acquire(device_t d) { CHECK(d != NULL); }
static void device_release(device_t d) { CHECK(d != NULL); }
static void *device_private(device_t d) { return d; }
static const char *device_xname(device_t d) { (void)d; return "dmac-test"; }
static int splhigh(void) { return 0; }
static unsigned getticks(void) { return ticks; }
static unsigned mstohz(unsigned ms) { return ms; }
static inline void delay(unsigned us)
{
	CHECK(us > 0 && irq_lock->held == 0);
	ticks++;
}

static uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle, size_t reg)
{
	(void)tag;
	(void)handle;
	CHECK(reg < sizeof(registers) && (reg & 3) == 0);
	if (reg == 0x43c && idle_pending) {
		idle_reads++;
		if (idle_ready_after != UINT_MAX &&
		    idle_reads >= idle_ready_after) {
			registers[reg / 4] = 0x701;
			idle_pending = false;
		}
	}
	return registers[reg / 4];
}

static void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle,
    size_t reg, uint32_t value)
{
	(void)tag;
	(void)handle;
	CHECK(reg < sizeof(registers) && (reg & 3) == 0);
	if (reg == 0x084) {
		registers[reg / 4] &= ~value;
		return;
	}
	if (reg == 0x418)
		value = (value & 0xffffff) | 7;
	registers[reg / 4] = value;
	if (reg == 0x400 && value == 0)
		registers[0x408 / 4] = 0;
	if (reg == 0x408 && value == 1) {
		bus_dmamap_t map;
		uint64_t address = registers[0x414 / 4] != 0 ?
		    registers[0x414 / 4] : registers[0x410 / 4];

		CHECK(registers[0x400 / 4] == 1);
		CHECK(pthread_mutex_lock(&maps_lock) == 0);
		for (map = dma_maps; map != NULL; map = map->next)
			if (map->dm_segs[0].ds_addr == address)
				break;
		CHECK(map != NULL && !map->dma_submitted);
		CHECK(map->sync_size == (size_t)registers[0x418 / 4] + 1);
		CHECK(map->sync_pre == BUS_DMASYNC_PREREAD ||
		    map->sync_pre == BUS_DMASYNC_PREWRITE);
		map->dma_submitted = true;
		map->dma_drained = false;
		CHECK(pthread_mutex_unlock(&maps_lock) == 0);
		registers[0x428 / 4] &= ~__BIT(registers[0x404 / 4]);
		registers[reg / 4] = 0;
		submitted = true;
		submits++;
	}
}

static void
bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t handle,
    size_t start, size_t size, int flags)
{
	(void)tag;
	(void)handle;
	(void)start;
	(void)size;
	CHECK(flags == BUS_SPACE_BARRIER_WRITE);
}

static int
bus_dmamem_alloc(bus_dma_tag_t tag, size_t size, size_t align, size_t boundary,
    bus_dma_segment_t *seg, int nseg, int *count, int flags)
{
	(void)tag;
	(void)boundary;
	(void)flags;
	CHECK(align == PAGE_SIZE && nseg == 1);
	if (++alloc_step == alloc_fail_at)
		return ENOMEM;
	seg->data = malloc(size);
	CHECK(seg->data != NULL);
	seg->ds_addr = physical_address;
	*count = 1;
	return 0;
}

static int
bus_dmamem_map(bus_dma_tag_t tag, bus_dma_segment_t *seg, int nseg,
    size_t size, void **data, int flags)
{
	(void)tag;
	(void)size;
	map_flags = flags;
	CHECK(nseg == 1);
	if (++alloc_step == alloc_fail_at)
		return ENOMEM;
	*data = seg->data;
	return 0;
}

static int
bus_dmamap_create(bus_dma_tag_t tag, size_t size, int nseg, size_t maxseg,
    size_t boundary, int flags, bus_dmamap_t *map)
{
	(void)tag;
	(void)size;
	(void)maxseg;
	(void)boundary;
	(void)flags;
	CHECK(nseg == 1);
	if (++alloc_step == alloc_fail_at)
		return ENOMEM;
	*map = calloc(1, sizeof(**map));
	CHECK(*map != NULL);
	CHECK(pthread_mutex_lock(&maps_lock) == 0);
	(*map)->next = dma_maps;
	dma_maps = *map;
	CHECK(pthread_mutex_unlock(&maps_lock) == 0);
	return 0;
}

static int
bus_dmamap_load(bus_dma_tag_t tag, bus_dmamap_t map, void *data,
    size_t size, void *proc, int flags)
{
	(void)tag;
	(void)proc;
	if (++alloc_step == alloc_fail_at)
		return ENOMEM;
	CHECK(pthread_mutex_lock(&maps_lock) == 0);
	map->dm_nsegs = 1;
	map->dm_segs[0].ds_addr = physical_address;
	map->cpu_data = data;
	physical_address += (size + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
	load_flags = flags;
	CHECK(pthread_mutex_unlock(&maps_lock) == 0);
	return 0;
}

static void
bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t map)
{
	(void)t;
	CHECK(pthread_mutex_lock(&maps_lock) == 0);
	CHECK(map->sync_size == 0 && !map->dma_submitted);
	CHECK(pthread_mutex_unlock(&maps_lock) == 0);
}

static void
bus_dmamap_destroy(bus_dma_tag_t t, bus_dmamap_t map)
{
	bus_dmamap_t *link;

	(void)t;
	CHECK(pthread_mutex_lock(&maps_lock) == 0);
	CHECK(map->sync_size == 0 && !map->dma_submitted);
	for (link = &dma_maps; *link != NULL; link = &(*link)->next)
		if (*link == map)
			break;
	CHECK(*link == map);
	*link = map->next;
	free(map);
	CHECK(pthread_mutex_unlock(&maps_lock) == 0);
}
static void bus_dmamem_unmap(bus_dma_tag_t t, void *p, size_t n)
{ (void)t; (void)p; (void)n; }
static void bus_dmamem_free(bus_dma_tag_t t, bus_dma_segment_t *s, int n)
{ (void)t; CHECK(n == 1); free(s->data); }

static void
bus_dmamap_sync(bus_dma_tag_t tag, bus_dmamap_t map, size_t offset,
    size_t size, int flags)
{
	(void)tag;
	CHECK(pthread_mutex_lock(&maps_lock) == 0);
	CHECK(offset == 0 && size > 0 &&
	    sync_calls < sizeof(sync_flags) / sizeof(sync_flags[0]));
	if (flags == BUS_DMASYNC_PREREAD || flags == BUS_DMASYNC_PREWRITE) {
		CHECK(map->sync_size == 0 && !map->dma_submitted);
		map->sync_size = size;
		map->sync_pre = flags;
	} else {
		CHECK(map->sync_size == size);
		CHECK((map->sync_pre == BUS_DMASYNC_PREREAD &&
		    flags == BUS_DMASYNC_POSTREAD) ||
		    (map->sync_pre == BUS_DMASYNC_PREWRITE &&
		    flags == BUS_DMASYNC_POSTWRITE));
		CHECK(!map->dma_submitted || map->dma_drained);
		CHECK(!map->dma_submitted || !idle_pending);
		map->sync_size = 0;
		map->sync_pre = 0;
		map->dma_submitted = false;
	}
	sync_sizes[sync_calls] = size;
	sync_flags[sync_calls++] = flags;
	CHECK(pthread_mutex_unlock(&maps_lock) == 0);
}

static void
deliver_irq(unsigned pending, uint32_t done)
{
	mutex_enter(irq_lock);
	registers[0x428 / 4] = done;
	registers[0x084 / 4] = pending;
	mutex_exit(irq_lock);
	CHECK(adiaxidmac_intr(irq_cookie) == (pending != 0));
	mutex_enter(irq_lock);
	CHECK(registers[0x084 / 4] == 0);
	mutex_exit(irq_lock);
}

/*
 * Model the kernel CV contract: enqueue before releasing the interlock,
 * deliver interrupts while asleep, then reacquire the same interlock.
 * No kernel scheduling or wall-clock timing is simulated.
 */
static int
cv_timedwait(kcondvar_t *cv, kmutex_t *lock, int timeout_ticks)
{
	const uint32_t matching = __BIT(registers[0x404 / 4]);
	const uint32_t other = __BIT((registers[0x404 / 4] + 1) % 4);
	unsigned started = ticks, signals = signal_calls;
	bool spurious = false, expired = false;

	CHECK(cv == done_cv && cv->initialized && !cv->waiting);
	CHECK(lock == irq_lock && lock->held == 1 && submitted);
	CHECK(timeout_ticks > 0 && wait_calls < sizeof(wait_delays) / sizeof(wait_delays[0]));
	wait_delays[wait_calls++] = (unsigned)timeout_ticks;
	cv->waiting = true;
	cv->signaled = false;
	if (irq_mode == IRQ_THREADED) {
		const struct timespec limit = deadline((unsigned)timeout_ticks);
		int error;

		CHECK(pthread_mutex_lock(&gate_lock) == 0);
		thread_waits++;
		CHECK(pthread_cond_broadcast(&gate_cv) == 0);
		CHECK(pthread_mutex_unlock(&gate_lock) == 0);
		lock->held = 0;
		error = pthread_cond_timedwait(&cv->native, &lock->native, &limit);
		CHECK(error == 0 || error == ETIMEDOUT);
		lock->held = 1;
		cv->waiting = false;
		ticks += error == 0 ? 1 : (unsigned)timeout_ticks;
		return error == 0 ? 0 : EWOULDBLOCK;
	}
	mutex_exit(lock);
	switch (irq_mode) {
	case IRQ_MATCH:
		ticks++;
		deliver_irq(2, matching);
		break;
	case IRQ_WRONG_THEN_MATCH:
		CHECK(timeout_ticks >= 2);
		ticks++;
		deliver_irq(2, other);
		CHECK(signal_calls == signals && !cv->signaled);
		ticks++;
		deliver_irq(2, matching);
		break;
	case IRQ_SOT:
		deliver_irq(1, matching);
		break;
	case IRQ_DONE_ONLY:
		registers[0x428 / 4] = matching;
		break;
	case IRQ_SPURIOUS:
		ticks++;
		spurious = true;
		break;
	case IRQ_SPURIOUS_THEN_MATCH:
		ticks++;
		if (wait_calls < 3)
			spurious = true;
		else
			deliver_irq(2, matching);
		break;
	case IRQ_WRONG_ONLY:
		deliver_irq(2, other);
		break;
	case IRQ_TIMEOUT_MATCH:
		ticks += (unsigned)timeout_ticks;
		deliver_irq(2, matching);
		expired = true;
		break;
	case IRQ_EMPTY:
		deliver_irq(0, matching);
		break;
	case IRQ_NONE:
		break;
	case IRQ_THREADED:
	case IRQ_BEFORE_WAIT:
		CHECK(false); /* A completed predicate must not enter the CV. */
		break;
	}
	if (!cv->signaled && !spurious) {
		CHECK(signal_calls == signals);
		ticks += (unsigned)timeout_ticks - (ticks - started);
		expired = true;
	}
	CHECK(ticks - started <= (unsigned)timeout_ticks);
	cv->waiting = false;
	mutex_enter(lock);
	return expired ? EWOULDBLOCK : 0;
}

static void *
buffer_memcpy(void *destination, const void *source, size_t size)
{
	bus_dmamap_t map;

	CHECK(pthread_mutex_lock(&maps_lock) == 0);
	for (map = dma_maps; map != NULL; map = map->next)
		if (destination == map->cpu_data || source == map->cpu_data)
			CHECK(map->sync_size == 0 && !map->dma_submitted);
	CHECK(pthread_mutex_unlock(&maps_lock) == 0);
	if (destination == copy_target) {
		CHECK(pthread_mutex_lock(&gate_lock) == 0);
		copy_waiting = true;
		CHECK(pthread_cond_broadcast(&gate_cv) == 0);
		while (!copy_release)
			CHECK(pthread_cond_wait(&gate_cv, &gate_lock) == 0);
		CHECK(pthread_mutex_unlock(&gate_lock) == 0);
	}
	return memcpy(destination, source, size);
}

#include "api.h"
#undef memcpy
#define memcpy buffer_memcpy
#include "driver.inc"
#undef memcpy

static struct adi_axi_dmac_softc fixture;
static bool fixture_initialized;

static bool
pause_after_drain(const kmutex_t *lock)
{
	if (lock != buffer_lock || postdrain_buffer == NULL || !postdrain_armed)
		return false;
	if (postdrain_buffer->b_inflight != 0 || !postdrain_buffer->b_transferring)
		return false;
	postdrain_armed = false;
	return true;
}

/* The kernel drain callback signature requires a mutable void pointer. */
static int
// cppcheck-suppress constParameterCallback
drain(void *cookie, unsigned timeout)
{
	bus_dmamap_t map;

	CHECK(cookie == &fixture && timeout > 0);
	CHECK(registers[DMAC_CONTROL / 4] == 0);
	drain_calls++;
	drained = drain_error == 0;
	ticks += drain_elapsed;
	idle_reads = 0;
	idle_pending = drained && idle_ready_after != 0;
	/* Exact HDL clears both IDs on disable, before outstanding BRESP. */
	registers[0x42c / 4] = registers[0x404 / 4] = 0;
	registers[0x43c / 4] = idle_pending ? 0x072 : 0x701;
	CHECK(pthread_mutex_lock(&maps_lock) == 0);
	for (map = dma_maps; map != NULL; map = map->next)
		if (map->dma_submitted)
			map->dma_drained = drained;
	CHECK(pthread_mutex_unlock(&maps_lock) == 0);
	return drain_error;
}

static const struct adi_axi_dmac_drain_ops drain_ops = { .drain = drain };

static void
reset_fixture(uint32_t interface)
{
	CHECK(dma_maps == NULL);
	if (fixture_initialized) {
		cv_destroy(&fixture.sc_done_cv);
		mutex_destroy(&fixture.sc_irq_lock);
		mutex_destroy(&fixture.sc_buffer_lock);
		mutex_destroy(&fixture.sc_lock);
	}
	memset(&fixture, 0, sizeof(fixture));
	mutex_init(&fixture.sc_lock, MUTEX_DEFAULT, IPL_NONE);
	mutex_init(&fixture.sc_buffer_lock, MUTEX_DEFAULT, IPL_NONE);
	mutex_init(&fixture.sc_irq_lock, MUTEX_DEFAULT, IPL_BIO);
	cv_init(&fixture.sc_done_cv, "adidma");
	fixture_initialized = true;
	irq_lock = &fixture.sc_irq_lock;
	buffer_lock = &fixture.sc_buffer_lock;
	done_cv = &fixture.sc_done_cv;
	memset(registers, 0, sizeof(registers));
	ticks = drain_calls = sync_calls = submits = 0;
	wait_calls = signal_calls = 0;
	before_wait_delivered = false;
	thread_waits = 0;
	observed_lock = NULL;
	lock_blocked = copy_waiting = copy_release = false;
	copy_target = NULL;
	postdrain_buffer = NULL;
	postdrain_waiting = postdrain_release = postdrain_armed = false;
	alloc_step = alloc_fail_at = 0;
	drain_error = 0;
	map_flags = load_flags = -1;
	irq_mode = IRQ_MATCH;
	submitted = drained = false;
	idle_ready_after = idle_reads = drain_elapsed = 0;
	idle_pending = false;
	physical_address = 0x100000;
	fixture.sc_dev = &fixture;
	fixture.sc_phandle = 17;
	fixture.sc_size = sizeof(registers);
	CHECK(dmac_interface(&fixture, 0x040461, interface) == 0);
	LIST_INIT(&dmacs);
	LIST_INSERT_HEAD(&dmacs, &fixture, sc_next);
	irq_cookie = &fixture;
}

static struct adi_axi_dmac_channel *
acquire(void)
{
	struct adi_axi_dmac_channel *c = NULL;
	CHECK(adi_axi_dmac_acquire(17, &fixture, &drain_ops, &fixture, &c) == 0);
	CHECK(c != NULL && c->c_max_length == 0x1000000);
	CHECK(c->c_length_mask == 7 && drain_calls == 1);
	return c;
}

static void
check_interfaces(void)
{
	reset_fixture(0x0323); /* 64-bit MM -> 64-bit FIFO. */
	CHECK(!fixture.sc_read && fixture.sc_alignment == 8);
	CHECK(dmac_interface(&fixture, 0x040461, 0x0212) == 0);
	CHECK(!fixture.sc_read && fixture.sc_stream_dest && fixture.sc_alignment == 4);
	CHECK(dmac_interface(&fixture, 0x040361, 0x2303) == 0);
	CHECK(fixture.sc_read && fixture.sc_alignment == 8);
	CHECK(dmac_interface(&fixture, 0x040461, 0x1303) == 0);
	CHECK(fixture.sc_read && fixture.sc_stream_source);
	/* Live Pluto v0.39 DMAC 4.5.c: burst-width bits accompany FIFO RX. */
	CHECK(dmac_interface(&fixture, 0x040563, 0x00072303) == 0);
	CHECK(fixture.sc_read && !fixture.sc_stream_source &&
	    fixture.sc_alignment == 8);
	CHECK(dmac_interface(&fixture, 0x040661, 0x0323) == EOPNOTSUPP);
	CHECK(dmac_interface(&fixture, 0x040461, 0x0303) == EOPNOTSUPP);
	CHECK(dmac_interface(&fixture, 0x040461, 0x2313) == EOPNOTSUPP);
	CHECK(dmac_interface(&fixture, 0x040461, 0x0833) == EOPNOTSUPP);
	puts("PASS interface direction and byte-width alignment");
}

static void
check_transfer(uint32_t interface, enum irq_mode irq, int expected,
    unsigned initial_ticks)
{
	struct adi_axi_dmac_channel *c;
	struct adi_axi_dmac_buffer *b;
	unsigned char data[64];
	int pre, post;
	unsigned i, signals;

	reset_fixture(interface);
	c = acquire();
	CHECK(adi_axi_dmac_buffer_alloc(c, sizeof(data), &b) == 0);
	memset(data, 0xa5, sizeof(data));
	if (!fixture.sc_read)
		CHECK(adi_axi_dmac_buffer_write(b, data, sizeof(data)) == 0);
	CHECK(map_flags == (BUS_DMA_WAITOK |
	    (fixture.sc_read ? BUS_DMA_COHERENT : 0)));
	CHECK(load_flags == (BUS_DMA_WAITOK |
	    (fixture.sc_read ? BUS_DMA_READ : BUS_DMA_WRITE)));
	irq_mode = irq;
	ticks = initial_ticks;
	registers[DMAC_ID / 4] = 2;
	/* An old completion is cleared when this ID is submitted. */
	registers[DMAC_DONE / 4] = __BIT(2);
	CHECK(adi_axi_dmac_transfer(b, sizeof(data), 5) == expected);
	CHECK(submits == 1 && sync_calls == 2 && drain_calls == 2);
	pre = fixture.sc_read ? BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE;
	post = fixture.sc_read ? BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE;
	CHECK(sync_flags[0] == pre && sync_flags[1] == post);
	CHECK(registers[(fixture.sc_read ? DMAC_DEST : DMAC_SOURCE) / 4] ==
	    b->b_map->dm_segs[0].ds_addr);
	CHECK(registers[(fixture.sc_read ? DMAC_SOURCE : DMAC_DEST) / 4] == 0);
	CHECK(registers[DMAC_FLAGS / 4] ==
	    (fixture.sc_stream_dest ? DMAC_FLAG_TLAST : 0));
	CHECK(registers[DMAC_X_LENGTH / 4] == sizeof(data) - 1);
	CHECK(b->b_inflight == 0 && fixture.sc_drained);
	if (expected == 0) {
		unsigned char received[64];

		CHECK(adi_axi_dmac_buffer_read(b, received, sizeof(received)) == 0);
		if (!fixture.sc_read)
			CHECK(memcmp(data, received, sizeof(data)) == 0);
	} else {
		CHECK(b->b_valid == 0);
	}
	switch (irq) {
	case IRQ_BEFORE_WAIT:
		CHECK(wait_calls == 0 && signal_calls == 1);
		CHECK(ticks == initial_ticks);
		break;
	case IRQ_MATCH:
		CHECK(wait_calls == 1 && signal_calls == 1);
		CHECK(ticks - initial_ticks == 1);
		break;
	case IRQ_WRONG_THEN_MATCH:
		CHECK(wait_calls == 1 && signal_calls == 1);
		CHECK(ticks - initial_ticks == 2);
		break;
	case IRQ_SPURIOUS_THEN_MATCH:
		CHECK(wait_calls == 3 && signal_calls == 1);
		CHECK(ticks - initial_ticks == 3);
		break;
	case IRQ_SPURIOUS:
		CHECK(wait_calls == 5 && signal_calls == 0);
		CHECK(ticks - initial_ticks == 5);
		break;
	case IRQ_TIMEOUT_MATCH:
		CHECK(wait_calls == 1 && signal_calls == 1);
		CHECK(ticks - initial_ticks == 5);
		break;
	default:
		CHECK(wait_calls == 1 && signal_calls == 0);
		CHECK(ticks - initial_ticks == 5);
		break;
	}
	for (i = 0; i < wait_calls; i++)
		CHECK(wait_delays[i] == 5 - i);
	signals = signal_calls;
	deliver_irq(2, __BIT(registers[DMAC_ID / 4]));
	CHECK(signal_calls == signals); /* No waiter after the checked stop. */
	CHECK(adi_axi_dmac_buffer_free(b) == 0);
	CHECK(adi_axi_dmac_release(c) == 0);
}

static void
check_bounds_and_ownership(void)
{
	struct adi_axi_dmac_channel *c, *other;
	struct adi_axi_dmac_buffer *b;
	struct adi_axi_dmac_guard *guard;
	unsigned char data[64] = { 0 };

	reset_fixture(0x0323);
	CHECK(adi_axi_dmac_reserve(17, &fixture, &guard) == 0);
	CHECK(adi_axi_dmac_guard_cold_check(guard) == 0);
	CHECK(adi_axi_dmac_guard_check(guard) == 0);
	CHECK(adi_axi_dmac_acquire(17, &fixture, &drain_ops, &fixture, &other) == EBUSY);
	CHECK(other == NULL);
	adi_axi_dmac_unreserve(guard);
	c = acquire();
	CHECK(adi_axi_dmac_reserve(17, &fixture, &guard) == EBUSY);
	CHECK(adi_axi_dmac_buffer_alloc(c, 2080000, &b) == 0);
	CHECK(adi_axi_dmac_release(c) == EBUSY);
	CHECK(adi_axi_dmac_transfer(b, 8, 5) == EINVAL);
	CHECK(adi_axi_dmac_buffer_write(b, data, sizeof(data)) == 0);
	CHECK(adi_axi_dmac_transfer(b, 0, 5) == EINVAL);
	CHECK(adi_axi_dmac_transfer(b, 65, 5) == EINVAL);
	CHECK(adi_axi_dmac_transfer(b, 64, 0) == EINVAL);
	CHECK(submits == 0);
	CHECK(adi_axi_dmac_buffer_free(b) == 0);
	physical_address++;
	CHECK(adi_axi_dmac_buffer_alloc(c, 64, &b) == EFBIG);
	CHECK(b == NULL);
	physical_address = UINT64_C(0xfffffff8);
	CHECK(adi_axi_dmac_buffer_alloc(c, 64, &b) == EFBIG);
	CHECK(adi_axi_dmac_release(c) == 0);
	puts("PASS bounded length, DMA address, buffer validity and ownership");
}

static void
check_quarantine(enum irq_mode irq)
{
	struct adi_axi_dmac_channel *c;
	struct adi_axi_dmac_buffer *b;
	unsigned char data[64] = { 0 };

	reset_fixture(0x0323);
	c = acquire();
	CHECK(adi_axi_dmac_buffer_alloc(c, sizeof(data), &b) == 0);
	CHECK(adi_axi_dmac_buffer_write(b, data, sizeof(data)) == 0);
	irq_mode = irq;
	drain_error = EIO;
	CHECK(adi_axi_dmac_transfer(b, sizeof(data), 5) == EIO);
	CHECK(fixture.sc_quarantined && b->b_inflight == sizeof(data));
	CHECK(sync_calls == 1); /* No POSTWRITE without a drain certificate. */
	CHECK(adi_axi_dmac_buffer_write(b, data, sizeof(data)) == EBUSY);
	CHECK(adi_axi_dmac_buffer_free(b) == EBUSY);
	CHECK(adi_axi_dmac_stop(c, 5) == EIO);
	/* Model removal of power before releasing retained fixture memory. */
	fixture.sc_quarantined = false;
	drain_error = 0;
	CHECK(adi_axi_dmac_stop(c, 5) == 0);
	CHECK(adi_axi_dmac_buffer_free(b) == 0);
	CHECK(adi_axi_dmac_release(c) == 0);
	puts("PASS failed drain retains DMA memory and quarantines the provider");
}

static void
check_hardware_idle(unsigned ready_after, int expected, unsigned start,
    unsigned callback_ticks)
{
	struct adi_axi_dmac_channel *c;
	struct adi_axi_dmac_buffer *b;
	unsigned char data[64];

	reset_fixture(0x2303);
	CHECK(dmac_interface(&fixture, 0x040563, 0x2303) == 0);
	c = acquire();
	CHECK(adi_axi_dmac_buffer_alloc(c, sizeof(data), &b) == 0);
	idle_ready_after = ready_after;
	drain_elapsed = callback_ticks;
	ticks = start;
	CHECK(adi_axi_dmac_transfer(b, sizeof(data), 5) == expected);
	if (expected == 0) {
		CHECK(!idle_pending && idle_reads == ready_after);
		CHECK(fixture.sc_drained && b->b_inflight == 0);
		CHECK(adi_axi_dmac_buffer_read(b, data, sizeof(data)) == 0);
	} else {
		CHECK(fixture.sc_quarantined && !fixture.sc_drained);
		CHECK(b->b_inflight == sizeof(data) && sync_calls == 1);
		CHECK(adi_axi_dmac_buffer_read(b, data, sizeof(data)) == EBUSY);
		CHECK(adi_axi_dmac_buffer_free(b) == EBUSY);
		CHECK(adi_axi_dmac_stop(c, 5) == EIO);
		/* Model power removal before retained memory can be released. */
		fixture.sc_quarantined = false;
	}
	idle_ready_after = drain_elapsed = 0;
	CHECK(adi_axi_dmac_stop(c, 5) == 0);
	CHECK(adi_axi_dmac_buffer_free(b) == 0);
	CHECK(adi_axi_dmac_release(c) == 0);
	puts(expected == 0 ? "PASS RX publication waits for hardware idle" :
	    "PASS hardware idle timeout retains DMA memory and original deadline");
}

static void
check_guard_idle_witness(void)
{
	struct adi_axi_dmac_guard *g;
	unsigned submits_before;

	reset_fixture(0x2303);
	CHECK(adi_axi_dmac_reserve(17, &fixture, &g) == 0);
	CHECK(adi_axi_dmac_guard_wait_idle(g, 2) == EOPNOTSUPP);
	CHECK(dmac_interface(&fixture, 0x040563, 0x2303) == 0);
	submits_before = submits;
	registers[0x43c / 4] = 0; /* Missing debug registers are not idle. */
	CHECK(adi_axi_dmac_guard_wait_idle(g, 2) == ETIMEDOUT);
	registers[0x43c / 4] = 0x701;
	registers[DMAC_CONTROL / 4] = DMAC_ENABLE;
	CHECK(adi_axi_dmac_guard_wait_idle(g, 2) == ETIMEDOUT);
	registers[DMAC_CONTROL / 4] = 0;
	registers[DMAC_SUBMIT / 4] = 1;
	CHECK(adi_axi_dmac_guard_wait_idle(g, 2) == ETIMEDOUT);
	registers[DMAC_SUBMIT / 4] = 0;
	registers[0x43c / 4] = 0x781; /* Different SG topology is not accepted. */
	CHECK(adi_axi_dmac_guard_wait_idle(g, 2) == ETIMEDOUT);
	registers[0x43c / 4] = 0x701;
	CHECK(adi_axi_dmac_guard_wait_idle(g, 2) == 0);
	CHECK(submits == submits_before && !fixture.sc_drained);
	CHECK(adi_axi_dmac_unreserve(g) == 0);
	puts("PASS read-only exact-version reset-manager witness and absent-debug refusal");
}

static void
check_guard_prepare_reuse(void)
{
	struct adi_axi_dmac_guard *g;
	unsigned calls;

	reset_fixture(0x2303);
	CHECK(dmac_interface(&fixture, 0x040563, 0x2303) == 0);
	CHECK(adi_axi_dmac_reserve(17, &fixture, &g) == 0);
	CHECK(adi_axi_dmac_guard_bind(g, &drain_ops, &fixture) == 0);
	CHECK(adi_axi_dmac_guard_prepare(g) == 0);
	calls = drain_calls;
	CHECK(adi_axi_dmac_guard_prepare(g) == 0);
	CHECK(drain_calls == calls && fixture.sc_channel == g->g_channel);
	fixture.sc_drained = false;
	CHECK(adi_axi_dmac_guard_prepare(g) == EIO);
	CHECK(fixture.sc_quarantined);
	/* Simulate power removal before releasing the retained test owner. */
	fixture.sc_quarantined = false;
	fixture.sc_drained = true;
	CHECK(adi_axi_dmac_unreserve(g) == 0);
	puts("PASS repeated guard preparation reuses only the proved idle owner");
}

static void
check_large_and_allocation(void)
{
	const size_t size = 2080000;
	struct adi_axi_dmac_channel *c;
	struct adi_axi_dmac_buffer *b;
	unsigned char *data;
	unsigned step;

	reset_fixture(0x0323);
	c = acquire();
	for (step = 1; step <= 4; step++) {
		alloc_step = 0;
		alloc_fail_at = step;
		CHECK(adi_axi_dmac_buffer_alloc(c, size, &b) == ENOMEM);
		CHECK(b == NULL && LIST_EMPTY(&c->c_buffers));
	}
	alloc_fail_at = 0;
	CHECK(adi_axi_dmac_buffer_alloc(c, size, &b) == 0);
	data = malloc(size);
	CHECK(data != NULL);
	memset(data, 0x5a, size);
	CHECK(adi_axi_dmac_buffer_write(b, data, size) == 0);
	CHECK(adi_axi_dmac_transfer(b, size, 20) == 0);
	CHECK(registers[DMAC_X_LENGTH / 4] == size - 1);
	CHECK(b->b_valid == size);
	free(data);
	CHECK(adi_axi_dmac_buffer_free(b) == 0);
	CHECK(adi_axi_dmac_release(c) == 0);
	puts("PASS 2080000-byte finite TX and allocation failure cleanup");
}

enum call_operation {
	CALL_TRANSFER,
	CALL_WRITE,
	CALL_PREPARE,
	CALL_STOP,
	CALL_FREE,
	CALL_RELEASE
};

struct thread_call {
	enum call_operation operation;
	struct adi_axi_dmac_channel *channel;
	struct adi_axi_dmac_buffer *buffer;
	const void *data;
	int result;
	bool returned;
};

static void *
run_call(void *cookie)
{
	struct thread_call *call = cookie;

	switch (call->operation) {
	case CALL_TRANSFER:
		call->result = adi_axi_dmac_transfer(call->buffer, 64, 2000);
		break;
	case CALL_WRITE:
		call->result = adi_axi_dmac_buffer_write(call->buffer, call->data, 64);
		break;
	case CALL_PREPARE:
		call->result = adi_axi_dmac_buffer_prepare(call->buffer, 64);
		break;
	case CALL_STOP:
		call->result = adi_axi_dmac_stop(call->channel, 1000);
		break;
	case CALL_FREE:
		call->result = adi_axi_dmac_buffer_free(call->buffer);
		break;
	case CALL_RELEASE:
		call->result = adi_axi_dmac_release(call->channel);
		break;
	}
	CHECK(pthread_mutex_lock(&gate_lock) == 0);
	call->returned = true;
	CHECK(pthread_cond_broadcast(&gate_cv) == 0);
	CHECK(pthread_mutex_unlock(&gate_lock) == 0);
	return NULL;
}

static void
check_not_returned(const struct thread_call *call)
{
	CHECK(pthread_mutex_lock(&gate_lock) == 0);
	CHECK(!call->returned);
	CHECK(pthread_mutex_unlock(&gate_lock) == 0);
}

static void
check_concurrent_transfers(void)
{
	struct adi_axi_dmac_channel *c;
	struct adi_axi_dmac_buffer *a, *b;
	struct thread_call first = { .operation = CALL_TRANSFER };
	struct thread_call second = { .operation = CALL_TRANSFER };
	pthread_t first_thread, second_thread;
	unsigned char initial[64], next[64], received[64];

	reset_fixture(0x0323);
	c = acquire();
	CHECK(adi_axi_dmac_buffer_alloc(c, 64, &a) == 0);
	CHECK(adi_axi_dmac_buffer_alloc(c, 64, &b) == 0);
	memset(initial, 0x31, sizeof(initial));
	memset(next, 0x72, sizeof(next));
	CHECK(adi_axi_dmac_buffer_write(a, initial, sizeof(initial)) == 0);
	irq_mode = IRQ_THREADED;
	registers[DMAC_ID / 4] = 2;
	observed_lock = &fixture.sc_lock;
	first.buffer = a;
	second.buffer = b;
	CHECK(pthread_create(&first_thread, NULL, run_call, &first) == 0);
	wait_transfer(1);
	CHECK(adi_axi_dmac_buffer_write(a, next, sizeof(next)) == EBUSY);
	CHECK(adi_axi_dmac_buffer_read(a, received, sizeof(received)) == EBUSY);
	CHECK(adi_axi_dmac_buffer_write(b, next, sizeof(next)) == 0);
	CHECK(adi_axi_dmac_buffer_read(b, received, sizeof(received)) == 0);
	CHECK(memcmp(next, received, sizeof(next)) == 0);
	check_not_returned(&first);
	CHECK(pthread_create(&second_thread, NULL, run_call, &second) == 0);
	wait_flag(&lock_blocked);
	check_not_returned(&second);
	deliver_irq(DMAC_IRQ_EOT, __BIT(fixture.sc_id));
	CHECK(pthread_join(first_thread, NULL) == 0 && first.result == 0);
	wait_transfer(2);
	CHECK(adi_axi_dmac_buffer_write(b, initial, sizeof(initial)) == EBUSY);
	CHECK(adi_axi_dmac_buffer_write(a, next, sizeof(next)) == 0);
	deliver_irq(DMAC_IRQ_EOT, __BIT(fixture.sc_id));
	CHECK(pthread_join(second_thread, NULL) == 0 && second.result == 0);
	CHECK(submits == 2 && sync_calls == 4 && drain_calls == 3);
	CHECK(adi_axi_dmac_buffer_free(a) == 0);
	CHECK(adi_axi_dmac_buffer_free(b) == 0);
	CHECK(adi_axi_dmac_release(c) == 0);
	puts("PASS pthread inactive preparation, active EBUSY and single transfer");
}

static void
check_concurrent_stop(bool failed)
{
	struct adi_axi_dmac_channel *c;
	struct adi_axi_dmac_buffer *a, *b;
	struct thread_call transfer = { .operation = CALL_TRANSFER };
	struct thread_call stop = { .operation = CALL_STOP };
	pthread_t transfer_thread, stop_thread;
	unsigned char data[64] = { 0x57 };

	reset_fixture(0x0323);
	c = acquire();
	CHECK(adi_axi_dmac_buffer_alloc(c, 64, &a) == 0);
	CHECK(adi_axi_dmac_buffer_alloc(c, 64, &b) == 0);
	CHECK(adi_axi_dmac_buffer_write(a, data, sizeof(data)) == 0);
	irq_mode = IRQ_THREADED;
	registers[DMAC_ID / 4] = 2;
	observed_lock = &fixture.sc_lock;
	drain_error = failed ? EIO : 0;
	transfer.buffer = a;
	stop.channel = c;
	CHECK(pthread_create(&transfer_thread, NULL, run_call, &transfer) == 0);
	wait_transfer(1);
	CHECK(adi_axi_dmac_buffer_write(b, data, sizeof(data)) == 0);
	CHECK(pthread_create(&stop_thread, NULL, run_call, &stop) == 0);
	wait_flag(&lock_blocked);
	check_not_returned(&stop);
	deliver_irq(DMAC_IRQ_EOT, __BIT(fixture.sc_id));
	CHECK(pthread_join(transfer_thread, NULL) == 0);
	CHECK(pthread_join(stop_thread, NULL) == 0);
	CHECK(transfer.result == (failed ? EIO : 0));
	CHECK(stop.result == (failed ? EIO : 0));
	if (failed) {
		CHECK(fixture.sc_quarantined && a->b_inflight == sizeof(data));
		CHECK(sync_calls == 1);
		CHECK(adi_axi_dmac_buffer_write(a, data, sizeof(data)) == EBUSY);
		CHECK(adi_axi_dmac_buffer_write(b, data, sizeof(data)) == EBUSY);
		CHECK(adi_axi_dmac_buffer_free(a) == EBUSY);
		CHECK(adi_axi_dmac_buffer_free(b) == EBUSY);
		/* Model power removal only after all worker calls have returned. */
		fixture.sc_quarantined = false;
		drain_error = 0;
		CHECK(adi_axi_dmac_stop(c, 5) == 0);
	}
	CHECK(adi_axi_dmac_release(c) == EBUSY);
	CHECK(adi_axi_dmac_buffer_free(a) == 0);
	CHECK(adi_axi_dmac_buffer_free(b) == 0);
	CHECK(adi_axi_dmac_release(c) == 0);
	puts(failed ? "PASS pthread failed drain quarantines both buffers" :
	    "PASS pthread stop waits for transfer and preserves buffer ownership");
}

static void
check_copy_serialization(enum call_operation operation)
{
	struct adi_axi_dmac_channel *c;
	struct adi_axi_dmac_buffer *b;
	struct thread_call writer = { .operation = CALL_WRITE };
	struct thread_call other = { .operation = operation };
	pthread_t writer_thread, other_thread;
	unsigned char data[64] = { 0xb4 };

	reset_fixture(0x0323);
	c = acquire();
	CHECK(adi_axi_dmac_buffer_alloc(c, 64, &b) == 0);
	irq_mode = IRQ_THREADED;
	registers[DMAC_ID / 4] = 2;
	observed_lock = &fixture.sc_buffer_lock;
	copy_target = b->b_data;
	writer.buffer = other.buffer = b;
	other.channel = c;
	writer.data = data;
	CHECK(pthread_create(&writer_thread, NULL, run_call, &writer) == 0);
	wait_flag(&copy_waiting);
	CHECK(pthread_create(&other_thread, NULL, run_call, &other) == 0);
	if (operation == CALL_RELEASE) {
		CHECK(pthread_join(other_thread, NULL) == 0 && other.result == EBUSY);
	} else {
		wait_flag(&lock_blocked);
		check_not_returned(&other);
	}
	CHECK(pthread_mutex_lock(&gate_lock) == 0);
	copy_release = true;
	CHECK(pthread_cond_broadcast(&gate_cv) == 0);
	CHECK(pthread_mutex_unlock(&gate_lock) == 0);
	CHECK(pthread_join(writer_thread, NULL) == 0 && writer.result == 0);
	if (operation == CALL_TRANSFER) {
		wait_transfer(1);
		CHECK(memcmp(b->b_data, data, sizeof(data)) == 0);
		deliver_irq(DMAC_IRQ_EOT, __BIT(fixture.sc_id));
	}
	if (operation != CALL_RELEASE)
		CHECK(pthread_join(other_thread, NULL) == 0 && other.result == 0);
	if (operation != CALL_FREE)
		CHECK(adi_axi_dmac_buffer_free(b) == 0);
	CHECK(adi_axi_dmac_release(c) == 0);
	puts("PASS pthread copy serializes with transfer/free and pins channel");
}

static void
check_postdrain_ownership(void)
{
	struct adi_axi_dmac_channel *c;
	struct adi_axi_dmac_buffer *b;
	struct thread_call transfer = { .operation = CALL_TRANSFER };
	pthread_t thread;
	unsigned char data[64] = { 0x8d };

	reset_fixture(0x0323);
	c = acquire();
	CHECK(adi_axi_dmac_buffer_alloc(c, 64, &b) == 0);
	CHECK(adi_axi_dmac_buffer_write(b, data, sizeof(data)) == 0);
	irq_mode = IRQ_THREADED;
	registers[DMAC_ID / 4] = 2;
	postdrain_buffer = b;
	postdrain_armed = true;
	transfer.buffer = b;
	CHECK(pthread_create(&thread, NULL, run_call, &transfer) == 0);
	wait_transfer(1);
	deliver_irq(DMAC_IRQ_EOT, __BIT(fixture.sc_id));
	wait_flag(&postdrain_waiting);
	CHECK(adi_axi_dmac_buffer_write(b, data, sizeof(data)) == EBUSY);
	CHECK(adi_axi_dmac_buffer_read(b, data, sizeof(data)) == EBUSY);
	CHECK(pthread_mutex_lock(&gate_lock) == 0);
	postdrain_release = true;
	CHECK(pthread_cond_broadcast(&gate_cv) == 0);
	CHECK(pthread_mutex_unlock(&gate_lock) == 0);
	CHECK(pthread_join(thread, NULL) == 0 && transfer.result == 0);
	postdrain_buffer = NULL;
	CHECK(adi_axi_dmac_buffer_free(b) == 0);
	CHECK(adi_axi_dmac_release(c) == 0);
	puts("PASS pthread active ownership persists after drain until publication");
}

static void
check_prepare_contract(void)
{
	struct adi_axi_dmac_channel *c;
	struct adi_axi_dmac_buffer *b;
	unsigned char data[128] = { 0x93 }, received[128];
	unsigned before;

	reset_fixture(0x0323);
	c = acquire();
	CHECK(adi_axi_dmac_buffer_alloc(c, sizeof(data), &b) == 0);
	CHECK(adi_axi_dmac_buffer_prepare(b, 64) == EINVAL);
	CHECK(adi_axi_dmac_buffer_write(b, data, sizeof(data)) == 0);
	CHECK(adi_axi_dmac_buffer_prepare(b, 0) == EINVAL);
	CHECK(adi_axi_dmac_buffer_prepare(b, 65) == EINVAL);
	CHECK(adi_axi_dmac_buffer_prepare(b, 136) == EINVAL);
	CHECK(sync_calls == 0);
	CHECK(adi_axi_dmac_buffer_prepare(b, 64) == 0);
	CHECK(sync_calls == 1 && sync_flags[0] == BUS_DMASYNC_PREWRITE);
	CHECK(sync_sizes[0] == 64 && b->b_prepared == 64 && submits == 0);
	CHECK(adi_axi_dmac_buffer_prepare(b, 64) == 0);
	CHECK(adi_axi_dmac_buffer_prepare(b, 0) == EINVAL);
	CHECK(adi_axi_dmac_buffer_prepare(b, 65) == EINVAL);
	CHECK(adi_axi_dmac_buffer_read(b, received, 129) == EINVAL);
	CHECK(adi_axi_dmac_buffer_write(b, data, 129) == EINVAL);
	CHECK(adi_axi_dmac_transfer(b, 65, 5) == EINVAL);
	CHECK(adi_axi_dmac_transfer(b, 64, 0) == EINVAL);
	CHECK(sync_calls == 1 && b->b_prepared == 64);
	CHECK(adi_axi_dmac_buffer_read(b, received, 64) == 0);
	CHECK(memcmp(data, received, 64) == 0);
	CHECK(sync_calls == 2 && sync_flags[1] == BUS_DMASYNC_POSTWRITE);
	CHECK(sync_sizes[1] == 64 && b->b_prepared == 0);
	CHECK(adi_axi_dmac_buffer_prepare(b, 64) == 0);
	CHECK(adi_axi_dmac_buffer_write(b, data, sizeof(data)) == 0);
	CHECK(sync_calls == 4 && sync_flags[3] == BUS_DMASYNC_POSTWRITE);
	CHECK(b->b_prepared == 0 && b->b_valid == sizeof(data));
	CHECK(adi_axi_dmac_buffer_prepare(b, 64) == 0);
	CHECK(adi_axi_dmac_buffer_prepare(b, 128) == 0);
	CHECK(sync_calls == 7 && sync_flags[5] == BUS_DMASYNC_POSTWRITE);
	CHECK(sync_sizes[5] == 64 && sync_flags[6] == BUS_DMASYNC_PREWRITE);
	CHECK(sync_sizes[6] == 128);
	CHECK(adi_axi_dmac_transfer(b, 64, 5) == 0);
	CHECK(sync_calls == 10 && sync_flags[7] == BUS_DMASYNC_POSTWRITE);
	CHECK(sync_sizes[7] == 128 && sync_flags[8] == BUS_DMASYNC_PREWRITE);
	CHECK(sync_sizes[8] == 64 && sync_flags[9] == BUS_DMASYNC_POSTWRITE);
	CHECK(adi_axi_dmac_buffer_prepare(b, 64) == 0);
	before = sync_calls;
	CHECK(adi_axi_dmac_transfer(b, 64, 5) == 0);
	CHECK(sync_calls == before + 1);
	CHECK(sync_flags[before] == BUS_DMASYNC_POSTWRITE);
	CHECK(b->b_prepared == 0 && b->b_inflight == 0);
	CHECK(adi_axi_dmac_buffer_prepare(b, 64) == 0);
	before = sync_calls;
	CHECK(adi_axi_dmac_stop(c, 5) == 0);
	CHECK(sync_calls == before + 1 && b->b_prepared == 0);
	CHECK(sync_flags[before] == BUS_DMASYNC_POSTWRITE);
	CHECK(adi_axi_dmac_buffer_prepare(b, 64) == 0);
	before = sync_calls;
	CHECK(adi_axi_dmac_buffer_free(b) == 0);
	CHECK(sync_calls == before + 1 && sync_flags[before] == BUS_DMASYNC_POSTWRITE);
	CHECK(adi_axi_dmac_release(c) == 0);
	puts("PASS prepared TX pairing, idempotence, invalid calls and CPU cancellation");
}

static void
check_prepare_rx(void)
{
	struct adi_axi_dmac_channel *c;
	struct adi_axi_dmac_buffer *b;

	reset_fixture(0x2303);
	c = acquire();
	CHECK(adi_axi_dmac_buffer_alloc(c, 64, &b) == 0);
	CHECK(adi_axi_dmac_buffer_prepare(b, 64) == EOPNOTSUPP);
	CHECK(sync_calls == 0 && b->b_prepared == 0);
	CHECK(adi_axi_dmac_transfer(b, 64, 5) == 0);
	CHECK(sync_calls == 2 && sync_flags[0] == BUS_DMASYNC_PREREAD &&
	    sync_flags[1] == BUS_DMASYNC_POSTREAD);
	CHECK(adi_axi_dmac_buffer_free(b) == 0);
	CHECK(adi_axi_dmac_release(c) == 0);
	puts("PASS TX preparation rejects RX without changing its sync path");
}

enum prepare_failure {
	PREPARE_TIMEOUT,
	PREPARE_DRAIN_FAILURE,
	PREPARE_INVALID_ID,
	PREPARE_UNKNOWN_OWNER,
	PREPARE_STOP_FAILURE
};

static void
check_prepare_failure(enum prepare_failure failure)
{
	struct adi_axi_dmac_channel *c;
	struct adi_axi_dmac_buffer *a, *b;
	unsigned char data[64] = { 0x3e };
	int error;

	reset_fixture(0x0323);
	c = acquire();
	CHECK(adi_axi_dmac_buffer_alloc(c, 64, &a) == 0);
	CHECK(adi_axi_dmac_buffer_alloc(c, 64, &b) == 0);
	CHECK(adi_axi_dmac_buffer_write(a, data, 64) == 0);
	CHECK(adi_axi_dmac_buffer_write(b, data, 64) == 0);
	CHECK(adi_axi_dmac_buffer_prepare(a, 64) == 0);
	CHECK(adi_axi_dmac_buffer_prepare(b, 64) == 0);
	CHECK(sync_calls == 2);
	if (failure == PREPARE_TIMEOUT)
		irq_mode = IRQ_NONE;
	else if (failure == PREPARE_DRAIN_FAILURE || failure == PREPARE_STOP_FAILURE)
		drain_error = EIO;
	else if (failure == PREPARE_INVALID_ID)
		registers[DMAC_ID / 4] = 32;
	else
		registers[DMAC_CONTROL / 4] = DMAC_ENABLE;
	if (failure == PREPARE_STOP_FAILURE)
		error = adi_axi_dmac_stop(c, 5);
	else
		error = adi_axi_dmac_transfer(a, 64, 5);
	CHECK(error == (failure == PREPARE_TIMEOUT ? ETIMEDOUT : EIO));
	CHECK(b->b_prepared == 64);
	if (failure == PREPARE_TIMEOUT || failure == PREPARE_INVALID_ID) {
		CHECK(sync_calls == 3 && sync_flags[2] == BUS_DMASYNC_POSTWRITE);
		CHECK(a->b_prepared == 0 && a->b_inflight == 0 && a->b_valid == 0);
		CHECK(!fixture.sc_quarantined);
	} else {
		CHECK(sync_calls == 2 && fixture.sc_quarantined);
		CHECK(adi_axi_dmac_buffer_prepare(a, 64) == EBUSY);
		CHECK(adi_axi_dmac_buffer_prepare(b, 64) == EBUSY);
		CHECK(adi_axi_dmac_buffer_read(b, data, 64) == EBUSY);
		CHECK(adi_axi_dmac_buffer_write(b, data, 64) == EBUSY);
		CHECK(adi_axi_dmac_buffer_free(a) == EBUSY);
		CHECK(adi_axi_dmac_buffer_free(b) == EBUSY);
		CHECK(sync_calls == 2);
		/* Model power removal before cleanup of retained DMA ownership. */
		fixture.sc_quarantined = false;
		drain_error = 0;
		registers[DMAC_CONTROL / 4] = 0;
	}
	CHECK(adi_axi_dmac_stop(c, 5) == 0);
	CHECK(sync_calls == 4 && a->b_prepared == 0 && b->b_prepared == 0);
	CHECK(adi_axi_dmac_buffer_free(a) == 0);
	CHECK(adi_axi_dmac_buffer_free(b) == 0);
	CHECK(adi_axi_dmac_release(c) == 0);
	puts("PASS prepared error/timeout/stop ownership and failed-drain retention");
}

static void
check_concurrent_prepared(void)
{
	struct adi_axi_dmac_channel *c;
	struct adi_axi_dmac_buffer *a, *b;
	struct thread_call first = { .operation = CALL_TRANSFER };
	struct thread_call second = { .operation = CALL_TRANSFER };
	pthread_t first_thread, second_thread;
	const unsigned char data[64] = { 0x47 };
	unsigned char received[64];

	reset_fixture(0x0323);
	c = acquire();
	CHECK(adi_axi_dmac_buffer_alloc(c, 64, &a) == 0);
	CHECK(adi_axi_dmac_buffer_alloc(c, 64, &b) == 0);
	CHECK(adi_axi_dmac_buffer_write(a, data, 64) == 0);
	CHECK(adi_axi_dmac_buffer_prepare(a, 64) == 0);
	irq_mode = IRQ_THREADED;
	registers[DMAC_ID / 4] = 2;
	first.buffer = a;
	second.buffer = b;
	CHECK(pthread_create(&first_thread, NULL, run_call, &first) == 0);
	wait_transfer(1);
	CHECK(adi_axi_dmac_buffer_prepare(a, 64) == EBUSY);
	CHECK(adi_axi_dmac_buffer_write(b, data, 64) == 0);
	CHECK(adi_axi_dmac_buffer_prepare(b, 64) == 0);
	CHECK(adi_axi_dmac_buffer_read(b, received, 64) == 0);
	CHECK(memcmp(data, received, 64) == 0);
	CHECK(adi_axi_dmac_buffer_prepare(b, 64) == 0);
	CHECK(adi_axi_dmac_buffer_write(b, data, 64) == 0);
	CHECK(adi_axi_dmac_buffer_prepare(b, 64) == 0);
	CHECK(sync_calls == 6);
	check_not_returned(&first);
	deliver_irq(DMAC_IRQ_EOT, __BIT(fixture.sc_id));
	CHECK(pthread_join(first_thread, NULL) == 0 && first.result == 0);
	CHECK(sync_calls == 7 && b->b_prepared == 64 && a->b_inflight == 0);
	CHECK(pthread_create(&second_thread, NULL, run_call, &second) == 0);
	wait_transfer(2);
	CHECK(sync_calls == 7); /* No PREWRITE at the second submission. */
	deliver_irq(DMAC_IRQ_EOT, __BIT(fixture.sc_id));
	CHECK(pthread_join(second_thread, NULL) == 0 && second.result == 0);
	CHECK(sync_calls == 8 && sync_flags[7] == BUS_DMASYNC_POSTWRITE);
	CHECK(adi_axi_dmac_buffer_free(a) == 0);
	CHECK(adi_axi_dmac_buffer_free(b) == 0);
	CHECK(adi_axi_dmac_release(c) == 0);
	puts("PASS pthread prepare/cancel during DMA and prepared successor survives drain");
}

static void
check_shutdown_access(void)
{
	struct adi_axi_dmac_channel *c;
	struct adi_axi_dmac_buffer *b;
	unsigned char data[64] = { 0 };

	reset_fixture(0x0323);
	c = acquire();
	CHECK(adi_axi_dmac_buffer_alloc(c, 64, &b) == 0);
	CHECK(adi_axi_dmac_buffer_write(b, data, sizeof(data)) == 0);
	CHECK(adi_axi_dmac_buffer_prepare(b, sizeof(data)) == 0);
	CHECK(adiaxidmac_shutdown(&fixture, 0));
	CHECK(b->b_prepared == 0 && sync_calls == 2);
	CHECK(sync_flags[1] == BUS_DMASYNC_POSTWRITE);
	CHECK(adi_axi_dmac_buffer_prepare(b, sizeof(data)) == ESHUTDOWN);
	CHECK(adi_axi_dmac_buffer_write(b, data, sizeof(data)) == ESHUTDOWN);
	CHECK(adi_axi_dmac_buffer_read(b, data, sizeof(data)) == ESHUTDOWN);
	CHECK(adi_axi_dmac_buffer_free(b) == 0);
	CHECK(adi_axi_dmac_release(c) == 0);
	puts("PASS shutdown refuses subsequent CPU buffer access");
}

int
main(void)
{
	(void)alarm(30);
	(void)adiaxidmac_shutdown_final;
	check_interfaces();
	check_transfer(0x0323, IRQ_MATCH, 0, 0);
	check_transfer(0x0313, IRQ_MATCH, 0, 0);
	check_transfer(0x2303, IRQ_MATCH, 0, 0);
	check_transfer(0x0323, IRQ_BEFORE_WAIT, 0, 0);
	check_transfer(0x0323, IRQ_WRONG_THEN_MATCH, 0, 0);
	check_transfer(0x0323, IRQ_NONE, ETIMEDOUT, 0);
	check_transfer(0x0323, IRQ_SOT, ETIMEDOUT, 0);
	check_transfer(0x0323, IRQ_DONE_ONLY, ETIMEDOUT, 0);
	check_transfer(0x0323, IRQ_WRONG_ONLY, ETIMEDOUT, 0);
	check_transfer(0x0323, IRQ_EMPTY, ETIMEDOUT, 0);
	check_transfer(0x0323, IRQ_SPURIOUS, ETIMEDOUT, 0);
	check_transfer(0x0323, IRQ_SPURIOUS_THEN_MATCH, 0, 0);
	check_transfer(0x0323, IRQ_TIMEOUT_MATCH, 0, 0);
	check_transfer(0x0323, IRQ_WRONG_THEN_MATCH, 0, UINT_MAX - 1);
	check_transfer(0x0323, IRQ_SPURIOUS, ETIMEDOUT, UINT_MAX - 1);
	puts("PASS cached TX, coherent RX, sync ordering and matching EOT");
	puts("PASS completion before wait, IRQ wake, spurious wakes and bounded timeout");
	puts("PASS unrelated IRQs, timeout-completion race and tick wrap");
	check_bounds_and_ownership();
	check_quarantine(IRQ_MATCH);
	check_quarantine(IRQ_NONE);
	check_hardware_idle(3, 0, 0, 0);
	check_hardware_idle(3, 0, UINT_MAX - 1, 0);
	check_hardware_idle(UINT_MAX, ETIMEDOUT, 0, 0);
	check_hardware_idle(3, ETIMEDOUT, 0, 4);
	check_guard_idle_witness();
	check_guard_prepare_reuse();
	check_large_and_allocation();
	check_concurrent_transfers();
	check_concurrent_stop(false);
	check_concurrent_stop(true);
	check_copy_serialization(CALL_TRANSFER);
	check_copy_serialization(CALL_FREE);
	check_copy_serialization(CALL_RELEASE);
	check_postdrain_ownership();
	check_prepare_contract();
	check_prepare_rx();
	check_prepare_failure(PREPARE_TIMEOUT);
	check_prepare_failure(PREPARE_DRAIN_FAILURE);
	check_prepare_failure(PREPARE_INVALID_ID);
	check_prepare_failure(PREPARE_UNKNOWN_OWNER);
	check_prepare_failure(PREPARE_STOP_FAILURE);
	check_concurrent_prepared();
	check_copy_serialization(CALL_PREPARE);
	check_shutdown_access();
	reset_fixture(0x0323);
	CHECK(adiaxidmac_shutdown(&fixture, 0));
	cv_destroy(&fixture.sc_done_cv);
	mutex_destroy(&fixture.sc_irq_lock);
	mutex_destroy(&fixture.sc_buffer_lock);
	mutex_destroy(&fixture.sc_lock);
	puts("PASS finite ADI AXI DMAC host contract");
	(void)alarm(0);
	return 0;
}
