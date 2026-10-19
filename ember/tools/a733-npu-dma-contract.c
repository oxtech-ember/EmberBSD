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
#define KM_SLEEP 1
#define _BUS_DMAMAP_IS_BOUNCING 0x20000
static unsigned checks;
#define CHECK(c) do { checks++; if (!(c)) { \
	fprintf(stderr, "check %u failed at line %d: %s\n", \
	    checks, __LINE__, #c); exit(1); } } while (0)

static struct fake_tag parent_tag = { 0, true };
static struct slot { void *cpu; size_t bytes; uint64_t address; } slots[3];
static unsigned stack[32], depth, calls, fail_call, allocated, current, syncs;
static bool borrowed;
static unsigned bad_at, bad_kind;
static uint64_t addresses[3];
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
	CHECK(allocated < 3); current = allocated++;
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
	for (id = 0; id < 3; id++) if (slots[id].cpu == cpu) break;
	CHECK(id < 3 && n == slots[id].bytes); pop(11 + id * 4); free(cpu);
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
	CHECK(map->id == syncs && offset == 0 && n == slots[map->id].bytes);
	CHECK(flags == BUS_DMASYNC_PREWRITE); syncs++;
}
#include "sun60i_a733_npu_mmu.h"
#include "sun60i_a733_npu_mmu.c"
#include "sun60i_a733_npu_dma.h"
#include "sun60i_a733_npu_dma.c"
static struct sun60i_a733_npu_dma *active;
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
	bad_at = bad_kind = 0; borrowed = false; active = NULL;
	addresses[0] = 0x100000; addresses[1] = 0x110000; addresses[2] = 0x120000;
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
	CHECK(!info.retained);
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
	success(false, 8, false); success(true, 16, false);
	success(false, 4096, true); success(true, 16, true);
	printf("A733 NPU DMA32: %u checks passed\n", checks); return 0;
}
