/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted source tests. */
#define _POSIX_C_SOURCE 200112L
#include <assert.h>
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
#define BUS_DMASYNC_PREREAD 1
#define BUS_DMASYNC_POSTREAD 2
#define BUS_DMASYNC_PREWRITE 4
#define BUS_DMASYNC_POSTWRITE 8

typedef uint64_t bus_addr_t;
typedef size_t bus_size_t;
typedef struct { bus_addr_t ds_addr; bus_size_t ds_len; } bus_dma_segment_t;
typedef struct fake_map {
	bus_size_t dm_mapsize;
	int dm_nsegs;
	bus_dma_segment_t dm_segs[1];
	unsigned int id;
	bool write_open, read_open;
} *bus_dmamap_t;
typedef void *bus_dma_tag_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
#include "memory.h"
#include "start.h"
#include "boot.h"

static unsigned int checks;
#define CHECK(e) do { \
	checks++; \
	if (!(e)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); \
		exit(1); \
	} \
} while (0)

static struct sun60i_a733_gpu_fwmem mem;
static struct sun60i_a733_gpu_fwboot_result result;
static struct fake_map maps[5];
static uint8_t *cpu[5], *device[5], *saved[5];
static const size_t sizes[5] = {16384, 16384, 16384, 139264, 12288};
static struct sun60i_a733_gpu_dma *const regions[5] = {
	&mem.code, &mem.table, &mem.data, &mem.work, &mem.config
};
static unsigned int syncs, start_calls, delays, postreads, completion_poll;
static unsigned int allocations, frees;
static uint64_t clock_ns, delay_total_us, jump_after_read;
static unsigned int last_delay;
static bool freeze_clock, touched, released;
static int start_error;
static uint8_t completion_value;
static struct { unsigned int id; int op; } events[11000];
static size_t event_count;

static void bus_dmamap_sync(bus_dma_tag_t, bus_dmamap_t, bus_addr_t,
    bus_size_t, int);
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

/* Simulated GPU writes affect device RAM, never the stale CPU view. */
static void
publish_device(void)
{
	uint8_t *p = device[4];

	put32(p, 0x11223344);
	put32(p + 4, 0x55667788);
	put32(p + 8, 0x89abcdef);
	put32(p + 12, 0xfedcba98);
	p[8192 + 352] = completion_value;
	put32(p + 8192 + 360, 0xa1b2c3d4);
	p += 4096 + 40;
	put32(p, 3);
	put32(p + 16, 4);
	put64(p + 8, UINT64_C(0x00240038006800b7));
	put64(p + 24, UINT64_C(0x1122334455667788));
	put32(p + 32, 0x12345678);
	put32(p + 36, 0x23456789);
	put32(p + 40, 0x3456789a);
	put32(p + 44, 0x456789ab);
	p[48] = 2;
	/* This diagnostic byte must not be read through a C bool lvalue. */
	p[56] = 0xfe;
}

static void
bus_dmamap_sync(bus_dma_tag_t tag, bus_dmamap_t map, bus_addr_t offset,
    bus_size_t size, int op)
{
	unsigned int i = map->id;

	CHECK(tag == &mem);
	CHECK(result.attempted && result.device_owned);
	CHECK(i < 5 && offset == 0 && size == sizes[i]);
	CHECK((op & (BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE)) == 0 ||
	    (op & (BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE)) == 0);
	CHECK(event_count < __arraycount(events));
	events[event_count].id = i;
	events[event_count++].op = op;
	syncs++;
	if (op & BUS_DMASYNC_PREWRITE) {
		CHECK(start_calls == 0 && !map->write_open);
		memcpy(device[i], cpu[i], size);
		map->write_open = true;
	}
	if (op & BUS_DMASYNC_POSTREAD) {
		CHECK(map->read_open);
		map->read_open = false;
		if (i == 4 && start_error == 0 && touched && released) {
			postreads++;
			if (completion_poll != 0 && postreads == completion_poll)
				publish_device();
			if (jump_after_read != 0)
				clock_ns += jump_after_read;
		}
		memcpy(cpu[i], device[i], size);
	}
	if (op & BUS_DMASYNC_POSTWRITE) {
		CHECK(map->write_open && !touched && !released);
		map->write_open = false;
	}
	if (op & BUS_DMASYNC_PREREAD) {
		CHECK(!map->read_open);
		/* Any CPU write after start would violate read-only observation. */
		if (start_calls != 0)
			CHECK(memcmp(cpu[i], device[i], size) == 0);
		map->read_open = true;
	}
}

int
sun60i_a733_gpu_fwstart(bus_space_tag_t tag, bus_space_handle_t handle,
    const struct sun60i_a733_gpu_fwinfo *info, uint64_t boot_code,
    uint64_t boot_data, uint64_t exception_code,
    struct sun60i_a733_gpu_fwstart_result *out)
{
	CHECK(result.attempted && result.device_owned);
	CHECK(tag == &mem && handle == 0x12340000);
	CHECK(info == &mem.image.info);
	CHECK(boot_code == mem.boot_code && boot_data == mem.boot_data &&
	    exception_code == mem.exception_code);
	CHECK(start_calls == 0 && syncs == 5);
	for (unsigned int i = 0; i < 5; i++) {
		CHECK(maps[i].write_open && maps[i].read_open == (i >= 2));
		CHECK(events[i].id == i && events[i].op ==
		    (BUS_DMASYNC_PREWRITE | (i >= 2 ? BUS_DMASYNC_PREREAD : 0)));
		CHECK(memcmp(cpu[i], device[i], sizes[i]) == 0);
	}
	start_calls++;
	memset(out, 0, sizeof(*out));
	out->touched = touched;
	out->released = released;
	out->writes = touched ? 17 : 0;
	out->last_register = touched ? 0x100 : 0;
	return start_error;
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
	CHECK(us > 0 && us <= 1000);
	CHECK(result.attempted && result.device_owned);
	delays++;
	last_delay = us;
	delay_total_us += us;
	if (!freeze_clock)
		clock_ns += (uint64_t)us * 1000;
}

static void
reset(void)
{
	memset(&mem, 0, sizeof(mem));
	memset(&result, 0, sizeof(result));
	memset(maps, 0, sizeof(maps));
	mem.dmat = &mem;
	for (unsigned int i = 0; i < 5; i++) {
		struct sun60i_a733_gpu_dma *r = regions[i];

		maps[i].id = i;
		maps[i].dm_mapsize = sizes[i];
		maps[i].dm_nsegs = 1;
		maps[i].dm_segs[0].ds_addr = UINT64_C(0x120000000) + i * 0x100000;
		maps[i].dm_segs[0].ds_len = sizes[i];
		r->map = &maps[i];
		r->segment = maps[i].dm_segs[0];
		r->cpu = cpu[i];
		r->size = sizes[i];
		r->address = maps[i].dm_segs[0].ds_addr;
		r->nsegments = 1;
		r->loaded = true;
		memset(cpu[i], 0x80 + (int)i, sizes[i]);
		memset(device[i], 0xcc, sizes[i]);
	}
	mem.image.info.allocation_size[A733_GPU_FW_CODE] = 16384;
	mem.image.info.allocation_size[A733_GPU_FW_DATA] = 16384;
	mem.boot_code = mem.code.address + 4096;
	mem.exception_code = mem.code.address + 8192;
	mem.boot_data = mem.data.address + 4096;
	mem.stack = mem.data.address + 8192;
	CHECK(sun60i_a733_gpu_fwinit(mem.work.cpu, mem.work.size,
	    mem.config.cpu, mem.config.size, mem.work.address, 600000000,
	    2, 16, &mem.interface_layout) == 0);
	mem.interface_prepared = true;
	syncs = start_calls = delays = postreads = 0;
	event_count = 0;
	clock_ns = 0;
	delay_total_us = jump_after_read = 0;
	last_delay = 0;
	freeze_clock = false;
	touched = released = true;
	start_error = 0;
	completion_poll = 1;
	completion_value = 1;
}

static void
assert_snapshot(void)
{
	CHECK(result.snapshot_valid);
	CHECK(result.firmware_started == completion_value);
	CHECK(result.firmware_started_timestamp == 0xa1b2c3d4);
	CHECK(result.fw_state == 0x11223344 && result.os_state == 0x55667788);
	CHECK(result.alive_fw_token == 0x89abcdef);
	CHECK(result.alive_os_token == 0xfedcba98);
	CHECK(result.hw_layout_version == 3 && result.fw_layout_version == 4);
	CHECK(result.hw_bvnc == UINT64_C(0x00240038006800b7));
	CHECK(result.fw_bvnc == UINT64_C(0x1122334455667788));
	CHECK(result.fw_processor_version == 0x12345678);
	CHECK(result.ddk_version == 0x23456789 && result.ddk_build == 0x3456789a);
	CHECK(result.build_options == 0x456789ab);
	CHECK(result.compchecks_updated == 0xfe && result.os_count_support == 2);
}

static int
run(void)
{
	return sun60i_a733_gpu_fwboot(&mem, 0x12340000, &mem, &result);
}

static void
assert_retained(void)
{
	struct sun60i_a733_gpu_fwboot_result old = result;
	unsigned int old_syncs = syncs, old_starts = start_calls;

	CHECK(result.attempted && result.device_owned);
	for (unsigned int i = 0; i < 5; i++) {
		CHECK(maps[i].write_open);
		CHECK(maps[i].read_open == (i >= 2));
	}
	CHECK(run() == EBUSY);
	CHECK(memcmp(&old, &result, sizeof(old)) == 0);
	CHECK(old_syncs == syncs && old_starts == start_calls);
}

static void
boot_tests(void)
{
	reset();
	CHECK(run() == 0);
	CHECK(result.error == 0 && result.polls == 1 && delays == 0);
	assert_snapshot();
	assert_retained();
	reset();
	completion_poll = 3;
	clock_ns = 999900000;
	CHECK(run() == 0);
	CHECK(result.polls == 3 && delays == 2 && delay_total_us == 2000);
	assert_snapshot();
	assert_retained();
	for (unsigned int value = 2; value <= 255; value += 253) {
		reset();
		completion_value = (uint8_t)value;
		CHECK(run() == EPROTO);
		CHECK(result.error == EPROTO && result.polls == 1 && delays == 0);
		assert_snapshot();
		assert_retained();
	}
	reset();
	completion_poll = 0;
	CHECK(run() == ETIMEDOUT);
	CHECK(result.error == ETIMEDOUT && result.snapshot_valid);
	CHECK(result.firmware_started == 0 && result.polls == 5000);
	CHECK(delays == 5000 && delay_total_us == 5000000);
	assert_retained();
	reset();
	completion_poll = 0;
	freeze_clock = true;
	CHECK(run() == ETIMEDOUT);
	CHECK(result.polls == 5001 && delays == 5000);
	assert_retained();
	reset();
	completion_poll = 0;
	jump_after_read = UINT64_C(6000000000);
	CHECK(run() == ETIMEDOUT);
	CHECK(result.polls == 1 && delays == 0);
	assert_retained();
	reset();
	completion_poll = 0;
	jump_after_read = UINT64_C(4999500000);
	CHECK(run() == ETIMEDOUT);
	CHECK(result.polls == 1 && delays == 1 && last_delay == 500);
	assert_retained();
	for (unsigned int release = 0; release < 2; release++) {
		reset();
		start_error = EFAULT;
		released = release != 0;
		CHECK(run() == EFAULT);
		CHECK(result.error == EFAULT && !result.snapshot_valid);
		CHECK(result.polls == 0 && delays == 0 && syncs == 5);
		CHECK(result.start.touched && result.start.released == released);
		assert_retained();
	}
	reset();
	start_error = EOPNOTSUPP;
	touched = released = false;
	CHECK(run() == EOPNOTSUPP);
	CHECK(result.attempted && !result.device_owned);
	CHECK(result.error == EOPNOTSUPP && syncs == 10 && delays == 0);
	for (unsigned int i = 0; i < 5; i++) {
		CHECK(!maps[i].write_open && !maps[i].read_open);
		CHECK(events[5 + i].id == i && events[5 + i].op ==
		    (BUS_DMASYNC_POSTWRITE | (i >= 2 ? BUS_DMASYNC_POSTREAD : 0)));
		CHECK(memcmp(cpu[i], device[i], sizes[i]) == 0);
	}
	{
		struct sun60i_a733_gpu_fwboot_result old = result;

		CHECK(run() == EBUSY && syncs == 10 && start_calls == 1);
		CHECK(memcmp(&old, &result, sizeof(old)) == 0);
	}
	/* Defend against an inconsistent leaf-helper success report. */
	reset();
	released = false;
	CHECK(run() == EPROTO);
	assert_retained();
	reset();
	touched = false;
	CHECK(run() == EPROTO);
	assert_retained();
}

static void
rejected(bus_space_tag_t tag, struct sun60i_a733_gpu_fwmem *owner,
    struct sun60i_a733_gpu_fwboot_result *out)
{
	struct sun60i_a733_gpu_fwboot_result old = result;
	struct sun60i_a733_gpu_fwmem old_mem = mem;

	for (unsigned int i = 0; i < 5; i++)
		memcpy(saved[i], cpu[i], sizes[i]);
	CHECK(sun60i_a733_gpu_fwboot(tag, 0x12340000, owner, out) == EINVAL);
	CHECK(syncs == 0 && start_calls == 0 && delays == 0);
	CHECK(memcmp(&old, &result, sizeof(old)) == 0);
	CHECK(memcmp(&old_mem, &mem, sizeof(mem)) == 0);
	for (unsigned int i = 0; i < 5; i++)
		CHECK(memcmp(saved[i], cpu[i], sizes[i]) == 0);
}

#define REJECT(statement) do { reset(); statement; \
	rejected(&mem, &mem, &result); } while (0)

static void
preflight_tests(void)
{
	reset(); rejected(NULL, &mem, &result);
	reset(); rejected(&mem, NULL, &result);
	reset(); rejected(&mem, &mem, NULL);
	reset(); rejected(&mem, &mem, (void *)((uint8_t *)&result + 1));
	reset(); rejected(&mem, &mem, (void *)(UINTPTR_MAX - 7));
	REJECT(mem.interface_prepared = false);
	REJECT(mem.dmat = NULL);
	for (unsigned int i = 0; i < 5; i++) {
		REJECT(regions[i]->loaded = false);
		REJECT(regions[i]->map = NULL);
		REJECT(regions[i]->cpu = NULL);
		REJECT(regions[i]->cpu = cpu[i] + 1);
		REJECT(regions[i]->nsegments = 0);
		REJECT(regions[i]->size = 0);
		REJECT(regions[i]->size = 4097);
		REJECT(regions[i]->size = 262144 + 4096);
		REJECT(regions[i]->address++);
		REJECT(regions[i]->address = UINT64_C(1) << 36);
		REJECT(regions[i]->address = 0x1fc00000);
		REJECT(maps[i].dm_mapsize--);
		REJECT(maps[i].dm_nsegs = 0);
		REJECT(maps[i].dm_segs[0].ds_len--);
		REJECT(maps[i].dm_segs[0].ds_addr += 4096);
	}
	for (unsigned int i = 0; i < 3; i++) {
		unsigned int id = A733_GPU_FWINIT_CONNECTION + i;

		REJECT(mem.interface_layout.object[id].offset += 4096);
		REJECT(mem.interface_layout.object[id].offset = UINT32_MAX);
		REJECT(mem.interface_layout.object[id].size--);
		REJECT(mem.interface_layout.object[id].fw_addr += 4096);
		REJECT(mem.interface_layout.object[id].buffer = A733_GPU_FWINIT_WORK);
	}
	REJECT(mem.interface_layout.total_work_size--);
	REJECT(mem.image.info.allocation_size[A733_GPU_FW_CODE]--);
	REJECT(mem.image.info.allocation_size[A733_GPU_FW_DATA]--);
	REJECT(mem.boot_code = mem.code.address + mem.code.size);
	REJECT(mem.boot_data = mem.data.address - 4096);
	REJECT(mem.exception_code = mem.boot_code);
	REJECT(mem.stack = mem.boot_data);
	REJECT(mem.stack++);
	REJECT(mem.data.cpu = mem.code.cpu);
	REJECT(mem.data.map = mem.code.map);
	reset();
	mem.table.address = mem.code.address;
	maps[1].dm_segs[0].ds_addr = mem.table.address;
	rejected(&mem, &mem, &result);
	reset();
	memcpy(cpu[0], &result, sizeof(result));
	rejected(&mem, &mem, (void *)cpu[0]);
	reset();
	result.device_owned = true;
	CHECK(run() == EBUSY && syncs == 0 && !result.attempted);
}

int
main(void)
{
	for (unsigned int i = 0; i < 5; i++) {
		void *p;

		CHECK(posix_memalign(&p, 4096, sizes[i]) == 0);
		cpu[i] = p;
		allocations++;
		CHECK(posix_memalign(&p, 4096, sizes[i]) == 0);
		device[i] = p;
		allocations++;
		saved[i] = malloc(sizes[i]);
		CHECK(saved[i] != NULL);
		allocations++;
	}
	boot_tests();
	preflight_tests();
	/* The mocked device is stopped here. Only the test owns deallocation. */
	for (unsigned int i = 0; i < 5; i++) {
		free(saved[i]); frees++;
		free(device[i]); frees++;
		free(cpu[i]); frees++;
	}
	CHECK(allocations == 15 && frees == allocations);
	printf("a733-gpu-fwboot-contract: PASS %u checks\n", checks);
	return 0;
}
