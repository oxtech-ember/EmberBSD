/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted real-source tests. */
/* Models native services only. No hardware DMA, firmware or MMIO occurs. */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cixvpu_fw_plan.h"

/* Native endian/atomic services for actual-source DMA publication. */
#define atomic_store_release(p,v) __atomic_store_n((p),(v),__ATOMIC_RELEASE)
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define htole32(v) ((uint32_t)(v))
#else
#define htole32(v) __builtin_bswap32((uint32_t)(v))
#endif
static uint32_t le32dec(const void *p)
{
    const uint8_t *b=p;
    return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;
}

static unsigned int checks;
#define CHECK(c) do { checks++; if (!(c)) { \
    fprintf(stderr, "FAIL line %u: %s\n", (unsigned int)__LINE__, #c); \
    exit(1); } } while (0)
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif
#define KM_SLEEP 0
#define BUS_DMA_WAITOK 0
#define BUS_DMA_COHERENT 4
#define BUS_DMASYNC_PREREAD 1
#define BUS_DMASYNC_POSTREAD 2
#define BUS_DMASYNC_PREWRITE 4
#define BUS_DMASYNC_POSTWRITE 8
/* Full public map/segment fields used by production; private fields unused. */
typedef uint64_t bus_addr_t;
typedef uint64_t bus_size_t;
typedef struct { bus_addr_t ds_addr; bus_size_t ds_len; } bus_dma_segment_t;
struct test_tag { unsigned int id; };
typedef struct test_tag *bus_dma_tag_t;
struct test_map {
    bus_size_t dm_maxsegsz, dm_mapsize;
    int dm_nsegs;
    bus_dma_segment_t dm_segs[1];
};
typedef struct test_map *bus_dmamap_t;
struct cixvpu_prepared;
static void (*sync_observer)(unsigned int, bus_addr_t, bus_size_t, int);
static void (*unload_observer)(unsigned int);
static struct test_tag tag32 = { 32 }, tag64 = { 64 }, child = { 40 };
struct resource {
    bus_dma_segment_t allocation;
    void *kva;
    bus_dmamap_t map;
    int flags;
};
static struct resource resources[2048];
static unsigned int stack[8193], depth, native_calls, live_kmem, next_owner;
static unsigned int fail_at, step, malformed, malformed_owner, selected;
static bool intr, softintr, alias_tag, fail_kmem;
static void
push(unsigned int token)
{
    CHECK(depth < sizeof(stack) / sizeof(stack[0]));
    stack[depth++] = token;
}
static void
pop(unsigned int token)
{
    CHECK(depth != 0 && stack[depth - 1] == token);
    depth--;
}
static bool
failure(void)
{
    native_calls++;
    return ++step == fail_at;
}
static int cpu_intr_p(void) { return intr; }
static int cpu_softintr_p(void) { return softintr; }
static void *
kmem_zalloc(size_t size, int flags)
{
    void *p;
    CHECK(flags == KM_SLEEP);
    if (fail_kmem) return NULL;
    p = calloc(1, size);
    CHECK(p != NULL);
    live_kmem++;
    return p;
}
static void
kmem_free(void *p, size_t size)
{
    CHECK(p != NULL && size != 0 && live_kmem != 0);
    live_kmem--;
    free(p);
}
static int
bus_dmatag_subregion(bus_dma_tag_t parent, bus_addr_t low, bus_addr_t high,
    bus_dma_tag_t *out, int flags)
{
    CHECK(parent == &tag32 || parent == &tag64);
    CHECK(low == 0 && high == UINT64_C(0xffffffffff));
    CHECK(flags == BUS_DMA_WAITOK);
    selected = parent->id;
    *out = &child; /* Deliberately dirty even on failure. */
    if (failure()) return EIO;
    if (alias_tag) *out = parent;
    push(0);
    return 0;
}
static void
bus_dmatag_destroy(bus_dma_tag_t tag)
{
    CHECK(tag == &child || (alias_tag && tag->id == selected));
    native_calls++;
    pop(0);
}
static void
tag_check(bus_dma_tag_t tag)
{
    CHECK(tag == &child || (alias_tag && tag->id == selected));
}
static int
bus_dmamem_alloc(bus_dma_tag_t tag, bus_size_t size, bus_size_t align,
    bus_size_t boundary, bus_dma_segment_t *segs, int capacity, int *n,
    int flags)
{
    unsigned int i = next_owner++;
    tag_check(tag);
    CHECK(size == 4096 && align == 4096 && boundary == 0);
    CHECK(capacity == 1 && flags == BUS_DMA_WAITOK && i < 2048);
    if (failure()) return ENOMEM;
    /* CPU physical allocation and loaded bus addresses intentionally differ. */
    segs[0].ds_addr = UINT64_C(0x20000000) + i * UINT64_C(0x3000);
    segs[0].ds_len = 4096;
    resources[i].allocation = segs[0];
    *n = 1;
    if (i == malformed_owner) {
        if (malformed == 1) *n = 0;
        if (malformed == 2) *n = 2;
        if (malformed == 3) *n = -1;
    }
    push(1 + i * 4);
    return 0;
}
static unsigned int
allocation_index(const bus_dma_segment_t *segs)
{
    uint64_t offset = segs[0].ds_addr - UINT64_C(0x20000000);
    unsigned int i = (unsigned int)(offset / 0x3000);
    CHECK(i < next_owner && offset % 0x3000 == 0);
    CHECK(segs[0].ds_len == resources[i].allocation.ds_len);
    return i;
}
static void
bus_dmamem_free(bus_dma_tag_t tag, bus_dma_segment_t *segs, int n)
{
    unsigned int i;
    tag_check(tag);
    CHECK(n == 1); /* Reject OOB cleanup of modeled corrupt nalloc. */
    i = allocation_index(segs);
    native_calls++;
    pop(1 + i * 4);
}
static int
bus_dmamem_map(bus_dma_tag_t tag, bus_dma_segment_t *segs, int n,
    size_t size, void **out, int flags)
{
    unsigned int i = allocation_index(segs);
    tag_check(tag);
    CHECK(n == 1 && size == 4096);
    CHECK(flags == 0 || flags == BUS_DMA_COHERENT);
    *out = NULL;
    if (failure()) return ENOMEM;
    resources[i].kva = malloc(4096);
    CHECK(resources[i].kva != NULL);
    memset(resources[i].kva, 0xa5, 4096);
    resources[i].flags = flags;
    *out = resources[i].kva;
    push(2 + i * 4);
    return 0;
}
static unsigned int
kva_index(void *kva)
{
    unsigned int i;
    for (i = 0; i < next_owner; i++)
        if (resources[i].kva == kva) return i;
    CHECK(false);
    return 0;
}
static void
bus_dmamem_unmap(bus_dma_tag_t tag, void *kva, size_t size)
{
    unsigned int i = kva_index(kva);
    tag_check(tag);
    CHECK(size == 4096);
    native_calls++;
    pop(2 + i * 4);
    free(kva);
    resources[i].kva = NULL;
}
static int
bus_dmamap_create(bus_dma_tag_t tag, bus_size_t size, int n,
    bus_size_t maxseg, bus_size_t boundary, int flags, bus_dmamap_t *out)
{
    unsigned int i = next_owner - 1;
    tag_check(tag);
    CHECK(size == 4096 && n == 1 && maxseg == 4096 && boundary == 4096);
    CHECK(flags == BUS_DMA_WAITOK);
    *out = NULL;
    if (failure()) return ENOMEM;
    *out = calloc(1, sizeof(**out));
    CHECK(*out != NULL);
    (*out)->dm_maxsegsz = maxseg;
    resources[i].map = *out;
    push(3 + i * 4);
    return 0;
}
static unsigned int
map_index(bus_dmamap_t map)
{
    unsigned int i;
    for (i = 0; i < next_owner; i++)
        if (resources[i].map == map) return i;
    CHECK(false);
    return 0;
}
static void
bus_dmamap_destroy(bus_dma_tag_t tag, bus_dmamap_t map)
{
    unsigned int i = map_index(map);
    tag_check(tag);
    native_calls++;
    pop(3 + i * 4);
    free(map);
    resources[i].map = NULL;
}
static uint64_t
bus_address(unsigned int i)
{
    return UINT64_C(0x9000000000) + i * UINT64_C(0x5000);
}
static int
bus_dmamap_load(bus_dma_tag_t tag, bus_dmamap_t map, void *kva,
    bus_size_t size, void *proc, int flags)
{
    unsigned int i = map_index(map);
    tag_check(tag);
    CHECK(kva == resources[i].kva && size == 4096 && proc == NULL);
    CHECK(flags == BUS_DMA_WAITOK); /* No premature direction assumption. */
    if (failure()) return EIO;
    map->dm_nsegs = 1;
    map->dm_mapsize = 4096;
    map->dm_segs[0].ds_addr = bus_address(i);
    map->dm_segs[0].ds_len = 4096;
    if (i == malformed_owner) {
        switch (malformed) {
        case 4: map->dm_nsegs = 0; break;
        case 5: map->dm_nsegs = 2; break;
        case 6: map->dm_nsegs = -1; break;
        case 7: map->dm_mapsize = 8192; break;
        case 8: map->dm_segs[0].ds_addr++; break;
        case 9: map->dm_segs[0].ds_len = 0; break;
        case 10: map->dm_segs[0].ds_len = 8192; break;
        case 11: map->dm_segs[0].ds_addr = UINT64_C(1) << 40; break;
        case 12: map->dm_segs[0].ds_addr = UINT64_MAX - 4095; break;
        case 13: map->dm_segs[0].ds_addr = (UINT64_C(1) << 40) - 4096; break;
        case 14: map->dm_segs[0].ds_addr = bus_address(0); break;
        default: break;
        }
    }
    push(4 + i * 4);
    return 0;
}
static void
bus_dmamap_unload(bus_dma_tag_t tag, bus_dmamap_t map)
{
    unsigned int i = map_index(map);
    if (unload_observer != NULL) unload_observer(i);
    tag_check(tag);
    native_calls++;
    pop(4 + i * 4);
}
static void
bus_dmamap_sync(bus_dma_tag_t tag, bus_dmamap_t map,
    bus_addr_t offset, bus_size_t length, int ops)
{
    tag_check(tag);
    CHECK(sync_observer != NULL);
    native_calls++;
    sync_observer(map_index(map), offset, length, ops);
}

#ifdef CIXVPU_DMA_API
#include "dma-header.inc"
#include "dma-source.inc"
#else
struct cixvpu_dma;
struct cixvpu_dma_page {
    void *kva;
    bus_addr_t bus_address;
    struct cixvpu_fw_plan_page plan_page;
};
static int cixvpu_dma_alloc(bus_dma_tag_t a, bus_dma_tag_t b,
    const struct cixvpu_fw_plan *p, struct cixvpu_dma **out)
{ (void)a; (void)b; (void)p; (void)out; return ENOSYS; }
static int cixvpu_dma_page(struct cixvpu_dma *p, unsigned int i,
    struct cixvpu_dma_page *out)
{ (void)p; (void)i; (void)out; return ENOSYS; }
static int cixvpu_dma_free(struct cixvpu_dma **p)
{ (void)p; return ENOSYS; }
#endif
static void
put32(uint8_t *p, uint32_t n)
{
    p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8);
    p[2] = (uint8_t)(n >> 16); p[3] = (uint8_t)(n >> 24);
}
static struct cixvpu_fw_plan
fixture(void)
{
    uint8_t header[CIXVPU_FW_HEADER_SIZE] = { 0 };
    struct cixvpu_fw_plan plan;
    header[4] = 5; header[5] = 3; header[6] = header[7] = 8;
    put32(header + 96, 4097);
    put32(header + 100, 0x8000); put32(header + 104, 6);
    put32(header + 108, 0x23); put32(header + 172, 0xa000);
    put32(header + 176, 4097);
    CHECK(cixvpu_fw_plan_build(header, 4097, 4, &plan) == 0);
    /* T2 + private3*4 + shared2 + common7 + print128 + root1 + leaf5. */
    CHECK(plan.backing_pages == 157 && plan.mappings == 163);
    return plan;
}
static void
reset(void)
{
    (void)bus_dmamap_sync; /* The allocation-only runner must never call it. */
    CHECK(depth == 0 && live_kmem == 0);
    memset(resources, 0, sizeof(resources));
    native_calls = next_owner = step = fail_at = malformed = selected = 0;
    malformed_owner = 0;
    intr = softintr = alias_tag = fail_kmem = false;
    sync_observer = NULL; unload_observer = NULL;
}
static void
allocation_error(const struct cixvpu_fw_plan *plan)
{
    struct cixvpu_dma *out = NULL;
    CHECK(cixvpu_dma_alloc(&tag32, &tag64, plan, &out) != 0);
    CHECK(out == NULL && depth == 0 && live_kmem == 0);
}
static void
page_error(struct cixvpu_dma *pool, unsigned int index)
{
    struct cixvpu_dma_page page, before;
    unsigned int calls = native_calls;
    memset(&page, 0xa5, sizeof(page)); before = page;
    CHECK(cixvpu_dma_page(pool, index, &page) != 0);
    CHECK(memcmp(&page, &before, sizeof(page)) == 0);
    CHECK(native_calls == calls);
}
int
main(void)
{
    struct cixvpu_fw_plan plan = fixture(), bad;
    struct cixvpu_dma *pool = NULL, *before;
    struct cixvpu_dma_page page;
    unsigned int i, j, calls;
    reset();
    if (PAGE_SIZE != 4096) {
        CHECK(cixvpu_dma_alloc(&tag32, &tag64, &plan, &pool) == EOPNOTSUPP);
        CHECK(pool == NULL && native_calls == 0 && live_kmem == 0);
        printf("cixvpu-dma: %u checks, non-4KiB rejected\n", checks);
        return 0;
    }
    /* Missing implementation, wrong tag preference or alias duplicate owners fails. */
    CHECK(cixvpu_dma_alloc(&tag32, &tag64, &plan, &pool) == 0);
    CHECK(selected == 64 && next_owner == 157 && pool != NULL);
    for (i = 0; i < 157; i++) {
        CHECK(cixvpu_dma_page(pool, i, &page) == 0);
        CHECK(page.plan_page.backing_index == i);
        CHECK(page.kva == resources[i].kva && page.bus_address == bus_address(i));
        for (j = 0; j < 4096; j++) CHECK(((uint8_t *)page.kva)[j] == 0);
        CHECK(resources[i].flags == ((i >= 16 && i < 151) ? BUS_DMA_COHERENT : 0));
    }
    /* Snapshot must survive mutation of caller's plan. */
    bad = plan; plan.backing_pages = 0;
    CHECK(cixvpu_dma_page(pool, 156, &page) == 0);
    plan = bad;
    page_error(pool, 157); page_error(NULL, 0);
    calls = native_calls; before = pool;
    CHECK(cixvpu_dma_alloc(&tag32, &tag64, &plan, &pool) == EBUSY);
    CHECK(pool == before && native_calls == calls);
    intr = true; page_error(pool, 0);
    CHECK(cixvpu_dma_free(&pool) == EWOULDBLOCK && pool == before);
    intr = false;
    CHECK(cixvpu_dma_free(&pool) == 0 && pool == NULL);
    CHECK(depth == 0 && live_kmem == 0);
    reset(); alias_tag = true;
    CHECK(cixvpu_dma_alloc(&tag32, NULL, &plan, &pool) == 0 && selected == 32);
    CHECK(cixvpu_dma_free(&pool) == 0 && depth == 0 && live_kmem == 0);
    reset();
    CHECK(cixvpu_dma_alloc(NULL, NULL, &plan, &pool) == EINVAL);
    CHECK(cixvpu_dma_alloc(&tag32, &tag64, NULL, &pool) == EINVAL);
    CHECK(cixvpu_dma_alloc(&tag32, &tag64, &plan, NULL) == EINVAL);
    CHECK(cixvpu_dma_free(NULL) == EINVAL);
    CHECK(cixvpu_dma_free(&pool) == EINVAL);
    CHECK(native_calls == 0 && live_kmem == 0);
    intr = true; allocation_error(&plan); intr = false;
    softintr = true; allocation_error(&plan); softintr = false;
    CHECK(native_calls == 0);
    fail_kmem = true; allocation_error(&plan); reset();
    bad = plan; bad.backing_pages++; allocation_error(&bad);
    bad = plan; bad.mappings--; allocation_error(&bad);
    bad = plan; bad.cores = UINT32_MAX; allocation_error(&bad);
    bad = plan; bad.info.shared_pages++; allocation_error(&bad);
    CHECK(native_calls == 0);
    /* Exhaust every native failure position, including first and final owner. */
    for (i = 1; i <= 1 + 4 * 157; i++) {
        reset(); fail_at = i; allocation_error(&plan);
    }
    for (i = 1; i <= 12; i++) {
        for (j = 0; j < 3; j++) {
            reset(); malformed = i;
            malformed_owner = j == 0 ? 0 : j == 1 ? 78 : 156;
            allocation_error(&plan);
        }
    }
    reset(); malformed = 14; malformed_owner = 78;
    allocation_error(&plan);
    reset(); malformed = 13;
    CHECK(cixvpu_dma_alloc(&tag32, &tag64, &plan, &pool) == 0);
    CHECK(cixvpu_dma_page(pool, 0, &page) == 0);
    CHECK(page.bus_address == (UINT64_C(1) << 40) - 4096);
    CHECK(cixvpu_dma_free(&pool) == 0 && depth == 0 && live_kmem == 0);
    printf("cixvpu-dma: %u checks, 629 failure positions, 0 failures\n", checks);
    return 0;
}
