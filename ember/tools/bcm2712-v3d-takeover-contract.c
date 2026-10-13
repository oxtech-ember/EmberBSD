/* Origin: EmberBSD actual-source V3D takeover failure contract, 2026-10-09. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef __BIT
#define __BIT(n) (UINT32_C(1) << (n))
#endif
#define KASSERT(v) assert(v)
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define aprint_normal_dev(...) ((void)0)

typedef uint64_t bus_addr_t;
typedef size_t bus_size_t;
typedef unsigned int bus_space_tag_t;
typedef unsigned int bus_space_handle_t;
typedef struct device { unsigned int id; } *device_t;
typedef pthread_mutex_t kmutex_t;
struct bcm2835pmwdog_softc {
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;
};
static struct bcm2835pmwdog_softc *bcmpmwdog_v3d_sc;
static void mutex_init(kmutex_t *m, int kind, int ipl)
{
	(void)kind;
	(void)ipl;
	assert(pthread_mutex_init(m, NULL) == 0);
}
static void mutex_enter(kmutex_t *m)
{
	assert(pthread_mutex_lock(m) == 0);
}
static void mutex_exit(kmutex_t *m)
{
	assert(pthread_mutex_unlock(m) == 0);
}
static int bus_space_map(bus_space_tag_t, bus_addr_t, bus_size_t, int,
    bus_space_handle_t *);
static void bus_space_unmap(bus_space_tag_t, bus_space_handle_t, bus_size_t);
static int bus_space_peek_4(bus_space_tag_t, bus_space_handle_t,
    bus_size_t, uint32_t *);
static int bus_space_poke_4(bus_space_tag_t, bus_space_handle_t,
    bus_size_t, uint32_t);
static void bus_space_barrier(bus_space_tag_t, bus_space_handle_t,
    bus_size_t, bus_size_t, int);
static void delay(unsigned int);

#include "properties.h"
#include "pm-accessor.h"
#include "takeover.h"

/* Independent addresses: an accidental DMA/queue/W1C write aborts the test. */
static const struct {
	unsigned int block;
	bus_size_t offset;
	uint32_t initial;
} registers[] = {
	{ 0, 0x00c, 0x81117 }, { 0, 0x010, 0x1900 },
	{ 0, 0x014, 0x20a10 }, { 1, 0x000, 0x07443356 },
	{ 1, 0x004, 0x81001441 }, { 1, 0x008, 0xc0078101 },
	{ 0, 0x1238, 0x20804664 }, { 0, 0x1200, 0 },
	{ 0, 0x1000, 0 }, { 0, 0x1204, 0 },
	{ 0, 0x121c, 0 }, { 0, 0x1220, 0 },
	{ 0, 0x700, 0x010000 }, { 0, 0x704, 0 },
	{ 1, 0x900, 0x10 }, { 0, 0x600, 0 },
	{ 1, 0x100, 0x8000 }, { 1, 0x104, 0x8000 },
	{ 1, 0x130, 0 }, { 1, 0xf20, 0 },
	{ 0, 0x050, 0 }, { 1, 0x050, 0 },
};
#define NREG (sizeof(registers) / sizeof(registers[0]))
static struct device dev = { 1 }, foreign = { 2 };
static struct bcm2835pmwdog_softc pm = { 1, 100 }, other_pm = { 1, 101 };
static uint32_t values[NREG], masks[2], sms[2], pm_value, pm_initial;
static unsigned int cases, maps, unmaps, reads, writes, barriers;
static unsigned int fail_map, fail_read, fail_write, corrupt_write;
static unsigned int elapsed_us, assert_us, pm_writes, sms_phase, sms_polls;
static unsigned int sms_cycles, bad_sms_phase;
static uint32_t bad_sms_ree, bad_sms_tee;
static int corrupt_reg;
static uint32_t corrupt_value;
static bool active[3], fail_after_write, barrier_pending, reset_asserted;
static bool change_sms_snapshot, change_pm_baseline;

static void
setup(bool available)
{
	unsigned int i;

	if (bcmpmwdog_v3d_claimed.ready)
		assert(pthread_mutex_destroy(&bcmpmwdog_v3d_claimed.lock) == 0);
	memset(&bcmpmwdog_v3d_claimed, 0, sizeof(bcmpmwdog_v3d_claimed));
	memset(&tv3d, 0, sizeof(tv3d));
	memset(active, 0, sizeof(active));
	for (i = 0; i < NREG; i++)
		values[i] = registers[i].initial;
	masks[0] = 0x80000000;
	masks[1] = 0x40000000;
	sms[0] = 0;
	sms[1] = 0x50;
	/* Include unrelated writable PM bits, not just observed 0x1040. */
	pm_initial = pm_value = 0x001a1045;
	maps = unmaps = reads = writes = barriers = 0;
	fail_map = fail_read = fail_write = corrupt_write = 0;
	elapsed_us = assert_us = pm_writes = sms_phase = sms_polls = 0;
	sms_cycles = bad_sms_phase = 0;
	bad_sms_ree = 0;
	bad_sms_tee = 0x50;
	corrupt_reg = -1;
	corrupt_value = 0;
	fail_after_write = barrier_pending = reset_asserted = false;
	change_sms_snapshot = change_pm_baseline = false;
	bcmpmwdog_v3d_sc = available ? &pm : NULL;
	if (available)
		bcmpmwdog_v3d_register(&pm, 0x107d200000, 0x308);
	cases++;
}

static int
bus_space_map(bus_space_tag_t tag, bus_addr_t address, bus_size_t size,
    int flags, bus_space_handle_t *handle)
{
	static const uint64_t addresses[] = {
		0x1002000000, 0x1002008000, 0x1002030800
	};
	static const size_t lengths[] = { 0x4000, 0x6000, 0x700 };
	unsigned int block;

	assert(tag == 1 && flags == 0 && maps < 3);
	assert(bcmpmwdog_v3d_claimed.owner == &dev);
	block = maps++;
	assert(address == addresses[block] && size == lengths[block]);
	if (maps == fail_map)
		return ENOMEM;
	assert(!active[block]);
	active[block] = true;
	*handle = block;
	return 0;
}

static void
bus_space_unmap(bus_space_tag_t tag, bus_space_handle_t handle, bus_size_t size)
{
	static const size_t lengths[] = { 0x4000, 0x6000, 0x700 };

	assert(tag == 1 && handle < 3 && active[handle]);
	assert(size == lengths[handle]);
	assert(writes == 0 && !bcmpmwdog_v3d_claimed.sealed);
	active[handle] = false;
	unmaps++;
}

static int
bus_space_peek_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset, uint32_t *value)
{
	unsigned int i;

	assert(tag == 1 && !barrier_pending);
	if (++reads == fail_read)
		return EFAULT;
	if (handle == 100) {
		assert(offset == 0x304);
		*value = pm_value;
		if (change_pm_baseline && sms_phase == 2)
			*value ^= __BIT(1);
		return 0;
	}
	assert(handle < 3 && active[handle]);
	if (handle == 2) {
		assert(offset == 0 || offset == 0x400);
		if (sms_phase != 0) {
			if (offset == 0)
				sms_polls++;
			if (sms_phase == bad_sms_phase) {
				*value = offset == 0 ? bad_sms_ree : bad_sms_tee;
				return 0;
			}
			if (sms_polls <= sms_cycles) {
				*value = offset == 0 ?
				    (sms_phase == 2 ? 0x140a : 0x1400) :
				    (sms_phase == 1 ? 0x155d : 0x1550);
				return 0;
			}
		}
		*value = sms[offset != 0];
		if (change_sms_snapshot && reads > 5)
			*value ^= 0x10;
		return 0;
	}
	if (offset == 0x5c) {
		*value = masks[handle];
		return 0;
	}
	for (i = 0; i < NREG; i++) {
		if (registers[i].block != handle || registers[i].offset != offset)
			continue;
		*value = values[i];
		if (pm_writes == 2 && corrupt_reg == (int)i)
			*value = corrupt_value;
		return 0;
	}
	assert(!"undocumented read, wrong block, or unapproved register");
	return EIO;
}

static int
bus_space_poke_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset, uint32_t value)
{
	static const unsigned int blocks[] = { 0, 1, 2, 2, 100, 100, 0, 1 };
	static const unsigned int offsets[] = { 0x60, 0x60, 0x400, 0,
	    0x304, 0x304, 0x60, 0x60 };
	static const uint32_t fields[] = { 0x7f, 0x0fff007f };

	assert(tag == 1 && !barrier_pending && writes < 8);
	assert(bcmpmwdog_v3d_claimed.owner == &dev);
	assert(bcmpmwdog_v3d_claimed.sealed && tv3d.sealed);
	assert(handle == blocks[writes] && offset == offsets[writes]);
	writes++;
	if (writes == fail_write && !fail_after_write)
		return EFAULT;
	if (handle == 100) {
		assert((value & 0xff000000) == 0x5a000000);
		assert((value & ~UINT32_C(0xff000040)) ==
		    (pm_initial & ~UINT32_C(0x40)));
		if (pm_writes++ == 0) {
			assert((value & __BIT(6)) == 0 && sms_phase == 2);
			assert_us = elapsed_us;
			reset_asserted = true;
		} else {
			assert(pm_writes == 2 && (value & __BIT(6)) != 0);
			assert(reset_asserted && elapsed_us - assert_us >= 1);
			reset_asserted = false;
			/* Model masks cleared by reset, requiring the second W1S. */
			masks[0] &= ~fields[0];
			masks[1] &= ~fields[1];
		}
		pm_value = value & 0xffffff;
		if (writes == corrupt_write)
			pm_value ^= __BIT(8);
	} else if (handle == 2) {
		assert(active[2]);
		assert(value == (offset == 0 ? 4 : __BIT(29)));
		sms_phase = offset == 0 ? 2 : 1;
		sms_polls = 0;
	} else {
		assert(active[handle] && value == fields[handle]);
		masks[handle] |= value;
		if (writes == corrupt_write)
			masks[handle] ^= __BIT(31);
	}
	if (writes == fail_write)
		return EFAULT;
	barrier_pending = true;
	return 0;
}

static void
bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset, bus_size_t size, int flags)
{
	(void)handle;
	(void)offset;
	assert(tag == 1 && size == 4 && flags == 3 && barrier_pending);
	barrier_pending = false;
	barriers++;
}

static void
delay(unsigned int us)
{
	assert(!barrier_pending && (us == 1 || us == 100));
	if (us == 1)
		assert(reset_asserted);
	elapsed_us += us;
}

static void
stopped(int expected)
{
	unsigned int old_writes;

	assert(bcmv3d_takeover_probe(&dev, 1) == expected);
	assert(!tv3d.complete);
	if (tv3d.sealed) {
		assert(tv3d.claimed && unmaps == 0);
		assert(active[0] && active[1] && active[2]);
		assert(bcmpmwdog_v3d_release(&dev) == EBUSY);
		assert(bcmpmwdog_v3d_claim(&foreign) == EBUSY);
	} else {
		assert(writes == 0 && !tv3d.claimed);
		assert(!active[0] && !active[1] && !active[2]);
		assert(bcmpmwdog_v3d_claimed.owner == NULL);
	}
	old_writes = writes;
	assert(bcmv3d_takeover_probe(&dev, 1) == EBUSY);
	assert(writes == old_writes);
}

static void
physical_snapshot(void)
{
	/*
	 * CM5 Rev 1.0 / BCM2712 D0 / 4 GiB, R5 boot 2026-10-09.
	 * SSH dmesg receipt SHA256:
	 * 56450c7765791190c4428385c8efdeecf490652003f53d97fac3551d26f83de2.
	 * Register order follows the independent register fixture above.
	 */
	static const uint32_t actual[] = {
		0x81117, 0x1900, 0x20a10, 0x07443356, 0x81001431,
		0xc0078101, 0x20804664, 0, 0x80000000, 0, 0, 0xfff,
		0x2000, 0, 0, 0x30, 0, 0, 0, 0x1000, 0, 0
	};

	assert(sizeof(actual) == sizeof(values));
	memcpy(values, actual, sizeof(values));
	pm_initial = pm_value = 0x1040;
	sms[0] = 0;
	sms[1] = 0x50;
}

struct claim_call { device_t owner; int result; };
static void *
claim_thread(void *arg)
{
	struct claim_call *call = arg;

	call->result = bcmpmwdog_v3d_claim(call->owner);
	return NULL;
}

int
main(void)
{
	unsigned int i, bit, successful_reads;
	pthread_t threads[2];
	struct claim_call calls[2] = { { &dev, 0 }, { &foreign, 0 } };
	static const unsigned int busy_regs[] = { 7, 8, 12, 14, 15, 19, 20, 21 };
	static const uint32_t busy_bits[] = { 1, 4, 1, 1, 1, 1, 1, 1 };

	setup(true);
	assert(bcmv3d_takeover_probe(&dev, 1) == 0);
	assert(tv3d.complete && tv3d.claimed && tv3d.sealed);
	assert(writes == 8 && barriers == 8 && unmaps == 0 && pm_writes == 2);
	assert(pm_value == pm_initial && elapsed_us == 1);
	assert(masks[0] == 0x8000007f && masks[1] == 0x4fff007f);
	assert(bcmpmwdog_v3d_reset(&dev) == EBUSY);
	assert(bcmpmwdog_v3d_release(&dev) == EBUSY);
	assert(bcmv3d_takeover_probe(&dev, 1) == EBUSY && writes == 8);
	successful_reads = reads;

	/* Every recoverable read failure, including both PM readbacks. */
	for (i = 1; i <= successful_reads; i++) {
		setup(true);
		fail_read = i;
		stopped(EFAULT);
	}
	for (i = 1; i <= 3; i++) {
		setup(true);
		fail_map = i;
		stopped(ENOMEM);
		assert(unmaps == i - 1);
	}
	/* A failed poke may already have changed hardware. Never release. */
	for (i = 1; i <= 8; i++) {
		setup(true);
		fail_write = i;
		stopped(EFAULT);
		setup(true);
		fail_write = i;
		fail_after_write = true;
		stopped(EFAULT);
	}
	for (i = 1; i <= 8; i++) {
		if (i == 3 || i == 4)
			continue;
		setup(true);
		corrupt_write = i;
		stopped(EIO);
	}
	setup(false);
	stopped(ENXIO);
	for (i = 0; i < 3; i++) {
		setup(false);
		bcmpmwdog_v3d_sc = &pm;
		bcmpmwdog_v3d_register(i == 0 ? &other_pm : &pm,
		    i == 1 ? 0x107d200004 : 0x107d200000,
		    i == 2 ? 0x30c : 0x308);
		stopped(ENXIO);
	}
	setup(true);
	assert(bcmpmwdog_v3d_claim(NULL) == ENXIO);
	assert(bcmpmwdog_v3d_claim(&dev) == 0);
	assert(bcmpmwdog_v3d_claim(&foreign) == EBUSY);
	assert(bcmpmwdog_v3d_release(&foreign) == EPERM);
	assert(bcmpmwdog_v3d_seal(&foreign) == EPERM);
	assert(bcmpmwdog_v3d_reset(&foreign) == EPERM);
	assert(bcmpmwdog_v3d_reset(&dev) == EPERM);
	assert(bcmpmwdog_v3d_release(&dev) == 0 && writes == 0);
	assert(bcmpmwdog_v3d_claim(&foreign) == 0);
	assert(bcmpmwdog_v3d_seal(&foreign) == 0);
	assert(bcmpmwdog_v3d_seal(&foreign) == EBUSY);
	assert(bcmpmwdog_v3d_release(&foreign) == EBUSY && writes == 0);
	setup(true);
	assert(pthread_create(&threads[0], NULL, claim_thread, &calls[0]) == 0);
	assert(pthread_create(&threads[1], NULL, claim_thread, &calls[1]) == 0);
	assert(pthread_join(threads[0], NULL) == 0);
	assert(pthread_join(threads[1], NULL) == 0);
	assert((calls[0].result == 0 && calls[1].result == EBUSY) ||
	    (calls[1].result == 0 && calls[0].result == EBUSY));
	assert(writes == 0);

	for (i = 0; i < 3; i++) {
		setup(true);
		pm_value = i == 0 ? 0x1000 : i == 1 ? 0x40 : 0x5a001040;
		stopped(EBUSY);
	}
	setup(true);
	change_pm_baseline = true;
	stopped(EIO);
	assert(pm_writes == 0);
	setup(true);
	change_sms_snapshot = true;
	stopped(EBUSY);
	for (i = 0; i < 2; i++) {
		for (bit = 0; bit < 32; bit++) {
			setup(true);
			sms[i] ^= __BIT(bit);
			stopped(EOPNOTSUPP);
		}
	}
	for (i = 0; i < sizeof(busy_regs) / sizeof(busy_regs[0]); i++) {
		setup(true);
		values[busy_regs[i]] |= busy_bits[i];
		stopped(EBUSY);
		setup(true);
		corrupt_reg = (int)busy_regs[i];
		corrupt_value = values[busy_regs[i]] | busy_bits[i];
		stopped(EBUSY);
	}
	setup(true);
	values[0] ^= 1;
	stopped(ENODEV);
	setup(true);
	values[3] ^= 1;
	stopped(ENODEV);
	for (bit = 0; bit < 32; bit++) {
		if (((__BIT(0) | __BIT(7) | __BIT(12) | __BIT(20) |
		    __BIT(27)) & __BIT(bit)) == 0)
			continue;
		setup(true);
		values[7] |= __BIT(bit);
		stopped(EBUSY);
	}
	for (i = 0; i <= 6; i++) {
		setup(true);
		corrupt_reg = (int)i;
		corrupt_value = values[i] ^ __BIT(16);
		stopped(i == 3 ? ENODEV : EIO);
	}
	for (i = 1; i <= 2; i++) {
		setup(true);
		bad_sms_phase = i;
		bad_sms_ree = i == 2 ? 0xa : 0;
		bad_sms_tee = i == 1 ? 0x5d : 0x50;
		stopped(ETIMEDOUT);
		assert(elapsed_us == 100000 && pm_writes == 0);
		for (bit = 0; bit < 32; bit++) {
			setup(true);
			bad_sms_phase = i;
			bad_sms_tee ^= __BIT(bit);
			/* Power-off states are allowed only in the clear phase. */
			stopped(bit >= 8 && bit <= 16 ? ETIMEDOUT : EIO);
			setup(true);
			bad_sms_phase = i;
			bad_sms_ree ^= __BIT(bit);
			stopped((bit >= 8 && bit <= 16) || (i == 2 && bit == 2) ?
			    ETIMEDOUT : EIO);
		}
	}
	setup(true);
	sms_cycles = 3;
	assert(bcmv3d_takeover_probe(&dev, 1) == 0);
	assert(elapsed_us == 601 && tv3d.complete);

	/* The original whole-register zero gates reject this real snapshot. */
	setup(true);
	physical_snapshot();
	assert(bcmv3d_takeover_probe(&dev, 1) == 0);
	assert(tv3d.complete && writes == 8 && unmaps == 0);
	for (i = 0; i < 8; i++) {
		setup(true);
		physical_snapshot();
		values[15] = (i & 3) << 4;
		values[19] = (i & 4) != 0 ? 0x1000 : 0;
		assert(bcmv3d_takeover_probe(&dev, 1) == 0);
		assert(tv3d.complete && writes == 8 && unmaps == 0);
	}
	/* Preserve rejection of all other known and unknown register bits. */
	for (bit = 0; bit < 32; bit++) {
		if (bit != 4 && bit != 5) {
			setup(true);
			physical_snapshot();
			values[15] |= __BIT(bit);
			stopped(EBUSY);
			setup(true);
			physical_snapshot();
			corrupt_reg = 15;
			corrupt_value = values[15] | __BIT(bit);
			stopped(EBUSY);
		}
		if (bit != 12) {
			setup(true);
			physical_snapshot();
			values[19] |= __BIT(bit);
			stopped(EBUSY);
			setup(true);
			physical_snapshot();
			corrupt_reg = 19;
			corrupt_value = values[19] | __BIT(bit);
			stopped(EBUSY);
		}
	}
	printf("BCM2712 V3D takeover actual-source contract: %u cases passed\n",
	    cases);
	return 0;
}
