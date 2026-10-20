/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; native API lifecycle fault controls. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef uint64_t bus_addr_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef struct { unsigned unused; } kmutex_t;
typedef size_t bus_size_t;
typedef struct fake_tag { unsigned refs; bool parent; } *bus_dma_tag_t;
typedef struct { uint64_t ds_addr, ds_len; unsigned id; } bus_dma_segment_t;
typedef struct fake_map {
	int dm_nsegs, _dm_flags;
	size_t dm_mapsize;
	bus_dma_segment_t dm_segs[1];
	unsigned id;
} *bus_dmamap_t;
#define BUS_DMA_WAITOK 1
#define BUS_DMASYNC_PREWRITE 2
#define BUS_DMASYNC_PREREAD 4
#define BUS_DMASYNC_POSTWRITE 8
#define BUS_DMASYNC_POSTREAD 16
#define KM_SLEEP 1
#define _BUS_DMAMAP_IS_BOUNCING 0x20000
static unsigned checks;
#define CHECK(c) do { checks++; if (!(c)) { \
	fprintf(stderr, "check %u failed at line %d: %s\n", \
	    checks, __LINE__, #c); exit(1); } } while (0)

static struct fake_tag parent_tag = { 0, true };
static struct slot { void *cpu; size_t bytes; uint64_t address; } slots[6];
static unsigned stack[32], depth, calls, fail_call, allocated, current, syncs;
static bool borrowed, task_mode, checked_mode;
static unsigned completed_calls;
static bool completion_allowed;

static unsigned bad_at, bad_kind;
static uint64_t addresses[6];
static bool retained_now(void);
static void push(unsigned token) { CHECK(depth < 32); stack[depth++] = token; }
static void pop(unsigned token) { CHECK(depth > 0 && stack[--depth] == token); }
static bool fails(void) { return ++calls == fail_call; }
static void *kmem_zalloc(size_t n, int flags)
{
	CHECK(flags == KM_SLEEP); void *p = calloc(1, n); CHECK(p != NULL);
	push(1); return p;
}
static void kmem_free(void *p, size_t n)
{ (void)n; pop(1); free(p); }
static int bus_dmatag_subregion(bus_dma_tag_t parent, bus_addr_t lo,
    bus_addr_t hi, bus_dma_tag_t *out, int flags)
{
	CHECK(parent == &parent_tag && lo == 0 && hi == UINT32_MAX);
	CHECK(flags == BUS_DMA_WAITOK); if (fails()) return EIO;
	if (borrowed) *out = parent;
	else { *out = calloc(1, sizeof(**out)); CHECK(*out != NULL); }
	(*out)->refs++; push(2); return 0;
}
static void bus_dmatag_destroy(bus_dma_tag_t t)
{
	pop(2); CHECK(t != NULL && t->refs == 1); t->refs--;
	if (!t->parent) free(t);
}
static int bus_dmamem_alloc(bus_dma_tag_t tag, bus_size_t n,
    bus_size_t align, bus_size_t boundary, bus_dma_segment_t *seg,
    int maxseg, int *count, int flags)
{
	CHECK(tag->refs == 1 && align == 4096 && boundary == 0);
	CHECK(maxseg == 1 && flags == BUS_DMA_WAITOK); if (fails()) return EIO;
	CHECK(allocated < (task_mode ? 6U : 3U)); current = allocated++;
	slots[current].bytes = n; slots[current].address = addresses[current];
	*seg = (bus_dma_segment_t){ addresses[current], n, current };
	*count = 1; push(10 + current * 4); return 0;
}
static void bus_dmamem_free(bus_dma_tag_t tag, bus_dma_segment_t *seg, int count)
{ CHECK(tag->refs == 1 && count == 1); pop(10 + seg->id * 4); }
static int bus_dmamem_map(bus_dma_tag_t tag, bus_dma_segment_t *seg,
    int count, size_t n, void **out, int flags)
{
	CHECK(tag->refs == 1 && count == 1 && flags == BUS_DMA_WAITOK);
	CHECK(n == slots[seg->id].bytes); if (fails()) return EIO;
	*out = malloc(n); CHECK(*out != NULL); memset(*out, 0xa5, n);
	slots[seg->id].cpu = *out; push(11 + seg->id * 4); return 0;
}
static void bus_dmamem_unmap(bus_dma_tag_t tag, void *cpu, size_t n)
{
	CHECK(tag->refs == 1); unsigned id;
	for (id = 0; id < 6; id++) if (slots[id].cpu == cpu) break;
	CHECK(id < 6 && n == slots[id].bytes); pop(11 + id * 4); free(cpu);
}
static int bus_dmamap_create(bus_dma_tag_t tag, bus_size_t n,
    int maxseg, bus_size_t maxsize, bus_size_t boundary, int flags,
    bus_dmamap_t *out)
{
	CHECK(tag->refs == 1 && maxseg == 1 && n == maxsize && boundary == 0);
	CHECK(flags == BUS_DMA_WAITOK); if (fails()) return EIO;
	*out = calloc(1, sizeof(**out)); CHECK(*out != NULL);
	(*out)->id = current; push(12 + current * 4); return 0;
}
static void bus_dmamap_destroy(bus_dma_tag_t tag, bus_dmamap_t map)
{ CHECK(tag->refs == 1); pop(12 + map->id * 4); free(map); }
static int bus_dmamap_load(bus_dma_tag_t tag, bus_dmamap_t map,
    void *cpu, bus_size_t n, void *proc, int flags)
{
	CHECK(tag->refs == 1 && proc == NULL && flags == BUS_DMA_WAITOK);
	CHECK(cpu == slots[map->id].cpu && n == slots[map->id].bytes);
	if (fails()) return EIO;
	map->dm_nsegs = 1; map->dm_mapsize = n;
	map->dm_segs[0] = (bus_dma_segment_t){ slots[map->id].address, n, map->id };
	if (map->id == bad_at) {
		switch (bad_kind) {
		case 1: map->_dm_flags = _BUS_DMAMAP_IS_BOUNCING; break;
		case 2: map->dm_nsegs = 2; break;
		case 3: map->dm_mapsize--; break;
		case 4: map->dm_segs[0].ds_len--; break;
		case 5: map->dm_mapsize++; break;
		case 6: map->dm_segs[0].ds_len++; break;
		default: break;
		}
	}
	push(13 + map->id * 4); return 0;
}
static void bus_dmamap_unload(bus_dma_tag_t tag, bus_dmamap_t map)
{ CHECK(tag->refs == 1); pop(13 + map->id * 4); }
static void bus_dmamap_sync(bus_dma_tag_t tag, bus_dmamap_t map,
    bus_addr_t offset, bus_size_t n, int flags)
{
	CHECK(tag->refs == 1 && retained_now());
	CHECK(map->id == syncs % 6 && offset == 0 && n == slots[map->id].bytes);
	if (checked_mode) {
		CHECK(flags == (syncs < 6 ? BUS_DMASYNC_PREWRITE |
		    BUS_DMASYNC_PREREAD : BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD));
		if (syncs >= 6) CHECK(completion_allowed && completed_calls != 0);
	} else {
		CHECK(flags == (BUS_DMASYNC_PREWRITE |
		    (task_mode && map->id == 5 ? BUS_DMASYNC_PREREAD : 0)));
	}
	syncs++;
}
#include "sun60i_a733_npu_mmu.h"
#include "sun60i_a733_npu_mmu.c"
#include "sun60i_a733_npu_nn.h"
#include "sun60i_a733_npu_nn.c"
#include "sun60i_a733_npu_dma.h"
#include "sun60i_a733_npu_run.h"
static struct sun60i_a733_npu_run run_cookie, other_run_cookie;
#include "sun60i_a733_npu_dma.c"
static struct sun60i_a733_npu_dma *active;
bool sun60i_a733_npu_run_task_completed(struct sun60i_a733_npu_run *run,
    const struct sun60i_a733_npu_dma *dma)
{
	completed_calls++;
	return run == (void *)&run_cookie && dma == active && completion_allowed;
}
static bool retained_now(void)
{
	struct sun60i_a733_npu_dma_info info;
	CHECK(sun60i_a733_npu_dma_info(active, &info) == 0);
	return info.retained;
}
static void reset(void)
{
	CHECK(depth == 0 && parent_tag.refs == 0);
	memset(slots, 0, sizeof(slots)); calls = fail_call = allocated = syncs = 0;
	bad_at = bad_kind = 0; borrowed = task_mode = checked_mode = false;
	completed_calls = 0; completion_allowed = false; active = NULL;
	addresses[0] = 0x100000; addresses[1] = 0x110000; addresses[2] = 0x120000;
	addresses[3] = 0x130000; addresses[4] = 0x140000; addresses[5] = 0x150000;
}
static uint8_t test_commands[4096];
static void reject(int error)
{
	struct sun60i_a733_npu_dma *out = (void *)(uintptr_t)1;
	CHECK(sun60i_a733_npu_dma_prepare(&parent_tag, test_commands, 16, &out) == error);
	CHECK(out == (void *)(uintptr_t)1 && syncs == 0 && depth == 0);
	reset();
}
static void word(const uint8_t *p, uint32_t value)
{
	CHECK(p[0] == (value & 255)); CHECK(p[1] == ((value >> 8) & 255));
	CHECK(p[2] == ((value >> 16) & 255)); CHECK(p[3] == ((value >> 24) & 255));
}
static void success(bool borrow, size_t bytes, bool retain)
{
	struct sun60i_a733_npu_dma_info info;
	reset(); borrowed = borrow;
	CHECK(sun60i_a733_npu_dma_prepare(&parent_tag, test_commands, bytes, &active) == 0);
	CHECK(syncs == 0 && calls == 13 && depth == 14);
	CHECK(sun60i_a733_npu_dma_info(active, &info) == 0);
	CHECK(info.mtlb_address == 0x100000 && info.pd_entry == 0x1001);
	CHECK(info.stlb_address == 0x110000 && info.command_address == 0x120000);
	CHECK(info.command_virtual == 0x01001000 && info.command_bytes == bytes);
	CHECK(!info.retained && info.profile == A733_NPU_DMA_PROFILE_FE);
	CHECK(sun60i_a733_npu_dma_info(active, NULL) == EINVAL);
	for (size_t i = 0; i < 3; i++) {
		active->buffers[i].loaded = false;
		CHECK(sun60i_a733_npu_dma_retain(active) == EINVAL);
		CHECK(syncs == 0 && !retained_now());
		active->buffers[i].loaded = true;
	}
	for (size_t i = 0; i < 256; i++) word((uint8_t *)slots[0].cpu + i * 4,
	    i == 1 ? 0x110001 : 2);
	for (size_t i = 1024; i < 4096; i++) CHECK(((uint8_t *)slots[0].cpu)[i] == 0);
	for (size_t i = 0; i < 4096; i++) word((uint8_t *)slots[1].cpu + i * 4,
	    i == 1 ? 0x120003 : 2);
	CHECK(memcmp(slots[2].cpu, test_commands, bytes) == 0);
	for (size_t i = bytes; i < 4096; i++) CHECK(((uint8_t *)slots[2].cpu)[i] == 0);
	if (retain) {
		CHECK(sun60i_a733_npu_dma_retain(active) == 0);
		CHECK(syncs == 3 && retained_now()); unsigned saved_depth = depth;
		CHECK(sun60i_a733_npu_dma_retain(active) == EBUSY && syncs == 3);
		CHECK(sun60i_a733_npu_dma_destroy(active) == EBUSY && depth == saved_depth);
		/* No reset/quiesce exists. Test cleanup alone ends the simulated life. */
		active->retained = false;
	}
	CHECK(sun60i_a733_npu_dma_destroy(active) == 0); reset();
}
static void replacement_reject(const void *commands, size_t bytes, int error)
{
	uint8_t saved[4096];
	size_t count = active->command_bytes;
	memcpy(saved, slots[2].cpu, sizeof(saved));
	CHECK(sun60i_a733_npu_dma_set_commands(active, commands, bytes) == error);
	CHECK(active->command_bytes == count && syncs == 0 && calls == 13);
	CHECK(memcmp(saved, slots[2].cpu, sizeof(saved)) == 0);
}
static void replacement(void)
{
	uint8_t root[4096], stlb[16384];
	struct sun60i_a733_npu_dma_info old, now;
	reset();
	CHECK(!npu_dma_spans_overlap((void *)0x1000, 0x1000,
	    (void *)0x2000, 0x1000));
	CHECK(!npu_dma_spans_overlap((void *)0x2000, 0x1000,
	    (void *)0x1000, 0x1000));
	CHECK(npu_dma_spans_overlap((void *)0x1000, 0x1001,
	    (void *)0x2000, 0x1000));
	CHECK(npu_dma_spans_overlap((void *)0x2000, 0x1000,
	    (void *)0x1000, 0x1001));
	CHECK(sun60i_a733_npu_dma_set_commands(NULL, test_commands, 40) == EINVAL);
	CHECK(sun60i_a733_npu_dma_prepare(&parent_tag, test_commands, 4096,
	    &active) == 0);
	CHECK(sun60i_a733_npu_dma_info(active, &old) == 0);
	memcpy(root, slots[0].cpu, sizeof(root));
	memcpy(stlb, slots[1].cpu, sizeof(stlb));
	replacement_reject(NULL, 40, EINVAL);
	const size_t bad[] = { 0, 7, 9, 4097, SIZE_MAX };
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		replacement_reject(test_commands, bad[i], EINVAL);
	replacement_reject((void *)(UINTPTR_MAX - 7), 16, EINVAL);
	replacement_reject(active, 16, EINVAL);
	replacement_reject((void *)((uintptr_t)active - 8), 16, EINVAL);
	replacement_reject((void *)((uintptr_t)active + sizeof(*active) - 8),
	    16, EINVAL);
	for (unsigned i = 0; i < 3; i++) {
		uintptr_t start = (uintptr_t)slots[i].cpu;
		replacement_reject((void *)start, 16, EINVAL);
		replacement_reject((void *)(start - 8), 16, EINVAL);
		replacement_reject((void *)(start + slots[i].bytes - 8), 16, EINVAL);
		active->buffers[i].bytes++;
		replacement_reject(test_commands, 40, EINVAL);
		active->buffers[i].bytes--;
		void *saved_cpu = active->buffers[i].cpu;
		active->buffers[i].cpu = (void *)(UINTPTR_MAX - slots[i].bytes + 1);
		replacement_reject(test_commands, 40, EINVAL);
		active->buffers[i].cpu = saved_cpu;
		active->buffers[i].loaded = false;
		replacement_reject(test_commands, 40, EINVAL);
		active->buffers[i].loaded = true;
		void *cpu = active->buffers[i].cpu;
		active->buffers[i].cpu = NULL;
		replacement_reject(test_commands, 40, EINVAL);
		active->buffers[i].cpu = cpu;
		bus_dmamap_t map = active->buffers[i].map;
		active->buffers[i].map = NULL;
		replacement_reject(test_commands, 40, EINVAL);
		active->buffers[i].map = map;
	}
	active->retained = true;
	replacement_reject(test_commands, 40, EBUSY);
	active->retained = false;
	const size_t sizes[] = { 40, 8, 4096, 16 };
	for (size_t n = 0; n < sizeof(sizes) / sizeof(sizes[0]); n++) {
		CHECK(sun60i_a733_npu_dma_set_commands(active, test_commands,
		    sizes[n]) == 0);
		CHECK(syncs == 0 && calls == 13 && !active->retained);
		CHECK(memcmp(slots[2].cpu, test_commands, sizes[n]) == 0);
		for (size_t i = sizes[n]; i < 4096; i++)
			CHECK(((uint8_t *)slots[2].cpu)[i] == 0);
		CHECK(sun60i_a733_npu_dma_info(active, &now) == 0);
		old.command_bytes = sizes[n];
		CHECK(now.mtlb_address == old.mtlb_address &&
		    now.stlb_address == old.stlb_address &&
		    now.command_address == old.command_address &&
		    now.command_virtual == old.command_virtual &&
		    now.pd_entry == old.pd_entry && now.command_bytes == sizes[n]);
		CHECK(memcmp(root, slots[0].cpu, sizeof(root)) == 0);
		CHECK(memcmp(stlb, slots[1].cpu, sizeof(stlb)) == 0);
	}
	CHECK(sun60i_a733_npu_dma_destroy(active) == 0);
	reset();
}

static uint8_t test_constants[4096], test_input[4096];

static int
task_prepare(struct sun60i_a733_npu_dma **out)
{

	task_mode = true;
	return sun60i_a733_npu_dma_task_prepare(&parent_tag,
	    test_commands, 16, test_constants, 64, test_input, 64, out);
}

static void
task_reject(int error)
{
	struct sun60i_a733_npu_dma *out = (void *)(uintptr_t)1;

	CHECK(task_prepare(&out) == error);
	CHECK(out == (void *)(uintptr_t)1 && !syncs && !depth);
	reset();
}

static void
task_arguments(void)
{
	struct sun60i_a733_npu_dma *out = (void *)(uintptr_t)1;
	uint8_t saved_commands[4096], saved_constants[4096], saved_input[4096];
	const size_t bad[] = { 0, 4097, SIZE_MAX };
	const void *sources[] = { test_commands, test_constants, test_input };

	reset(); task_mode = true;
	memcpy(saved_commands, test_commands, sizeof(saved_commands));
	memcpy(saved_constants, test_constants, sizeof(saved_constants));
	memcpy(saved_input, test_input, sizeof(saved_input));
	for (unsigned which = 0; which < 3; which++) {
		for (unsigned n = 0; n < 3; n++) {
			size_t sizes[] = { 16, 64, 64 };
			sizes[which] = bad[n];
			CHECK(sun60i_a733_npu_dma_task_prepare(&parent_tag,
			    test_commands, sizes[0], test_constants, sizes[1],
			    test_input, sizes[2], &out) == EINVAL);
		}
		for (unsigned n = 0; n < 2; n++) {
			const void *inputs[] = { test_commands, test_constants, test_input };
			inputs[which] = n == 0 ? NULL : (void *)(UINTPTR_MAX - 7);
			CHECK(sun60i_a733_npu_dma_task_prepare(&parent_tag,
			    inputs[0], 16, inputs[1], 64, inputs[2], 64, &out) == EINVAL);
		}
		for (unsigned n = 0; n < 3; n++) {
			uintptr_t base = (uintptr_t)sources[which];
			size_t length = which == 0 ? 16 : 64;
			uintptr_t address = n == 0 ? base : n == 1 ? base - 1 : base + length - 1;
			CHECK(sun60i_a733_npu_dma_task_prepare(&parent_tag,
			    test_commands, 16, test_constants, 64, test_input, 64,
			    (struct sun60i_a733_npu_dma **)address) == EINVAL);
		}
	}
	CHECK(sun60i_a733_npu_dma_task_prepare(NULL, test_commands, 16,
	    test_constants, 64, test_input, 64, &out) == EINVAL);
	CHECK(sun60i_a733_npu_dma_task_prepare(&parent_tag, test_commands, 16,
	    test_constants, 64, test_input, 64, NULL) == EINVAL);
	CHECK(sun60i_a733_npu_dma_task_prepare(&parent_tag, test_commands, 16,
	    test_constants, 64, test_input, 64,
	    (struct sun60i_a733_npu_dma **)(UINTPTR_MAX - 3)) == EINVAL);
	CHECK(sun60i_a733_npu_dma_task_prepare(&parent_tag, test_commands, 9,
	    test_constants, 64, test_input, 64, &out) == EINVAL);
	CHECK(out == (void *)(uintptr_t)1 && !calls && !depth && !syncs);
	CHECK(memcmp(saved_commands, test_commands, sizeof(saved_commands)) == 0);
	CHECK(memcmp(saved_constants, test_constants, sizeof(saved_constants)) == 0);
	CHECK(memcmp(saved_input, test_input, sizeof(saved_input)) == 0);
	reset();
}

static void
task_success(bool borrow, bool retain)
{
	struct sun60i_a733_npu_dma_task_info info, saved;
	struct sun60i_a733_npu_dma_info common;
	const uint32_t virtuals[] = { 0x01001000, 0x01002000, 0x01003000, 0x01004000 };
	uint8_t image[4096];

	reset(); borrowed = borrow;
	CHECK(task_prepare(&active) == 0 && calls == 25 && depth == 26 && !syncs);
	CHECK(sun60i_a733_npu_dma_info(active, &common) == 0);
	CHECK(common.profile == A733_NPU_DMA_PROFILE_TASK && !common.retained);
	CHECK(sun60i_a733_npu_dma_task_info(active, &info) == 0);
	CHECK(info.memory.profile == A733_NPU_DMA_PROFILE_TASK);
	CHECK(info.constants_address == addresses[3] && info.input_address == addresses[4]);
	CHECK(info.output_address == addresses[5] && info.output_bytes == 4096);
	CHECK(info.constants_virtual == virtuals[1] && info.input_virtual == virtuals[2]);
	CHECK(info.output_virtual == virtuals[3] && info.constants_bytes == 64 && info.input_bytes == 64);
	for (size_t i = 0; i < 256; i++)
		word((uint8_t *)slots[0].cpu + i * 4, i == 1 ? (uint32_t)addresses[1] | 1 : 2);
	for (size_t i = 1024; i < 4096; i++) CHECK(((uint8_t *)slots[0].cpu)[i] == 0);
	for (size_t i = 0; i < 4096; i++)
		word((uint8_t *)slots[1].cpu + i * 4,
		    i >= 1 && i <= 4 ? (uint32_t)addresses[i + 1] | (i == 4 ? 7 : 3) : 2);
	for (unsigned b = 2; b < 6; b++) {
		const uint8_t *expected = b == 2 ? test_commands : b == 3 ? test_constants : test_input;
		size_t bytes = b == 2 ? 16 : 64;
		for (size_t i = 0; i < 4096; i++)
			CHECK(((uint8_t *)slots[b].cpu)[i] == (b == 5 ?
			    A733_NPU_DMA_TASK_OUTPUT_POISON : i < bytes ? expected[i] : 0));
	}
	CHECK(sun60i_a733_npu_dma_task_info(active, NULL) == EINVAL);
	for (unsigned b = 0; b < 6; b++) {
		memcpy(image, slots[b].cpu, sizeof(image));
		CHECK(sun60i_a733_npu_dma_set_commands(active, test_commands, 40) == EINVAL);
		CHECK(memcmp(image, slots[b].cpu, sizeof(image)) == 0);
		active->buffers[b].loaded = false;
		CHECK(sun60i_a733_npu_dma_retain(active) == EINVAL && !syncs && !active->retained);
		active->buffers[b].loaded = true;
	}
	CHECK(sun60i_a733_npu_dma_task_info(active,
	    (struct sun60i_a733_npu_dma_task_info *)active) == EINVAL);
	CHECK(sun60i_a733_npu_dma_task_info(active,
	    (struct sun60i_a733_npu_dma_task_info *)(UINTPTR_MAX - 3)) == EINVAL);
	for (unsigned b = 0; b < 6; b++) {
		CHECK(sun60i_a733_npu_dma_task_info(active,
		    (struct sun60i_a733_npu_dma_task_info *)slots[b].cpu) == EINVAL);
	}
	active->profile = 0;
	CHECK(sun60i_a733_npu_dma_destroy(active) == EINVAL && depth == 26);
	CHECK(sun60i_a733_npu_dma_retain(active) == EINVAL && !syncs);
	active->profile = A733_NPU_DMA_PROFILE_TASK;
	memcpy(&saved, &info, sizeof(saved));
	CHECK(sun60i_a733_npu_dma_task_info(NULL, &info) == EINVAL);
	CHECK(memcmp(&saved, &info, sizeof(info)) == 0);
	CHECK(calls == 25 && !syncs && active->command_bytes == 16);
	if (retain) {
		CHECK(sun60i_a733_npu_dma_retain(active) == 0 && syncs == 6 && active->retained);
		CHECK(sun60i_a733_npu_dma_destroy(active) == EBUSY && depth == 26);
		CHECK(sun60i_a733_npu_dma_retain(active) == EBUSY && syncs == 6);
		/* Only fixture cleanup ends this simulated retained lifetime. */
		active->retained = false;
	}
	CHECK(sun60i_a733_npu_dma_destroy(active) == 0);
	reset();
}

static void
task_contract(void)
{
	struct sun60i_a733_npu_dma_task_info info, saved;
	const uint64_t bad[] = { 1, 0x100000001, 0x100000000, UINT64_MAX };

	memset(test_constants, 0x3a, sizeof(test_constants));
	memset(test_input, 0xbd, sizeof(test_input));
	task_arguments();
	for (unsigned i = 1; i <= 25; i++) { fail_call = i; task_reject(EIO); }
	for (unsigned b = 0; b < 6; b++) {
		for (unsigned kind = 1; kind <= 6; kind++) {
			bad_at = b; bad_kind = kind;
			task_reject(kind == 1 ? EOPNOTSUPP : EINVAL);
		}
		for (unsigned i = 0; i < 4; i++) {
			addresses[b] = bad[i]; task_reject(ERANGE);
		}
	}
	addresses[1] = 0xfffff000; task_reject(ERANGE);
	for (unsigned i = 0; i < 6; i++) {
		for (unsigned j = i + 1; j < 6; j++) {
			addresses[j] = addresses[i]; task_reject(EEXIST);
		}
	}
	addresses[5] = addresses[1] + 0x3000; task_reject(EEXIST);
	addresses[2] = 0x01001000; task_reject(EADDRINUSE);
	CHECK(sun60i_a733_npu_dma_prepare(&parent_tag, test_commands, 16, &active) == 0);
	memset(&info, 0xa5, sizeof(info)); saved = info;
	CHECK(sun60i_a733_npu_dma_task_info(active, &info) == EINVAL);
	CHECK(memcmp(&info, &saved, sizeof(info)) == 0);
	CHECK(sun60i_a733_npu_dma_destroy(active) == 0); reset();
	task_success(false, false); task_success(true, false);
	task_success(false, true); task_success(true, true);
	for (unsigned n = 0; n < 2; n++) {
		task_mode = true;
		size_t bytes = n == 0 ? 8 : 4096, data_bytes = n == 0 ? 1 : 4096;
		CHECK(sun60i_a733_npu_dma_task_prepare(&parent_tag,
		    test_commands, bytes, test_constants, data_bytes,
		    test_input, data_bytes, &active) == 0);
		CHECK(sun60i_a733_npu_dma_task_info(active, &info) == 0);
		CHECK(info.memory.command_bytes == bytes && info.constants_bytes == data_bytes &&
		    info.input_bytes == data_bytes);
		CHECK(memcmp(slots[2].cpu, test_commands, bytes) == 0);
		CHECK(memcmp(slots[3].cpu, test_constants, data_bytes) == 0);
		CHECK(memcmp(slots[4].cpu, test_input, data_bytes) == 0);
		CHECK(sun60i_a733_npu_dma_destroy(active) == 0); reset();
	}
	/* Four-page STLB alignment is not a sixteen-KiB alignment requirement. */
	addresses[1] = 0x111000;
	CHECK(task_prepare(&active) == 0);
	CHECK(sun60i_a733_npu_dma_destroy(active) == 0); reset();
}


static struct sun60i_a733_npu_dma_task_reference *
checked_prepare(void)
{
	struct sun60i_a733_npu_dma_task_reference *ref = calloc(1,
	    sizeof(*ref));
	struct sun60i_a733_npu_mmu_page pages[4];
	struct sun60i_a733_npu_mmu_plan plan;

	CHECK(ref != NULL);
	CHECK(sun60i_a733_npu_nn_tiny_image(&ref->image,
	    sizeof(ref->image)) == 0);
	CHECK(sun60i_a733_npu_nn_task_commands(ref->commands,
	    sizeof(ref->commands)) == 0);
	task_mode = checked_mode = true;
	CHECK(sun60i_a733_npu_dma_task_prepare(&parent_tag,
	    ref->commands, sizeof(ref->commands), ref->image.constants,
	    sizeof(ref->image.constants), ref->image.input,
	        sizeof(ref->image.input), &active) == 0);
	for (size_t i = 0; i < 4; i++)
		pages[i] = (struct sun60i_a733_npu_mmu_page){
			addresses[i + 2], i == 3 ? 1 : 0
		};
	plan = (struct sun60i_a733_npu_mmu_plan){
		addresses[0], addresses[1], 0x01001000, pages, 4
	};
	CHECK(sun60i_a733_npu_mmu_build(&plan, ref->mtlb, sizeof(ref->mtlb),
	    ref->stlb, sizeof(ref->stlb)) == 0);
	CHECK(sun60i_a733_npu_dma_task_validate(active, ref) == 0);
	return ref;
}

static void
checked_cleanup(struct sun60i_a733_npu_dma_task_reference *ref)
{
	/*
	 * End only this simulated retained lifetime. Native code cannot do
	 * this.
	 */
	active->retained = false;
	CHECK(sun60i_a733_npu_dma_destroy(active) == 0);
	free(ref);
	reset();
}

static void
checked_contract(void)
{
	struct sun60i_a733_npu_dma_task_report report, saved;
	struct sun60i_a733_npu_run *run = (void *)&run_cookie, *wrong =
	    (void *)&other_run_cookie;
	struct sun60i_a733_npu_dma_task_reference *ref;
	const size_t sizes[] = { 4096, 16384, 4096, 4096, 4096, 4096 };

	for (unsigned target = 0; target < 6; target++) {
		for (unsigned end = 0; end < 2; end++) {
			ref = checked_prepare();
			memset(&report, 0x5a, sizeof(report));
			saved = report;
			CHECK(sun60i_a733_npu_dma_task_check(active, run, ref,
			    &report) == EBUSY);
			CHECK(!syncs && memcmp(&report, &saved,
			    sizeof(report)) == 0);
			CHECK(sun60i_a733_npu_dma_task_retain_checked(active,
			    run, ref) == 0);
			CHECK(syncs == 6 && active->retained);
			CHECK(sun60i_a733_npu_dma_task_check(active, run, ref,
			    &report) == EBUSY);
			CHECK(syncs == 6 && memcmp(&report, &saved,
			    sizeof(report)) == 0);
			completion_allowed = true;
			CHECK(sun60i_a733_npu_dma_task_check(active, wrong,
			    ref, &report) == EINVAL);
			struct sun60i_a733_npu_dma_task_reference *other =
			    malloc(sizeof(*other));
			CHECK(other != NULL);
			*other = *ref;
			CHECK(sun60i_a733_npu_dma_task_check(active, run,
			    other, &report) == EINVAL);
			free(other);
			CHECK(syncs == 6);
			/*
			 * Simulated device work only. No production path
			 * copies the oracle.
			 */
			memcpy(slots[5].cpu, ref->image.expected,
			    sizeof(ref->image.expected));
			size_t offset = end ? sizes[target] - 1 : 0;
			((uint8_t *)slots[target].cpu)[offset] ^= 1;
			CHECK(sun60i_a733_npu_dma_task_check(active, run, ref,
			    &report) == EIO);
			CHECK(syncs == 12 && report.valid && !report.pass);
			for (unsigned i = 0; i < 6; i++) {
				CHECK(report.mismatches[i] == (i == target ?
				    1U : 0U));
				CHECK(report.first[i] == (i == target ?
				    offset : UINT32_MAX));
			}
			CHECK(!sun60i_a733_npu_dma_task_verified(active, run));
			CHECK(sun60i_a733_npu_dma_task_check(active, run, ref,
			    &report) == EALREADY);
			CHECK(sun60i_a733_npu_dma_destroy(active) == EBUSY &&
			    syncs == 12);
			checked_cleanup(ref);
		}
	}
	for (unsigned write_output = 0; write_output < 2; write_output++) {
		ref = checked_prepare();
		CHECK(sun60i_a733_npu_dma_task_retain_checked(active, run,
		    ref) == 0);
		completion_allowed = true;
		if (write_output)
			memcpy(slots[5].cpu, ref->image.expected, 512);
		CHECK(sun60i_a733_npu_dma_task_check(active, run, ref,
		    &report) ==
		    (write_output ? 0 : EIO));
		CHECK(report.valid && report.pass == write_output &&
		    syncs == 12);
		CHECK(sun60i_a733_npu_dma_task_verified(active, run) ==
		    (write_output != 0));
		CHECK(!sun60i_a733_npu_dma_task_verified(active, wrong));
		checked_cleanup(ref);
	}

	/*
	 * Every prepublication byte, tail and table class is checked without
	 * sync.
	 */
	ref = checked_prepare();
	for (unsigned b = 0; b < 6; b++) {
		for (unsigned end = 0; end < 2; end++) {
			size_t offset = end ? sizes[b] - 1 : 0;
			((uint8_t *)slots[b].cpu)[offset] ^= 1;
			CHECK(sun60i_a733_npu_dma_task_validate(active,
			    ref) == EIO && !syncs);
			((uint8_t *)slots[b].cpu)[offset] ^= 1;
		}
	}
	for (unsigned b = 0; b < 6; b++) {
		struct npu_dma_buffer *buf = &active->buffers[b];
		buf->loaded = false;
		CHECK(sun60i_a733_npu_dma_task_validate(active, ref) ==
		    EINVAL);
		buf->loaded = true;
		buf->nsegments = 2;
		CHECK(sun60i_a733_npu_dma_task_validate(active, ref) ==
		    EINVAL);
		buf->nsegments = 1;
		buf->segment.ds_len--;
		CHECK(sun60i_a733_npu_dma_task_validate(active, ref) ==
		    EINVAL);
		buf->segment.ds_len++;
		buf->segment.ds_addr++;
		CHECK(sun60i_a733_npu_dma_task_validate(active, ref) ==
		    EINVAL);
		buf->segment.ds_addr--;
		buf->map->dm_segs[0].ds_addr++;
		CHECK(sun60i_a733_npu_dma_task_validate(active, ref) ==
		    EINVAL);
		buf->map->dm_segs[0].ds_addr--;
		void *cpu = buf->cpu;
		buf->cpu = (void *)(UINTPTR_MAX - 7);
		CHECK(sun60i_a733_npu_dma_task_validate(active, ref) ==
		    EINVAL);
		buf->cpu = NULL;
		CHECK(sun60i_a733_npu_dma_task_validate(active, ref) ==
		    EINVAL);
		buf->cpu = cpu;
		bus_dmamap_t map = buf->map;
		buf->map = NULL;
		CHECK(sun60i_a733_npu_dma_task_validate(active, ref) ==
		    EINVAL);
		buf->map = map;
		bus_addr_t address = buf->address;
		const bus_addr_t invalid[] = {1, UINT64_C(0x100000000),
		    UINT64_MAX};
		for (unsigned n = 0; n < 3; n++) {
			buf->address = buf->segment.ds_addr =
			    map->dm_segs[0].ds_addr = invalid[n];
			CHECK(sun60i_a733_npu_dma_task_validate(active,
			    ref) == EINVAL);
		}
		buf->address = buf->segment.ds_addr =
		    map->dm_segs[0].ds_addr = address;
		if (b == 0)
			continue;
		buf->cpu = slots[0].cpu;
		CHECK(sun60i_a733_npu_dma_task_validate(active, ref) ==
		    EINVAL);
		buf->cpu = cpu;
	}
	active->pd_entry ^= 1;
	CHECK(sun60i_a733_npu_dma_task_validate(active, ref) == EINVAL);
	active->pd_entry ^= 1;
	active->profile = A733_NPU_DMA_PROFILE_FE;
	CHECK(sun60i_a733_npu_dma_task_validate(active, ref) == EINVAL);
	active->profile = A733_NPU_DMA_PROFILE_TASK;
	CHECK(!syncs && !active->retained);
	checked_cleanup(ref);
	/*
	 * Invalid references, output alias, metadata, and all15 physical
	 * pairs.
	 */
	ref = checked_prepare();
	CHECK(sun60i_a733_npu_dma_task_validate(active, NULL) == EINVAL);
	CHECK(sun60i_a733_npu_dma_task_validate(active,
	    (void *)(UINTPTR_MAX - 7)) == EINVAL);
	CHECK(sun60i_a733_npu_dma_task_validate(active, (void *)active) ==
	    EINVAL);
	for (unsigned i = 0; i < 6; i++) {
		CHECK(sun60i_a733_npu_dma_task_validate(active,
		    slots[i].cpu) == EINVAL);
		bus_dmamap_t map = active->buffers[i].map;
		map->_dm_flags |= _BUS_DMAMAP_IS_BOUNCING;
		CHECK(sun60i_a733_npu_dma_task_retain_checked(active, run,
		    ref) == EINVAL && !syncs);
		map->_dm_flags = 0;
		map->dm_mapsize--;
		CHECK(sun60i_a733_npu_dma_task_validate(active, ref) ==
		    EINVAL);
		map->dm_mapsize++;
		map->dm_nsegs = 2;
		CHECK(sun60i_a733_npu_dma_task_validate(active, ref) ==
		    EINVAL);
		map->dm_nsegs = 1;
		map->dm_segs[0].ds_len--;
		CHECK(sun60i_a733_npu_dma_task_validate(active, ref) ==
		    EINVAL);
		map->dm_segs[0].ds_len++;
		for (unsigned j = i + 1; j < 6; j++) {
			bus_addr_t address = active->buffers[j].address;
			active->buffers[j].address =
			    active->buffers[i].address;
			active->buffers[j].segment.ds_addr =
			    active->buffers[j].map->dm_segs[0].ds_addr =
			    active->buffers[j].address;
			CHECK(sun60i_a733_npu_dma_task_validate(active,
			    ref) == EINVAL);
			active->buffers[j].address = address;
			active->buffers[j].segment.ds_addr =
			    active->buffers[j].map->dm_segs[0].ds_addr =
			    address;
		}
	}
	for (unsigned i = 0; i < 3; i++) {
		size_t *count = i == 0 ? &active->command_bytes : i == 1 ?
		    &active->constants_bytes : &active->input_bytes;
		(*count)--;
		CHECK(sun60i_a733_npu_dma_task_validate(active, ref) ==
		    EINVAL);
		(*count)++;
	}
	ref->mtlb[0] ^= 1;
	CHECK(sun60i_a733_npu_dma_task_validate(active, ref) == EIO);
	ref->mtlb[0] ^= 1;
	((uint8_t *)slots[5].cpu)[4095] = 0;
	CHECK(sun60i_a733_npu_dma_task_retain_checked(active, run, ref) ==
	    EIO && !syncs);
	((uint8_t *)slots[5].cpu)[4095] = 0xa5;
	CHECK(sun60i_a733_npu_dma_task_retain_checked(active, NULL, ref) ==
	    EINVAL && !syncs);
	CHECK(sun60i_a733_npu_dma_task_retain_checked(active, run, ref) == 0);
	completion_allowed = true;
	memset(&report, 0x5a, sizeof(report));
	saved = report;
	CHECK(sun60i_a733_npu_dma_task_check(active, run, ref, NULL) ==
	    EINVAL);
	CHECK(sun60i_a733_npu_dma_task_check(active, run, ref,
	    (void *)(UINTPTR_MAX - 7)) == EINVAL);
	CHECK(sun60i_a733_npu_dma_task_check(active, run, ref,
	    (void *)active) == EINVAL);
	CHECK(sun60i_a733_npu_dma_task_check(active, run, ref, (void *)ref) ==
	    EINVAL);
	CHECK(sun60i_a733_npu_dma_task_check(active, run, ref, (void *)run) ==
	    EINVAL);
	for (unsigned i = 0; i < 6; i++)
		CHECK(sun60i_a733_npu_dma_task_check(active, run, ref,
		    slots[i].cpu) == EINVAL);
	CHECK(syncs == 6 && memcmp(&report, &saved, sizeof(report)) == 0);
	checked_cleanup(ref);
}

int main(void)
{
	struct sun60i_a733_npu_dma *out = (void *)(uintptr_t)1;
	struct sun60i_a733_npu_dma_info info, saved;
	for (size_t i = 0; i < sizeof(test_commands); i++) test_commands[i] = (uint8_t)(i ^ 0xa6);
	reset();
	CHECK(sun60i_a733_npu_dma_prepare(NULL, test_commands, 16, &out) == EINVAL);
	CHECK(sun60i_a733_npu_dma_prepare(&parent_tag, NULL, 16, &out) == EINVAL);
	CHECK(sun60i_a733_npu_dma_prepare(&parent_tag, test_commands, 16, NULL) == EINVAL);
	const size_t bad_sizes[] = { 0, 7, 9, 4097, SIZE_MAX };
	for (size_t i = 0; i < 5; i++)
		CHECK(sun60i_a733_npu_dma_prepare(&parent_tag, test_commands, bad_sizes[i], &out) == EINVAL);
	CHECK(sun60i_a733_npu_dma_prepare(&parent_tag,
	    (void *)(UINTPTR_MAX - 7), 16, &out) == EINVAL);
	CHECK(out == (void *)(uintptr_t)1 && calls == 0 && depth == 0);
	for (unsigned i = 1; i <= 13; i++) { fail_call = i; reject(EIO); }
	for (unsigned b = 0; b < 3; b++) for (unsigned kind = 1; kind <= 6; kind++) {
		bad_at = b; bad_kind = kind; reject(kind == 1 ? EOPNOTSUPP : EINVAL);
	}
	const uint64_t bad_addresses[] = { 1, 0x100000001, 0x100000000,
	    UINT64_MAX, 0xfffff000 };
	for (unsigned b = 0; b < 3; b++) for (unsigned i = 0; i < 5; i++) {
		/* A single 4 KiB last page is valid; the 16 KiB STLB cannot fit. */
		if (i == 4 && b != 1) continue;
		addresses[b] = bad_addresses[i]; reject(ERANGE);
	}
	addresses[1] = addresses[0]; reject(EEXIST);
	addresses[2] = addresses[0]; reject(EEXIST);
	addresses[2] = addresses[1] + 0x3000; reject(EEXIST);
	memset(&info, 0xa5, sizeof(info)); saved = info;
	CHECK(sun60i_a733_npu_dma_info(NULL, &info) == EINVAL);
	CHECK(memcmp(&info, &saved, sizeof(info)) == 0);
	CHECK(sun60i_a733_npu_dma_retain(NULL) == EINVAL);
	CHECK(sun60i_a733_npu_dma_destroy(NULL) == EINVAL);
	replacement();
	success(false, 8, false); success(true, 16, false);
	success(false, 4096, true); success(true, 16, true);
	task_contract();
	checked_contract();
	printf("A733 NPU DMA32: %u checks passed\n", checks); return 0;
}
