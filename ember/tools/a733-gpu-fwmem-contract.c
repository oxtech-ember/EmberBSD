/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted native owner checks. */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sun60i_a733_gpu_fwimage.h"
#include "sun60i_a733_gpu_fwinit.h"
#include "powervr/pvr_rogue_fwif.h"
#include "sun60i_a733_gpu_mips.h"

#define CHECK(c) do { \
	checks++; \
	if (!(c)) { \
		fprintf(stderr, "%d: %s\n", __LINE__, #c); \
		exit(1); \
	} \
} while (0)
#define KASSERT(c) CHECK(c)
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#define BUS_DMA_WAITOK 0
#define KM_SLEEP 0
#define BVNC UINT64_C(0x00240038006800b7)
#define LAYOUT(n, field) (8240 + (n) * 24 + (field))
#define PAGE_SIZE 4096
#define DMA_BASE UINT64_C(0x40000000)

typedef void *bus_dma_tag_t;
typedef uint64_t bus_addr_t;
typedef struct {
	bus_addr_t ds_addr;
	size_t ds_len;
	void *memory;
} bus_dma_segment_t;
typedef struct {
	unsigned int dm_nsegs;
	bus_dma_segment_t dm_segs[1];
	bool loaded;
} *bus_dmamap_t;

enum fault {
	FAULT_NONE, FAULT_NSEGS, FAULT_LENGTH, FAULT_ALIGNMENT, FAULT_WIDTH,
	FAULT_REMAP_WINDOW, FAULT_CODE_DATA_ALIAS, FAULT_CODE_TABLE_ALIAS,
	FAULT_DATA_TABLE_ALIAS, FAULT_PARTIAL_ALIAS, FAULT_INTERFACE_ADDRESS
};

static unsigned int checks, operations, fail_at;
static unsigned int live_allocs, live_maps, live_kmem;
static unsigned int mapped, loaded, unloaded;
static enum fault fault_kind;
static uint64_t next_address, data_address, interface_address;
static unsigned int interface_load;
static int test_dmat;

static void *
kmem_alloc(size_t n, int flags)
{
	void *p;

	CHECK(flags == KM_SLEEP);
	p = malloc(n);
	CHECK(p != NULL);
	live_kmem++;
	return p;
}

static void *
kmem_zalloc(size_t n, int flags)
{
	void *p;

	CHECK(flags == KM_SLEEP);
	p = calloc(1, n);
	CHECK(p != NULL);
	live_kmem++;
	return p;
}

static void
kmem_free(void *p, size_t n)
{
	CHECK(p != NULL && n != 0 && live_kmem != 0);
	live_kmem--;
	free(p);
}

static bool
fail(void)
{
	return ++operations == fail_at;
}

static int
bus_dmamem_alloc(bus_dma_tag_t tag, size_t n, size_t alignment,
    size_t boundary, bus_dma_segment_t *seg, int max, int *count, int flags)
{
	CHECK(tag == &test_dmat && alignment == PAGE_SIZE && boundary == 0);
	CHECK(max == 1 && flags == BUS_DMA_WAITOK);
	if (fail())
		return ENOMEM;
	seg->memory = aligned_alloc(alignment, n);
	CHECK(seg->memory != NULL);
	seg->ds_len = n;
	seg->ds_addr = next_address;
	next_address += n;
	*count = 1;
	live_allocs++;
	return 0;
}

static int
bus_dmamem_map(bus_dma_tag_t tag, bus_dma_segment_t *seg, int count,
    size_t n, void **cpu, int flags)
{
	CHECK(tag == &test_dmat && count == 1 && n == seg->ds_len);
	CHECK(flags == BUS_DMA_WAITOK);
	if (fail())
		return ENOMEM;
	*cpu = seg->memory;
	mapped++;
	return 0;
}

static int
bus_dmamap_create(bus_dma_tag_t tag, size_t n, int max, size_t maxseg,
    size_t boundary, int flags, bus_dmamap_t *map)
{
	CHECK(tag == &test_dmat && max == 1 && n == maxseg && boundary == 0);
	CHECK(flags == BUS_DMA_WAITOK);
	if (fail())
		return ENOMEM;
	*map = calloc(1, sizeof(**map));
	CHECK(*map != NULL);
	live_maps++;
	return 0;
}

/* Keep the real bus_dma hook signature, including mutable pointer types. */
// cppcheck-suppress-begin constParameterPointer
static int
bus_dmamap_load(bus_dma_tag_t tag, bus_dmamap_t map, void *cpu,
    size_t n, void *proc, int flags)
{
	CHECK(tag == &test_dmat && cpu != NULL && proc == NULL);
	CHECK(flags == BUS_DMA_WAITOK);
	if (fail())
		return EIO;
	map->dm_nsegs = 1;
	map->dm_segs[0].ds_len = n;
	map->dm_segs[0].ds_addr = next_address - n;
	if (loaded == 1)
		data_address = map->dm_segs[0].ds_addr;
	/* These faults model malformed backend output, not bus_dma behavior. */
	switch (fault_kind) {
	case FAULT_NSEGS:
		map->dm_nsegs = 2;
		break;
	case FAULT_LENGTH:
		map->dm_segs[0].ds_len--;
		break;
	case FAULT_ALIGNMENT:
		map->dm_segs[0].ds_addr++;
		break;
	case FAULT_WIDTH:
		map->dm_segs[0].ds_addr = UINT64_C(1) << 36;
		break;
	case FAULT_REMAP_WINDOW:
		if (loaded == 0)
			map->dm_segs[0].ds_addr = UINT64_C(0x1fc00000);
		break;
	case FAULT_CODE_DATA_ALIAS:
		if (loaded == 1)
			map->dm_segs[0].ds_addr = DMA_BASE;
		break;
	case FAULT_CODE_TABLE_ALIAS:
		if (loaded == 2)
			map->dm_segs[0].ds_addr = DMA_BASE + PAGE_SIZE;
		break;
	case FAULT_DATA_TABLE_ALIAS:
		if (loaded == 2)
			map->dm_segs[0].ds_addr = data_address;
		break;
	case FAULT_PARTIAL_ALIAS:
		if (loaded == 1)
			map->dm_segs[0].ds_addr = DMA_BASE - PAGE_SIZE;
		break;
	case FAULT_INTERFACE_ADDRESS:
		if (loaded == interface_load)
			map->dm_segs[0].ds_addr = interface_address;
		break;
	case FAULT_NONE:
		break;
	}
	map->loaded = true;
	loaded++;
	return 0;
}

// cppcheck-suppress-end constParameterPointer

static void
bus_dmamap_unload(bus_dma_tag_t tag, bus_dmamap_t map)
{
	CHECK(tag == &test_dmat && map->loaded);
	map->loaded = false;
	loaded--;
	unloaded++;
}

static void
bus_dmamap_destroy(bus_dma_tag_t tag, bus_dmamap_t map)
{
	CHECK(tag == &test_dmat && !map->loaded && live_maps != 0);
	live_maps--;
	free(map);
}

/* Keep the real bus_dma hook signature; this stub only records unmapping. */
// cppcheck-suppress-begin constParameterPointer
static void
bus_dmamem_unmap(bus_dma_tag_t tag, void *cpu, size_t size)
{
	CHECK(tag == &test_dmat && cpu != NULL && size != 0 && mapped != 0);
	mapped--;
}

// cppcheck-suppress-end constParameterPointer

static void
bus_dmamem_free(bus_dma_tag_t tag, bus_dma_segment_t *seg, int n)
{
	CHECK(tag == &test_dmat && n == 1 && live_allocs != 0);
	live_allocs--;
	free(seg->memory);
}

/* The shell harness removes only kernel includes and __KERNEL_RCSID. */
#include "memory.h"
#include "driver.h"

static uint8_t blob[12288];

static void
put32(size_t off, uint32_t v)
{
	for (unsigned int i = 0; i < 4; i++)
		blob[off + i] = (uint8_t)(v >> (8 * i));
}

static void
put64(size_t off, uint64_t v)
{
	put32(off, (uint32_t)v);
	put32(off + 4, (uint32_t)(v >> 32));
}

static uint64_t
getle(const void *p, unsigned int n)
{
	uint64_t v = 0;
	const uint8_t *s = p;

	for (unsigned int i = 0; i < n; i++)
		v |= (uint64_t)s[i] << (8 * i);
	return v;
}

static void
fixture(void)
{
	static const uint32_t va[] = {
		0xc0000000, 0x9fc02000, 0xbfc00000,
		0xc0032000, 0xbfc01000, 0xcf600000
	};

	memset(blob, 0, sizeof(blob));
	memcpy(blob, "\177ELF\1\1\1", 7);
	blob[16] = 2;
	blob[18] = 8;
	put32(20, 1);
	put32(24, 0xbfc00001);
	put32(28, 52);
	blob[40] = 52;
	blob[42] = 32;
	blob[44] = 1;
	put32(52, 1);
	put32(56, 256);
	put32(60, va[2]);
	put32(64, va[2]);
	put32(68, 4);
	put32(72, 8);
	put32(76, 5);
	put32(80, 4);
	put32(256, 0x76543210);
	put32(8192, 3);
	put32(8196, 48);
	put32(8200, 6);
	put32(8204, 24);
	put64(8208, BVNC);
	put32(8216, PAGE_SIZE);
	put32(8220, 1);
	put32(8224, 0x10001);
	put32(8232, PAGE_SIZE);
	for (unsigned int i = 0; i < 6; i++) {
		put32(LAYOUT(i, 0), 4 + i);
		put32(LAYOUT(i, 4), i < 3 ? 1 : 2);
		put32(LAYOUT(i, 8), va[i]);
		put32(LAYOUT(i, 12), PAGE_SIZE);
		put32(LAYOUT(i, 16), PAGE_SIZE);
		put32(LAYOUT(i, 20), (i % 3) * PAGE_SIZE);
	}
	put64(4096 + 16, 1);
	put64(4096 + 24, 1);
	put64(4096 + 32, (UINT64_C(1) << 27) | (UINT64_C(1) << 36));
	put64(4096 + 40, 36);
}

static void
reset(unsigned int failop, enum fault kind)
{
	CHECK(live_allocs == 0 && live_maps == 0 && live_kmem == 0);
	CHECK(mapped == 0 && loaded == 0);
	operations = 0;
	unloaded = 0;
	fail_at = failop;
	fault_kind = kind;
	next_address = DMA_BASE;
	data_address = 0;
}

static void
run(const void *input, size_t size, int wanted)
{
	/* A non-NULL sentinel detects accidental clearing on failure. */
	struct sun60i_a733_gpu_fwmem sentinel;
	struct sun60i_a733_gpu_fwmem *mem = &sentinel;
	uint8_t *saved;
	int error;

	saved = malloc(size);
	CHECK(saved != NULL);
	memcpy(saved, input, size);
	error = sun60i_a733_gpu_fwmem_prepare(&test_dmat, input, size, BVNC, &mem);
	if (error != wanted)
		fprintf(stderr, "error=%d wanted=%d operation=%u fault=%u\n",
		    error, wanted, operations, (unsigned int)fault_kind);
	CHECK(error == wanted);
	CHECK(memcmp(saved, input, size) == 0);
	free(saved);
	if (error != 0) {
		CHECK(mem == &sentinel);
	} else {
		size_t off;

		CHECK(mem != NULL && mem != &sentinel);
		CHECK(live_allocs == 3 && live_maps == 3 && loaded == 3);
		CHECK(mem->image.info.bvnc == BVNC);
		CHECK(getle(mem->table.cpu, 4) ==
		    (mem->code.address >> 6 | 0x17));
		CHECK(getle((uint8_t *)mem->table.cpu + 0x32 * 4, 4) ==
		    (mem->data.address >> 6 | 0x17));
		off = (size_t)(mem->boot_data - mem->data.address);
		CHECK(getle((uint8_t *)mem->data.cpu + off, 8) == mem->stack);
		CHECK(getle((uint8_t *)mem->data.cpu + off + 8, 8) == 0x01800000);
		for (unsigned int i = 0; i < 4; i++)
			CHECK(getle((uint8_t *)mem->data.cpu + off + 16 + i * 8, 8)
			    == mem->table.address + i * PAGE_SIZE);
		CHECK(getle((uint8_t *)mem->data.cpu + off + 48, 4) == 12);
		CHECK(getle((uint8_t *)mem->data.cpu + off + 52, 4) == 4);
		CHECK(getle((uint8_t *)mem->data.cpu + off + 56, 8) == 0);
		sun60i_a733_gpu_fwmem_destroy(mem);
	}
	CHECK(live_allocs == 0 && live_maps == 0 && live_kmem == 0);
	CHECK(mapped == 0 && loaded == 0);
}

static void
load_extra(uint32_t address, uint32_t flags)
{
	blob[44] = 2;
	put32(84, 1);
	put32(88, 260);
	put32(92, address);
	put32(96, address);
	put32(100, 4);
	put32(104, 4);
	put32(108, flags);
	put32(112, 4);
}

static void
remap_tests(void)
{
	/* A second exceptions page cannot fit its fixed 4K wrapper window. */
	fixture();
	reset(0, FAULT_NONE);
	put32(LAYOUT(1, 12), 8192);
	put32(LAYOUT(1, 16), 8192);
	put32(LAYOUT(2, 20), 12288);
	load_extra(0x9fc03000, 5);
	run(blob, sizeof(blob), EOPNOTSUPP);
	CHECK(unloaded == 3);
	/* A second boot-data page would enter the exceptions remap window. */
	fixture();
	reset(0, FAULT_NONE);
	put32(LAYOUT(4, 12), 8192);
	put32(LAYOUT(4, 16), 8192);
	put32(LAYOUT(5, 20), 12288);
	load_extra(0xbfc02000, 6);
	run(blob, sizeof(blob), EOPNOTSUPP);
	CHECK(unloaded == 3);
	/* A stack larger than one page is outside this initial boot scope. */
	fixture();
	reset(0, FAULT_NONE);
	put32(LAYOUT(5, 12), 8192);
	put32(LAYOUT(5, 16), 8192);
	run(blob, sizeof(blob), EOPNOTSUPP);
	CHECK(unloaded == 3);
	fixture();
}

static void
alias_tests(void)
{
	static const enum fault faults[] = {
		FAULT_CODE_TABLE_ALIAS, FAULT_DATA_TABLE_ALIAS,
		FAULT_CODE_DATA_ALIAS, FAULT_PARTIAL_ALIAS
	};

	for (size_t i = 0; i < __arraycount(faults); i++) {
		reset(0, faults[i]);
		run(blob, sizeof(blob), EEXIST);
		CHECK(unloaded == 3);
	}
}

static void
argument_tests(void)
{
	struct sun60i_a733_gpu_fwmem sentinel;
	struct sun60i_a733_gpu_fwmem *out = &sentinel;

	reset(0, FAULT_NONE);
	CHECK(sun60i_a733_gpu_fwmem_prepare(NULL, blob, sizeof(blob), BVNC,
	    &out) == EINVAL);
	CHECK(out == &sentinel && operations == 0);
	CHECK(sun60i_a733_gpu_fwmem_prepare(&test_dmat, blob, sizeof(blob), BVNC,
	    NULL) == EINVAL);
	CHECK(operations == 0);
	CHECK(sun60i_a733_gpu_fwmem_prepare(&test_dmat, NULL, sizeof(blob), BVNC,
	    &out) == EINVAL);
	CHECK(out == &sentinel && operations == 0);
	sun60i_a733_gpu_fwmem_destroy(NULL);
}

static struct sun60i_a733_gpu_fwmem *
interface_owner(void)
{
	struct sun60i_a733_gpu_fwmem *mem = NULL;

	fixture();
	reset(0, FAULT_NONE);
	CHECK(sun60i_a733_gpu_fwmem_prepare(&test_dmat, blob, sizeof(blob),
	    BVNC, &mem) == 0);
	CHECK(mem != NULL && !mem->interface_prepared);
	CHECK(live_allocs == 3 && live_maps == 3 && loaded == 3);
	return mem;
}

static void
interface_error(struct sun60i_a733_gpu_fwmem *mem, uint64_t hz,
    uint32_t clusters, uint32_t slc_kib, int wanted)
{
	struct sun60i_a733_gpu_fwmem before;
	const struct sun60i_a733_gpu_dma *areas[] = {
		&mem->code, &mem->data, &mem->table, &mem->work, &mem->config
	};
	uint8_t *copies[5] = { NULL };
	unsigned int alloc_count = live_allocs, map_count = live_maps;
	unsigned int kmem_count = live_kmem, load_count = loaded;
	unsigned int cpu_map_count = mapped;
	int error;

	memcpy(&before, mem, sizeof(before));
	for (size_t i = 0; i < __arraycount(areas); i++) {
		if (areas[i]->size == 0)
			continue;
		copies[i] = malloc(areas[i]->size);
		CHECK(copies[i] != NULL);
		memcpy(copies[i], areas[i]->cpu, areas[i]->size);
	}
	error = sun60i_a733_gpu_fwmem_interface(mem, hz, clusters, slc_kib);
	if (error != wanted)
		fprintf(stderr, "interface error=%d wanted=%d operation=%u\n",
		    error, wanted, operations);
	CHECK(error == wanted);
	CHECK(memcmp(&before, mem, sizeof(before)) == 0);
	for (size_t i = 0; i < __arraycount(areas); i++) {
		if (copies[i] != NULL) {
			CHECK(memcmp(copies[i], areas[i]->cpu, areas[i]->size) == 0);
			free(copies[i]);
		}
	}
	CHECK(live_allocs == alloc_count && live_maps == map_count);
	CHECK(live_kmem == kmem_count && loaded == load_count);
	CHECK(mapped == cpu_map_count);
}

static void
pte_expected(uint8_t *table, size_t index, uint64_t pa)
{
	uint32_t pte = (uint32_t)(pa >> 6) | 0x17;

	for (unsigned int i = 0; i < 4; i++)
		table[index * 4 + i] = (uint8_t)(pte >> (i * 8));
}

static void
interface_success(struct sun60i_a733_gpu_fwmem *mem)
{
	const struct rogue_fwif_ccb_ctl *kccb;
	const struct rogue_fwif_osinit *osinit;
	const struct rogue_fwif_sysinit *sysinit;
	const struct sun60i_a733_gpu_fwinit_region *region;
	uint8_t expected[16384];
	unsigned int previous_operations;

	memcpy(expected, mem->table.cpu, sizeof(expected));
	CHECK(sun60i_a733_gpu_fwmem_interface(mem, UINT64_C(600000000),
	    1, 128) == 0);
	CHECK(mem->interface_prepared);
	CHECK(mem->work.size == 139264 && mem->config.size == 12288);
	CHECK(mem->interface_layout.total_work_size == 139264);
	CHECK(live_allocs == 5 && live_maps == 5 && loaded == 5);
	CHECK(live_kmem == 1 && mapped == 5);
	for (size_t i = 0; i < 34; i++)
		pte_expected(expected, 0x100 + i, mem->work.address + i * 4096);
	pte_expected(expected, 0xfd0, mem->config.address);
	pte_expected(expected, 0xfe0, mem->config.address + 4096);
	pte_expected(expected, 0xff0, mem->config.address + 8192);
	CHECK(memcmp(expected, mem->table.cpu, sizeof(expected)) == 0);
	kccb = mem->work.cpu;
	CHECK(kccb->wrap_mask == 127 && kccb->cmd_size == 88);
	osinit = (const void *)((const uint8_t *)mem->config.cpu + 4096);
	CHECK(osinit->kernel_ccbctl_fw_addr == UINT32_C(0xc0100000));
	sysinit = (const void *)((const uint8_t *)mem->config.cpu + 8192);
	CHECK(sysinit->initial_core_clock_speed == 600000000);
	region = &mem->interface_layout.object[A733_GPU_FWINIT_FAULT];
	CHECK(sysinit->fault_phys_addr == mem->work.address + region->offset);
	CHECK(region->offset == 24576 && region->size == 4096);
	previous_operations = operations;
	interface_error(mem, 600000000, 1, 128, EBUSY);
	CHECK(operations == previous_operations);
}

static void
interface_tests(void)
{
	static const uint32_t occupied_va[] = {
		A733_GPU_FWINIT_WORK_VA, A733_GPU_FWINIT_CONNECTION_VA,
		A733_GPU_FWINIT_OSINIT_VA, A733_GPU_FWINIT_SYSINIT_VA
	};
	struct sun60i_a733_gpu_fwmem empty = { 0 };
	struct sun60i_a733_gpu_fwmem *mem;
	uint64_t unrelated_page = UINT64_C(0x70000000);
	uint64_t saved_address;
	unsigned int previous_operations;

	CHECK(sun60i_a733_gpu_fwmem_interface(NULL, 600000000, 1, 128) == EINVAL);
	CHECK(sun60i_a733_gpu_fwmem_interface(&empty, 600000000, 1, 128) == EINVAL);
	mem = interface_owner();
	previous_operations = operations;
	mem->code.loaded = false;
	interface_error(mem, 600000000, 1, 128, EINVAL);
	mem->code.loaded = true;
	mem->work.size = 4096;
	CHECK(sun60i_a733_gpu_fwmem_interface(mem, 600000000, 1, 128) == EINVAL);
	CHECK(mem->work.size == 4096);
	mem->work.size = 0;
	CHECK(operations == previous_operations);
	interface_success(mem);
	sun60i_a733_gpu_fwmem_destroy(mem);

	/* Every new allocation/map/load failure preserves all existing state. */
	for (unsigned int i = 1; i <= 8; i++) {
		mem = interface_owner();
		fail_at = operations + i;
		interface_error(mem, 600000000, 1, 128,
		    i % 4 == 0 ? EIO : ENOMEM);
		fail_at = 0;
		interface_success(mem);
		sun60i_a733_gpu_fwmem_destroy(mem);
	}
	/* The real initializer rejects these values after the allocations. */
	mem = interface_owner();
	interface_error(mem, 0, 1, 128, EINVAL);
	interface_error(mem, UINT64_C(1) << 32, 1, 128, EINVAL);
	interface_error(mem, 600000000, 0, 128, EINVAL);
	interface_error(mem, 600000000, 1, 0, EINVAL);
	interface_success(mem);
	sun60i_a733_gpu_fwmem_destroy(mem);

	/* Test each late map failure, including the last sparse config page. */
	for (size_t i = 0; i < __arraycount(occupied_va); i++) {
		mem = interface_owner();
		CHECK(sun60i_a733_gpu_mips_map(mem->table.cpu, mem->table.size,
		    occupied_va[i], &unrelated_page, 1) == 0);
		interface_error(mem, 600000000, 1, 128, EEXIST);
		sun60i_a733_gpu_fwmem_destroy(mem);
	}

	/* Malformed backend ranges must not overlap any existing allocation. */
	for (unsigned int new_area = 3; new_area <= 4; new_area++) {
		for (unsigned int old_area = 0; old_area < 3; old_area++) {
			const struct sun60i_a733_gpu_dma *area;

			mem = interface_owner();
			area = old_area == 0 ? &mem->code :
			    old_area == 1 ? &mem->data : &mem->table;
			fault_kind = FAULT_INTERFACE_ADDRESS;
			interface_load = new_area;
			interface_address = area->address + area->size - 4096;
			interface_error(mem, 600000000, 1, 128, EEXIST);
			fault_kind = FAULT_NONE;
			interface_success(mem);
			sun60i_a733_gpu_fwmem_destroy(mem);
		}
	}
	mem = interface_owner();
	fault_kind = FAULT_INTERFACE_ADDRESS;
	interface_load = 4;
	interface_address = next_address; /* The forthcoming work range. */
	interface_error(mem, 600000000, 1, 128, EEXIST);
	fault_kind = FAULT_NONE;
	interface_success(mem);
	sun60i_a733_gpu_fwmem_destroy(mem);
	mem = interface_owner();
	saved_address = mem->code.address;
	mem->code.address = mem->data.address;
	interface_error(mem, 600000000, 1, 128, EEXIST);
	mem->code.address = saved_address;
	sun60i_a733_gpu_fwmem_destroy(mem);

	/* Work or a later config page enters the sensitive wrapper window. */
	for (unsigned int new_area = 3; new_area <= 4; new_area++) {
		mem = interface_owner();
		fault_kind = FAULT_INTERFACE_ADDRESS;
		interface_load = new_area;
		interface_address = UINT64_C(0x1fbff000);
		interface_error(mem, 600000000, 1, 128, EOPNOTSUPP);
		fault_kind = FAULT_NONE;
		interface_success(mem);
		sun60i_a733_gpu_fwmem_destroy(mem);
	}
	reset(0, FAULT_NONE);
}

static void
pinned_blob_test(const char *path)
{
	FILE *f;
	long size;
	void *bytes;

	f = fopen(path, "rb");
	CHECK(f != NULL);
	CHECK(fseek(f, 0, SEEK_END) == 0);
	size = ftell(f);
	CHECK(size > 0 && size <= 1048576);
	CHECK(fseek(f, 0, SEEK_SET) == 0);
	bytes = malloc((size_t)size);
	CHECK(bytes != NULL);
	CHECK(fread(bytes, 1, (size_t)size, f) == (size_t)size);
	CHECK(fclose(f) == 0);
	reset(0, FAULT_NONE);
	run(bytes, (size_t)size, 0);
	{
		struct sun60i_a733_gpu_fwmem *mem = NULL;

		reset(0, FAULT_NONE);
		CHECK(sun60i_a733_gpu_fwmem_prepare(&test_dmat, bytes,
		    (size_t)size, BVNC, &mem) == 0);
		interface_success(mem);
		sun60i_a733_gpu_fwmem_destroy(mem);
	}
	free(bytes);
}

int
main(int argc, char **argv)
{
	CHECK(argc <= 2);
	fixture();
	reset(0, FAULT_NONE);
	run(blob, sizeof(blob), 0);
	CHECK(unloaded == 3);
	for (unsigned int i = 1; i <= 12; i++) {
		reset(i, FAULT_NONE);
		run(blob, sizeof(blob), i % 4 == 0 ? EIO : ENOMEM);
	}
	for (enum fault i = FAULT_NSEGS; i <= FAULT_WIDTH; i++) {
		reset(0, i);
		run(blob, sizeof(blob), EINVAL);
	}
	reset(0, FAULT_REMAP_WINDOW);
	run(blob, sizeof(blob), EOPNOTSUPP);
	remap_tests();
	alias_tests();
	argument_tests();
	reset(0, FAULT_NONE);
	put32(24, 0xbfc00000);
	run(blob, sizeof(blob), EOPNOTSUPP);
	fixture();
	reset(0, FAULT_NONE);
	put32(4096 + 40, 32);
	run(blob, sizeof(blob), EOPNOTSUPP);
	CHECK(operations == 0);
	fixture();
	reset(0, FAULT_NONE);
	blob[0] = 0;
	run(blob, sizeof(blob), EINVAL);
	CHECK(unloaded == 3);
	fixture();
	reset(0, FAULT_NONE);
	put32(LAYOUT(0, 16), 4095);
	run(blob, sizeof(blob), EINVAL);
	CHECK(operations == 0);
	fixture();
	interface_tests();
	if (argc == 2)
		pinned_blob_test(argv[1]);
	printf("PASS: %u native firmware DMA owner checks\n", checks);
	return 0;
}
