/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted native owner tests. */
#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sun60i_a733_gpu_fwinfo.h"
#include "sun60i_a733_gpu_mmu.h"
#include "powervr/pvr_rogue_fwif.h"

#define __CTASSERT(e) _Static_assert(e, #e)
#define BUS_DMA_WAITOK 0
#define BUS_DMA_COHERENT 4
#define _BUS_DMAMAP_COHERENT 0x10000
#define _BUS_DMAMAP_IS_BOUNCING 0x20000
#define KM_SLEEP 0
#define PAGE 4096
#define BVNC UINT64_C(0x00240038006800b7)

typedef void *bus_dma_tag_t;
typedef uint64_t bus_addr_t;
typedef struct {
	uint64_t ds_addr;
	size_t ds_len;
	unsigned int id;
} bus_dma_segment_t;
typedef struct {
	int dm_nsegs, _dm_flags;
	size_t dm_mapsize;
	bus_dma_segment_t dm_segs[1];
	unsigned int id;
} *bus_dmamap_t;
#include "vm.h"

static unsigned int checks, operations, fail_at, current, live_kmem;
static unsigned int allocs, mappings, maps, loads;
static int test_dma_tag;
static uint64_t address_base;
static struct sun60i_a733_gpu_fwinfo *profile;
static struct allocation {
	void *memory, *mapped_cpu;
	bool allocated, mapped, created, loaded;
	int mapping_flags;
} allocations[5];
static enum fault {
	NONE, NONCOHERENT, BOUNCE, SEGCOUNT, LENGTH, MAPSIZE, PA_ALIGN,
	PA_WIDTH, CPU_ALIGN, CPU_ALIAS, PA_ALIAS, PROFILE_ALIAS
} fault;
static unsigned int fault_page, alias_page;

#define CHECK(e) do { checks++; if (!(e)) { \
	fprintf(stderr, "%d: %s\n", __LINE__, #e); exit(1); \
} } while (0)

static bool
fail(void)
{
	return ++operations == fail_at;
}

static void *
kmem_zalloc(size_t size, int flags)
{
	void *p;

	CHECK(flags == KM_SLEEP && size == sizeof(struct sun60i_a733_gpu_vm));
	p = calloc(1, size);
	CHECK(p != NULL);
	live_kmem++;
	return p;
}

static void
kmem_free(void *p, size_t size)
{
	CHECK(live_kmem == 1 && size == sizeof(struct sun60i_a733_gpu_vm));
	CHECK(allocs == 0 && mappings == 0 && maps == 0 && loads == 0);
	live_kmem--;
	free(p);
}

static int
bus_dmamem_alloc(bus_dma_tag_t t, size_t size, size_t alignment,
    size_t boundary, bus_dma_segment_t *seg, int max, int *count, int flags)
{
	void *p;

	CHECK(t == &test_dma_tag && size == PAGE && alignment == PAGE && boundary == 0);
	CHECK(max == 1 && flags == BUS_DMA_WAITOK);
	if (fail())
		return ENOMEM;
	CHECK(current < 5 && posix_memalign(&p, PAGE, PAGE) == 0);
	memset(p, 0x5a, PAGE);
	allocations[current].memory = p;
	allocations[current].allocated = true;
	seg->id = current;
	/* Allocation segments are not the loaded DMA addresses. */
	seg->ds_addr = UINT64_C(0x12345000) + current * PAGE;
	seg->ds_len = PAGE;
	*count = 1;
	current++;
	allocs++;
	return 0;
}

/* Preserve the native bus_dma hook's mutable pointer types. */
// cppcheck-suppress-begin constParameterPointer
static int
bus_dmamem_map(bus_dma_tag_t t, bus_dma_segment_t *seg, int count,
    size_t size, void **cpu, int flags)
{
	struct allocation *a = &allocations[seg->id];

	CHECK(t == &test_dma_tag && count == 1 && size == PAGE && a->allocated);
	CHECK(flags == (BUS_DMA_WAITOK | BUS_DMA_COHERENT));
	if (fail())
		return ENOMEM;
	*cpu = a->memory;
	if (seg->id == fault_page) {
		if (fault == CPU_ALIGN)
			*cpu = (uint8_t *)a->memory + 1;
		if (fault == CPU_ALIAS)
			*cpu = allocations[alias_page].memory;
		if (fault == PROFILE_ALIAS)
			*cpu = profile;
	}
	a->mapped_cpu = *cpu;
	a->mapped = true;
	a->mapping_flags = flags;
	mappings++;
	return 0;
}
// cppcheck-suppress-end constParameterPointer

static int
bus_dmamap_create(bus_dma_tag_t t, size_t size, int nseg, size_t maxseg,
    size_t boundary, int flags, bus_dmamap_t *map)
{
	CHECK(t == &test_dma_tag && size == PAGE && nseg == 1 && maxseg == PAGE);
	CHECK(boundary == 0 && flags == BUS_DMA_WAITOK && current > 0);
	if (fail())
		return ENOMEM;
	*map = calloc(1, sizeof(**map));
	CHECK(*map != NULL);
	(*map)->id = current - 1;
	allocations[current - 1].created = true;
	maps++;
	return 0;
}

/* Preserve the native bus_dma hook's mutable pointer types. */
// cppcheck-suppress-begin constParameterPointer
static int
bus_dmamap_load(bus_dma_tag_t t, bus_dmamap_t map, void *cpu,
    size_t size, void *process, int flags)
{
	struct allocation *a = &allocations[map->id];

	CHECK(t == &test_dma_tag && size == PAGE && process == NULL && flags == BUS_DMA_WAITOK);
	CHECK(a->mapped && a->created && !a->loaded && cpu == a->mapped_cpu);
	CHECK(a->mapping_flags == BUS_DMA_COHERENT);
	if (fail())
		return ENOMEM;
	map->dm_mapsize = PAGE;
	map->dm_nsegs = 1;
	map->dm_segs[0].ds_addr = address_base + map->id * PAGE;
	map->dm_segs[0].ds_len = PAGE;
	map->_dm_flags = _BUS_DMAMAP_COHERENT;
	if (map->id == fault_page) {
		switch (fault) {
		case NONCOHERENT:
			map->_dm_flags = 0;
			break;
		case BOUNCE:
			map->_dm_flags |= _BUS_DMAMAP_IS_BOUNCING;
			break;
		case SEGCOUNT:
			map->dm_nsegs = 2;
			break;
		case LENGTH:
			map->dm_segs[0].ds_len--;
			break;
		case MAPSIZE:
			map->dm_mapsize--;
			break;
		case PA_ALIGN:
			map->dm_segs[0].ds_addr++;
			break;
		case PA_WIDTH:
			map->dm_segs[0].ds_addr = UINT64_C(1) << 36;
			break;
		case PA_ALIAS:
			map->dm_segs[0].ds_addr = address_base + alias_page * PAGE;
			break;
		default:
			break;
		}
	}
	a->loaded = true;
	loads++;
	return 0;
}
// cppcheck-suppress-end constParameterPointer

static void
bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t map)
{
	struct allocation *a = &allocations[map->id];

	CHECK(t == &test_dma_tag && a->loaded && a->created && loads != 0);
	a->loaded = false;
	loads--;
}

static void
bus_dmamap_destroy(bus_dma_tag_t t, bus_dmamap_t map)
{
	struct allocation *a = &allocations[map->id];

	CHECK(t == &test_dma_tag && !a->loaded && a->created && maps != 0);
	a->created = false;
	maps--;
	free(map);
}

/* Preserve the native bus_dma hook's mutable pointer types. */
// cppcheck-suppress-begin constParameterPointer
static void
bus_dmamem_unmap(bus_dma_tag_t t, void *cpu, size_t size)
{
	unsigned int i = 5;

	CHECK(t == &test_dma_tag && size == PAGE && mappings != 0);
	while (i-- != 0) {
		struct allocation *a = &allocations[i];

		if (!a->mapped || a->mapped_cpu != cpu)
			continue;
		CHECK(!a->loaded && !a->created);
		a->mapped = false;
		mappings--;
		return;
	}
	CHECK(false);
}
// cppcheck-suppress-end constParameterPointer

/* Preserve the native bus_dma hook's mutable pointer types. */
// cppcheck-suppress-begin constParameterPointer
static void
bus_dmamem_free(bus_dma_tag_t t, bus_dma_segment_t *seg, int count)
{
	struct allocation *a = &allocations[seg->id];

	CHECK(t == &test_dma_tag && count == 1 && a->allocated && !a->mapped);
	CHECK(!a->loaded && !a->created && allocs != 0);
	if (fail_at != 0 || fault != NONE) {
		/* No page writes are allowed before all validation succeeds. */
		for (unsigned int i = 0; i < PAGE; i++)
			CHECK(((uint8_t *)a->memory)[i] == 0x5a);
	}
	a->allocated = false;
	allocs--;
	free(a->memory);
}
// cppcheck-suppress-end constParameterPointer
#include "driver.h"

static uint64_t
get64(const uint8_t *p)
{
	uint64_t value = 0;

	for (unsigned int i = 0; i < 8; i++)
		value |= (uint64_t)p[i] << (8 * i);
	return value;
}

static uint32_t
get32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void
reset(void)
{
	CHECK(allocs == 0 && mappings == 0 && maps == 0 && loads == 0 && live_kmem == 0);
	memset(allocations, 0, sizeof(allocations));
	operations = fail_at = current = 0;
	fault = NONE;
	fault_page = alias_page = 0;
	address_base = UINT64_C(0x123450000);
}

static void
check_tables(const struct sun60i_a733_gpu_vm *vm)
{
	uint64_t va = vm->scratch_va;
	unsigned int pc = (unsigned int)(va / UINT64_C(1073741824));
	unsigned int pd = (unsigned int)(va / 2097152 % 512);
	unsigned int pt = (unsigned int)(va / 4096 % 512);
	uint32_t pce;
	uint64_t pde, pte;

	CHECK(vm->prepared && !vm->retained && vm->dmat == &test_dma_tag);
	CHECK(allocs == 5 && mappings == 5 && maps == 5 && loads == 5);
	for (unsigned int i = 0; i < 5; i++) {
		CHECK(vm->page[i].coherent && vm->page[i].loaded);
		CHECK(vm->page[i].address == address_base + i * PAGE);
	}
	for (unsigned int i = 0; i < 1024; i++)
		CHECK(get32((uint8_t *)vm->page[0].cpu + i * 4) ==
		    (i == pc ? (uint32_t)((address_base + PAGE) / 256 + 1) : 0));
	for (unsigned int i = 0; i < 512; i++) {
		CHECK(get64((uint8_t *)vm->page[1].cpu + i * 8) ==
		    (i == pd ? address_base + 2 * PAGE + 1 : 0));
		CHECK(get64((uint8_t *)vm->page[2].cpu + i * 8) ==
		    (i == pt ? address_base + 3 * PAGE + 1 : 0));
	}
	/* Walk the encoded hierarchy using independent masks and loaded PAs. */
	pce = get32((uint8_t *)vm->page[0].cpu + pc * 4);
	CHECK((uint64_t)(pce & UINT32_C(0xfffffff0)) * 256 == vm->page[1].address);
	pde = get64((uint8_t *)vm->page[1].cpu + pd * 8);
	CHECK((pde & UINT64_C(0xfffffff000)) == vm->page[2].address);
	pte = get64((uint8_t *)vm->page[2].cpu + pt * 8);
	CHECK((pte & UINT64_C(0xfffffff000)) == vm->page[3].address);
	CHECK((pte & 0xfff) == 1);
	for (unsigned int i = 0; i < PAGE; i++)
		CHECK(((uint8_t *)vm->page[3].cpu)[i] == 0);
	CHECK(get64(vm->page[4].cpu) == address_base);
	CHECK(get32((uint8_t *)vm->page[4].cpu + 8) == UINT32_MAX);
	for (unsigned int i = 12; i < PAGE; i++)
		CHECK(((uint8_t *)vm->page[4].cpu)[i] == 0);
}

static void
error_case(int expected)
{
	struct sun60i_a733_gpu_fwinfo saved = *profile;
	struct sun60i_a733_gpu_vm *vm = NULL;

	CHECK(sun60i_a733_gpu_vm_prepare(&test_dma_tag, profile,
	    UINT64_C(0x8000000000), &vm) == expected);
	CHECK(vm == NULL && memcmp(profile, &saved, sizeof(saved)) == 0);
	CHECK(allocs == 0 && mappings == 0 && maps == 0 && loads == 0 && live_kmem == 0);
}

static void
allocation_tests(void)
{
	for (unsigned int op = 1; op <= 20; op++) {
		reset();
		fail_at = op;
		error_case(ENOMEM);
		CHECK(operations == op);
	}
	for (unsigned int page = 0; page < 5; page++) {
		for (enum fault f = NONCOHERENT; f <= CPU_ALIGN; f++) {
			reset();
			fault_page = page;
			fault = f;
			error_case(f == NONCOHERENT || f == BOUNCE ? EOPNOTSUPP : EINVAL);
		}
		reset();
		fault_page = page;
		fault = PROFILE_ALIAS;
		error_case(EINVAL);
		for (unsigned int other = 0; other < page; other++) {
			reset();
			fault_page = page;
			alias_page = other;
			fault = PA_ALIAS;
			error_case(EINVAL);
			reset();
			fault_page = page;
			alias_page = other;
			fault = CPU_ALIAS;
			error_case(EINVAL);
		}
	}
}

static void
success(uint64_t va)
{
	struct sun60i_a733_gpu_vm *vm = NULL;

	reset();
	CHECK(sun60i_a733_gpu_vm_prepare(&test_dma_tag, profile, va, &vm) == 0);
	check_tables(vm);
	CHECK(operations == 20);
	CHECK(sun60i_a733_gpu_vm_destroy(&vm) == 0 && vm == NULL);
	reset();
}

static void
lifetime_tests(void)
{
	struct sun60i_a733_gpu_vm *vm = NULL, snapshot;
	uint8_t pages[5][PAGE];
	unsigned int old_operations;

	reset();
	CHECK(sun60i_a733_gpu_vm_destroy(NULL) == 0);
	CHECK(sun60i_a733_gpu_vm_destroy(&vm) == 0);
	CHECK(sun60i_a733_gpu_vm_retain(NULL) == EINVAL);
	CHECK(sun60i_a733_gpu_vm_prepare(&test_dma_tag, profile, UINT64_C(0x8000000000), &vm) == 0);
	check_tables(vm);
	/* A caller slot inside a page cannot be cleared after that page is freed. */
	memcpy(vm->page[3].cpu, &vm, sizeof(vm));
	CHECK(sun60i_a733_gpu_vm_destroy(vm->page[3].cpu) == EINVAL);
	memset(vm->page[3].cpu, 0, sizeof(vm));
	CHECK(sun60i_a733_gpu_vm_retain(vm) == 0 && vm->retained);
	snapshot = *vm;
	for (unsigned int i = 0; i < 5; i++)
		memcpy(pages[i], vm->page[i].cpu, PAGE);
	old_operations = operations;
	CHECK(sun60i_a733_gpu_vm_retain(vm) == EBUSY);
	CHECK(sun60i_a733_gpu_vm_destroy(&vm) == EBUSY && vm != NULL);
	CHECK(sun60i_a733_gpu_vm_prepare(&test_dma_tag, profile, UINT64_C(0x8000000000), &vm) == EBUSY);
	CHECK(memcmp(vm, &snapshot, sizeof(snapshot)) == 0 && operations == old_operations);
	CHECK(allocs == 5 && mappings == 5 && maps == 5 && loads == 5);
	for (unsigned int i = 0; i < 5; i++)
		CHECK(memcmp(pages[i], vm->page[i].cpu, PAGE) == 0);
	/* Test teardown only: no device exists; the fixture owns every allocation. */
	vm_free(vm);
	reset();
}

static void
argument_tests(void)
{
	struct sun60i_a733_gpu_fwinfo saved = *profile;
	struct sun60i_a733_gpu_vm *vm = NULL;
	static const uint64_t bad_va[] = {
		0, UINT64_C(0x7fffffffff), UINT64_C(0x8000000001),
		UINT64_C(0xa000000000), UINT64_C(1) << 40, UINT64_MAX
	};

	reset();
	for (unsigned int i = 0; i < sizeof(bad_va) / sizeof(bad_va[0]); i++)
		CHECK(sun60i_a733_gpu_vm_prepare(&test_dma_tag, profile, bad_va[i], &vm) == EINVAL);
	CHECK(sun60i_a733_gpu_vm_prepare(NULL, profile, UINT64_C(0x8000000000), &vm) == EINVAL);
	CHECK(sun60i_a733_gpu_vm_prepare(&test_dma_tag, NULL, UINT64_C(0x8000000000), &vm) == EINVAL);
	CHECK(sun60i_a733_gpu_vm_prepare(&test_dma_tag, profile, UINT64_C(0x8000000000), NULL) == EINVAL);
	CHECK(sun60i_a733_gpu_vm_prepare(&test_dma_tag, profile, UINT64_C(0x8000000000),
	    (void *)profile) == EINVAL);
	CHECK(sun60i_a733_gpu_vm_prepare(&test_dma_tag, (void *)((uint8_t *)profile + 1),
	    UINT64_C(0x8000000000), &vm) == EINVAL);
	CHECK(sun60i_a733_gpu_vm_prepare(&test_dma_tag, profile, UINT64_C(0x8000000000),
	    (void *)(UINTPTR_MAX - 3)) == EINVAL);
	CHECK(vm == NULL && operations == 0);
	profile->bvnc++;
	error_case(EOPNOTSUPP);
	*profile = saved;
	profile->page_size = 16384;
	error_case(EOPNOTSUPP);
	*profile = saved;
	profile->feature_value[A733_GPU_FW_FEATURE_PHYS_BUS_WIDTH] = 40;
	error_case(EOPNOTSUPP);
	*profile = saved;
	profile->feature_value[A733_GPU_FW_FEATURE_VIRTUAL_ADDRESS_SPACE_BITS] = 39;
	error_case(EOPNOTSUPP);
	*profile = saved;
	profile->feature_mask[0] = profile->feature_mask[1] = 0;
	error_case(EOPNOTSUPP);
	*profile = saved;
	CHECK(memcmp(profile, &saved, sizeof(saved)) == 0);
}

int
main(int argc, char **argv)
{
	void *p;

	CHECK(argc == 1 || argc == 2);
	CHECK(sizeof(*profile) <= PAGE && posix_memalign(&p, PAGE, PAGE) == 0);
	profile = p;
	memset(profile, 0, PAGE);
	profile->bvnc = BVNC;
	profile->page_size = PAGE;
	profile->feature_mask[0] = UINT64_C(1) << 36 | UINT64_C(1) << 60;
	profile->feature_value[A733_GPU_FW_FEATURE_PHYS_BUS_WIDTH] = 36;
	profile->feature_value[A733_GPU_FW_FEATURE_VIRTUAL_ADDRESS_SPACE_BITS] = 40;
	if (argc == 2) {
		uint8_t bytes[131072];
		FILE *input = fopen(argv[1], "rb");

		CHECK(input != NULL && fread(bytes, 1, sizeof(bytes), input) == sizeof(bytes));
		CHECK(fgetc(input) == EOF && !ferror(input) && fclose(input) == 0);
		CHECK(sun60i_a733_gpu_fwinfo_parse(bytes, sizeof(bytes), BVNC, profile) == 0);
	}
	argument_tests();
	allocation_tests();
	success(UINT64_C(0x8000000000));
	success(UINT64_C(0x8abcdef000));
	success(UINT64_C(0x9ffffff000));
	lifetime_tests();
	free(profile);
	printf("a733-gpu-vm-contract: PASS %u checks\n", checks);
	return 0;
}
