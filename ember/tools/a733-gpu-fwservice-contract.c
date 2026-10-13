/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted real-source tests. */
#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sun60i_a733_gpu_fwimage.h"
#include "sun60i_a733_gpu_fwinit.h"
#include "sun60i_a733_gpu_mips.h"
#include "powervr/pvr_rogue_fwif.h"

#define roundup(x, y) ((((x) + (y) - 1) / (y)) * (y))
#define KM_SLEEP 0
#define BUS_DMA_WAITOK 0
#define BUS_DMA_COHERENT 1
#define BUS_DMASYNC_PREREAD 1
#define BUS_DMASYNC_POSTREAD 2
#define BUS_DMASYNC_PREWRITE 4
#define BUS_DMASYNC_POSTWRITE 8
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
	unsigned int id, pending;
	bool loaded, owned;
} *bus_dmamap_t;
typedef void *bus_dma_tag_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
#include "mem.h"
#include "start.h"
#include "boot.h"
#include "probe.h"
#include "service.h"

static unsigned int checks;
#define CHECK(e) do { checks++; if (!(e)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); \
} } while (0)
#define KASSERT(e) CHECK(e)

static struct sun60i_a733_gpu_fwservice service;
static struct sun60i_a733_gpu_fwmem memory;
static struct sun60i_a733_gpu_fwboot_result boot;
static struct sun60i_a733_gpu_fwprobe_result predecessor;
static struct fake_map table_map;
static uint8_t table[16384], saved_table[16384];
struct allocation { void *cpu; uint64_t pa; size_t size; bool live; };
static struct allocation allocations[64];
static unsigned int allocated, released, map_creates, map_destroys;
static unsigned int heap_allocs, heap_frees, syncs, publications, acknowledgements;
static unsigned int commands, allocation_step, failure_step;
static uint64_t next_pa, force_pa;
static int transport_error, attach_error;
static bool transport_freezes, quiescent, table_edit;

static void *kmem_zalloc(size_t, int);
static void kmem_free(void *, size_t);
static int bus_dmamem_alloc(bus_dma_tag_t, size_t, size_t, size_t,
    bus_dma_segment_t *, int, int *, int);
static int bus_dmamem_map(bus_dma_tag_t, bus_dma_segment_t *, int, size_t,
    void **, int);
static int bus_dmamap_create(bus_dma_tag_t, size_t, int, size_t, size_t,
    int, bus_dmamap_t *);
static int bus_dmamap_load(bus_dma_tag_t, bus_dmamap_t, void *, size_t,
    void *, int);
static void bus_dmamap_unload(bus_dma_tag_t, bus_dmamap_t);
static void bus_dmamap_destroy(bus_dma_tag_t, bus_dmamap_t);
static void bus_dmamem_unmap(bus_dma_tag_t, void *, size_t);
static void bus_dmamem_free(bus_dma_tag_t, bus_dma_segment_t *, int);
static void bus_dmamap_sync(bus_dma_tag_t, bus_dmamap_t, size_t, size_t, int);
static uint32_t le32dec(const void *);
#include "dma-driver.h"
#include "service-driver.h"

static uint32_t
le32dec(const void *p)
{
	const uint8_t *b = p;
	return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
	    ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static void
put32(size_t offset, uint32_t value)
{
	CHECK(offset <= sizeof(table) - 4);
	for (unsigned int i = 0; i < 4; i++)
		table[offset + i] = (uint8_t)(value >> (i * 8));
}

static size_t
pte_offset(uint32_t address)
{
	CHECK(address >= UINT32_C(0xc0000000) && address < UINT32_C(0xc1000000));
	return (address - UINT32_C(0xc0000000)) / 4096 * 4;
}

static void *
kmem_zalloc(size_t size, int flags)
{
	void *p = calloc(1, size);
	CHECK(flags == KM_SLEEP && p != NULL);
	heap_allocs++;
	return p;
}

static void
kmem_free(void *p, size_t size)
{
	CHECK(p != NULL && size == sizeof(struct sun60i_a733_gpu_fwobject));
	heap_frees++;
	free(p);
}

static bool
fail_step(void)
{
	allocation_step++;
	return allocation_step == failure_step;
}

static int
bus_dmamem_alloc(bus_dma_tag_t tag, size_t size, size_t align, size_t boundary,
    bus_dma_segment_t *segment, int maxsegs, int *nsegments, int flags)
{
	void *p;
	CHECK(tag == &memory && align == 4096 && boundary == 0 && maxsegs == 1);
	CHECK(flags == BUS_DMA_WAITOK && allocated < 64);
	if (fail_step())
		return ENOMEM;
	CHECK(posix_memalign(&p, 4096, size) == 0);
	memset(p, 0xa5, size);
	allocations[allocated] = (struct allocation){ p,
	    force_pa != 0 ? force_pa : next_pa, size, true };
	next_pa += UINT64_C(0x100000);
	segment->ds_addr = allocations[allocated].pa;
	segment->ds_len = size;
	*nsegments = 1;
	allocated++;
	return 0;
}

static struct allocation *
find_pa(uint64_t pa)
{
	for (unsigned int i = 0; i < allocated; i++)
		if (allocations[i].live && allocations[i].pa == pa)
			return &allocations[i];
	CHECK(false);
	return NULL;
}

static int
bus_dmamem_map(bus_dma_tag_t tag, bus_dma_segment_t *segment, int nsegments,
    size_t size, void **out, int flags)
{
	CHECK(tag == &memory && nsegments == 1 && flags == BUS_DMA_COHERENT);
	if (fail_step())
		return ENOMEM;
	struct allocation *a = find_pa(segment->ds_addr);
	CHECK(size == a->size);
	*out = a->cpu;
	return 0;
}

static int
bus_dmamap_create(bus_dma_tag_t tag, size_t size, int nsegments,
    size_t maxsegsz, size_t boundary, int flags, bus_dmamap_t *out)
{
	CHECK(tag == &memory && nsegments == 1 && maxsegsz == size);
	CHECK(boundary == 0 && flags == BUS_DMA_WAITOK);
	if (fail_step())
		return ENOMEM;
	*out = calloc(1, sizeof(**out));
	CHECK(*out != NULL);
	map_creates++;
	return 0;
}

static int
bus_dmamap_load(bus_dma_tag_t tag, bus_dmamap_t map, void *cpu, size_t size,
    void *process, int flags)
{
	CHECK(tag == &memory && process == NULL && flags == BUS_DMA_WAITOK);
	if (fail_step())
		return ENOMEM;
	for (unsigned int i = 0; i < allocated; i++) {
		if (!allocations[i].live || allocations[i].cpu != cpu)
			continue;
		CHECK(size == allocations[i].size);
		map->id = i + 1;
		map->dm_mapsize = size;
		map->dm_nsegs = failure_step == 6 ? 2 : 1;
		map->dm_segs[0] = (bus_dma_segment_t){ allocations[i].pa, size };
		map->_dm_flags = failure_step == 5 ? _BUS_DMAMAP_IS_BOUNCING :
		    _BUS_DMAMAP_COHERENT;
		map->loaded = true;
		return 0;
	}
	CHECK(false);
	return EINVAL;
}

static void
bus_dmamap_unload(bus_dma_tag_t tag, bus_dmamap_t map)
{
	CHECK(tag == &memory && map->loaded);
	if (!quiescent) {
		CHECK(map->pending == 0);
		if (map->owned)
			CHECK(acknowledgements == publications);
	}
	map->loaded = false;
}

static void
bus_dmamap_destroy(bus_dma_tag_t tag, bus_dmamap_t map)
{
	CHECK(tag == &memory && !map->loaded);
	map_destroys++;
	free(map);
}

static void
bus_dmamem_unmap(bus_dma_tag_t tag, void *cpu, size_t size)
{
	CHECK(tag == &memory && cpu != NULL && size > 0);
}

static void
bus_dmamem_free(bus_dma_tag_t tag, bus_dma_segment_t *segment, int nsegments)
{
	struct allocation *a = find_pa(segment->ds_addr);
	CHECK(tag == &memory && nsegments == 1 && a->live);
	a->live = false;
	released++;
	free(a->cpu);
}

static void
bus_dmamap_sync(bus_dma_tag_t tag, bus_dmamap_t map, size_t offset,
    size_t size, int operations)
{
	CHECK(tag == &memory && offset == 0 && size == map->dm_mapsize);
	syncs++;
	if (map == &table_map) {
		if (operations == BUS_DMASYNC_POSTWRITE) {
			table_edit = true;
			return;
		}
		CHECK(operations == BUS_DMASYNC_PREWRITE && table_edit);
		table_edit = false;
		publications++;
		CHECK(service.objects != NULL);
		for (struct sun60i_a733_gpu_fwobject *o = service.objects;
		    o != NULL; o = o->next) {
			if (o->mapped) {
				CHECK(o->dma.map->pending == (BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE));
				o->dma.map->owned = true;
			}
		}
		return;
	}
	CHECK(map->loaded && map->id != 0);
	if ((operations & BUS_DMASYNC_POSTREAD) != 0)
		map->pending &= ~BUS_DMASYNC_PREREAD;
	if ((operations & BUS_DMASYNC_POSTWRITE) != 0)
		map->pending &= ~BUS_DMASYNC_PREWRITE;
	if ((operations & (BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE)) != 0 && map->owned)
		CHECK(quiescent || acknowledgements == publications);
	map->pending |= operations & (BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

int
sun60i_a733_gpu_fwqueue_attach(struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *b,
    const struct sun60i_a733_gpu_fwprobe_result *prev,
    struct sun60i_a733_gpu_fwqueue *q)
{
	CHECK(mem == &memory && b == &boot && prev == &predecessor);
	CHECK(q == &service.queue && !q->attached);
	if (attach_error != 0)
		return attach_error;
	q->attached = true;
	q->expected_log_type = 0x3f;
	return 0;
}

int
sun60i_a733_gpu_fwqueue_cmd(bus_space_tag_t tag, bus_space_handle_t handle,
    struct sun60i_a733_gpu_fwmem *mem, const struct sun60i_a733_gpu_fwboot_result *b,
    struct sun60i_a733_gpu_fwqueue *q, const struct rogue_fwif_kccb_cmd *cmd,
    uint32_t log_type, struct sun60i_a733_gpu_fwprobe_result *out)
{
	CHECK(tag == &memory && handle == 0x12340000 && mem == &memory && b == &boot);
	CHECK(q == &service.queue && q->attached && !q->failed && log_type == 0x3f);
	CHECK(cmd->cmd_type == 0x2abc0066 && cmd->kccb_flags == 0);
	CHECK(cmd->cmd_data.mmu_cache_data.cache_flags == 0x0400001f);
	CHECK(cmd->cmd_data.mmu_cache_data.mmu_cache_sync_fw_addr == 0xc0104400);
	CHECK(cmd->cmd_data.mmu_cache_data.mmu_cache_sync_update_value == 0);
	CHECK(!table_edit && publications > acknowledgements);
	commands++;
	memset(out, 0, sizeof(*out));
	out->error = transport_error;
	if (transport_error != 0) {
		if (transport_freezes)
			q->failed = true;
		return transport_error;
	}
	acknowledgements = publications;
	return 0;
}

int
sun60i_a733_gpu_fwqueue_observe(struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *b,
    struct sun60i_a733_gpu_fwqueue *q,
    struct sun60i_a733_gpu_fwprobe_observation *out)
{
	CHECK(mem == &memory && b == &boot && q == &service.queue);
	memset(out, 0, sizeof(*out));
	out->valid = true;
	return 0;
}

static void
finish(void)
{
	/* Only this fixture may simulate a fully stopped device. */
	quiescent = true;
	while (service.objects != NULL) {
		struct sun60i_a733_gpu_fwobject *obj = service.objects;
		service.objects = obj->next;
		sun60i_a733_gpu_dma_free(&memory, &obj->dma);
		kmem_free(obj, sizeof(*obj));
	}
	CHECK(allocated == released && map_creates == map_destroys);
	CHECK(heap_allocs == heap_frees);
}

static void
reset(void)
{
	memset(&service, 0, sizeof(service));
	memset(&memory, 0, sizeof(memory));
	memset(&boot, 0, sizeof(boot));
	memset(&predecessor, 0, sizeof(predecessor));
	memset(&table_map, 0, sizeof(table_map));
	memset(table, 0, sizeof(table));
	memset(allocations, 0, sizeof(allocations));
	memory.dmat = &memory;
	memory.table.cpu = table;
	memory.table.map = &table_map;
	memory.table.size = sizeof(table);
	table_map.dm_mapsize = sizeof(table);
	memory.interface_layout.object[A733_GPU_FWINIT_MMUCACHE_SYNC].fw_addr = 0xc0104400;
	allocated = released = map_creates = map_destroys = 0;
	heap_allocs = heap_frees = syncs = publications = acknowledgements = 0;
	commands = failure_step = allocation_step = 0;
	next_pa = UINT64_C(0x140000000);
	force_pa = 0;
	transport_error = attach_error = 0;
	transport_freezes = true;
	quiescent = table_edit = false;
}

static void
attach(void)
{
	CHECK(sun60i_a733_gpu_fwservice_attach(&service, &memory, 0x12340000,
	    &memory, &boot, &predecessor) == 0);
	CHECK(service.memory == &memory && service.boot == &boot);
}

static void
basic_tests(void)
{
	struct sun60i_a733_gpu_fwobject *a = NULL, *b = NULL;
	struct sun60i_a733_gpu_fwobject foreign = { 0 }, *f = &foreign;

	reset(); attach();
	CHECK(sun60i_a733_gpu_fwservice_attach(&service, &memory, 0x12340000,
	    &memory, &boot, &predecessor) == EBUSY);
	CHECK(sun60i_a733_gpu_fwservice_alloc(&service, 4096, &service.objects) == EINVAL);
	CHECK(service.objects == NULL && allocated == 0);
	CHECK(sun60i_a733_gpu_fwservice_alloc(&service, 5000, &a) == 0);
	CHECK(sun60i_a733_gpu_fwservice_alloc(&service, 4096, &a->next) == EINVAL);
	CHECK(a->next == NULL && allocated == 1);
	CHECK(a != NULL && a->mapped && a->size == 5000 && a->dma.size == 8192);
	CHECK(a->fw_addr == 0xc0200000 && a->dma.address == UINT64_C(0x140000000));
	CHECK(le32dec(table + 512 * 4) == (UINT64_C(0x140000000) >> 6 | 0x17));
	CHECK(publications == 1 && commands == 1 && acknowledgements == 1);
	CHECK(sun60i_a733_gpu_fwservice_alloc(&service, 4096, &b) == 0);
	CHECK(b->fw_addr == 0xc0202000 && b->next == a);
	CHECK(sun60i_a733_gpu_fwservice_free(&service, &service.objects) == EINVAL);
	CHECK(sun60i_a733_gpu_fwservice_free(&service, &b->next) == EINVAL);
	CHECK(service.objects == b && b->next == a && publications == 2 && released == 0);
	CHECK(sun60i_a733_gpu_fwservice_free(&service, &f) == ENOENT && f == &foreign);
	CHECK(sun60i_a733_gpu_fwservice_free(&service, &a) == 0 && a == NULL);
	CHECK(service.objects == b && b->next == NULL);
	CHECK(le32dec(table + 512 * 4) == 0 && le32dec(table + 513 * 4) == 0);
	CHECK(sun60i_a733_gpu_fwservice_alloc(&service, 1, &a) == 0);
	CHECK(a->fw_addr == 0xc0200000);
	CHECK(sun60i_a733_gpu_fwservice_free(&service, &b) == 0 && b == NULL);
	CHECK(sun60i_a733_gpu_fwservice_free(&service, &a) == 0 && a == NULL);
	CHECK(service.objects == NULL && released == 3 && commands == 6);
	finish();

	reset(); attach();
	/* An existing diagnostic PTE reserves the first candidate page. */
	put32(512 * 4, (UINT64_C(0x150000000) >> 6) | 0x17);
	CHECK(sun60i_a733_gpu_fwservice_alloc(&service, 4096, &a) == 0);
	CHECK(a->fw_addr == 0xc0201000);
	CHECK(sun60i_a733_gpu_fwservice_free(&service, &a) == 0);
	CHECK(le32dec(table + 512 * 4) == ((UINT64_C(0x150000000) >> 6) | 0x17));
	finish();
}

static void
failure_tests(void)
{
	struct sun60i_a733_gpu_fwobject *obj = NULL, *other = NULL;
	unsigned int old_syncs, old_commands;

	reset();
	CHECK(sun60i_a733_gpu_fwservice_alloc(&service, 4096, &obj) == EIO);
	attach_error = EINVAL;
	CHECK(sun60i_a733_gpu_fwservice_attach(&service, &memory, 0x12340000,
	    &memory, &boot, &predecessor) == EINVAL);
	CHECK(service.memory == NULL); finish();
	reset(); attach();
	CHECK(sun60i_a733_gpu_fwservice_alloc(&service, 0, &obj) == EINVAL);
	CHECK(sun60i_a733_gpu_fwservice_alloc(&service, 262145, &obj) == EINVAL);
	CHECK(sun60i_a733_gpu_fwservice_alloc(&service, SIZE_MAX, &obj) == EINVAL);
	CHECK(sun60i_a733_gpu_fwservice_alloc(&service, 4096, NULL) == EINVAL);
	CHECK(sun60i_a733_gpu_fwservice_free(&service, &obj) == EINVAL);
	CHECK(sun60i_a733_gpu_fwservice_free(&service, NULL) == EINVAL);
	CHECK(allocated == 0 && syncs == 0); finish();
	reset(); attach();
	CHECK(sun60i_a733_gpu_fwservice_alloc(&service, 262144, &obj) == 0);
	CHECK(obj->dma.size == 262144 && le32dec(table + 575 * 4) != 0);
	CHECK(sun60i_a733_gpu_fwservice_free(&service, &obj) == 0 && obj == NULL);
	CHECK(le32dec(table + 575 * 4) == 0); finish();
	for (unsigned int step = 1; step <= 6; step++) {
		reset(); attach(); failure_step = step;
		CHECK(sun60i_a733_gpu_fwservice_alloc(&service, 4096, &obj) != 0);
		CHECK(obj == NULL && service.objects == NULL && !service.queue.failed);
		CHECK(publications == 0 && commands == 0); finish();
	}
	/* map() failure occurs before publication and keeps the old table. */
	reset(); attach(); put32(0, 8); memcpy(saved_table, table, sizeof(table));
	CHECK(sun60i_a733_gpu_fwservice_alloc(&service, 4096, &obj) == EINVAL);
	CHECK(memcmp(saved_table, table, sizeof(table)) == 0);
	CHECK(service.objects == NULL && !service.queue.failed && publications == 0);
	finish();
	/* The allocator must not alias an already mapped physical page. */
	reset(); attach(); put32(0, (UINT64_C(0x150000000) >> 6) | 0x17);
	force_pa = UINT64_C(0x150000000);
	memcpy(saved_table, table, sizeof(table));
	CHECK(sun60i_a733_gpu_fwservice_alloc(&service, 4096, &obj) == EEXIST);
	CHECK(memcmp(saved_table, table, sizeof(table)) == 0); finish();
	/* Every mapped candidate remains reserved, including the last arena page. */
	reset(); attach();
	for (size_t off = 512 * 4; off < pte_offset(FWSERVICE_END); off += 4)
		put32(off, 0x17);
	CHECK(sun60i_a733_gpu_fwservice_alloc(&service, 4096, &obj) == ENOSPC);
	CHECK(allocated == 0 && commands == 0); finish();

	/* Failed invalidation retains the DMA and freezes even on preflight error. */
	for (unsigned int kind = 0; kind < 2; kind++) {
		reset(); attach(); transport_error = kind == 0 ? ETIMEDOUT : EINVAL;
		transport_freezes = kind == 0;
		CHECK(sun60i_a733_gpu_fwservice_alloc(&service, 4096, &obj) == transport_error);
		CHECK(obj == NULL && service.objects != NULL && service.objects->mapped);
		CHECK(service.queue.failed && released == 0 && publications == 1);
		CHECK(acknowledgements == 0 && service.objects->dma.loaded);
		old_syncs = syncs; old_commands = commands;
		CHECK(sun60i_a733_gpu_fwservice_alloc(&service, 4096, &other) == EIO);
		CHECK(syncs == old_syncs && commands == old_commands); finish();
	}
	for (unsigned int kind = 0; kind < 2; kind++) {
		reset(); attach();
		CHECK(sun60i_a733_gpu_fwservice_alloc(&service, 4096, &obj) == 0);
		transport_error = kind == 0 ? ETIMEDOUT : EINVAL;
		transport_freezes = kind == 0;
		CHECK(sun60i_a733_gpu_fwservice_free(&service, &obj) == transport_error);
		CHECK(obj != NULL && !obj->mapped && obj->dma.loaded && released == 0);
		CHECK(service.queue.failed && le32dec(table + 512 * 4) == 0);
		old_syncs = syncs; old_commands = commands;
		CHECK(sun60i_a733_gpu_fwservice_free(&service, &obj) == EIO);
		CHECK(syncs == old_syncs && commands == old_commands); finish(); obj = NULL;
	}
	/* Corrupt internal sizes must fail before the fixed page array is filled. */
	for (unsigned int i = 0; i < 6; i++) {
		static const size_t invalid_sizes[] = { 0, 1, 4095, 4097, 262145, 266240 };
		reset(); attach();
		CHECK(sun60i_a733_gpu_fwservice_alloc(&service, 4096, &obj) == 0);
		obj->dma.size = invalid_sizes[i];
		old_syncs = syncs; old_commands = commands;
		memcpy(saved_table, table, sizeof(table));
		CHECK(sun60i_a733_gpu_fwservice_free(&service, &obj) == EINVAL);
		CHECK(service.queue.failed && obj != NULL && obj->mapped && released == 0);
		CHECK(syncs == old_syncs && commands == old_commands);
		CHECK(memcmp(saved_table, table, sizeof(table)) == 0);
		/* Restore fixture metadata only after its modeled device is stopped. */
		quiescent = true;
		obj->dma.size = 4096;
		finish(); obj = NULL;
	}
	/* A mismatched unmap does not clear any PTE or release any memory. */

	reset(); attach();
	CHECK(sun60i_a733_gpu_fwservice_alloc(&service, 8192, &obj) == 0);
	put32(513 * 4, (UINT64_C(0x150000000) >> 6) | 0x17);
	memcpy(saved_table, table, sizeof(table));
	CHECK(sun60i_a733_gpu_fwservice_free(&service, &obj) == ENOENT);
	CHECK(service.queue.failed && released == 0 && obj->mapped);
	CHECK(memcmp(saved_table, table, sizeof(table)) == 0); finish();
}

int
main(void)
{
	basic_tests();
	failure_tests();
	printf("a733-gpu-fwservice-contract: PASS %u checks\n", checks);
	return 0;
}
