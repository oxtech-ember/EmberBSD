/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted real-source tests. */
#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sun60i_a733_gpu_fwimage.h"
#include "sun60i_a733_gpu_fwinit.h"
#include "powervr/pvr_rogue_fwif.h"

#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#define __CTASSERT(e) _Static_assert(e, #e)
#define roundup(x, y) ((((x) + (y) - 1) / (y)) * (y))
#define BUS_DMASYNC_PREREAD 1
#define BUS_DMASYNC_POSTREAD 2
#define BUS_DMASYNC_PREWRITE 4
#define BUS_DMASYNC_POSTWRITE 8
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
#define _BUS_DMAMAP_COHERENT 0x10000
#define _BUS_DMAMAP_IS_BOUNCING 0x20000

typedef uint64_t bus_addr_t;
typedef size_t bus_size_t;
typedef struct { bus_addr_t ds_addr; bus_size_t ds_len; } bus_dma_segment_t;
typedef struct fake_map {
	bus_size_t dm_mapsize;
	int dm_nsegs;
	bus_dma_segment_t dm_segs[1];
	int _dm_flags;
	unsigned int id;
} *bus_dmamap_t;
typedef void *bus_dma_tag_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
#include "mem.h"
#include "start.h"
#include "boot.h"
#include "probe.h"

static unsigned int assertions;
#define CHECK(e) do { \
	assertions++; \
	if (!(e)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); \
		exit(1); \
	} \
} while (0)

static struct sun60i_a733_gpu_fwmem owner;
static struct sun60i_a733_gpu_fwboot_result booted;
static struct sun60i_a733_gpu_fwprobe_result result, previous, previous_saved;
static unsigned int test_slot;
static uint32_t test_log_type;
static struct rogue_fwif_kccb_cmd test_command;
static bool skip_sync_update;
static struct fake_map maps[5];
static const size_t sizes[] = {16384, 16384, 16384, 139264, 12288};
static uint8_t *cpu[5], *expected[5], *saved[5];
static struct sun60i_a733_gpu_dma *const areas[] = {
	&owner.code, &owner.table, &owner.data, &owner.work, &owner.config
};
static unsigned int syncs, writes, barriers, waits, observations, completions;
static unsigned int phase, read_phase, ready_at, complete_at;
static unsigned int allocations, frees;
static unsigned int last_wait;
static uint64_t clock_ns, total_wait_us, jump_ns;
static bool freeze_clock, poke_fault, fault_after_execution;
static bool read_only_progress, response_without_read, foreign_updates;
static bool delayed_payload;
static uint32_t response;
static unsigned int firmware_failure;

static void bus_dmamap_sync(bus_dma_tag_t, bus_dmamap_t, bus_addr_t,
    bus_size_t, int);
static void bus_space_barrier(bus_space_tag_t, bus_space_handle_t,
    bus_size_t, bus_size_t, int);
static int bus_space_poke_4(bus_space_tag_t, bus_space_handle_t,
    bus_size_t, uint32_t);
static void getnanouptime(struct timespec *);
static void delay(unsigned int);
static void
test_timespecsub(const struct timespec *a, const struct timespec *b,
    struct timespec *out)
{
	out->tv_sec = a->tv_sec - b->tv_sec;
	out->tv_nsec = a->tv_nsec - b->tv_nsec;
	if (out->tv_nsec < 0) {
		out->tv_sec--;
		out->tv_nsec += 1000000000;
	}
}
#define timespecsub(a, b, out) test_timespecsub(a, b, out)
#include "driver.h"

/* Independent pinned ABI literals; do not derive the oracle from the helper. */
#define KCOMMAND 192
#define KRETURN 11456
#define MMUSYNC 17408
#define FCONTROL 11968
#define FCOMMAND 12160
#define SYSDATA 17472
#define TRACECTL 135104
#define OSDATA 136000
#define COMPCHECKS (4096 + 40)
#define STARTED (8192 + 352)

static uint32_t
get32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void
put32(uint8_t *p, uint32_t value)
{
	for (unsigned int i = 0; i < 4; i++)
		p[i] = (uint8_t)(value >> (i * 8));
}

static void
put64(uint8_t *p, uint64_t value)
{
	put32(p, (uint32_t)value);
	put32(p + 4, (uint32_t)(value >> 32));
}

static void
fw32(unsigned int area, size_t offset, uint32_t value)
{
	CHECK(offset <= sizes[area] - 4);
	put32(cpu[area] + offset, value);
	put32(expected[area] + offset, value);
}

static void
fw8(unsigned int area, size_t offset, uint8_t value)
{
	CHECK(offset < sizes[area]);
	cpu[area][offset] = expected[area][offset] = value;
}

static void
unchanged_foreign(void)
{
	if (test_slot >= 1)
		CHECK(memcmp(&previous, &previous_saved, sizeof(previous)) == 0);
	for (unsigned int i = 0; i < 5; i++)
		CHECK(memcmp(cpu[i], expected[i], sizes[i]) == 0);
}

static void
foreign_activity(void)
{
	if (!foreign_updates)
		return;
	/* Different owners within one cache line: never overwrite these. */
	fw32(3, KRETURN + (test_slot + 1) * 4,
	    get32(cpu[3] + KRETURN + (test_slot + 1) * 4) + 1);
	fw32(4, 8, get32(cpu[4] + 8) + 1);
	/* Other byte in the KCCB producer's cache line, not ours to clear. */
	fw32(3, 4, get32(cpu[3] + 4) + 1);
}

static void
complete_command(void)
{
	fw32(3, KRETURN + test_slot * 4, response);
	if (test_slot == 1 && !skip_sync_update)
		fw32(3, MMUSYNC, 0);
	if (!response_without_read)
		fw32(3, 128, test_slot + 1);
	fw32(3, OSDATA + 568, get32(cpu[3] + OSDATA + 568) + 1);
}

static void
firmware_tick(void)
{
	observations++;
	if (!result.published && ready_at != 0 && observations == ready_at)
		fw8(4, COMPCHECKS + 56, 1);
	if (result.published) {
		completions++;
		if (read_only_progress)
			fw32(3, 128, test_slot + 1);
		if (complete_at != 0 && completions == complete_at)
			complete_command();
		switch (firmware_failure) {
		case 1: fw8(4, STARTED, 0); break;
		case 2: fw8(4, COMPCHECKS + 56, 0); break;
		case 3: fw32(3, 128, 128); break;
		case 4: fw32(3, 0, test_slot + 2); break;
		case 5: fw32(3, FCONTROL, 1);
			fw32(3, FCOMMAND, 0x2abc0101); break;
		case 6: fw32(3, SYSDATA + 3576, 1U << 6); break;
		case 7: fw32(3, SYSDATA + 3536, 1); break;
		case 8: fw32(3, SYSDATA + 8, 2); break;
		case 9: fw32(3, 128, 0); break;
		case 10: fw32(3, TRACECTL, test_log_type ^ 4); break;
		default: break;
		}
	}
	foreign_activity();
	clock_ns += jump_ns;
}

static void
bus_dmamap_sync(bus_dma_tag_t tag, bus_dmamap_t map, bus_addr_t offset,
    bus_size_t size, int op)
{
	unsigned int id = map->id;
	uint8_t golden[88] = {0};

	CHECK(tag == &owner && result.attempted);
	CHECK(id == 3 || id == 4);
	CHECK((map->_dm_flags & 0x30000) == 0x10000);
	CHECK(offset <= sizes[id] && size <= sizes[id] - offset);
	syncs++;
	put32(golden, test_slot == 0 ? 0x2abc00ce : 0x2abc0066);
	if (test_slot >= 2)
		memcpy(golden, &test_command, sizeof(test_command));
	if (test_slot == 1) {
		put32(golden + 8, 0x0400001f);
		put32(golden + 12, 0xc0104400);
		/* Zero sync update is not a sequence number. */
		put32(golden + 16, 0);
	}
	CHECK(result.slot == test_slot);
	CHECK(result.command == get32(golden));
	if (size == sizes[id] && offset == 0) {
		CHECK(op == BUS_DMASYNC_POSTREAD || op == BUS_DMASYNC_PREREAD);
		CHECK(id == (read_phase == 0 || read_phase == 3 ? 3U : 4U));
		CHECK(op == (read_phase < 3 ? BUS_DMASYNC_POSTREAD :
		    BUS_DMASYNC_PREREAD));
		if (read_phase == 0) {
			unchanged_foreign();
			firmware_tick();
		}
		if (read_phase == 2) {
			/* Device publication flag must be sampled before DMA acquire. */
			CHECK(result.after.compchecks_updated == cpu[4][COMPCHECKS + 56]);
			if (delayed_payload && result.after.compchecks_updated == 1) {
				fw32(4, COMPCHECKS, 3);
				fw32(4, COMPCHECKS + 16, 3);
				fw32(4, COMPCHECKS + 8, 0x006800b7);
				fw32(4, COMPCHECKS + 12, 0x00240038);
				fw32(4, COMPCHECKS + 24, 0x006800b7);
				fw32(4, COMPCHECKS + 28, 0x00240038);
			}
		}
		read_phase = (read_phase + 1) % 5;
		unchanged_foreign();
		return;
	}
	CHECK(read_phase == 0 && id == 3 && !result.kick_attempted);
	switch (phase++) {
	case 0:
		CHECK(offset == (KCOMMAND + test_slot * 88) && size == 88 && op == BUS_DMASYNC_POSTWRITE);
		CHECK(!result.published && cpu[4][COMPCHECKS + 56] == 1);
		foreign_activity();
		unchanged_foreign();
		break;
	case 1:
		CHECK(offset == (KRETURN + test_slot * 4) && size == 4 &&
		    op == (BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE));
		CHECK(memcmp(cpu[3] + (KCOMMAND + test_slot * 88), golden, 88) == 0);
		memcpy(expected[3] + (KCOMMAND + test_slot * 88), golden, 88);
		unchanged_foreign();
		break;
	case 2:
		CHECK(offset == (KCOMMAND + test_slot * 88) && size == 88 && op == BUS_DMASYNC_PREWRITE);
		CHECK(get32(cpu[3] + (KRETURN + test_slot * 4)) == 0);
		put32(expected[3] + (KRETURN + test_slot * 4), 0);
		unchanged_foreign();
		break;
	case 3:
		CHECK(offset == (KRETURN + test_slot * 4) && size == 4 &&
		    op == (BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD));
		CHECK(get32(cpu[3]) == test_slot && !result.published);
		foreign_activity();
		unchanged_foreign();
		break;
	case 4:
		CHECK(offset == 0 && size == 4 && op == BUS_DMASYNC_POSTWRITE);
		CHECK(!result.published && get32(cpu[3]) == test_slot);
		break;
	case 5:
		CHECK(offset == 0 && size == 4 && op == BUS_DMASYNC_PREWRITE);
		CHECK(result.published && get32(cpu[3]) == test_slot + 1);
		put32(expected[3], test_slot + 1);
		unchanged_foreign();
		break;
	default: CHECK(false);
	}
}

static void
bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset, bus_size_t size, int flags)
{
	CHECK(tag == &owner && handle == 0x12340000);
	CHECK(offset == 0x0b00 && size == 4 && flags == 3);
	CHECK(phase == 6 && result.published && result.kick_attempted);
	CHECK(!result.kicked && read_phase == 0 && barriers < 2);
	CHECK(writes == barriers);
	barriers++;
}

static int
bus_space_poke_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset, uint32_t value)
{
	CHECK(tag == &owner && handle == 0x12340000);
	CHECK(offset == 0x0b00 && value == 0);
	CHECK(phase == 6 && barriers == 1 && writes == 0);
	CHECK(result.attempted && result.published && result.kick_attempted);
	CHECK(!result.kicked && get32(cpu[3]) == test_slot + 1);
	CHECK(get32(cpu[3] + (KRETURN + test_slot * 4)) == 0);
	unchanged_foreign();
	writes++;
	if (poke_fault && fault_after_execution)
		complete_command();
	return poke_fault ? EFAULT : 0;
}

static void
getnanouptime(struct timespec *out)
{
	out->tv_sec = (time_t)(clock_ns / 1000000000);
	out->tv_nsec = (long)(clock_ns % 1000000000);
}

static void
delay(unsigned int us)
{
	CHECK(us > 0 && us <= 100);
	CHECK(result.attempted);
	waits++;
	last_wait = us;
	total_wait_us += us;
	if (!freeze_clock)
		clock_ns += (uint64_t)us * 1000;
}

static void
reset(void)
{
	test_slot = 0;
	skip_sync_update = false;
	memset(&previous, 0, sizeof(previous));
	memset(&previous_saved, 0, sizeof(previous_saved));
	memset(&owner, 0, sizeof(owner));
	memset(&booted, 0, sizeof(booted));
	memset(&result, 0, sizeof(result));
	memset(maps, 0, sizeof(maps));
	owner.dmat = &owner;
	for (unsigned int i = 0; i < 5; i++) {
		struct sun60i_a733_gpu_dma *a = areas[i];

		maps[i].id = i;
		maps[i].dm_mapsize = sizes[i];
		maps[i].dm_nsegs = 1;
		maps[i].dm_segs[0].ds_addr = UINT64_C(0x120000000) + i * 0x100000;
		maps[i].dm_segs[0].ds_len = sizes[i];
		maps[i]._dm_flags = i >= 3 ? _BUS_DMAMAP_COHERENT : 0;
		a->map = &maps[i];
		a->segment = maps[i].dm_segs[0];
		a->cpu = cpu[i];
		a->size = sizes[i];
		a->address = maps[i].dm_segs[0].ds_addr;
		a->nsegments = 1;
		a->loaded = true;
		a->coherent = i >= 3;
		memset(cpu[i], 0xa0 + (int)i, sizes[i]);
	}
	CHECK(sun60i_a733_gpu_fwinit(cpu[3], sizes[3], cpu[4], sizes[4],
	    areas[3]->address, 600000000, 2, 16, &owner.interface_layout) == 0);
	owner.interface_prepared = true;
	owner.image.info.bvnc = UINT64_C(0x00240038006800b7);
	booted.attempted = booted.device_owned = booted.snapshot_valid = true;
	booted.start.touched = booted.start.released = true;
	booted.firmware_started = 1;
	cpu[4][STARTED] = 1;
	put64(cpu[4] + COMPCHECKS + 8, owner.image.info.bvnc);
	put64(cpu[4] + COMPCHECKS + 24, owner.image.info.bvnc);
	put32(cpu[4] + COMPCHECKS + 32, 0x81234567);
	put32(cpu[4] + COMPCHECKS + 36, 0x23456789);
	put32(cpu[4] + COMPCHECKS + 40, 0x3456789a);
	put32(cpu[4] + COMPCHECKS + 44, 0x456789ab);
	cpu[4][COMPCHECKS + 48] = 8;
	cpu[4][COMPCHECKS + 56] = 1;
	put32(cpu[3] + SYSDATA + 8, 3);
	put32(cpu[3] + SYSDATA + 3576, 1);
	put32(cpu[3] + KRETURN, 0xdeadbeef);
	put32(cpu[3] + OSDATA + 568, 0xfffffff0);
	put32(cpu[4], 0x12345678);
	put32(cpu[4] + 4, 0x23456789);
	put32(cpu[4] + 8, 0x3456789a);
	put32(cpu[4] + 12, 0x456789ab);
	for (unsigned int i = 0; i < 5; i++)
		memcpy(expected[i], cpu[i], sizes[i]);
	syncs = writes = barriers = waits = observations = completions = 0;
	phase = read_phase = 0;
	clock_ns = total_wait_us = jump_ns = 0;
	last_wait = 0;
	freeze_clock = poke_fault = fault_after_execution = false;
	read_only_progress = response_without_read = false;
	foreign_updates = true;
	delayed_payload = false;
	ready_at = 0;
	complete_at = 1;
	response = 1;
	firmware_failure = 0;
}

static int
run(void)
{
	if (test_slot >= 2)
		return sun60i_a733_gpu_fwprobe_cmd(&owner, 0x12340000,
		    &owner, &booted, &previous, &test_command, test_slot,
		    test_log_type, &result);
	if (test_slot == 1)
		return sun60i_a733_gpu_fwprobe_mmucache(&owner, 0x12340000,
		    &owner, &booted, &previous, &result);
	return sun60i_a733_gpu_fwprobe(&owner, 0x12340000, &owner, &booted, &result);
}

static void
retained(void)
{
	struct sun60i_a733_gpu_fwprobe_result old = result;
	unsigned int old_syncs = syncs, old_writes = writes;

	CHECK(result.attempted && booted.device_owned);
	unchanged_foreign();
	CHECK(run() == EBUSY);
	CHECK(memcmp(&old, &result, sizeof(old)) == 0);
	CHECK(syncs == old_syncs && writes == old_writes);
}

static void
runtime_tests(void)
{
	reset();
	CHECK(run() == 0);
	CHECK(result.error == 0 && result.polls == 2 && result.slot == 0);
	CHECK(result.command == 0x2abc00ce && result.cache_flags == 0);
	CHECK(result.sync_fw_addr == 0 && result.sync_update_value == 0);
	CHECK(result.published && result.kick_attempted && result.kicked);
	CHECK(result.before.rtn == 0xdeadbeef && result.after.rtn == 1);
	CHECK(result.after.read_offset == 1 && result.after.write_offset == 1);
	CHECK(result.after.cmds_executed == 0xfffffff1);
	CHECK(result.after.fw_processor_version == 0x81234567);
	CHECK(result.after.ddk_version == 0x23456789);
	CHECK(result.after.ddk_build == 0x3456789a);
	CHECK(result.after.build_options == 0x456789ab);
	CHECK(result.after.hw_bvnc == owner.image.info.bvnc);
	CHECK(result.after.fw_bvnc == owner.image.info.bvnc);
	CHECK(result.after.os_count_support == 8);
	CHECK(result.after.fw_state == 0x12345678 &&
	    result.after.os_state == 0x23456789);
	CHECK(result.after.alive_os_token == 0x456789ab);
	CHECK(result.after.alive_fw_token > result.before.alive_fw_token);
	CHECK(writes == 1 && barriers == 2 && waits == 0);
	retained();
	reset();
	fw8(4, COMPCHECKS + 56, 0);
	ready_at = 4;
	complete_at = 3;
	clock_ns = 999950000;
	CHECK(run() == 0);
	CHECK(result.polls == 7 && waits == 5 && total_wait_us == 500);
	CHECK(result.before.compchecks_updated == 1);
	retained();
	reset();
	fw8(4, COMPCHECKS + 56, 0);
	fw32(4, COMPCHECKS, 0);
	fw32(4, COMPCHECKS + 16, 0);
	fw32(4, COMPCHECKS + 8, 0);
	fw32(4, COMPCHECKS + 24, 0);
	ready_at = 3;
	delayed_payload = true;
	CHECK(run() == 0 && result.polls == 4);
	CHECK(result.before.hw_bvnc == owner.image.info.bvnc);
	CHECK(result.before.fw_bvnc == owner.image.info.bvnc);
	CHECK(result.before.hw_layout_version == 3);
	CHECK(result.before.fw_layout_version == 3);
	retained();

	reset();
	fw32(3, OSDATA + 568, UINT32_MAX);
	CHECK(run() == 0 && result.after.cmds_executed == 0);
	retained();
	for (unsigned int after = 0; after < 2; after++) {
		reset();
		poke_fault = true;
		fault_after_execution = after != 0;
		CHECK(run() == EFAULT && result.error == EFAULT);
		CHECK(result.attempted && result.published && result.kick_attempted);
		CHECK(!result.kicked && writes == 1 && barriers == 1);
		CHECK(result.polls == 1);
		retained();
	}
	for (unsigned int mode = 0; mode < 4; mode++) {
		reset();
		complete_at = 0;
		if (mode == 1)
			read_only_progress = true;
		if (mode == 2) {
			response_without_read = true;
			complete_at = 1;
		}
		if (mode == 3) {
			fw8(4, COMPCHECKS + 56, 0);
			ready_at = 0;
		}
		CHECK(run() == ETIMEDOUT);
		CHECK(total_wait_us == (mode == 3 ? 1000000U : 999900U));
		CHECK(waits == (mode == 3 ? 10000U : 9999U));
		CHECK(result.polls == (mode == 3 ? 10000U : 10001U));
		CHECK(result.published == (mode != 3));
		retained();
	}
	reset();
	fw8(4, COMPCHECKS + 56, 0);
	freeze_clock = true;
	CHECK(run() == ETIMEDOUT);
	CHECK(result.polls == 10001 && waits == 10000 && !result.published);
	retained();
	reset();
	fw8(4, COMPCHECKS + 56, 0);
	freeze_clock = true;
	ready_at = 10001;
	CHECK(run() == ETIMEDOUT && !result.published && result.polls == 10001);
	retained();
	reset();
	jump_ns = 1000000000;
	CHECK(run() == ETIMEDOUT && !result.published && waits == 0);
	retained();
	reset();
	fw8(4, COMPCHECKS + 56, 0);
	jump_ns = 999950000;
	CHECK(run() == ETIMEDOUT && !result.published && last_wait == 50);
	retained();
	for (unsigned int value = 2; value <= 7; value++) {
		reset();
		response = value;
		CHECK(run() == EPROTO && result.after.rtn == value);
		retained();
	}
	for (unsigned int failure = 1; failure <= 8; failure++) {
		reset();
		firmware_failure = failure;
		int expected_error = failure <= 4 ? EPROTO : (failure == 8 ? EBUSY : EIO);

		CHECK(run() == expected_error);
		CHECK(result.published && result.kicked);
		if (failure == 5)
			CHECK(result.after.fw_command == 0x2abc0101);
		retained();
	}
}

static void
runtime_reject(int error)
{
	CHECK(run() == error && result.error == error);
	CHECK(result.attempted && !result.published && !result.kick_attempted);
	CHECK(writes == 0 && phase == 0 && result.polls == 1);
	retained();
}

static void
compatibility_tests(void)
{
	reset(); fw8(4, STARTED, 0); runtime_reject(EPROTO);
	reset(); fw8(4, STARTED, 2); runtime_reject(EPROTO);
	reset(); fw8(4, COMPCHECKS + 56, 2); runtime_reject(EPROTO);
	reset(); fw8(4, COMPCHECKS + 56, 255); runtime_reject(EPROTO);
	reset(); fw32(4, COMPCHECKS, 4); runtime_reject(EOPNOTSUPP);
	reset(); fw32(4, COMPCHECKS + 16, 2); runtime_reject(EOPNOTSUPP);
	reset(); fw32(4, COMPCHECKS + 8, 1); runtime_reject(ENODEV);
	reset(); fw32(4, COMPCHECKS + 24, 1); runtime_reject(ENODEV);
	reset(); fw32(3, 0, 1); runtime_reject(EBUSY);
	reset(); fw32(3, 128, test_slot + 1); runtime_reject(EBUSY);
	reset(); fw32(3, 132, 255); runtime_reject(EPROTO);
	reset(); fw32(3, 136, 80); runtime_reject(EPROTO);
	reset(); fw32(3, FCONTROL + 132, 63); runtime_reject(EPROTO);
	reset(); fw32(3, FCONTROL + 136, 80); runtime_reject(EPROTO);
	reset(); fw32(3, FCONTROL + 128, 32); runtime_reject(EPROTO);
	reset(); fw32(3, FCONTROL, 32); runtime_reject(EPROTO);
	reset(); fw32(3, FCONTROL, 1); runtime_reject(EIO);
	reset(); fw32(3, TRACECTL, 1); runtime_reject(EPROTO);
	reset(); fw32(3, SYSDATA + 3536, 1); runtime_reject(EIO);
	reset(); fw32(3, SYSDATA + 3576, 1U << 7); runtime_reject(EIO);
	reset(); fw32(3, SYSDATA + 8, 0); runtime_reject(EBUSY);
	reset(); fw32(3, SYSDATA + 8, 2); runtime_reject(EBUSY);
	reset(); fw32(3, SYSDATA + 8, 1); CHECK(run() == 0); retained();
}

static void
structural_reject(int error, struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    struct sun60i_a733_gpu_fwprobe_result *out)
{
	struct sun60i_a733_gpu_fwmem old_owner = owner;
	struct sun60i_a733_gpu_fwboot_result old_boot = booted;
	struct sun60i_a733_gpu_fwprobe_result old_result = result;

	for (unsigned int i = 0; i < 5; i++)
		memcpy(saved[i], cpu[i], sizes[i]);
	CHECK(sun60i_a733_gpu_fwprobe(&owner, 0x12340000, mem, boot, out) == error);
	CHECK(syncs == 0 && writes == 0 && observations == 0);
	CHECK(memcmp(&owner, &old_owner, sizeof(owner)) == 0);
	CHECK(memcmp(&booted, &old_boot, sizeof(booted)) == 0);
	CHECK(memcmp(&result, &old_result, sizeof(result)) == 0);
	for (unsigned int i = 0; i < 5; i++)
		CHECK(memcmp(cpu[i], saved[i], sizes[i]) == 0);
}
#define REJECT(error, statement) do { reset(); statement; \
	structural_reject(error, &owner, &booted, &result); } while (0)

static void
structural_tests(void)
{
	reset(); structural_reject(EINVAL, NULL, &booted, &result);
	reset(); structural_reject(EINVAL, &owner, NULL, &result);
	reset(); structural_reject(EINVAL, &owner, &booted, NULL);
	reset(); structural_reject(EINVAL, &owner, &booted,
	    (void *)((uint8_t *)&result + 1));
	reset(); structural_reject(EINVAL, &owner, &booted, (void *)(UINTPTR_MAX - 7));
	REJECT(EINVAL, owner.interface_prepared = false);
	REJECT(EINVAL, owner.dmat = NULL);
	REJECT(EINVAL, booted.attempted = false);
	REJECT(EINVAL, booted.device_owned = false);
	REJECT(EINVAL, booted.start.touched = false);
	REJECT(EINVAL, booted.start.released = false);
	REJECT(EINVAL, booted.snapshot_valid = false);
	REJECT(EINVAL, booted.firmware_started = 0);
	REJECT(EINVAL, booted.error = EFAULT);
	REJECT(EOPNOTSUPP, owner.image.info.bvnc++);
	for (unsigned int i = 0; i < 5; i++) {
		REJECT(EINVAL, areas[i]->loaded = false);
		REJECT(EINVAL, areas[i]->map = NULL);
		REJECT(EINVAL, areas[i]->cpu = NULL);
		REJECT(EINVAL, areas[i]->cpu = cpu[i] + 1);
		REJECT(EINVAL, areas[i]->size = 0);
		REJECT(EINVAL, areas[i]->size = 4097);
		REJECT(EINVAL, areas[i]->size = 266240);
		REJECT(EINVAL, areas[i]->nsegments = 0);
		REJECT(EINVAL, areas[i]->address++);
		REJECT(EINVAL, areas[i]->address = UINT64_C(1) << 36);
		REJECT(EINVAL, maps[i].dm_mapsize--);
		REJECT(EINVAL, maps[i].dm_nsegs = 2);
		REJECT(EINVAL, maps[i].dm_segs[0].ds_addr++);
		REJECT(EINVAL, maps[i].dm_segs[0].ds_len--);
	}
	for (unsigned int i = 3; i < 5; i++) {
		REJECT(EOPNOTSUPP, areas[i]->coherent = false);
		REJECT(EOPNOTSUPP, maps[i]._dm_flags = 0);
		REJECT(EOPNOTSUPP, maps[i]._dm_flags |= _BUS_DMAMAP_IS_BOUNCING);
	}
	for (unsigned int i = 0; i < A733_GPU_FWINIT_OBJECT_COUNT; i++) {
		REJECT(EINVAL, owner.interface_layout.object[i].offset++);
		REJECT(EINVAL, owner.interface_layout.object[i].offset = UINT32_MAX);
		REJECT(EINVAL, owner.interface_layout.object[i].size--);
		REJECT(EINVAL, owner.interface_layout.object[i].fw_addr++);
		REJECT(EINVAL, owner.interface_layout.object[i].buffer ^= 1);
	}
	REJECT(EINVAL, owner.interface_layout.total_work_size--);
	REJECT(EINVAL, owner.work.cpu = owner.config.cpu);
	REJECT(EINVAL, owner.work.map = owner.config.map);
	reset();
	owner.work.address = owner.config.address;
	maps[3].dm_segs[0].ds_addr = owner.work.address;
	structural_reject(EINVAL, &owner, &booted, &result);
	reset();
	memset(cpu[3], 0, sizeof(result));
	structural_reject(EINVAL, &owner, &booted, (void *)cpu[3]);
	reset();
	result.kick_attempted = true;
	CHECK(run() == EBUSY && syncs == 0 && !result.attempted);
}

/* A real successful first command authorizes the distinct second operation. */
static void
prepare_cache(void)
{
	reset();
	CHECK(run() == 0);
	previous = previous_saved = result;
	memset(&result, 0, sizeof(result));
	test_slot = 1;
	syncs = writes = barriers = waits = observations = completions = 0;
	phase = read_phase = 0;
	clock_ns = total_wait_us = jump_ns = 0;
	last_wait = 0;
	/* Deliberately stale completion in the new slot. It must be cleared. */
	fw32(3, KRETURN + 4, 1);
}

static void
cache_reject(int error, const struct sun60i_a733_gpu_fwprobe_result *prior,
    struct sun60i_a733_gpu_fwprobe_result *out)
{
	struct sun60i_a733_gpu_fwprobe_result old = result, old_previous = previous;
	struct sun60i_a733_gpu_fwmem old_owner = owner;
	struct sun60i_a733_gpu_fwboot_result old_boot = booted;

	for (unsigned int i = 0; i < 5; i++)
		memcpy(saved[i], cpu[i], sizes[i]);
	CHECK(sun60i_a733_gpu_fwprobe_mmucache(&owner, 0x12340000,
	    &owner, &booted, prior, out) == error);
	CHECK(syncs == 0 && writes == 0 && observations == 0);
	CHECK(memcmp(&result, &old, sizeof(result)) == 0);
	CHECK(memcmp(&previous, &old_previous, sizeof(previous)) == 0);
	CHECK(memcmp(&owner, &old_owner, sizeof(owner)) == 0);
	CHECK(memcmp(&booted, &old_boot, sizeof(booted)) == 0);
	for (unsigned int i = 0; i < 5; i++)
		CHECK(memcmp(cpu[i], saved[i], sizes[i]) == 0);
}

#define CACHE_REJECT(error, statement) do { prepare_cache(); statement; \
	cache_reject(error, &previous, &result); } while (0)

static void
cache_tests(void)
{
	prepare_cache();
	CHECK(run() == 0);
	CHECK(result.error == 0 && result.slot == 1 && result.polls == 2);
	CHECK(result.command == 0x2abc0066 && result.cache_flags == 0x0400001f);
	CHECK(result.sync_fw_addr == 0xc0104400 && result.sync_update_value == 0);
	CHECK(result.before.read_offset == 1 && result.before.write_offset == 1);
	CHECK(result.before.rtn == 1 && result.after.rtn == 1);
	CHECK(result.after.read_offset == 2 && result.after.write_offset == 2);
	CHECK(result.before.mmucache_sync == 0 && result.after.mmucache_sync == 0);
	CHECK(writes == 1 && barriers == 2);
	retained();

	prepare_cache();
	fw32(3, MMUSYNC, 0xa5a5a5a5);
	skip_sync_update = true;
	CHECK(run() == 0 && result.after.mmucache_sync == 0xa5a5a5a5);
	retained();
	prepare_cache();
	complete_at = 4;
	CHECK(run() == 0 && result.polls == 5 && waits == 3);
	retained();
	prepare_cache();
	fw8(4, COMPCHECKS + 56, 0);
	ready_at = 3;
	CHECK(run() == 0 && result.polls == 4);
	retained();

	for (unsigned int mode = 0; mode < 4; mode++) {
		prepare_cache();
		complete_at = 0;
		if (mode == 1)
			read_only_progress = true;
		if (mode == 2) {
			response_without_read = true;
			complete_at = 1;
		}
		if (mode == 3)
			freeze_clock = true;
		CHECK(run() == ETIMEDOUT && result.polls == 10001);
		CHECK(result.after.mmucache_sync == 0);
		retained();
	}
	for (unsigned int after = 0; after < 2; after++) {
		prepare_cache();
		poke_fault = true;
		fault_after_execution = after != 0;
		CHECK(run() == EFAULT && result.published && result.kick_attempted);
		CHECK(!result.kicked && writes == 1 && barriers == 1);
		retained();
	}
	for (unsigned int failure = 1; failure <= 9; failure++) {
		prepare_cache();
		firmware_failure = failure;
		int error = failure <= 4 || failure == 9 ? EPROTO :
		    (failure == 8 ? EBUSY : EIO);

		CHECK(run() == error);
		retained();
	}
	for (unsigned int value = 2; value <= 7; value++) {
		prepare_cache(); response = value;
		CHECK(run() == EPROTO && result.after.rtn == value);
		retained();
	}
	prepare_cache(); fw32(3, 128, 0); runtime_reject(EBUSY);
	prepare_cache(); fw32(3, 0, 0); runtime_reject(EBUSY);
	prepare_cache(); fw32(3, 128, 2); runtime_reject(EBUSY);
	prepare_cache(); fw32(3, 0, 2); runtime_reject(EBUSY);
	prepare_cache(); jump_ns = 1000000000;
	CHECK(run() == ETIMEDOUT && !result.published); retained();

	CACHE_REJECT(EINVAL, previous.attempted = false);
	CACHE_REJECT(EINVAL, previous.published = false);
	CACHE_REJECT(EINVAL, previous.kick_attempted = false);
	CACHE_REJECT(EINVAL, previous.kicked = false);
	CACHE_REJECT(EINVAL, previous.error = ETIMEDOUT);
	CACHE_REJECT(EINVAL, previous.command = 0x2abc0066);
	CACHE_REJECT(EINVAL, previous.slot = 1);
	CACHE_REJECT(EINVAL, previous.cache_flags = 1);
	CACHE_REJECT(EINVAL, previous.sync_fw_addr = 0xc0104400);
	CACHE_REJECT(EINVAL, previous.sync_update_value = 1);
	CACHE_REJECT(EINVAL, previous.polls = 1);
	CACHE_REJECT(EINVAL, previous.polls = 10002);
	CACHE_REJECT(EINVAL, previous.before.read_offset = 1);
	CACHE_REJECT(EINVAL, previous.before.write_offset = 1);
	CACHE_REJECT(EINVAL, previous.after.read_offset = 0);
	CACHE_REJECT(EINVAL, previous.after.write_offset = 0);
	CACHE_REJECT(EINVAL, previous.after.rtn = 0);
	CACHE_REJECT(EINVAL, previous.after.rtn = 3);
	CACHE_REJECT(EINVAL, previous.before.hw_bvnc++);
	CACHE_REJECT(EINVAL, previous.after.fw_faults = 1);
	CACHE_REJECT(EOPNOTSUPP, owner.work.coherent = false);
	CACHE_REJECT(EOPNOTSUPP, maps[4]._dm_flags |= _BUS_DMAMAP_IS_BOUNCING);
	CACHE_REJECT(EINVAL, owner.interface_layout.object[A733_GPU_FWINIT_MMUCACHE_SYNC].offset++);
	CACHE_REJECT(EINVAL, owner.interface_layout.object[A733_GPU_FWINIT_MMUCACHE_SYNC].size++);
	CACHE_REJECT(EINVAL, owner.interface_layout.object[A733_GPU_FWINIT_MMUCACHE_SYNC].fw_addr++);
	prepare_cache(); cache_reject(EINVAL, NULL, &result);
	prepare_cache(); cache_reject(EINVAL, (void *)((uint8_t *)&previous + 1), &result);
	prepare_cache(); cache_reject(EINVAL, (void *)(UINTPTR_MAX - 7), &result);
	prepare_cache(); cache_reject(EINVAL, &result, &result);
	prepare_cache(); cache_reject(EBUSY, &previous, &previous);
	prepare_cache(); cache_reject(EINVAL, (void *)&owner, &result);
	prepare_cache(); cache_reject(EINVAL, (void *)&booted, &result);
	prepare_cache(); cache_reject(EINVAL, (void *)cpu[3], &result);
	prepare_cache();
	memset(cpu[4], 0, sizeof(result));
	cache_reject(EINVAL, &previous, (void *)cpu[4]);
	prepare_cache(); cache_reject(EINVAL, &previous, NULL);
}

/* Continue the real queue without recreating the owner or firmware state. */
static void
next_command(uint32_t command, uint32_t log_type)
{
	previous = previous_saved = result;
	memset(&result, 0, sizeof(result));
	test_slot++;
	test_log_type = log_type;
	memset(&test_command, 0, sizeof(test_command));
	test_command.cmd_type = command;
	syncs = writes = barriers = waits = observations = completions = 0;
	phase = read_phase = 0;
	clock_ns = total_wait_us = jump_ns = 0;
	last_wait = 0;
}

static void
prepare_trace(void)
{
	prepare_cache();
	CHECK(run() == 0);
	next_command(0x2abc0066, 0);
	CHECK(run() == 0 && result.slot == 2);
	next_command(0x2abc00ce, 0x3f);
	/* This is the stage-21 host write, before LOGTYPE_UPDATE. */
	fw32(3, TRACECTL, 0x3f);
}

static void
trace_tests(void)
{
	static const uint32_t invalid[] = { 1, 2, 0x3e, 0x8000, 0x80003f };
	unsigned int start = assertions;

	prepare_trace();
	CHECK(run() == 0 && result.slot == 3);
	CHECK(result.published && result.kicked && result.after.rtn == 1);
	CHECK(result.before.log_type == 0x3f && result.after.log_type == 0x3f);
	CHECK(result.after.read_offset == 4 && result.after.write_offset == 4);
	retained();
	next_command(0x2abc0065, 0x3f);
	CHECK(run() == 0 && result.slot == 4);
	CHECK(result.command == 0x2abc0065 && result.published && result.kicked);
	CHECK(result.after.read_offset == 5 && result.after.write_offset == 5);
	CHECK(result.after.log_type == 0x3f && result.after.rtn == 1);
	retained();

	for (unsigned int i = 0; i < __arraycount(invalid); i++) {
		prepare_trace();
		test_log_type = invalid[i];
		CHECK(run() == EINVAL && !result.attempted && syncs == 0);
		unchanged_foreign();
	}
	prepare_trace();
	fw32(3, TRACECTL, 0);
	CHECK(run() == EPROTO && !result.published && writes == 0);
	retained();
	prepare_trace();
	firmware_failure = 10;
	CHECK(run() == EPROTO && result.published && result.after.rtn == 1);
	retained();

	/* Tracing never bypasses the established runtime failure checks. */
	for (unsigned int failure = 1; failure <= 9; failure++) {
		prepare_trace();
		firmware_failure = failure;
		int error = failure <= 4 || failure == 9 ? EPROTO :
		    (failure == 8 ? EBUSY : EIO);

		CHECK(run() == error && result.published);
		retained();
	}
	prepare_trace();
	CHECK(run() == 0);
	next_command(0x2abc0065, 0);
	fw32(3, TRACECTL, 0);
	CHECK(run() == EINVAL && !result.attempted && syncs == 0);
	unchanged_foreign();

	/* Initial MMUCACHE still forbids the valid later trace setting. */
	prepare_cache();
	fw32(3, TRACECTL, 0x3f);
	CHECK(run() == EPROTO && !result.published && writes == 0);
	retained();
	printf("a733-gpu-fwprobe trace chain: PASS %u checks\n",
	    assertions - start);
}

static void
observation_tests(void)
{
	struct sun60i_a733_gpu_fwprobe_observation out, keep;
	struct sun60i_a733_gpu_fwprobe_result acceptance;
	unsigned int start = assertions, old_syncs, old_writes, old_barriers;

	prepare_trace();
	CHECK(run() == 0);
	next_command(ROGUE_FWIF_KCCB_CMD_KICK, 0x3f);
	CHECK(run() == 0);
	acceptance = result;
	foreign_updates = false;
	old_writes = writes;
	old_barriers = barriers;
	for (unsigned int i = 0; i < 5; i++)
		memcpy(saved[i], cpu[i], sizes[i]);
	memset(&out, 0, sizeof(out));
	CHECK(sun60i_a733_gpu_fwprobe_observe(&owner, &booted, 4, 0x3f,
	    &out) == 0);
	CHECK(out.valid && out.error == 0 && out.state.rtn == 1);
	CHECK(writes == old_writes && barriers == old_barriers);
	CHECK(memcmp(&result, &acceptance, sizeof(result)) == 0);
	for (unsigned int i = 0; i < 5; i++)
		CHECK(memcmp(saved[i], cpu[i], sizes[i]) == 0);
	/* Firmware queues RESET only after the successful KCCB return. */
	fw32(3, FCONTROL, 1);
	fw32(3, FCOMMAND, 0x2abc0069);
	CHECK(sun60i_a733_gpu_fwprobe_observe(&owner, &booted, 4, 0x3f,
	    &out) == EIO);
	CHECK(out.valid && out.error == EIO && out.state.fw_command == 0x2abc0069);
	CHECK(out.state.fw_read_offset == 0 && out.state.fw_write_offset == 1);
	CHECK(get32(cpu[3] + FCONTROL + 128) == 0);
	CHECK(memcmp(&result, &acceptance, sizeof(result)) == 0);
	CHECK(writes == old_writes && barriers == old_barriers);
	fw32(3, FCONTROL, 0);
	fw32(3, SYSDATA + 3536, 1);
	CHECK(sun60i_a733_gpu_fwprobe_observe(&owner, &booted, 4, 0x3f,
	    &out) == EIO);
	fw32(3, SYSDATA + 3536, 0);
	fw32(3, SYSDATA + 3576, 1U << 6);
	CHECK(sun60i_a733_gpu_fwprobe_observe(&owner, &booted, 4, 0x3f,
	    &out) == EIO);
	fw32(3, SYSDATA + 3576, 1);
	fw32(3, FCONTROL + 128, UINT32_MAX);
	CHECK(sun60i_a733_gpu_fwprobe_observe(&owner, &booted, 4, 0x3f,
	    &out) == EPROTO);
	CHECK(out.state.fw_command == 0);
	fw32(3, FCONTROL + 128, 0);
	fw32(3, TRACECTL, 0);
	CHECK(sun60i_a733_gpu_fwprobe_observe(&owner, &booted, 4, 0x3f,
	    &out) == EPROTO);
	fw32(3, TRACECTL, 0x3f);
	fw32(3, KRETURN + 16, 0);
	CHECK(sun60i_a733_gpu_fwprobe_observe(&owner, &booted, 4, 0x3f,
	    &out) == EPROTO);

	/* Structural rejection precedes every DMA read and preserves output. */
	memset(&keep, 0xa5, sizeof(keep));
	out = keep;
	old_syncs = syncs;
	CHECK(sun60i_a733_gpu_fwprobe_observe(NULL, &booted, 4, 0x3f,
	    &out) == EINVAL);
	CHECK(sun60i_a733_gpu_fwprobe_observe(&owner, &booted,
	    A733_GPU_FWINIT_KCCB_COUNT, 0x3f, &out) == EINVAL);
	CHECK(sun60i_a733_gpu_fwprobe_observe(&owner, &booted, 4, 0x80003f,
	    &out) == EINVAL);
	CHECK(sun60i_a733_gpu_fwprobe_observe(&owner, &booted, 4, 0x3f,
	    (void *)cpu[3]) == EINVAL);
	CHECK(sun60i_a733_gpu_fwprobe_observe(&owner, &booted, 4, 0x3f,
	    (void *)&owner) == EINVAL);
	owner.interface_layout.object[A733_GPU_FWINIT_FWCCB_CMDS].size++;
	CHECK(sun60i_a733_gpu_fwprobe_observe(&owner, &booted, 4, 0x3f,
	    &out) == EINVAL);
	CHECK(syncs == old_syncs && memcmp(&out, &keep, sizeof(out)) == 0);
	printf("a733-gpu-fwprobe fresh observation: PASS %u checks\n",
	    assertions - start);
}

int
main(void)
{
	for (unsigned int i = 0; i < 5; i++) {
		void *p;

		CHECK(posix_memalign(&p, 4096, sizes[i]) == 0);
		cpu[i] = p;
		expected[i] = malloc(sizes[i]);
		saved[i] = malloc(sizes[i]);
		CHECK(expected[i] != NULL && saved[i] != NULL);
		allocations += 3;
	}
	runtime_tests();
	compatibility_tests();
	structural_tests();
	cache_tests();
	trace_tests();
	observation_tests();
	/* No real device ran. The test alone owns final reclamation. */
	for (unsigned int i = 0; i < 5; i++) {
		free(cpu[i]);
		free(expected[i]);
		free(saved[i]);
		frees += 3;
	}
	CHECK(allocations == 15 && frees == allocations);
	printf("a733-gpu-fwprobe-contract: PASS %u checks\n", assertions);
	return 0;
}
