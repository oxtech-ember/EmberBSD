/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Actual VM code and encoders. Only kernel/DMA boundaries are modelled. */
#define _POSIX_C_SOURCE 200112L
#include <sys/types.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sun60i_a733_gpu_mmu.h"

static unsigned int checks;
#define CHECK(e) do { checks++; if (!(e)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); abort(); \
} } while (0)
#define KASSERT(e) CHECK(e)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define KM_SLEEP 1
#define BUS_DMA_WAITOK 1
#define BUS_DMA_COHERENT 2
#define BUS_DMASYNC_PREWRITE 4
#define _BUS_DMAMAP_COHERENT 8
#define _BUS_DMAMAP_IS_BOUNCING 16
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define htole32(x) ((uint32_t)(x))
#define htole64(x) ((uint64_t)(x))
#else
#define htole32(x) __builtin_bswap32((uint32_t)(x))
#define htole64(x) __builtin_bswap64((uint64_t)(x))
#endif

typedef void *bus_dma_tag_t;
struct allocation {
	struct allocation *next;
	void *cpu;
	size_t size;
	uint64_t pa;
	unsigned int serial;
};
typedef struct {
	uint64_t ds_addr;
	size_t ds_len;
	struct allocation *allocation;
} bus_dma_segment_t;
typedef struct test_dmamap {
	unsigned int _dm_flags;
	int dm_nsegs;
	size_t dm_mapsize;
	bus_dma_segment_t dm_segs[1];
	void *cpu;
	bool loaded;
	uint8_t synced[4096];
} *bus_dmamap_t;
static struct allocation *metadata, *memory;
static unsigned int metadata_count, memory_count, mapped, maps, loaded;
static unsigned int allocations, fault_at, fault_stage, current_serial;
static uint64_t next_pa = UINT64_C(0x12345000);
struct pvr_vm;
static struct pvr_vm *active_vm;

static struct allocation *
record(struct allocation **head, void *cpu, size_t size)
{
	struct allocation *r = calloc(1, sizeof(*r));

	CHECK(r != NULL);
	r->cpu = cpu; r->size = size; r->next = *head; *head = r;
	return r;
}

static void
forget(struct allocation **head, void *cpu)
{
	struct allocation **p, *r;

	for (p = head; *p != NULL && (*p)->cpu != cpu; p = &(*p)->next)
		continue;
	CHECK(*p != NULL);
	r = *p; *p = r->next; free(r);
}

static void *
kmem_zalloc(size_t size, int flags)
{
	void *p = calloc(1, size);

	CHECK(p != NULL && flags == KM_SLEEP);
	record(&metadata, p, size); metadata_count++;
	return p;
}

static void
kmem_free(void *p, size_t size)
{
	struct allocation *r;

	for (r = metadata; r != NULL && r->cpu != p; r = r->next)
		continue;
	CHECK(r != NULL && r->size == size && metadata_count != 0);
	forget(&metadata, p); metadata_count--; free(p);
}

static bool
fault(unsigned int stage)
{

	return current_serial == fault_at && stage == fault_stage;
}

static int
bus_dmamem_alloc(bus_dma_tag_t tag, size_t size, size_t alignment,
    size_t boundary, bus_dma_segment_t *segment, int max, int *n, int flags)
{
	struct allocation *r;
	void *p = NULL;

	CHECK(tag != NULL && size == 4096 && alignment == 4096 &&
	    boundary == 0 && max == 1 && flags == BUS_DMA_WAITOK);
	current_serial = ++allocations;
	if (fault(1)) return ENOMEM;
	CHECK(posix_memalign(&p, alignment, size) == 0 && p != NULL);
	memset(p, 0xa5, size);
	r = record(&memory, p, size);
	r->pa = next_pa; next_pa += 4096; r->serial = current_serial;
	*segment = (bus_dma_segment_t){ r->pa, size, r };
	*n = 1; memory_count++;
	return 0;
}

static void
bus_dmamem_free(bus_dma_tag_t t, bus_dma_segment_t *s, int n)
{
	void *p = s->allocation->cpu;

	CHECK(n == 1 && memory_count != 0);
	forget(&memory, p); memory_count--; free(p);
}

static int
bus_dmamem_map(bus_dma_tag_t t, bus_dma_segment_t *s, int n,
    size_t size, void **cpu, int flags)
{

	CHECK(n == 1 && size == 4096 && flags == (BUS_DMA_WAITOK | BUS_DMA_COHERENT));
	if (fault(2)) return ENOMEM;
	*cpu = s->allocation->cpu; mapped++;
	return 0;
}

static void
bus_dmamem_unmap(bus_dma_tag_t t, void *cpu, size_t size)
{

	CHECK(size == 4096 && mapped != 0); mapped--;
}

static int
bus_dmamap_create(bus_dma_tag_t t, size_t size, int n, size_t max,
    size_t boundary, int flags, bus_dmamap_t *out)
{

	CHECK(size == 4096 && n == 1 && max == size && boundary == 0 && flags == BUS_DMA_WAITOK);
	if (fault(3)) return ENOMEM;
	*out = calloc(1, sizeof(**out)); CHECK(*out != NULL); maps++;
	return 0;
}

static void
bus_dmamap_destroy(bus_dma_tag_t t, bus_dmamap_t map)
{

	CHECK(!map->loaded && maps != 0); maps--; free(map);
}

static int
bus_dmamap_load(bus_dma_tag_t t, bus_dmamap_t map, void *cpu,
    size_t size, void *proc, int flags)
{
	struct allocation *r;

	CHECK(size == 4096 && flags == BUS_DMA_WAITOK);
	if (fault(4)) return EIO;
	for (r = memory; r != NULL && r->cpu != cpu; r = r->next)
		continue;
	CHECK(r != NULL);
	map->cpu = cpu; map->loaded = true; loaded++;
	map->_dm_flags = _BUS_DMAMAP_COHERENT;
	map->dm_nsegs = 1;
	map->dm_mapsize = size;
	map->dm_segs[0] = (bus_dma_segment_t){ r->pa, size, r };
	if (fault(5)) map->_dm_flags = 0;
	if (fault(6)) map->_dm_flags |= _BUS_DMAMAP_IS_BOUNCING;
	if (fault(7)) map->dm_nsegs = 2;
	if (fault(8)) map->dm_segs[0].ds_len--;
	if (fault(9)) map->dm_segs[0].ds_addr++;
	if (fault(10)) map->dm_segs[0].ds_addr = UINT64_C(1) << 36;
	if (fault(11)) map->dm_mapsize--;
	return 0;
}

static void bus_dmamap_sync(bus_dma_tag_t, bus_dmamap_t, size_t, size_t, int);
static void bus_dmamap_unload(bus_dma_tag_t, bus_dmamap_t);
#include "pvr-vm-body.h"

static uint64_t
read_le(const void *p, unsigned int bytes)
{
	const uint8_t *b = p;
	uint64_t value = 0;
	unsigned int n;

	for (n = 0; n < bytes; n++) value |= (uint64_t)b[n] << (n * 8);
	return value;
}

static void
bus_dmamap_sync(bus_dma_tag_t t, bus_dmamap_t map, size_t off,
    size_t size, int flags)
{
	struct pvr_vm_pd *pd;
	struct pvr_vm_pt *pt;
	unsigned int pc, index;

	CHECK(map->loaded && off == 0 && size == 4096 && flags == BUS_DMASYNC_PREWRITE);
	/* A parent cannot be synchronized before its current child bytes. */
	if (active_vm != NULL) {
		for (pc = 0; pc < 1024; pc++) {
			pd = active_vm->dirs[pc];
			if (map == active_vm->catalogue.map && read_le((uint8_t *)map->cpu + pc * 4, 4) != 0) {
				CHECK(pd != NULL);
				CHECK(memcmp(pd->dma.cpu, pd->dma.map->synced, 4096) == 0);
			}
			if (pd == NULL || pd->dma.map != map) continue;
			for (index = 0; index < 512; index++) {
				if (read_le((uint8_t *)map->cpu + index * 8, 8) == 0) continue;
				pt = pd->tables[index]; CHECK(pt != NULL);
				CHECK(memcmp(pt->dma.cpu, pt->dma.map->synced, 4096) == 0);
			}
		}
	}
	memcpy(map->synced, map->cpu, 4096);
}

static void
bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t map)
{
	struct pvr_vm_pd *pd;
	uint64_t entry, pa;
	unsigned int pc, index;

	CHECK(map != NULL);
	pa = map->dm_segs[0].ds_addr;
	CHECK(map->loaded && loaded != 0);
	/* Removal must be synchronized before a child allocation is released. */
	if (active_vm != NULL) {
		for (pc = 0; pc < 1024; pc++) {
			entry = read_le(active_vm->catalogue.map->synced + pc * 4, 4);
			if (entry & 1)
				CHECK(((entry & UINT64_C(0xfffffff0)) << 8) != pa);
			pd = active_vm->dirs[pc]; if (pd == NULL) continue;
			for (index = 0; index < 512; index++) {
				entry = read_le(pd->dma.map->synced + index * 8, 8);
				if (entry & 1)
					CHECK((entry & UINT64_C(0xffffff000)) != pa);
			}
		}
	}
	map->loaded = false; loaded--;
}

struct snapshot {
	struct snapshot *next;
	void *cpu, *bytes;
	size_t size;
};

static struct snapshot *
snapshot(void)
{
	struct snapshot *head = NULL, *s;
	struct allocation *r;
	unsigned int kind;

	for (kind = 0; kind < 2; kind++) {
		for (r = kind == 0 ? metadata : memory; r != NULL; r = r->next) {
			s = calloc(1, sizeof(*s)); CHECK(s != NULL);
			s->cpu = r->cpu; s->size = r->size; s->bytes = malloc(r->size);
			CHECK(s->bytes != NULL); memcpy(s->bytes, r->cpu, r->size);
			s->next = head; head = s;
		}
	}
	return head;
}

static void
check_snapshot(struct snapshot *s)
{
	struct snapshot *next;

	while (s != NULL) {
		CHECK(memcmp(s->cpu, s->bytes, s->size) == 0);
		next = s->next; free(s->bytes); free(s); s = next;
	}
}

static void
empty(void)
{

	CHECK(metadata == NULL && memory == NULL && metadata_count == 0 &&
	    memory_count == 0 && mapped == 0 && maps == 0 && loaded == 0);
}

static uint64_t
leaf(struct pvr_vm *vm, uint64_t va)
{
	struct pvr_vm_pd *pd = vm->dirs[va >> 30];
	struct pvr_vm_pt *pt;
	unsigned int pc = va >> 30, di = (va >> 21) & 511, ti = (va >> 12) & 511;

	CHECK(memcmp(vm->catalogue.cpu, vm->catalogue.map->synced, 4096) == 0);
	if (pd == NULL) {
		CHECK(read_le((uint8_t *)vm->catalogue.cpu + pc * 4, 4) == 0); return 0;
	}
	CHECK(read_le((uint8_t *)vm->catalogue.cpu + pc * 4, 4) == (pd->dma.pa >> 8 | 1));
	CHECK(memcmp(pd->dma.cpu, pd->dma.map->synced, 4096) == 0);
	pt = pd->tables[di];
	if (pt == NULL) {
		CHECK(read_le((uint8_t *)pd->dma.cpu + di * 8, 8) == 0); return 0;
	}
	CHECK(read_le((uint8_t *)pd->dma.cpu + di * 8, 8) == (pt->dma.pa | 1));
	CHECK(memcmp(pt->dma.cpu, pt->dma.map->synced, 4096) == 0);
	return read_le((uint8_t *)pt->dma.cpu + ti * 8, 8);
}

static void
reject_map(struct pvr_vm *vm, uint64_t va, uint64_t pa, uint64_t size,
    uint64_t flags, int wanted)
{
	struct snapshot *s = snapshot();
	unsigned int m = metadata_count, d = memory_count;

	CHECK(pvr_vm_map(vm, va, pa, size, flags) == wanted);
	CHECK(metadata_count == m && memory_count == d && mapped == d &&
	    maps == d && loaded == d);
	check_snapshot(s);
}

int
main(void)
{
	struct pvr_vm *vm = NULL, *sentinel = (void *)(uintptr_t)1;
	struct snapshot *s;
	const uint64_t va_end = UINT64_C(1) << 40, pa_end = UINT64_C(1) << 36;
	const uint64_t one_g = UINT64_C(1) << 30, two_m = UINT64_C(1) << 21;
	const uint64_t options[] = { 2, 4, 8, UINT64_C(1) << 62 };
	uint64_t va, flags, pa;
	unsigned int stage, nth, n, before;
	int wanted;

	CHECK(pvr_vm_create(NULL, &vm) == EINVAL && vm == NULL);
	CHECK(pvr_vm_create(&vm, NULL) == EINVAL);
	CHECK(pvr_vm_create(&vm, &sentinel) == EINVAL && sentinel == (void *)(uintptr_t)1);
	for (stage = 1; stage <= 11; stage++) {
		fault_at = allocations + 1; fault_stage = stage;
		wanted = stage == 4 ? EIO : stage <= 3 ? ENOMEM : stage <= 6 ? EOPNOTSUPP : EINVAL;
		CHECK(pvr_vm_create(&vm, &vm) == wanted && vm == NULL); empty();
	}
	fault_at = 0;
	CHECK(pvr_vm_create(&vm, &vm) == 0); active_vm = vm;
	CHECK(pvr_vm_root_pa(vm) == vm->catalogue.map->dm_segs[0].ds_addr);
	for (n = 0; n < 4096; n++) CHECK(((uint8_t *)vm->catalogue.cpu)[n] == 0);
	CHECK(pvr_vm_map(NULL, 0, 0, 4096, 0) == EINVAL);
	reject_map(vm, 0, 0, 0, 0, EINVAL);
	reject_map(vm, 1, 0, 4096, 0, EINVAL);
	reject_map(vm, 0, 1, 4096, 0, EINVAL);
	reject_map(vm, 0, 0, 4095, 0, EINVAL);
	reject_map(vm, va_end, 0, 4096, 0, EINVAL);
	reject_map(vm, va_end - 4096, 0, 8192, 0, EINVAL);
	reject_map(vm, 0, pa_end, 4096, 0, EINVAL);
	reject_map(vm, 0, pa_end - 4096, 8192, 0, EINVAL);
	reject_map(vm, 0, 0, UINT64_MAX, 0, EINVAL);
	for (n = 0; n < 64; n++)
		if (n != 1 && n != 2 && n != 3 && n != 62)
			reject_map(vm, 0, 0, 4096, UINT64_C(1) << n, EINVAL);
	/* Exercise every allowed leaf option and adjacent ranges sharing a PT. */
	for (n = 0; n < 16; n++) {
		flags = 0;
		for (stage = 0; stage < 4; stage++) if (n & (1U << stage)) flags |= options[stage];
		CHECK(pvr_vm_map(vm, (uint64_t)n * 4096, 0x400000 + (uint64_t)n * 4096, 4096, flags) == 0);
		CHECK(leaf(vm, (uint64_t)n * 4096) == ((0x400000 + (uint64_t)n * 4096) | flags | 1));
	}
	CHECK(memory_count == 3 && vm->dirs[0]->tables[0]->used == 16);
	reject_map(vm, 0, 0x800000, 4096, 0, EEXIST);
	reject_map(vm, 4096, 0x800000, 8192, 0, EEXIST);
	s = snapshot();
	CHECK(pvr_vm_unmap(vm, 0, 8192) == ENOENT);
	CHECK(pvr_vm_unmap(vm, 0, 1) == EINVAL);
	CHECK(pvr_vm_unmap(vm, va_end, 4096) == EINVAL);
	check_snapshot(s);
	CHECK(pvr_vm_unmap(vm, 7 * 4096, 4096) == 0);
	CHECK(leaf(vm, 7 * 4096) == 0 && vm->dirs[0]->tables[0]->used == 15);
	CHECK(pvr_vm_map(vm, 7 * 4096, 0x900000, 4096, 2) == 0);
	CHECK(leaf(vm, 7 * 4096) == (0x900000 | 3));
	/* Existing PD plus new PT, then a new PD and PT, across one GiB. */
	for (nth = 1; nth <= 3; nth++) {
		for (stage = 1; stage <= 11; stage++) {
			fault_at = allocations + nth; fault_stage = stage;
			wanted = stage == 4 ? EIO : stage <= 3 ? ENOMEM : stage <= 6 ? EOPNOTSUPP : EINVAL;
			reject_map(vm, one_g - 4096, 0x10000000, 3 * 4096, 8, wanted);
		}
	}
	fault_at = 0;
	CHECK(pvr_vm_map(vm, one_g - 4096, 0x10000000, 3 * 4096, 8) == 0);
	for (n = 0; n < 3; n++) CHECK(leaf(vm, one_g - 4096 + n * 4096) == ((0x10000000 + n * 4096) | 9));
	CHECK(pvr_vm_unmap(vm, one_g - 4096, 3 * 4096) == 0);
	CHECK(vm->dirs[1] == NULL && vm->dirs[0]->tables[511] == NULL && memory_count == 3);
	/* A single map crosses multiple PTs. No two-MiB window limit exists. */
	va = two_m - 4096; pa = 0x20000000;
	CHECK(pvr_vm_map(vm, va, pa, 4 * two_m + 8192, 0) == 0);
	for (uint64_t off = 0; off < 4 * two_m + 8192; off += 4096)
		CHECK(leaf(vm, va + off) == ((pa + off) | 1));
	CHECK(pvr_vm_unmap(vm, va, 4 * two_m + 8192) == 0);
	CHECK(memory_count == 3);
	/* Sparse heap locations need only their own intermediate tables. */
	for (n = 0; n < 16; n++) {
		va = (uint64_t)(n + 5) << 34;
		CHECK(pvr_vm_map(vm, va, 0x60000000 + n * 4096, 4096, 0) == 0);
		CHECK(leaf(vm, va) == ((0x60000000 + n * 4096) | 1));
	}
	before = memory_count;
	CHECK(pvr_vm_map(vm, va_end - 4096, pa_end - 4096, 4096, 8) == 0);
	CHECK(leaf(vm, va_end - 4096) == ((pa_end - 4096) | 9));
	CHECK(pvr_vm_unmap(vm, va_end - 4096, 4096) == 0 && memory_count == before);
	CHECK(pvr_vm_unmap(vm, va_end - 4096, 4096) == ENOENT);
	pvr_vm_destroy(vm); active_vm = NULL; vm = NULL; empty();
	pvr_vm_destroy(NULL);
	/* Highest valid table PA is representable; no address truncation. */
	next_pa = pa_end - 4096;
	CHECK(pvr_vm_create(&vm, &vm) == 0 && pvr_vm_root_pa(vm) == pa_end - 4096);
	pvr_vm_destroy(vm); vm = NULL; empty();
	next_pa = pa_end;
	CHECK(pvr_vm_create(&vm, &vm) == EINVAL && vm == NULL); empty();
	printf("PASS: %u native PowerVR VM checks\n", checks);
	return 0;
}
