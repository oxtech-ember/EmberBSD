/* Origin: EmberBSD VideoCore mailbox transaction contract, 2026-10-08. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if KERNEL_ASSERTIONS
#define KASSERT(e) assert(e)
#else
#define KASSERT(e) ((void)0)
#endif
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define IPL_VM 1
#define CFARGS_NONE 0
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
#define BUS_DMA_WAITOK 0
#define BUS_DMASYNC_PREWRITE 1
#define BUS_DMASYNC_PREREAD 2
#define BUS_DMASYNC_POSTWRITE 4
#define BUS_DMASYNC_POSTREAD 8

typedef unsigned int u_int;
typedef unsigned int bus_space_tag_t;
typedef unsigned int bus_space_handle_t;
typedef unsigned int bus_dma_tag_t;
typedef void *device_t;
typedef struct {
	pthread_mutex_t lock;
	bool owned;
} kmutex_t;
typedef struct { unsigned int id; } kcondvar_t;
typedef struct { uint64_t ds_addr; size_t ds_len; } bus_dma_segment_t;
struct dma_slot;
struct dma_map {
	bus_dma_segment_t dm_segs[1];
	int dm_nsegs;
	struct dma_slot *slot;
	bool created, loaded;
};
typedef struct dma_map *bus_dmamap_t;
#ifndef bintimecmp
#define MBOX_TEST_BINTIME 1
struct bintime { int64_t sec; uint64_t frac; };
#define bintimecmp(a, b, op) ((a)->sec == (b)->sec ? \
    (a)->frac op (b)->frac : (a)->sec op (b)->sec)
#endif

#include "bcm2835_mbox-types.h"
#include "bcm2835_mboxreg.h"

struct dma_slot {
	bus_dma_segment_t segment;
	struct dma_map map;
	unsigned char data[256];
	bool allocated, mapped, submitted, completed, synced;
};
static struct bcm2835mbox_softc controller;
static struct dma_slot slots[16];
static kmutex_t *locks[32];
static unsigned int nlocks, ncv, cases;
static _Thread_local kmutex_t *held[32];
static _Thread_local unsigned int nheld;
static _Thread_local bool prove_blocked;
static pthread_mutex_t hw = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate = PTHREAD_COND_INITIALIZER;
static uint64_t now_us, tx_release, response_at, cv_step, next_address;
static uint32_t fifo[64];
static unsigned int head, tail, reads, writes, allocs, alloc_attempts;
static unsigned int frees, unmaps, unloads, destroys, pre_syncs, post_syncs;
static unsigned int delays, wakes, diagnostics, tx_status_reads;
static unsigned int fail_stage, response_mode, cv_error, dma_nsegs;
static uint64_t previous_wait;
static bool tx_full, rx_full, flood, irq_mode;
static bool threaded, owner_waiting, owner_release, other_done;
static bool second_started, raw_waiting, raw_reply, raw_write, freeze_clock;
static unsigned int messages;
static uint32_t sent[32];
static int cold;

static void prove_lock_blocked(kmutex_t *);

static void
mutex_init(kmutex_t *m, int type, int ipl)
{

	(void)type;
	(void)ipl;
	assert(nlocks < sizeof(locks) / sizeof(locks[0]));
	assert(pthread_mutex_init(&m->lock, NULL) == 0);
	m->owned = false;
	locks[nlocks++] = m;
}

static void
mutex_enter(kmutex_t *m)
{

	if (prove_blocked)
		prove_lock_blocked(m);
	assert(pthread_mutex_lock(&m->lock) == 0);
	m->owned = true;
	assert(nheld < sizeof(held) / sizeof(held[0]));
	held[nheld++] = m;
}

static bool
mutex_owned(kmutex_t *m)
{

	for (unsigned int i = 0; i < nheld; i++)
		if (held[i] == m)
			return true;
	return false;
}

static void
mutex_exit(kmutex_t *m)
{

	assert(mutex_owned(m));
	for (unsigned int i = 0; i < nheld; i++) {
		if (held[i] == m) {
			held[i] = held[--nheld];
			break;
		}
	}
	m->owned = false;
	assert(pthread_mutex_unlock(&m->lock) == 0);
}

static void
cv_init(kcondvar_t *cv, const char *name)
{

	(void)name;
	cv->id = ncv++;
}

static void
cv_broadcast(kcondvar_t *cv)
{

	assert(cv->id < 16);
}

static void
binuptime(struct bintime *bt)
{
	uint64_t now;

	assert(pthread_mutex_lock(&hw) == 0);
	now = freeze_clock ? 0 : now_us;
	assert(pthread_mutex_unlock(&hw) == 0);
	bt->sec = now / 1000000;
	bt->frac = ((__uint128_t)(now % 1000000) << 64) / 1000000;
}

#ifdef MBOX_TEST_BINTIME
static void
bintime_sub(struct bintime *a, const struct bintime *b)
{

	if (a->frac < b->frac)
		a->sec--;
	a->frac -= b->frac;
	a->sec -= b->sec;
}
#endif

static void
push(uint32_t value)
{

	assert(tail - head < sizeof(fifo) / sizeof(fifo[0]));
	fifo[tail++ % (sizeof(fifo) / sizeof(fifo[0]))] = value;
}

static void
complete_pending(void)
{

	for (unsigned int i = 0; i < allocs; i++) {
		struct dma_slot *s = &slots[i];
		if (!s->submitted || s->completed)
			continue;
		assert(s->allocated && s->mapped && s->map.loaded);
		memset(s->data, 0xa5, s->segment.ds_len);
		s->completed = true;
		push((uint32_t)s->segment.ds_addr | 8);
	}
}

static void
advance(uint64_t usecs)
{
	bool intr;

	assert(pthread_mutex_lock(&hw) == 0);
	now_us += usecs;
	if (response_mode == 1 && now_us >= response_at)
		complete_pending();
	intr = irq_mode && head != tail;
	assert(pthread_mutex_unlock(&hw) == 0);
	if (intr)
		bcmmbox_intr(&controller);
}

static void
delay(unsigned int usecs)
{

	assert(!mutex_owned(&controller.sc_lock));
	assert(!mutex_owned(&controller.sc_intr_lock));
	assert(pthread_mutex_lock(&hw) == 0);
	assert(++delays <= 100010);
	if (threaded && !owner_waiting && messages == 1) {
		owner_waiting = true;
		assert(pthread_cond_broadcast(&gate) == 0);
		while (!owner_release)
			assert(pthread_cond_wait(&gate, &hw) == 0);
	}
	assert(pthread_mutex_unlock(&hw) == 0);
	advance(usecs);
}

static int
cv_timedwaitbt(kcondvar_t *cv, kmutex_t *m, struct bintime *left,
    const struct bintime *epsilon)
{
	uint64_t usecs, step;
	int error;

	(void)epsilon;
	assert(cv->id < 16);
	assert(m == &controller.sc_intr_lock && mutex_owned(m));
	assert(!mutex_owned(&controller.sc_lock));
	usecs = left->sec * 1000000 +
	    (((__uint128_t)left->frac * 1000000) >> 64);
	assert(usecs > 0 && usecs <= previous_wait);
	previous_wait = usecs;
	assert(++wakes < 100010);
	step = cv_step < usecs ? cv_step : usecs;
	error = cv_error;
	mutex_exit(m);
	advance(step + (step == usecs));
	mutex_enter(m);
	return error;
}

/* The old interrupt path is deliberately finite only in the fixture. */
static void
cv_wait(kcondvar_t *cv, kmutex_t *m)
{
	struct bintime left = { .sec = 1 };
	const struct bintime epsilon = { 0 };

	assert(wakes < 20);
	(void)cv_timedwaitbt(cv, m, &left, &epsilon);
}

static void
bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t handle,
    unsigned int off, unsigned int len, int flags)
{

	assert(tag == 1 && handle == 2 && off == 0 && len == BCM2835_MBOX_SIZE);
	assert(flags == BUS_SPACE_BARRIER_READ || flags == BUS_SPACE_BARRIER_WRITE);
}

static uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
    unsigned int reg)
{
	uint32_t value;

	assert(tag == 1 && handle == 2);
	assert(pthread_mutex_lock(&hw) == 0);
	assert(++reads < 4000000);
	switch (reg) {
	case BCM2835_MBOX0_STATUS:
		value = (head == tail && !flood ? BCM2835_MBOX_STATUS_EMPTY : 0) |
		    (rx_full ? BCM2835_MBOX_STATUS_FULL : 0);
		break;
	case BCM2835_MBOX1_STATUS:
		tx_status_reads++;
		value = tx_full || now_us < tx_release ? BCM2835_MBOX_STATUS_FULL : 0;
		break;
	case BCM2835_MBOX0_READ:
		assert(head != tail || flood);
		value = head != tail ? fifo[head++ % 64] : 0x12344;
		break;
	default:
		abort();
	}
	assert(pthread_mutex_unlock(&hw) == 0);
	return value;
}

static void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle,
    unsigned int reg, uint32_t value)
{
	bool intr = false;

	assert(tag == 1 && handle == 2);
	if (reg == BCM2835_MBOX_CFG) {
		assert(value == BCM2835_MBOX_CFG_DATAIRQEN);
		return;
	}
	assert(reg == BCM2835_MBOX1_WRITE);
	assert(pthread_mutex_lock(&hw) == 0);
	assert(!tx_full && now_us >= tx_release);
	assert(messages < sizeof(sent) / sizeof(sent[0]));
	sent[messages++] = value;
	writes++;
	if (BCM2835_MBOX_CHAN(value) == 8) {
		bool found = false;
		for (unsigned int i = 0; i < allocs; i++) {
			struct dma_slot *s = &slots[i];
			if (s->segment.ds_addr != BCM2835_MBOX_DATA(value))
				continue;
			assert(s->allocated && s->mapped && s->map.loaded && s->synced);
			s->submitted = true;
			found = true;
		}
		if (!found) {
			assert(raw_write && value == 0x77778 && frees == 1);
			assert(pthread_mutex_unlock(&hw) == 0);
			return;
		}
		if (response_mode == 1 && now_us >= response_at)
			complete_pending();
		if (response_mode == 2)
			push(BCM2835_MBOX_DATA(value) + 0x1000 + 8);
		if (response_mode == 3) {
			complete_pending();
			push(value);
		}
		if (response_mode == 4)
			push(BCM2835_MBOX_DATA(value) | 3);
		intr = irq_mode && head != tail;
	}
	assert(pthread_mutex_unlock(&hw) == 0);
	if (intr)
		bcmmbox_intr(&controller);
}

static int
bus_dmamem_alloc(bus_dma_tag_t tag, size_t size, size_t alignment,
    size_t boundary, bus_dma_segment_t *seg, int maxsegs, int *nsegs,
    int flags)
{
	struct dma_slot *s;

	assert(tag == 3 && size <= 256 && alignment == 16 && boundary == 0);
	assert(maxsegs == 1 && flags == BUS_DMA_WAITOK);
	assert(pthread_mutex_lock(&hw) == 0);
	alloc_attempts++;
	if (fail_stage == 1) {
		assert(pthread_mutex_unlock(&hw) == 0);
		return ENOMEM;
	}
	assert(allocs < 16);
	s = &slots[allocs++];
	s->segment = (bus_dma_segment_t){ next_address, size };
	next_address += 0x1000;
	s->allocated = true;
	*seg = s->segment;
	*nsegs = 1;
	assert(pthread_mutex_unlock(&hw) == 0);
	return 0;
}

static struct dma_slot *
find_slot(uint64_t address)
{

	for (unsigned int i = 0; i < allocs; i++)
		if (slots[i].segment.ds_addr == address)
			return &slots[i];
	abort();
}

static int
bus_dmamem_map(bus_dma_tag_t tag, bus_dma_segment_t *segs, int nsegs,
    size_t size, void **buf, int flags)
{
	struct dma_slot *s = find_slot(segs[0].ds_addr);

	(void)tag;
	(void)size;
	(void)flags;
	assert(nsegs == 1 && s->allocated);
	if (fail_stage == 2)
		return ENOMEM;
	s->mapped = true;
	*buf = s->data;
	return 0;
}

static int
bus_dmamap_create(bus_dma_tag_t tag, size_t size, int nsegs,
    size_t maxsize, size_t boundary, int flags, bus_dmamap_t *map)
{
	struct dma_slot *s = &slots[allocs - 1];

	(void)tag;
	(void)flags;
	assert(nsegs == 1 && size == maxsize && boundary == 0);
	assert(s->mapped);
	if (fail_stage == 3)
		return ENOMEM;
	s->map.created = true;
	s->map.slot = s;
	*map = &s->map;
	return 0;
}

static int
bus_dmamap_load(bus_dma_tag_t tag, bus_dmamap_t map, void *buf,
    size_t size, void *proc, int flags)
{

	(void)tag;
	(void)size;
	(void)flags;
	assert(map->created && buf == map->slot->data && proc == NULL);
	if (fail_stage == 4)
		return ENOMEM;
	map->loaded = true;
	map->dm_nsegs = dma_nsegs;
	map->dm_segs[0] = map->slot->segment;
	return 0;
}

static void
bus_dmamap_sync(bus_dma_tag_t tag, bus_dmamap_t map, size_t off,
    size_t len, int flags)
{

	(void)tag;
	assert(map->loaded && off == 0 && len == map->slot->segment.ds_len);
	if (flags == (BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE)) {
		assert(!map->slot->synced);
		map->slot->synced = true;
		pre_syncs++;
	} else {
		assert(flags == (BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE));
		assert(map->slot->synced);
		assert(!map->slot->submitted || map->slot->completed);
		map->slot->synced = false;
		post_syncs++;
	}
}

static void
bus_dmamap_unload(bus_dma_tag_t tag, bus_dmamap_t map)
{

	(void)tag;
	assert(map->loaded && !map->slot->synced);
	assert(!map->slot->submitted || map->slot->completed);
	map->loaded = false;
	unloads++;
	if (raw_reply) {
		assert(pthread_mutex_lock(&hw) == 0);
		push(0x77778);
		assert(pthread_mutex_unlock(&hw) == 0);
	}
}

static void
bus_dmamap_destroy(bus_dma_tag_t tag, bus_dmamap_t map)
{

	(void)tag;
	assert(map->created && !map->loaded);
	map->created = false;
	destroys++;
}

static void
bus_dmamem_unmap(bus_dma_tag_t tag, void *buf, size_t size)
{

	(void)tag;
	(void)size;
	for (unsigned int i = 0; i < allocs; i++) {
		struct dma_slot *s = &slots[i];
		if (buf != s->data)
			continue;
		assert(s->mapped && !s->map.created);
		s->mapped = false;
		unmaps++;
		return;
	}
	abort();
}

static void
bus_dmamem_free(bus_dma_tag_t tag, bus_dma_segment_t *seg, int nsegs)
{
	struct dma_slot *s = find_slot(seg[0].ds_addr);

	(void)tag;
	assert(nsegs == 1 && s->allocated && !s->mapped);
	assert(!s->submitted || s->completed);
	s->allocated = false;
	frees++;
}

#define device_printf(dev, ...) ((void)(dev), diagnostics++)
#define aprint_error(...) ((void)0)
static void *
config_found(device_t dev, struct bcmmbox_attach_args *aa, void *print,
    int args)
{

	(void)dev;
	(void)print;
	(void)args;
	assert(aa->baa_dmat == 3);
	return NULL;
}

#include "mbox-subr-under-test.h"
#include "mbox-under-test.h"

static void
reset(unsigned int mode)
{

	for (unsigned int i = 0; i < nlocks; i++) {
		assert(!locks[i]->owned);
		assert(pthread_mutex_destroy(&locks[i]->lock) == 0);
	}
	memset(&controller, 0, sizeof(controller));
	memset(slots, 0, sizeof(slots));
	nlocks = ncv = 0;
	now_us = tx_release = response_at = 0;
	cv_step = 200000;
	next_address = 0x10000;
	previous_wait = UINT64_MAX;
	head = tail = reads = writes = messages = tx_status_reads = 0;
	allocs = alloc_attempts = frees = unmaps = unloads = destroys = 0;
	pre_syncs = post_syncs = delays = wakes = diagnostics = fail_stage = 0;
	response_mode = 1;
	cv_error = 0;
	dma_nsegs = 1;
	tx_full = rx_full = flood = threaded = false;
	owner_waiting = owner_release = other_done = second_started = false;
	raw_waiting = raw_reply = raw_write = freeze_clock = false;
	cold = mode == 0;
	irq_mode = mode == 2;
	controller.sc_iot = 1;
	controller.sc_ioh = 2;
	controller.sc_dmat = 3;
	controller.sc_intrh = mode != 1 ? &controller : NULL;
	bcm2835mbox_sc = NULL;
	bcmmbox_attach(&controller);
}

static int
request(unsigned char *buf, uint32_t *res)
{

	return bcmmbox_request(8, buf, 32, res);
}

static void
unchanged(const unsigned char *buf, uint32_t res)
{

	for (unsigned int i = 0; i < 32; i++)
		assert(buf[i] == 0x39);
	assert(res == 0x76543210);
}

static void
repeat_quarantine(void)
{
	unsigned char buf[32];
	uint32_t res = 0x76543210;
	unsigned int oldallocs = alloc_attempts, oldwrites = writes;

	memset(buf, 0x39, sizeof(buf));
	for (unsigned int i = 0; i < 8; i++) {
		assert(request(buf, &res) == EIO);
		assert(bcmmbox_read(8, &res) == EIO);
		bcmmbox_write(8, 0xdead0000);
		unchanged(buf, res);
		assert(alloc_attempts == oldallocs && writes == oldwrites);
	}
	bcmmbox_write(3, 0x12340);
	assert(writes == oldwrites + 1 && sent[messages - 1] == 0x12343);
	cases++;
}

static void
test_transactions(void)
{
	unsigned char buf[32];
	uint32_t res;

	for (unsigned int mode = 0; mode < 3; mode++) {
		reset(mode);
		memset(buf, 0x39, sizeof(buf));
		res = 0x76543210;
		rx_full = true;
		assert(request(buf, &res) == 0);
		assert(res == 0x10000 && buf[0] == 0xa5 && buf[31] == 0xa5);
		assert(writes == 1 && allocs == 1 && frees == 1);
		assert(unmaps == 1 && destroys == 1 && unloads == 1);
		assert(pre_syncs == 1 && post_syncs == 1 && tx_status_reads == 1);
		cases++;

		reset(mode);
		tx_full = true;
		memset(buf, 0x39, sizeof(buf));
		res = 0x76543210;
		assert(request(buf, &res) == ETIMEDOUT);
		unchanged(buf, res);
		assert(writes == 0 && frees == 1 && unloads == 1);
		assert(now_us <= 1000010 && tx_status_reads > 0);
		tx_full = false;
		assert(request(buf, &res) == 0); /* Pre-TX error permits retry. */
		assert(frees == 2);
		cases++;

		for (unsigned int failure = 0; failure < 5; failure++) {
			reset(mode);
			response_mode = failure;
			if (failure == 1) {
				tx_release = 600000;
				response_at = 1100000;
			}
			memset(buf, 0x39, sizeof(buf));
			res = 0x76543210;
			assert(request(buf, &res) ==
			    (failure == 2 || failure == 3 ? EIO : ETIMEDOUT));
			unchanged(buf, res);
			assert(writes == 1 && frees == 0 && unloads == 0);
			assert(post_syncs == 0 && slots[0].allocated && slots[0].map.loaded);
			assert(now_us <= 1000010);
			/* A genuine late DMA completion does not reopen the channel. */
			assert(pthread_mutex_lock(&hw) == 0);
			complete_pending();
			assert(pthread_mutex_unlock(&hw) == 0);
			bcmmbox_intr(&controller);
			assert(slots[0].data[0] == 0xa5);
			repeat_quarantine();
			cases++;
		}
	}
}

static void
test_failures(void)
{
	unsigned char buf[32];
	uint32_t res;

	for (unsigned int stage = 1; stage <= 4; stage++) {
		reset(1);
		fail_stage = stage;
		memset(buf, 0x39, sizeof(buf));
		res = 0x76543210;
		assert(request(buf, &res) == ENOMEM);
		unchanged(buf, res);
		assert(writes == 0 && pre_syncs == 0);
		assert(frees == (stage > 1) && unmaps == (stage > 2));
		assert(destroys == (stage > 3) && unloads == 0);
		fail_stage = 0;
		assert(request(buf, &res) == 0);
		cases++;
	}
	for (unsigned int bad = 0; bad < 4; bad++) {
		reset(1);
		next_address = bad == 0 ? UINT64_C(0x100000000) :
		    bad == 1 ? 0x10001 : bad == 2 ? 0xfffffff0 : 0x10000;
		if (bad == 3)
			dma_nsegs = 2;
		memset(buf, 0x39, sizeof(buf));
		res = 0x76543210;
		assert(request(buf, &res) == EFBIG);
		unchanged(buf, res);
		assert(frees == 1 && unloads == 1 && pre_syncs == 0 && writes == 0);
		cases++;
	}
	reset(1);
	memset(buf, 0x39, sizeof(buf));
	res = 0x76543210;
	assert(bcmmbox_request(16, buf, 32, &res) == EINVAL);
	assert(bcmmbox_request(8, NULL, 32, &res) == EINVAL);
	assert(bcmmbox_request(8, buf, 0, &res) == EINVAL);
	assert(bcmmbox_request(8, buf, 32, NULL) == EINVAL);
	assert(bcmmbox_read(16, &res) == EINVAL);
	assert(bcmmbox_read(8, NULL) == EINVAL);
	bcm2835mbox_sc = NULL;
	assert(request(buf, &res) == ENXIO);
	assert(bcmmbox_read(8, &res) == ENXIO);
	assert(alloc_attempts == 0 && writes == 0);
	unchanged(buf, res);
	cases++;
}

static void
test_receive(void)
{
	unsigned char buf[32];
	uint32_t res;

	for (unsigned int mode = 0; mode < 3; mode++) {
		reset(mode);
		push(0x22228);
		memset(buf, 0x39, sizeof(buf));
		res = 0x76543210;
		assert(request(buf, &res) == EIO);
		unchanged(buf, res);
		assert(alloc_attempts == 0 && writes == 0);
		repeat_quarantine();
		cases++;

		reset(mode);
		push(0xabc30);
		push(0xdef3f);
		bcmmbox_intr(&controller);
		assert(bcmmbox_read(0, &res) == 0 && res == 0xabc30);
		assert(bcmmbox_read(15, &res) == 0 && res == 0xdef30);
		res = 0x76543210;
		assert(bcmmbox_read(8, &res) == ETIMEDOUT);
		assert(res == 0x76543210);
		repeat_quarantine();
		cases++;
	}
	reset(1);
	flood = true;
	assert(bcmmbox_intr(&controller) == 1);
	assert(reads == 32); /* Sixteen STATUS/READ pairs, then yield. */
	flood = false;
	res = 0x76543210;
	assert(bcmmbox_read(4, &res) == EIO && res == 0x76543210);
	cases++;

	reset(2);
	response_mode = 0;
	cv_error = EINTR;
	memset(buf, 0x39, sizeof(buf));
	res = 0x76543210;
	assert(request(buf, &res) == EINTR);
	unchanged(buf, res);
	assert(frees == 0 && wakes == 1);
	repeat_quarantine();
	cases++;
}

struct thread_request { unsigned char buf[32]; uint32_t res; int error; };
static void *
request_thread(void *arg)
{
	struct thread_request *r = arg;

	memset(r->buf, 0x39, sizeof(r->buf));
	r->res = 0x76543210;
	assert(pthread_mutex_lock(&hw) == 0);
	if (owner_waiting) {
		second_started = true;
		assert(pthread_cond_broadcast(&gate) == 0);
	}
	assert(pthread_mutex_unlock(&hw) == 0);
	r->error = request(r->buf, &r->res);
	return NULL;
}

static void *
other_thread(void *unused)
{

	(void)unused;
	bcmmbox_write(3, 0xabc00);
	assert(pthread_mutex_lock(&hw) == 0);
	other_done = true;
	assert(pthread_cond_broadcast(&gate) == 0);
	assert(pthread_mutex_unlock(&hw) == 0);
	return NULL;
}

static void
wait_gate(bool *flag)
{
	struct timespec end;

	assert(clock_gettime(CLOCK_REALTIME, &end) == 0);
	end.tv_sec += 2;
	while (!*flag)
		assert(pthread_cond_timedwait(&gate, &hw, &end) == 0);
}

static void
test_concurrency(void)
{
	pthread_t first, second, other;
	struct thread_request a, b;

	reset(1);
	threaded = true;
	response_at = 10;
	assert(pthread_create(&first, NULL, request_thread, &a) == 0);
	assert(pthread_mutex_lock(&hw) == 0);
	wait_gate(&owner_waiting);
	assert(pthread_mutex_unlock(&hw) == 0);
	assert(pthread_create(&second, NULL, request_thread, &b) == 0);
	assert(pthread_create(&other, NULL, other_thread, NULL) == 0);
	assert(pthread_mutex_lock(&hw) == 0);
	wait_gate(&second_started);
	wait_gate(&other_done);
	assert(alloc_attempts == 1 && writes == 2); /* Only A and VCHIQ3. */
	owner_release = true;
	assert(pthread_cond_broadcast(&gate) == 0);
	assert(pthread_mutex_unlock(&hw) == 0);
	assert(pthread_join(first, NULL) == 0);
	assert(pthread_join(second, NULL) == 0);
	assert(pthread_join(other, NULL) == 0);
	assert(a.error == 0 && b.error == 0);
	assert(a.res == 0x10000 && b.res == 0x11000);
	assert(a.buf[0] == 0xa5 && b.buf[0] == 0xa5);
	assert(frees == 2 && writes == 3);
	cases++;
}

static void
prove_lock_blocked(kmutex_t *m)
{

	prove_blocked = false;
	assert(pthread_mutex_trylock(&m->lock) == EBUSY);
	assert(pthread_mutex_lock(&hw) == 0);
	raw_waiting = true;
	assert(pthread_cond_broadcast(&gate) == 0);
	assert(pthread_mutex_unlock(&hw) == 0);
}

static void *
raw_thread(void *arg)
{
	uint32_t *res = arg;

	prove_blocked = true;
	if (raw_write)
		bcmmbox_write(8, 0x77770);
	else
		assert(bcmmbox_read(8, res) == 0);
	return NULL;
}

static void
test_split_coexistence(void)
{
	pthread_t first, raw;
	struct thread_request a;
	uint32_t res;

	for (unsigned int write = 0; write < 2; write++) {
		reset(1);
		threaded = true;
		raw_write = write;
		raw_reply = !write;
		response_at = 10;
		res = 0;
		assert(pthread_create(&first, NULL, request_thread, &a) == 0);
		assert(pthread_mutex_lock(&hw) == 0);
		wait_gate(&owner_waiting);
		assert(pthread_mutex_unlock(&hw) == 0);
		assert(pthread_create(&raw, NULL, raw_thread, &res) == 0);
		assert(pthread_mutex_lock(&hw) == 0);
		wait_gate(&raw_waiting); /* Actual trylock proved the request owns it. */
		assert(alloc_attempts == 1 && writes == 1);
		owner_release = true;
		assert(pthread_cond_broadcast(&gate) == 0);
		assert(pthread_mutex_unlock(&hw) == 0);
		assert(pthread_join(first, NULL) == 0);
		assert(pthread_join(raw, NULL) == 0);
		assert(a.error == 0 && frees == 1);
		assert(write ? writes == 2 : res == 0x77770);
		cases++;
	}
}

static void
test_deadlines_and_legacy(void)
{
	unsigned char buf[32];
	uint32_t res;

	for (unsigned int mode = 0; mode < 3; mode++) {
		reset(mode);
		freeze_clock = true;
		response_mode = 0;
		cv_step = 0;
		memset(buf, 0x39, sizeof(buf));
		res = 0x76543210;
		assert(request(buf, &res) == ETIMEDOUT);
		unchanged(buf, res);
		assert(frees == 0 && delays + wakes == 100000);
		cases++;
	}
	reset(2);
	response_at = 300000;
	cv_error = EWOULDBLOCK;
	assert(request(buf, &res) == 0 && res == 0x10000 && wakes == 2);
	cases++;

	reset(1);
	bcm2835_mbox_write(1, 2, 3, 0x12340);
	assert(writes == 1 && sent[0] == 0x12343);
	push(0x12343);
	push(0xabcd8);
	bcm2835_mbox_read(1, 2, 8, &res);
	assert(res == 0xabcd0);
	cases++;
}

static void
test_original_failure(bool mismatch)
{
	unsigned char buf[32];
	uint32_t res = 0x76543210;

	reset(1);
	memset(buf, 0x39, sizeof(buf));
	response_mode = mismatch ? 2 : 1;
	tx_full = !mismatch;
	assert(request(buf, &res) == (mismatch ? EIO : ETIMEDOUT));
	unchanged(buf, res);
	assert(mismatch ? frees == 0 : writes == 0 && frees == 1);
	cases++;
}

int
main(int argc, char **argv)
{

	/* Keep the compatibility fixture compiled for the original source. */
	(void)cv_wait;
	(void)cv_timedwaitbt;
	(void)bintime_sub;
	(void)binuptime;
	if (argc == 1 || strcmp(argv[1], "transactions") == 0)
		test_transactions();
	if (argc == 1 || strcmp(argv[1], "failures") == 0)
		test_failures();
	if (argc == 1 || strcmp(argv[1], "receive") == 0)
		test_receive();
	if (argc == 1 || strcmp(argv[1], "concurrency") == 0) {
		test_concurrency();
		test_split_coexistence();
	}
	if (argc == 1 || strcmp(argv[1], "deadlines") == 0)
		test_deadlines_and_legacy();
	if (argc == 1 || strcmp(argv[1], "tx-full") == 0)
		test_original_failure(false);
	if (argc == 1 || strcmp(argv[1], "mismatch") == 0)
		test_original_failure(true);
	assert(cases != 0);
	printf("VideoCore mailbox: %u causal cases passed\n", cases);
	return 0;
}
