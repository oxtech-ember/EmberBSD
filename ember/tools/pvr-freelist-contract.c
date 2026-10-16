/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Real source. DMA, firmware commands, and a proven reset are boundary models. */
#define _POSIX_C_SOURCE 200112L
#include <sys/types.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif_client.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif_check.h>
#include "pvr_backend.h"

static unsigned checks;
#define CHECK(e) do { checks++; if (!(e)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); abort(); \
} } while (0)
#define KASSERT(e) CHECK(e)
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define KM_SLEEP 1
#define BUS_DMA_WAITOK 1
#define BUS_DMA_COHERENT 2
#define _BUS_DMAMAP_COHERENT 4
#define _BUS_DMAMAP_IS_BOUNCING 8
#define BUS_DMASYNC_PREWRITE 1
#define BUS_DMASYNC_PREREAD 2
#define BUS_DMASYNC_POSTWRITE 4
#define BUS_DMASYNC_POSTREAD 8
#ifndef htole32
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define htole32(v) ((uint32_t)(v))
#else
#define htole32(v) __builtin_bswap32((uint32_t)(v))
#endif
#endif

typedef void *bus_dma_tag_t;
struct allocation {
	struct allocation *next;
	void *cpu;
	size_t size;
	uint64_t pa;
	bool published;
};
typedef struct {
	uint64_t ds_addr;
	size_t ds_len;
	struct allocation *allocation;
} bus_dma_segment_t;
typedef struct test_dmamap {
	unsigned _dm_flags;
	int dm_nsegs;
	size_t dm_mapsize;
	bus_dma_segment_t dm_segs[1];
	bool loaded, owned;
} *bus_dmamap_t;
#include "pvr-freelist-header.h"

static struct allocation *metadata, *memory;
static unsigned meta_count, mem_count, map_count, kva_count, loaded_count;
static unsigned allocation_count, fault_at, fault_stage, current_serial;
static uint64_t next_pa;
static const uint8_t zeros[4096];
static struct {
	unsigned alloc_calls, free_calls, commands, syncs, flushes, checks;
	int alloc_error, free_error, command_error, flush_error, check_error;
	unsigned check_error_at, malformed;
	uint32_t response;
	bool retain_alloc, live, initialized, ack, stopped, reset;
	uint8_t image[64];
} fw;
static struct {
	uint8_t *cpu, *visible;
	size_t size;
	unsigned syncs;
} backing;

static struct allocation *
record(struct allocation **head, void *p, size_t size)
{
	struct allocation *r = calloc(1, sizeof(*r));
	CHECK(r != NULL);
	r->cpu = p; r->size = size; r->next = *head; *head = r;
	return r;
}

static void
forget(struct allocation **head, void *p, size_t size)
{
	struct allocation **link, *r;
	for (link = head; *link != NULL && (*link)->cpu != p; link = &(*link)->next)
		continue;
	CHECK(*link != NULL && (*link)->size == size);
	r = *link; *link = r->next; free(r);
}

static void *
kmem_zalloc(size_t size, int flags)
{
	void *p = calloc(1, size);
	CHECK(size != 0 && flags == KM_SLEEP && p != NULL);
	record(&metadata, p, size); meta_count++;
	return p;
}

static void
kmem_free(void *p, size_t size)
{
	CHECK(meta_count != 0); meta_count--;
	forget(&metadata, p, size); free(p);
}

static bool
fault(unsigned stage)
{
	return current_serial == fault_at && fault_stage == stage;
}

static int
bus_dmamem_alloc(bus_dma_tag_t t, size_t size, size_t align, size_t boundary,
    bus_dma_segment_t *seg, int max, int *n, int flags)
{
	void *p = NULL;
	struct allocation *r;
	CHECK(t != NULL && size == 4096 && align == 4096 && boundary == 0 &&
	    max == 1 && flags == BUS_DMA_WAITOK);
	current_serial = ++allocation_count;
	if (fault(1)) return ENOMEM;
	CHECK(posix_memalign(&p, align, size) == 0 && p != NULL);
	memset(p, 0xa5, size);
	r = record(&memory, p, size); r->pa = next_pa; next_pa += size;
	*seg = (bus_dma_segment_t){ r->pa, size, r };
	*n = 1; mem_count++;
	return 0;
}

static void
bus_dmamem_free(bus_dma_tag_t t, bus_dma_segment_t *seg, int n)
{
	void *p = seg->allocation->cpu;
	CHECK(n == 1 && mem_count != 0);
	if (seg->allocation->published) {
		CHECK(!fw.live || fw.reset);
		CHECK(!fw.initialized || fw.ack || fw.reset);
	}
	mem_count--; forget(&memory, p, 4096); free(p);
}

static int
bus_dmamem_map(bus_dma_tag_t t, bus_dma_segment_t *seg, int n,
    size_t size, void **cpu, int flags)
{
	CHECK(n == 1 && size == 4096 && flags == (BUS_DMA_WAITOK | BUS_DMA_COHERENT));
	if (fault(2)) return ENOMEM;
	*cpu = seg->allocation->cpu; kva_count++;
	return 0;
}

static void
bus_dmamem_unmap(bus_dma_tag_t t, void *cpu, size_t size)
{
	CHECK(size == 4096 && kva_count != 0); kva_count--;
}

static int
bus_dmamap_create(bus_dma_tag_t t, size_t size, int n, size_t max,
    size_t boundary, int flags, bus_dmamap_t *out)
{
	CHECK(size == 4096 && n == 1 && max == size && boundary == 0 && flags == BUS_DMA_WAITOK);
	if (fault(3)) return ENOMEM;
	*out = calloc(1, sizeof(**out)); CHECK(*out != NULL); map_count++;
	return 0;
}

static void
bus_dmamap_destroy(bus_dma_tag_t t, bus_dmamap_t map)
{
	CHECK(map != NULL && !map->loaded && map_count != 0); map_count--; free(map);
}

static int
bus_dmamap_load(bus_dma_tag_t t, bus_dmamap_t map, void *cpu, size_t size,
    void *proc, int flags)
{
	struct allocation *r;
	CHECK(map != NULL && size == 4096 && flags == BUS_DMA_WAITOK && proc == NULL);
	if (fault(4)) return EIO;
	for (r = memory; r != NULL && r->cpu != cpu; r = r->next)
		continue;
	CHECK(r != NULL);
	map->loaded = true; loaded_count++;
	map->_dm_flags = _BUS_DMAMAP_COHERENT;
	map->dm_nsegs = 1; map->dm_mapsize = size;
	map->dm_segs[0] = (bus_dma_segment_t){ r->pa, size, r };
	if (fault(5)) map->_dm_flags = 0;
	if (fault(6)) map->_dm_flags |= _BUS_DMAMAP_IS_BOUNCING;
	if (fault(7)) map->dm_nsegs = 2;
	if (fault(8)) map->dm_mapsize--;
	if (fault(9)) map->dm_segs[0].ds_len--;
	if (fault(10)) map->dm_segs[0].ds_addr++;
	if (fault(11)) map->dm_segs[0].ds_addr = UINT64_C(1) << 36;
	return 0;
}

static void
bus_dmamap_sync(bus_dma_tag_t t, bus_dmamap_t map, size_t off, size_t size, int flags)
{
	CHECK(map != NULL && map->loaded && off == 0 && size == 4096);
	if (flags == (BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD)) {
		CHECK(memcmp(map->dm_segs[0].allocation->cpu, zeros, 4096) == 0);
		map->owned = true;
	} else {
		CHECK(flags == (BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD));
		CHECK(map->owned); map->owned = false;
	}
}

static void
bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t map)
{
	CHECK(map != NULL && map->loaded && !map->owned && loaded_count != 0);
	map->loaded = false; loaded_count--;
}

static int
backend_check(void *cookie)
{
	CHECK(cookie == &fw); fw.checks++;
	if (fw.stopped) return EIO;
	return fw.check_error_at == fw.checks ? fw.check_error : 0;
}

static int
backend_alloc(void *cookie, size_t size, struct pvr_fw_object *out)
{
	CHECK(cookie == &fw && size == 64 && !fw.live);
	fw.alloc_calls++;
	if (fw.alloc_error != 0 && !fw.retain_alloc) return fw.alloc_error;
	out->cpu = calloc(1, size); CHECK(out->cpu != NULL);
	out->owner = out->cpu; out->size = size; out->address = 0xc0123000U;
	fw.live = true;
	if (fw.malformed == 1) out->size--;
	if (fw.malformed == 2) out->address++;
	if (fw.malformed == 3) out->address = 0;
	return fw.alloc_error;
}

static int
backend_free(void *cookie, struct pvr_fw_object *object)
{
	CHECK(cookie == &fw && object->owner != NULL && fw.live);
	CHECK(!fw.initialized || fw.ack || fw.reset);
	fw.free_calls++;
	if (fw.free_error != 0) return fw.free_error;
	free(object->owner); memset(object, 0, sizeof(*object)); fw.live = false;
	return 0;
}

static void
backend_sync(void *cookie, struct pvr_fw_object *object, int flags)
{
	CHECK(cookie == &fw && fw.live && object->size == 64);
	CHECK(flags == (BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD));
	CHECK(backing.syncs != 0);
	memcpy(fw.image, object->cpu, 64); fw.initialized = true; fw.syncs++;
}

static int
backend_command(void *cookie, const struct rogue_fwif_kccb_cmd *cmd, uint32_t *result)
{
	struct rogue_fwif_kccb_cmd expected = {0};
	expected.cmd_type = ROGUE_FWIF_KCCB_CMD_CLEANUP;
	expected.cmd_data.cleanup_data.cleanup_type = ROGUE_FWIF_CLEANUP_FREELIST;
	expected.cmd_data.cleanup_data.cleanup_data.freelist_fw_addr = 0xc0123000U;
	CHECK(cookie == &fw && fw.live && fw.initialized);
	CHECK(memcmp(cmd, &expected, sizeof(expected)) == 0);
	fw.commands++;
	if (fw.command_error != 0) return fw.command_error;
	*result = fw.response;
	if (*result == 1) fw.ack = true;
	return 0;
}

static int
backend_flush(void *cookie)
{
	CHECK(cookie == &fw && fw.initialized && fw.syncs != 0);
	fw.flushes++;
	return fw.flush_error;
}

static void
backend_fail(void *cookie)
{
	CHECK(cookie == &fw); fw.stopped = true;
}

static void
backing_sync(void *cookie, int flags)
{
	CHECK(cookie == &backing);
	if ((flags & BUS_DMASYNC_PREWRITE) != 0) {
		struct allocation *r;
		uint64_t lowest = UINT64_MAX, highest = 0;
		size_t offset;
		memcpy(backing.visible, backing.cpu, backing.size);
		for (r = memory; r != NULL; r = r->next) {
			if (r->pa < lowest) lowest = r->pa;
			if (r->pa > highest) highest = r->pa;
		}
		for (offset = 0; offset < backing.size; offset += 4) {
			const uint8_t *b = backing.visible + offset;
			uint64_t pa = ((uint64_t)b[0] | (uint64_t)b[1] << 8 |
			    (uint64_t)b[2] << 16 | (uint64_t)b[3] << 24) << 12;
			if (pa < lowest || pa > highest) continue;
			for (r = memory; r != NULL; r = r->next)
				if (r->pa == pa) { r->published = true; break; }
		}
	}
	backing.syncs++;
}

static const struct pvr_backend backend = {
	.cookie = &fw, .alloc = backend_alloc, .free = backend_free,
	.sync = backend_sync, .command = backend_command, .flush = backend_flush,
	.check = backend_check, .fail = backend_fail
};
#include "pvr-freelist-body.h"

static void
reset_state(void)
{
	CHECK(meta_count == 0 && mem_count == 0 && map_count == 0 && kva_count == 0 &&
	    loaded_count == 0 && metadata == NULL && memory == NULL && !fw.live);
	memset(&fw, 0, sizeof(fw)); fw.response = 1;
	allocation_count = fault_at = fault_stage = current_serial = 0;
	next_pa = UINT64_C(0x120000000);
	backing.syncs = 0;
	memset(backing.cpu, 0xa5, backing.size);
	memset(backing.visible, 0x5a, backing.size);
}

static struct pvr_freelist_config
config(uint32_t initial, uint32_t max, uint32_t grow, uint32_t threshold)
{
	return (struct pvr_freelist_config){
		.backing_cpu = backing.cpu, .backing_size = backing.size,
		.backing_pa = UINT64_C(0x220000000), .gpu_va = UINT64_C(0x8000400000),
		.sync_cookie = &backing, .backing_sync = backing_sync, .id = 27,
		.initial_pages = initial, .max_pages = max, .grow_pages = grow,
		.grow_threshold = threshold
	};
}

static uint32_t
get32(const void *p)
{
	const uint8_t *b = p;
	return b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static void
put32(uint8_t *p, uint32_t v)
{
	unsigned i;
	for (i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (i * 8));
}

static void
put64(uint8_t *p, uint64_t v)
{
	unsigned i;
	for (i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (i * 8));
}

static void
valid_case(uint32_t initial, uint32_t max, uint32_t grow, uint32_t threshold,
    uint32_t ready)
{
	struct pvr_freelist *fl = NULL;
	struct pvr_freelist_config c;
	uint8_t expected[64] = {0};
	uint32_t i;
	size_t offset;
	reset_state(); c = config(initial, max, grow, threshold);
	CHECK(pvr_freelist_create(&fw, &backend, &c, &fl) == 0 && fl != NULL);
	CHECK(pvr_freelist_fw_address(fl) == 0xc0123000U);
	CHECK(mem_count == initial && map_count == initial && loaded_count == initial);
	CHECK(fw.flushes == 1 && fw.checks == 2 && fw.syncs == 1);
	put64(expected, c.gpu_va);
	put64(expected + 8, (c.gpu_va + (max - initial + ready) * 4) & ~UINT64_C(15));
	put32(expected + 16, initial - ready - 1);
	put32(expected + 20, max); put32(expected + 24, grow);
	put32(expected + 28, initial - ready); put32(expected + 40, 27);
	put32(expected + 48, ready);
	CHECK(memcmp(expected, fw.image, 64) == 0);
	offset = (max - initial) * 4;
	for (i = 0; i < initial; i++)
		CHECK(get32(backing.visible + offset + i * 4) == 0x120000U + i);
	for (i = 0; i < offset; i++) CHECK(backing.cpu[i] == 0xa5);
	for (i = max * 4; i < backing.size; i++) CHECK(backing.cpu[i] == 0xa5);
	CHECK(pvr_freelist_destroy(&fl) == 0 && fl == NULL);
	CHECK(fw.commands == 1 && fw.free_calls == 1 && !fw.live);
	for (i = 0; i < initial * 4; i++) CHECK(backing.visible[offset + i] == 0);
}

static void
invalid_cases(void)
{
	struct pvr_freelist *fl = NULL;
	struct pvr_freelist_config c;
	unsigned i;
	for (i = 0; i < 17; i++) {
		reset_state(); c = config(4, 8, 4, 13);
		switch (i) {
		case 0: c.max_pages = 0; break;
		case 1: c.max_pages = 524292; break;
		case 2: c.initial_pages = 12; break;
		case 3: c.grow_pages = 12; break;
		case 4: c.grow_threshold = 101; break;
		case 5: c.initial_pages = 3; break;
		case 6: c.grow_pages = 3; break;
		case 7: c.max_pages = 7; break;
		case 8: c.grow_pages = 0; break;
		case 9: c.initial_pages = c.max_pages; break;
		case 10: c.backing_size = 31; break;
		case 11: c.gpu_va++; break;
		case 12: c.backing_pa++; break;
		case 13: c.gpu_va = (UINT64_C(1) << 40) - 16; break;
		case 14: c.backing_pa = (UINT64_C(1) << 36) - 16; break;
		case 15: c.id = 0; break;
		case 16: c.backing_sync = NULL; break;
		}
		CHECK(pvr_freelist_create(&fw, &backend, &c, &fl) == EINVAL);
		CHECK(fl == NULL && allocation_count == 0 && fw.alloc_calls == 0);
	}
	reset_state(); c = config(4, 4, 0, 0);
	c.gpu_va = (UINT64_C(1) << 40) - 16;
	c.backing_pa = (UINT64_C(1) << 36) - 16;
	next_pa = (UINT64_C(1) << 36) - 4 * 4096;
	CHECK(pvr_freelist_create(&fw, &backend, &c, &fl) == 0);
	CHECK(get32(backing.visible + 12) == 0xffffffU);
	CHECK(pvr_freelist_destroy(&fl) == 0);
	CHECK(pvr_freelist_fw_address(NULL) == 0);
}

static void
allocation_failures(void)
{
	struct pvr_freelist *fl = NULL;
	struct pvr_freelist_config c;
	unsigned ordinal, stage;
	for (ordinal = 1; ordinal <= 4; ordinal++) {
		for (stage = 1; stage <= 11; stage++) {
			reset_state(); c = config(4, 8, 4, 13);
			fault_at = ordinal; fault_stage = stage;
			CHECK(pvr_freelist_create(&fw, &backend, &c, &fl) != 0);
			CHECK(fl == NULL && meta_count == 0 && mem_count == 0 &&
			    map_count == 0 && kva_count == 0 && loaded_count == 0);
			CHECK(fw.alloc_calls == 0 && backing.syncs == 0 && backing.cpu[16] == 0xa5);
		}
	}
	reset_state(); c = config(4, 8, 4, 13); fw.alloc_error = ENOMEM;
	CHECK(pvr_freelist_create(&fw, &backend, &c, &fl) == ENOMEM);
	CHECK(fl == NULL && mem_count == 0 && meta_count == 0 && backing.syncs == 0);
	reset_state(); fw.alloc_error = EIO; fw.retain_alloc = true;
	CHECK(pvr_freelist_create(&fw, &backend, &c, &fl) == EIO && fl != NULL);
	CHECK(mem_count == 4 && fw.live && fw.syncs == 0 && backing.syncs == 0);
	CHECK(pvr_freelist_fw_address(fl) == 0);
	CHECK(pvr_freelist_destroy(&fl) == 0 && fl == NULL && fw.commands == 0);
}

/* No production reset API exists. The test models a separately proven reset. */
static void
reset_reclaim(struct pvr_freelist **owner)
{
	struct pvr_freelist *fl = *owner;
	fw.reset = true;
	if (fl->firmware.owner != NULL) {
		fw.free_error = 0;
		CHECK(backend_free(&fw, &fl->firmware) == 0);
	}
	pvr_freelist_release(fl); *owner = NULL;
}

static void
retained_failures(void)
{
	struct pvr_freelist *fl = NULL;
	struct pvr_freelist_config c;
	unsigned i;
	for (i = 0; i < 2; i++) {
		reset_state(); c = config(4, 8, 4, 13);
		if (i == 0) fw.flush_error = EIO;
		else { fw.check_error_at = 2; fw.check_error = EIO; }
		CHECK(pvr_freelist_create(&fw, &backend, &c, &fl) == EIO && fl != NULL);
		CHECK(pvr_freelist_fw_address(fl) == 0 && mem_count == 4 && fw.live);
		fw.flush_error = 0; fw.check_error_at = 0;
		CHECK(pvr_freelist_destroy(&fl) == 0 && fw.commands == 1);
	}
	reset_state(); c = config(4, 8, 4, 13);
	CHECK(pvr_freelist_create(&fw, &backend, &c, &fl) == 0);
	fw.response = 3;
	CHECK(pvr_freelist_destroy(&fl) == EBUSY && fl != NULL);
	CHECK(mem_count == 4 && fw.live && fw.free_calls == 0);
	fw.response = 1; fw.command_error = ETIMEDOUT;
	CHECK(pvr_freelist_destroy(&fl) == ETIMEDOUT && fl != NULL);
	CHECK(mem_count == 4 && fw.live && fw.free_calls == 0);
	fw.command_error = 0; fw.free_error = EIO;
	CHECK(pvr_freelist_destroy(&fl) == EIO && fl != NULL);
	CHECK(fw.commands == 3 && fw.ack && fw.live && mem_count == 4);
	CHECK(pvr_freelist_fw_address(fl) == 0);
	fw.free_error = 0;
	CHECK(pvr_freelist_destroy(&fl) == 0 && fl == NULL && fw.commands == 3);
	for (i = 0; i < 3; i++) {
		reset_state(); c = config(4, 8, 4, 13);
		CHECK(pvr_freelist_create(&fw, &backend, &c, &fl) == 0);
		fw.response = i == 0 ? 0 : i == 1 ? 2 : 5;
		CHECK(pvr_freelist_destroy(&fl) == EPROTO && fl != NULL);
		CHECK(fw.stopped && fw.live && mem_count == 4 && fw.free_calls == 0);
		CHECK(pvr_freelist_destroy(&fl) == EIO);
		reset_reclaim(&fl);
	}
	for (i = 1; i <= 3; i++) {
		reset_state(); c = config(4, 8, 4, 13); fw.malformed = i;
		CHECK(pvr_freelist_create(&fw, &backend, &c, &fl) == EPROTO && fl != NULL);
		CHECK(fw.stopped && fw.live && mem_count == 4 && backing.syncs == 0);
		CHECK(pvr_freelist_destroy(&fl) == EIO);
		reset_reclaim(&fl);
	}
	reset_state(); c = config(4, 8, 4, 13); fw.check_error_at = 1; fw.check_error = EIO;
	CHECK(pvr_freelist_create(&fw, &backend, &c, &fl) == EIO && fl == NULL);
	CHECK(allocation_count == 0 && fw.alloc_calls == 0);
	reset_state(); fw.alloc_error = EIO; fw.retain_alloc = true;
	CHECK(pvr_freelist_create(&fw, &backend, &c, &fl) == EIO && fl != NULL);
	fw.stopped = true;
	CHECK(pvr_freelist_destroy(&fl) == EIO && fw.commands == 0 && mem_count == 4);
	reset_reclaim(&fl);
}

static void
growth_cases(void)
{
	struct pvr_freelist *fl = NULL;
	struct pvr_freelist_config c;
	struct rogue_fwif_freelist_gs_data response, expected, saved;
	uint8_t image[64];
	uint32_t i;
	unsigned allocations_before;

	reset_state(); c = config(512, 1024, 256, 13);
	CHECK(pvr_freelist_create(&fw, &backend, &c, &fl) == 0);
	memcpy(image, fw.image, sizeof(image));
	CHECK(pvr_freelist_grow_complete(fl, 0) == EINVAL);
	CHECK(pvr_freelist_grow_prepare(fl, &response) == 0);
	expected = (struct rogue_fwif_freelist_gs_data){ 0xc0123000U, 256, 768, 96 };
	CHECK(memcmp(&response, &expected, sizeof(response)) == 0);
	CHECK(mem_count == 768 && fl->current_pages == 672 && fl->ready_pages == 96);
	CHECK(memcmp(image, fw.image, sizeof(image)) == 0 && fw.syncs == 1);
	CHECK(fw.commands == 0 && fw.flushes == 1 && fw.checks == 2);
	for (i = 0; i < 256; i++)
		CHECK(get32(backing.visible + (256 + i) * 4) == 0x120000U + 512 + i);
	for (i = 0; i < 512; i++)
		CHECK(get32(backing.visible + (512 + i) * 4) == 0x120000U + i);
	memset(&response, 0xa5, sizeof(response)); saved = response;
	CHECK(pvr_freelist_grow_prepare(fl, &response) == EBUSY);
	CHECK(memcmp(&response, &saved, sizeof(response)) == 0);
	CHECK(pvr_freelist_destroy(&fl) == EBUSY && fl != NULL);
	CHECK(fw.commands == 0 && fw.checks == 2 && mem_count == 768);
	CHECK(pvr_freelist_grow_complete(fl, 0) == 0);
	CHECK(pvr_freelist_grow_complete(fl, 0) == EINVAL);
	CHECK(pvr_freelist_grow_prepare(fl, &response) == 0);
	expected = (struct rogue_fwif_freelist_gs_data){ 0xc0123000U, 256, 1024, 132 };
	CHECK(memcmp(&response, &expected, sizeof(response)) == 0 && mem_count == 1024);
	for (i = 0; i < 256; i++)
		CHECK(get32(backing.visible + i * 4) == 0x120000U + 768 + i);
	CHECK(pvr_freelist_grow_complete(fl, 0) == 0);
	allocations_before = allocation_count;
	CHECK(pvr_freelist_grow_prepare(fl, &response) == 0);
	expected = (struct rogue_fwif_freelist_gs_data){ 0xc0123000U, 0, 1024, 0 };
	CHECK(memcmp(&response, &expected, sizeof(response)) == 0);
	CHECK(allocation_count == allocations_before && !fw.stopped);
	CHECK(pvr_freelist_grow_complete(fl, 0) == 0);
	CHECK(pvr_freelist_destroy(&fl) == 0 && fl == NULL);
	for (i = 0; i < 4096; i++) CHECK(backing.visible[i] == 0);

	reset_state(); c = config(0, 8, 4, 100);
	CHECK(pvr_freelist_create(&fw, &backend, &c, &fl) == 0);
	for (i = 1; i <= 2; i++) {
		CHECK(pvr_freelist_grow_prepare(fl, &response) == 0);
		CHECK(response.delta_pages == 4 && response.new_pages == i * 4 && response.ready_pages == 4);
		CHECK(pvr_freelist_grow_complete(fl, 0) == 0);
	}
	CHECK(pvr_freelist_destroy(&fl) == 0);
	reset_state(); c = config(4, 4, 0, 0);
	CHECK(pvr_freelist_create(&fw, &backend, &c, &fl) == 0);
	CHECK(pvr_freelist_grow_prepare(fl, &response) == 0);
	CHECK(response.delta_pages == 0 && response.new_pages == 4 && response.ready_pages == 0);
	CHECK(allocation_count == 4 && pvr_freelist_destroy(&fl) == EBUSY);
	CHECK(pvr_freelist_grow_complete(fl, 0) == 0 && pvr_freelist_destroy(&fl) == 0);
	reset_state(); c = config(4, 16, 8, 13);
	CHECK(pvr_freelist_create(&fw, &backend, &c, &fl) == 0);
	CHECK(pvr_freelist_grow_prepare(fl, &response) == 0 && response.delta_pages == 8);
	CHECK(pvr_freelist_grow_complete(fl, 0) == 0);
	CHECK(pvr_freelist_grow_prepare(fl, &response) == 0);
	CHECK(response.delta_pages == 0 && response.new_pages == 12 && allocation_count == 12);
	CHECK(pvr_freelist_grow_complete(fl, 0) == 0 && pvr_freelist_destroy(&fl) == 0);
}

static void
growth_failures(void)
{
	struct pvr_freelist *fl = NULL;
	struct pvr_freelist_config c;
	struct rogue_fwif_freelist_gs_data response, saved;
	uint8_t original[48];
	const int errors[] = { EIO, ETIMEDOUT, EINVAL, -EIO };
	unsigned ordinal, stage, i;

	for (ordinal = 1; ordinal <= 4; ordinal++) {
		for (stage = 1; stage <= 11; stage++) {
			reset_state(); c = config(4, 12, 4, 100);
			CHECK(pvr_freelist_create(&fw, &backend, &c, &fl) == 0);
			memcpy(original, backing.visible, sizeof(original));
			fault_at = 4 + ordinal; fault_stage = stage;
			CHECK(pvr_freelist_grow_prepare(fl, &response) == 0);
			CHECK(response.delta_pages == 0 && response.new_pages == 4 && response.ready_pages == 0);
			CHECK(mem_count == 4 && map_count == 4 && loaded_count == 4 && kva_count == 4);
			CHECK(meta_count == 2 && !fw.stopped && fl->current_pages == 4 && fl->ready_pages == 0);
			CHECK(memcmp(original, backing.visible, sizeof(original)) == 0);
			CHECK(memcmp(original, backing.cpu, sizeof(original)) == 0);
			CHECK(fw.commands == 0 && fw.flushes == 1 && fw.checks == 2 && fw.syncs == 1);
			CHECK(pvr_freelist_destroy(&fl) == EBUSY);
			CHECK(pvr_freelist_grow_complete(fl, 0) == 0);
			fault_at = fault_stage = 0;
			CHECK(pvr_freelist_grow_prepare(fl, &response) == 0);
			CHECK(response.delta_pages == 4 && response.new_pages == 8 && response.ready_pages == 4);
			CHECK(pvr_freelist_grow_complete(fl, 0) == 0);
			CHECK(pvr_freelist_destroy(&fl) == 0);
		}
	}
	for (i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
		reset_state(); c = config(4, 8, 4, 13);
		CHECK(pvr_freelist_create(&fw, &backend, &c, &fl) == 0);
		CHECK(pvr_freelist_grow_prepare(fl, &response) == 0 && response.delta_pages == 4);
		CHECK(pvr_freelist_grow_complete(fl, errors[i]) == (errors[i] > 0 ? errors[i] : EPROTO));
		CHECK(mem_count == 8 && fw.live && fw.stopped && fw.free_calls == 0);
		CHECK(pvr_freelist_fw_address(fl) == 0 && pvr_freelist_destroy(&fl) == EIO);
		memset(&response, 0xa5, sizeof(response)); saved = response;
		CHECK(pvr_freelist_grow_prepare(fl, &response) == EIO);
		CHECK(memcmp(&response, &saved, sizeof(response)) == 0);
		CHECK(pvr_freelist_grow_complete(fl, 0) == EIO && mem_count == 8);
		reset_reclaim(&fl);
	}
	reset_state(); c = config(4, 8, 4, 13);
	CHECK(pvr_freelist_create(&fw, &backend, &c, &fl) == 0);
	memset(&response, 0xa5, sizeof(response)); saved = response;
	CHECK(pvr_freelist_grow_prepare(NULL, &response) == EINVAL);
	CHECK(pvr_freelist_grow_prepare(fl, NULL) == EINVAL);
	CHECK(memcmp(&response, &saved, sizeof(response)) == 0);
	CHECK(pvr_freelist_grow_complete(NULL, 0) == EINVAL);
	CHECK(pvr_freelist_grow_complete(fl, EIO) == EINVAL && !fw.stopped);
	CHECK(pvr_freelist_destroy(&fl) == 0);
}

/* Current pages exclude the ready reserve; zero unless ready and quiet. */
static void
current_pages_tests(void)
{
	struct pvr_freelist *fl = NULL;
	struct pvr_freelist_config c;
	struct rogue_fwif_freelist_gs_data response;
	uint32_t before, grown;

	CHECK(pvr_freelist_current_pages(NULL) == 0);
	reset_state(); c = config(512, 1024, 256, 13);
	CHECK(pvr_freelist_create(&fw, &backend, &c, &fl) == 0 && fl != NULL);
	/* Ready owner: the getter reports exactly the current page count. */
	CHECK(fl->ready && !fl->grow_prepared && !fl->failed && !fl->cleaned);
	before = pvr_freelist_current_pages(fl);
	CHECK(before != 0 && before == fl->current_pages);
	/* Pending growth blocks the report until completion. */
	CHECK(pvr_freelist_grow_prepare(fl, &response) == 0);
	CHECK(fl->grow_prepared);
	CHECK(pvr_freelist_current_pages(fl) == 0);
	CHECK(pvr_freelist_grow_complete(fl, 0) == 0);
	grown = pvr_freelist_current_pages(fl);
	CHECK(grown > before && grown == fl->current_pages);
	/* Failed, cleaned and not-ready owners report zero. */
	fl->failed = true;
	CHECK(pvr_freelist_current_pages(fl) == 0);
	fl->failed = false;
	fl->cleaned = true;
	CHECK(pvr_freelist_current_pages(fl) == 0);
	fl->cleaned = false;
	fl->ready = false;
	CHECK(pvr_freelist_current_pages(fl) == 0);
	fl->ready = true;
	CHECK(pvr_freelist_current_pages(fl) == grown);
	CHECK(pvr_freelist_destroy(&fl) == 0 && fl == NULL);
}

int
main(void)
{
	backing.size = 524288 * 4;
	backing.cpu = malloc(backing.size); backing.visible = malloc(backing.size);
	CHECK(backing.cpu != NULL && backing.visible != NULL);
	valid_case(512, 65536, 256, 13, 64);
	valid_case(28, 28, 0, 0, 0);
	valid_case(4, 8, 4, 100, 4);
	valid_case(0, 524288, 4, 13, 0);
	invalid_cases(); allocation_failures(); retained_failures();
	growth_cases(); growth_failures(); current_pages_tests(); reset_state();
	free(backing.cpu); free(backing.visible);
	printf("pvr FreeList: %u checks passed\n", checks);
	return 0;
}
