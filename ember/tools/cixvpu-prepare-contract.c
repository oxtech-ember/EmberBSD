/* SPDX-License-Identifier: BSD-2-Clause */
/* Reuse actual allocator and its platform boundary, never a copied algorithm. */
#define main cixvpu_allocation_contract_main
#include "cixvpu-dma-contract.c"
#undef main
#include "cixvpu_mmu.h"
#include "cixvpu_ring.h"

_Alignas(max_align_t) static uint8_t image[8192], pinned_image[8192];
_Alignas(max_align_t) static uint8_t rpc_image[420736], rpc_pinned[420736];
static const uint8_t fixture_pin[32] = { 0x47, 0x21, 0x83, 0xb0 };
static size_t extent = sizeof(image);
static struct cixvpu_fw_plan make_plan(unsigned int cores)
{
    memset(image, 0x79, sizeof(image));
    memset(image, 0, CIXVPU_FW_HEADER_SIZE);
    image[4] = 5; image[5] = 3;
    put32(image + 96, 4097); put32(image + 100, 0x8000);
    put32(image + 104, 6); put32(image + 108, 0x23);
    put32(image + 172, 0xa000); put32(image + 176, 4097);
    memcpy(pinned_image, image, sizeof(image));
    struct cixvpu_fw_plan plan;
    CHECK(cixvpu_fw_plan_build(image, sizeof(image), cores, &plan) == 0);
    return plan;
}
#ifdef CIXVPU_PREPARE_API
/* Native SHA is a service boundary, not a modeled cryptographic claim.
 * Match the complete immutable synthetic fixture_pin; native object uses libkern SHA. */
#define SHA256_DIGEST_LENGTH 32
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
typedef struct { unsigned int phase; bool matches; } SHA256_CTX;
static unsigned int hashes, page_calls, map_calls, link_calls, encode_calls;
static unsigned int meta_calls, fail_meta, fail_page, fail_map, fail_link, fail_encode;
static unsigned int view_fault, duplicate_map, rpc_table_fault;
static unsigned int success_counts[4];
static bool fail_free, top_bus;
#ifdef CIXVPU_SYNC_API
static bool fail_seal;
#endif
static int SHA256_Init(SHA256_CTX *ctx)
{ ctx->phase = 1; ctx->matches = false; return 1; }
static int SHA256_Update(SHA256_CTX *ctx, const uint8_t *p, size_t n)
{ CHECK(ctx->phase == 1 && ((p == image && n == extent) ||
    (p == rpc_image && n == sizeof(rpc_image))));
  ctx->matches = memcmp(p, p == image ? pinned_image : rpc_pinned, n) == 0;
  ctx->phase = 2; hashes++; return 1; }
static int SHA256_Final(uint8_t out[32], SHA256_CTX *ctx)
{ CHECK(ctx->phase == 2); memcpy(out, fixture_pin, 32); if (!ctx->matches) out[0] ^= 1; return 1; }
static void *prepare_kmem(size_t n, int flags)
{ if (++meta_calls == fail_meta) return NULL; return kmem_zalloc(n, flags); }
static int prepare_page(struct cixvpu_dma *dma, unsigned int index, struct cixvpu_dma_page *out)
{
    if (++page_calls == fail_page) return EIO;
    int error = cixvpu_dma_page(dma, index, out);
    if (error == ERANGE && view_fault == 8) return cixvpu_dma_page(dma, 0, out);
    if (error == 0 && index == 1) {
        if (view_fault == 1) out->plan_page.image_bytes++;
        if (view_fault == 2) out->bus_address++;
        if (view_fault == 3) out->bus_address = UINT64_C(1) << 40;
        if (view_fault == 4) out->bus_address = bus_address(0);
        if (view_fault == 5) out->kva = resources[0].kva;
        if (view_fault == 6) out->kva = (void *)(UINTPTR_MAX - 3);
        if (view_fault == 7) out->kva = image;
    }
    return error;
}
static int prepare_map(void *table, size_t n, uint32_t va, uint64_t bus, unsigned int attr, unsigned int perm)
{
    if (++map_calls == fail_map) return EIO;
    int error = cixvpu_mmu_map(table, n, va, bus, attr, perm);
    if (error == 0 && va == 0x20000000 && rpc_table_fault == 1) put32((uint8_t *)table + 128, 1);
    if (error == 0 && map_calls == duplicate_map) return cixvpu_mmu_map(table, n, va, bus, attr, perm);
    return error;
}
static int prepare_link(void *table, size_t n, uint32_t va, uint64_t bus)
{ if (++link_calls == fail_link) return EIO;
  int error=cixvpu_mmu_link(table,n,va,bus);
  if(error==0 && va==0x20000000 && rpc_table_fault==2) put32((uint8_t *)table+129*4,1);
  return error; }
static int prepare_encode(uint64_t bus, unsigned int attr, unsigned int perm, uint32_t *out)
{ if (++encode_calls == fail_encode) return EIO; return cixvpu_mmu_encode(bus, attr, perm, out); }
#ifdef CIXVPU_SYNC_API
static int prepare_free(struct cixvpu_dma **owner, const struct cixvpu_prepared *p)
{ if (fail_free) return EIO; return cixvpu_dma_free_prepared(owner, p); }
static int prepare_seal(struct cixvpu_dma *owner, const struct cixvpu_prepared *p)
{ if (fail_seal) return EIO; return cixvpu_dma_seal(owner, p); }
#else
static int prepare_free(struct cixvpu_dma **owner)
{ if (fail_free) return EIO; return cixvpu_dma_free(owner); }
#endif
#include "prepare-header.inc"
#define kmem_zalloc prepare_kmem
#define cixvpu_dma_page(...) prepare_page(__VA_ARGS__)
#ifdef CIXVPU_SYNC_API
#define cixvpu_dma_free_prepared prepare_free
#define cixvpu_dma_seal prepare_seal
#else
#define cixvpu_dma_free prepare_free
#endif
#define cixvpu_mmu_map prepare_map
#define cixvpu_mmu_link prepare_link
#define cixvpu_mmu_encode prepare_encode
#include "prepare-source.inc"
#undef kmem_zalloc
#undef cixvpu_dma_page
#undef cixvpu_dma_free
#undef cixvpu_dma_free_prepared
#undef cixvpu_dma_seal
#undef cixvpu_mmu_map
#undef cixvpu_mmu_link
#undef cixvpu_mmu_encode

static struct cixvpu_dma *begin(unsigned int cores, struct cixvpu_fw_plan *plan)
{
    reset(); *plan = make_plan(cores);
    if (top_bus) { malformed = 13; top_bus = false; }
    struct cixvpu_dma *owner = NULL;
    CHECK(cixvpu_dma_alloc(&tag32, &tag64, plan, &owner) == 0);
    for (unsigned int i = 0; i < plan->backing_pages; i++) memset(resources[i].kva, 0xa5, 4096);
    hashes = page_calls = map_calls = link_calls = encode_calls = meta_calls = 0;
    fail_meta = fail_page = fail_map = fail_link = fail_encode = view_fault = duplicate_map = 0;
    fail_free = false;
#ifdef CIXVPU_SYNC_API
    fail_seal = false;
#endif
    return owner;
}
static uint32_t word(const void *p, unsigned int at)
{
    const uint8_t *b = p;
    return (uint32_t)b[4 * at] | (uint32_t)b[4 * at + 1] << 8 |
        (uint32_t)b[4 * at + 2] << 16 | (uint32_t)b[4 * at + 3] << 24;
}
static void prepared_bytes(const struct cixvpu_fw_plan *plan)
{
    unsigned int tables[6] = {0}, ntables = 0;
    uint32_t expected[6][1024] = {{0}};
    for (unsigned int i = 0; i < plan->backing_pages; i++) {
        struct cixvpu_fw_plan_page p; CHECK(cixvpu_fw_plan_page(plan, i, &p) == 0);
        const uint8_t *bytes = resources[i].kva;
        if (p.role == CIXVPU_FW_PLAN_ROOT || p.role == CIXVPU_FW_PLAN_LEAF) { tables[ntables++] = i; continue; }
        for (unsigned int j = 0; j < 4096; j++) CHECK(bytes[j] == (p.role == CIXVPU_FW_PLAN_TEXT && j < p.image_bytes ? image[p.image_offset + j] : 0));
    }
    CHECK(ntables == plan->cores + 2);
    for (unsigned int m = 0; m < plan->mappings; m++) {
        struct cixvpu_fw_plan_mapping p; CHECK(cixvpu_fw_plan_mapping(plan, m, &p) == 0);
        unsigned int t = 0; while (t < ntables && tables[t] != p.leaf_backing_index) t++;
        CHECK(t < ntables);
        expected[t][(p.address >> 12) & 1023] = (uint32_t)(bus_address(p.backing_index) >> 12) << 2 | p.attribute << 30 | p.permission;
    }
    /* Root is the first table owner; every link is PRIVATE/READ. */
    for (unsigned int t = 1; t < ntables; t++) {
        struct cixvpu_fw_plan_page p; CHECK(cixvpu_fw_plan_page(plan, tables[t], &p) == 0);
        expected[0][p.leaf_address >> 22] = (uint32_t)(bus_address(tables[t]) >> 12) << 2 | 1;
    }
    for (unsigned int t = 0; t < ntables; t++) for (unsigned int i = 0; i < 1024; i++) CHECK(word(resources[tables[t]].kva, i) == expected[t][i]);
    CHECK(word(resources[tables[1]].kva, 1) == ((uint32_t)(bus_address(0) >> 12) << 2 | 2));
}
static void success_tests(void)
{
    for (unsigned int cores = 1; cores <= 4; cores += 3) {
        struct cixvpu_fw_plan plan;
        struct cixvpu_dma *owner = begin(cores, &plan), *old = owner;
        struct cixvpu_prepared *out = NULL;
        CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, fixture_pin, &out) == 0);
        CHECK(owner == NULL && out != NULL && hashes == 1);
        struct cixvpu_dma_page stale, before;
        memset(&stale, 0xa5, sizeof(stale)); before = stale;
        unsigned int calls = native_calls;
        CHECK(cixvpu_dma_page(old, 0, &stale) == EBUSY);
        CHECK(memcmp(&stale, &before, sizeof(stale)) == 0);
        struct cixvpu_dma *alias = old;
        CHECK(cixvpu_dma_free(&alias) == EBUSY && alias == old);
        CHECK(native_calls == calls);
        prepared_bytes(&plan);
        for (unsigned int i = 0; i < plan.backing_pages; i++) {
            struct cixvpu_fw_plan_page p;
            CHECK(cixvpu_fw_plan_page(&plan, i, &p) == 0);
            if (p.role == CIXVPU_FW_PLAN_ROOT)
                CHECK(out->root_entry == ((uint32_t)(bus_address(i) >> 12) << 2 | 3));
        }
        success_counts[0] = page_calls; success_counts[1] = map_calls;
        success_counts[2] = link_calls; success_counts[3] = encode_calls;
        struct cixvpu_prepared *same = out;
        CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, fixture_pin, &out) == EINVAL && out == same);
        fail_free = true; struct cixvpu_prepared *saved = out;
        CHECK(cixvpu_prepared_free(&out) == EIO && out == saved);
        fail_free = false; intr = true;
        CHECK(cixvpu_prepared_free(&out) == EWOULDBLOCK && out == saved);
        intr = false; CHECK(cixvpu_prepared_free(&out) == 0 && out == NULL);
        CHECK(depth == 0 && live_kmem == 0);
    }
}
static void failure_tests(void)
{
    for (unsigned int kind = 0; kind < 13; kind++) {
        struct cixvpu_fw_plan plan;
        struct cixvpu_dma *owner = begin(4, &plan), *saved = owner;
        struct cixvpu_prepared *out = NULL;
        if (kind < 7) view_fault = kind + 1;
        if (kind == 7) image[4000] ^= 1;
        if (kind == 8) plan.info.description[0] ^= 1;
        if (kind == 9) plan.backing_pages++;
        if (kind == 10) fail_meta = 1;
        if (kind == 11) fail_meta = 2;
        if (kind == 12) duplicate_map = 2;
        CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, fixture_pin, &out) != 0);
        CHECK(owner == saved && out == NULL);
        if (kind != 12) for (unsigned int i = 0; i < owner->count; i++) for (unsigned int j = 0; j < 4096; j++) CHECK(((uint8_t *)resources[i].kva)[j] == 0xa5);
        CHECK(cixvpu_dma_free(&owner) == 0 && owner == NULL && depth == 0 && live_kmem == 0);
    }
    for (unsigned int stage = 0; stage < 4; stage++) {
        for (unsigned int at = 1; at <= success_counts[stage]; at++) {
            struct cixvpu_fw_plan plan;
            struct cixvpu_dma *owner = begin(4, &plan), *saved = owner;
            struct cixvpu_prepared *out = NULL;
            if (stage == 0) fail_page = at;
            if (stage == 1) fail_map = at;
            if (stage == 2) fail_link = at;
            if (stage == 3) fail_encode = at;
            CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, fixture_pin, &out) != 0);
            CHECK(owner == saved && out == NULL);
            CHECK(cixvpu_dma_free(&owner) == 0 && depth == 0 && live_kmem == 0);
        }
    }
}
static void handle_alias_tests(void)
{
    for (unsigned int kind = 0; kind < 4; kind++) {
        struct cixvpu_fw_plan plan;
        struct cixvpu_dma *owner = begin(4, &plan), *saved = owner;
        struct cixvpu_prepared *out = NULL;
        void *slot = kind < 2 ? (void *)(image + 8176) : resources[2].kva;
        uint8_t before[4096];
        if ((kind & 1) == 0) *(struct cixvpu_prepared **)slot = NULL;
        else *(struct cixvpu_dma **)slot = owner;
        memcpy(pinned_image, image, sizeof(image));
        if (kind >= 2) memcpy(before, resources[2].kva, sizeof(before));
        int error = (kind & 1) == 0 ?
            cixvpu_prepare_cpu(&owner, &plan, image, extent, fixture_pin, slot) :
            cixvpu_prepare_cpu(slot, &plan, image, extent, fixture_pin, &out);
        CHECK(error == EINVAL && owner == saved && out == NULL);
        if (kind >= 2) CHECK(memcmp(before, resources[2].kva, sizeof(before)) == 0);
        else CHECK(memcmp(image, pinned_image, sizeof(image)) == 0);
        CHECK(cixvpu_dma_free(&owner) == 0 && depth == 0 && live_kmem == 0);
    }
}
static void argument_tests(void)
{
    struct cixvpu_fw_plan plan;
    struct cixvpu_dma *owner = begin(4, &plan), *saved = owner;
    struct cixvpu_prepared canary = {0}, *out = &canary;
    CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, fixture_pin, &out) == EBUSY && out == &canary);
    out = NULL;
    CHECK(cixvpu_prepare_cpu(NULL, &plan, image, extent, fixture_pin, &out) == EINVAL);
    CHECK(cixvpu_prepare_cpu(&owner, NULL, image, extent, fixture_pin, &out) == EINVAL);
    CHECK(cixvpu_prepare_cpu(&owner, &plan, NULL, extent, fixture_pin, &out) == EINVAL);
    CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, NULL, &out) == EINVAL);
    CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, fixture_pin, NULL) == EINVAL);
    CHECK(cixvpu_prepare_cpu(&owner, &plan, image, SIZE_MAX, fixture_pin, &out) == EINVAL);
    CHECK(cixvpu_prepare_cpu(&owner, &plan, (void *)(UINTPTR_MAX - 3), extent, fixture_pin, &out) == EINVAL);
    intr = true;
    CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, fixture_pin, &out) == EWOULDBLOCK);
    intr = false;
    CHECK(out == NULL && owner == saved && hashes == 0 && page_calls == 0);
    CHECK(cixvpu_dma_free(&owner) == 0 && depth == 0 && live_kmem == 0);
    const size_t fields[] = {
        offsetof(struct cixvpu_fw_info, jump), offsetof(struct cixvpu_fw_info, minor),
        offsetof(struct cixvpu_fw_info, major), offsetof(struct cixvpu_fw_info, format_bytes),
        offsetof(struct cixvpu_fw_info, description) + 55, offsetof(struct cixvpu_fw_info, part) + 7,
        offsetof(struct cixvpu_fw_info, revision) + 7, offsetof(struct cixvpu_fw_info, version) + 15,
        offsetof(struct cixvpu_fw_info, text_size), offsetof(struct cixvpu_fw_info, text_pages),
        offsetof(struct cixvpu_fw_info, bss_address), offsetof(struct cixvpu_fw_info, bss_pages),
        offsetof(struct cixvpu_fw_info, bitmap) + 63, offsetof(struct cixvpu_fw_info, shared_address),
        offsetof(struct cixvpu_fw_info, shared_size), offsetof(struct cixvpu_fw_info, shared_pages),
        offsetof(struct cixvpu_fw_info, private_pages)
    };
    for (unsigned int i = 0; i < __arraycount(fields); i++) {
        owner = begin(4, &plan); saved = owner; out = NULL;
        ((uint8_t *)&plan.info)[fields[i]] ^= 1;
        CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, fixture_pin, &out) == EINVAL);
        CHECK(owner == saved && out == NULL && page_calls == 0);
        CHECK(cixvpu_dma_free(&owner) == 0 && depth == 0 && live_kmem == 0);
    }
}
static void boundary_tests(void)
{
    struct cixvpu_fw_plan plan;
    top_bus = true;
    struct cixvpu_dma *owner = begin(1, &plan);
    struct cixvpu_prepared *out = NULL;
    CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, fixture_pin, &out) == 0);
    for (unsigned int i = 0; i < plan.backing_pages; i++) {
        struct cixvpu_fw_plan_page p; CHECK(cixvpu_fw_plan_page(&plan, i, &p) == 0);
        if (p.role == CIXVPU_FW_PLAN_LEAF && p.leaf_address == 0)
            CHECK(word(resources[i].kva, 1) == 0x3ffffffe);
    }
    CHECK(cixvpu_prepared_free(&out) == 0 && out == NULL && depth == 0 && live_kmem == 0);
    CHECK(cixvpu_prepared_free(&out) == EINVAL);
    owner = begin(4, &plan); struct cixvpu_dma *saved = owner;
    view_fault = 8;
    CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, fixture_pin, &out) == EINVAL);
    CHECK(owner == saved && out == NULL);
    for (unsigned int i = 0; i < plan.backing_pages; i++)
        for (unsigned int j = 0; j < 4096; j++)
            CHECK(((const uint8_t *)resources[i].kva)[j] == 0xa5);
    CHECK(cixvpu_dma_free(&owner) == 0 && depth == 0 && live_kmem == 0);
}
#ifdef CIXVPU_SYNC_API
/* A separate poisoned device backing models bounce, not CPU cache hardware.
 * Literal masks are independently checked against the protocol role list. */
static uint8_t device_memory[2048][4096];
static unsigned int pres[2048], posts[2048], sync_calls;
static bool bounce;
static struct cixvpu_fw_plan sync_plan;
static const unsigned int want_pre[] = {
    [CIXVPU_FW_PLAN_TEXT]=4,
    [CIXVPU_FW_PLAN_PRIVATE]=5, [CIXVPU_FW_PLAN_SHARED]=5,
    [CIXVPU_FW_PLAN_MSG_HOST]=4, [CIXVPU_FW_PLAN_MSG_MVE]=5,
    [CIXVPU_FW_PLAN_BUF_IN_HOST]=4, [CIXVPU_FW_PLAN_BUF_IN_MVE]=5,
    [CIXVPU_FW_PLAN_BUF_OUT_HOST]=4, [CIXVPU_FW_PLAN_BUF_OUT_MVE]=5,
    [CIXVPU_FW_PLAN_RPC]=5, [CIXVPU_FW_PLAN_PRINT]=5,
    [CIXVPU_FW_PLAN_RPC_DATA]=5,
    [CIXVPU_FW_PLAN_RPC_SECOND_DATA]=5,
    [CIXVPU_FW_PLAN_ROOT]=4, [CIXVPU_FW_PLAN_LEAF]=4
};
static uint8_t *device_bytes(unsigned int i)
{ return bounce ? device_memory[i] : resources[i].kva; }
static void observe_sync(unsigned int i, bus_addr_t offset, bus_size_t length, int ops)
{
    struct cixvpu_fw_plan_page p;
    CHECK(i < sync_plan.backing_pages && offset == 0 && length == 4096);
    CHECK(cixvpu_fw_plan_page(&sync_plan, i, &p) == 0);
    CHECK(p.role < __arraycount(want_pre));
    sync_calls++;
    if (ops & (BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD)) {
        CHECK(ops == (int)want_pre[p.role] && pres[i] == 0 && posts[i] == 0);
        if (p.role == CIXVPU_FW_PLAN_ROOT || p.role == CIXVPU_FW_PLAN_LEAF) {
            for (unsigned int j = 0; j < sync_plan.backing_pages; j++) {
                struct cixvpu_fw_plan_page q;
                CHECK(cixvpu_fw_plan_page(&sync_plan, j, &q) == 0);
                if (q.role == CIXVPU_FW_PLAN_LEAF && p.role != CIXVPU_FW_PLAN_ROOT) continue;
                if (q.role == CIXVPU_FW_PLAN_ROOT) continue;
                CHECK(pres[j] != 0);
                CHECK(memcmp(device_bytes(j), resources[j].kva, 4096) == 0);
            }
        }
        if (bounce && (ops & BUS_DMASYNC_PREWRITE))
            memcpy(device_memory[i] + offset, (uint8_t *)resources[i].kva + offset, (size_t)length);
        pres[i] = (unsigned int)ops;
    } else {
        /* PRE4 -> POST8; PRE5 -> POST10. Never mix PRE with POST. */
        unsigned int expected = want_pre[p.role] == 4 ? 8 : 10;
        CHECK(pres[i] == want_pre[p.role] && posts[i] == 0 && (unsigned int)ops == expected);
        if (bounce && (ops & BUS_DMASYNC_POSTREAD))
            memcpy((uint8_t *)resources[i].kva + offset, device_memory[i] + offset, (size_t)length);
        posts[i] = (unsigned int)ops;
    }
}
static void observe_unload(unsigned int i)
{
    (void)i;
    for (unsigned int j = 0; j < sync_plan.backing_pages; j++) CHECK(posts[j] != 0);
}
static void sync_fixture(const struct cixvpu_fw_plan *plan, bool use_bounce)
{
    sync_plan = *plan; bounce = use_bounce; sync_calls = 0;
    memset(pres, 0, sizeof(pres)); memset(posts, 0, sizeof(posts));
    memset(device_memory, 0xb7, sizeof(device_memory));
    sync_observer = observe_sync;
}
static struct cixvpu_prepared *prepared_begin(unsigned int cores, struct cixvpu_fw_plan *plan)
{
    struct cixvpu_dma *owner = begin(cores, plan);
    struct cixvpu_prepared *out = NULL;
    CHECK(cixvpu_prepare_cpu(&owner, plan, image, extent, fixture_pin, &out) == 0 && owner == NULL);
    return out;
}
static void sync_success_tests(void)
{
    for (unsigned int cores = 1; cores <= 4; cores += 3) {
        for (unsigned int mode = 0; mode < 2; mode++) {
            struct cixvpu_fw_plan plan;
            struct cixvpu_prepared *out = prepared_begin(cores, &plan);
            sync_fixture(&plan, mode != 0);
            CHECK(cixvpu_prepared_sync_initial(out) == 0);
            CHECK(sync_calls == plan.backing_pages);
            for (unsigned int i = 0; i < plan.backing_pages; i++) {
                struct cixvpu_fw_plan_page p;
                CHECK(cixvpu_fw_plan_page(&plan, i, &p) == 0);
                CHECK(pres[i] == want_pre[p.role] && posts[i] == 0);
                CHECK(memcmp(device_bytes(i), resources[i].kva, 4096) == 0);
            }
            unsigned int calls = native_calls;
            CHECK(cixvpu_prepared_sync_initial(out) == EBUSY && native_calls == calls);
            struct cixvpu_dma_page view;

            CHECK(cixvpu_dma_page(out->dma, 0, &view) == EBUSY);
            unload_observer = observe_unload;
            CHECK(cixvpu_prepared_free(&out) == 0 && out == NULL);
            CHECK(sync_calls == 2 * plan.backing_pages && depth == 0 && live_kmem == 0);
        }
    }
}
static void sync_failure_tests(void)
{
    for (unsigned int kind = 0; kind < 23; kind++) {
        struct cixvpu_fw_plan plan;
        struct cixvpu_prepared *out = prepared_begin(4, &plan), *saved = out;
        struct cixvpu_dma *dma = out->dma;
        unsigned int last = plan.backing_pages - 1;
        struct cixvpu_dma_owner owner = dma->owners[last];
        struct test_map map = *owner.map;
        uint32_t count = dma->count;
        bus_dma_tag_t tag = dma->tag;
        sync_fixture(&plan, true);
        switch (kind) {
        case 0: dma->count = 0; break;
        case 1: dma->tag_acquired = false; break;
        case 2: dma->owners[last].loaded = false; break;
        case 3: dma->owners[last].map = NULL; break;
        case 4: owner.map->dm_nsegs = 2; break;
        case 5: owner.map->dm_mapsize = 8192; break;
        case 6: owner.map->dm_segs[0].ds_len = 0; break;
        case 7: owner.map->dm_segs[0].ds_addr++; break;
        case 8: owner.map->dm_segs[0].ds_addr = UINT64_C(1) << 40; break;
        case 9: dma->owners[last].bus_address += 4096; break;
        case 10: dma->owners[last].page.backing_index++; break;
        case 11: dma->owners[last].page.role = 99; break;
        case 12: dma->owners[last].page.ordinal++; break;
        case 13: out->plan.mappings++; break;
        case 14: dma->owners[last].page.attribute++; break;
        case 15: dma->owners[last].page.core++; break;
        case 16: dma->owners[last].allocated = false; break;
        case 17: dma->owners[last].mapped = false; break;
        case 18: dma->owners[last].created = false; break;
        case 19: dma->tag = NULL; break;
        case 20: dma->owners[last].page.role = CIXVPU_FW_PLAN_ROOT; break;
        case 21: out->plan.info.text_pages++; break;
        case 22: dma->owners[last].pre_ops = BUS_DMASYNC_PREWRITE; break;
        }
        unsigned int calls = native_calls;
        CHECK(cixvpu_prepared_sync_initial(out) != 0);
        CHECK(native_calls == calls && sync_calls == 0 && out == saved);
        CHECK(!dma->ever_visible && dma->phase == CIXVPU_DMA_SEALED);
        /* Restore deliberately corrupted modeled resources before cleanup. */
        dma->count = count; dma->tag = tag; dma->tag_acquired = true;
        dma->owners[last] = owner; *owner.map = map; out->plan = plan;
        CHECK(cixvpu_prepared_free(&out) == 0 && out == NULL && live_kmem == 0 && depth == 0);
        CHECK(sync_calls == 0);
    }
    struct cixvpu_fw_plan plan;
    struct cixvpu_dma *owner = begin(4, &plan), *saved = owner;
    struct cixvpu_prepared *out = NULL;
    fail_seal = true;
    CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, fixture_pin, &out) == EIO);
    CHECK(owner == saved && out == NULL && owner->phase == CIXVPU_DMA_ALLOC);
    CHECK(cixvpu_dma_free(&owner) == 0 && depth == 0 && live_kmem == 0);
}
static void sync_owner_tests(void)
{
    struct cixvpu_fw_plan plan;
    struct cixvpu_prepared *out = prepared_begin(4, &plan), wrong = {0};
    struct cixvpu_dma *dma = out->dma, *alias = dma;
    sync_fixture(&plan, true);
    unsigned int calls = native_calls;
    CHECK(cixvpu_prepared_sync_initial(NULL) == EINVAL);
    CHECK(cixvpu_dma_sync_initial(dma, &wrong, &plan) == EBUSY);
    CHECK(cixvpu_dma_free_prepared(&alias, &wrong) == EBUSY && alias == dma);
    CHECK(cixvpu_dma_mark_exposed(dma, out) == EBUSY);
    CHECK(cixvpu_dma_seal(dma, out) == EBUSY);
    CHECK(cixvpu_dma_sync_initial(dma, out, NULL) == EINVAL);
    CHECK(cixvpu_dma_mark_exposed(NULL, out) == EINVAL);
    CHECK(cixvpu_dma_mark_exposed(dma, NULL) == EINVAL);
    intr = true;
    CHECK(cixvpu_prepared_sync_initial(out) == EWOULDBLOCK);
    CHECK(cixvpu_dma_mark_exposed(dma, out) == EWOULDBLOCK);
    intr = false; softintr = true;
    CHECK(cixvpu_prepared_sync_initial(out) == EWOULDBLOCK);
    softintr = false;
    CHECK(native_calls == calls && sync_calls == 0);
    CHECK(cixvpu_prepared_sync_initial(out) == 0);
    calls = native_calls;
    CHECK(cixvpu_dma_mark_exposed(dma, &wrong) == EBUSY);
    intr = true;
    CHECK(cixvpu_prepared_free(&out) == EWOULDBLOCK);
    intr = false;
    /* Even a late bad receipt/map rejects before the first POST or unload. */
    unsigned int last = plan.backing_pages - 1;
    int receipt = dma->owners[last].pre_ops;
    dma->owners[last].pre_ops = 0;
    CHECK(cixvpu_prepared_free(&out) != 0 && out != NULL);
    CHECK(native_calls == calls);
    dma->owners[last].pre_ops = receipt;
    /* A count can stay below the global quota yet exceed the real allocation. */
    dma->count++;
    CHECK(cixvpu_prepared_free(&out) != 0 && out != NULL && native_calls == calls);
    dma->count -= 2;
    CHECK(cixvpu_prepared_free(&out) != 0 && out != NULL && native_calls == calls);
    dma->count++;
    dma->bytes--;
    CHECK(cixvpu_prepared_free(&out) != 0 && out != NULL && native_calls == calls);
    dma->bytes++;
    unload_observer = observe_unload;
    CHECK(cixvpu_prepared_free(&out) == 0 && out == NULL && live_kmem == 0 && depth == 0);
}
static void exposure_tests(void)
{
    struct cixvpu_fw_plan plan;
    struct cixvpu_prepared *out = prepared_begin(4, &plan), *saved = out;
    struct cixvpu_dma *dma = out->dma, *alias = dma;
    sync_fixture(&plan, true);
    CHECK(cixvpu_prepared_sync_initial(out) == 0);
    CHECK(cixvpu_dma_mark_exposed(dma, out) == 0);
    CHECK(dma->ever_visible && dma->phase == CIXVPU_DMA_EXPOSED);
    unsigned int calls = native_calls;
    struct cixvpu_dma_page page, before;
    memset(&page, 0x7b, sizeof(page)); before = page;
    CHECK(cixvpu_dma_page(alias, 0, &page) == EBUSY);
    CHECK(memcmp(&page, &before, sizeof(page)) == 0);
    CHECK(cixvpu_dma_free(&alias) == EBUSY && alias == dma);
    CHECK(cixvpu_prepared_free(&out) == EBUSY && out == saved);
    CHECK(cixvpu_dma_free_prepared(&alias, out) == EBUSY && alias == dma);
    CHECK(cixvpu_prepared_sync_initial(out) == EBUSY);
    CHECK(cixvpu_dma_seal(dma, out) == EBUSY);
    CHECK(cixvpu_dma_mark_exposed(dma, out) == EBUSY);
    CHECK(native_calls == calls);
    /* Fault the reversible phase in the model: the immutable bit must still
     * close every release path. This never changes ever_visible. */
    dma->phase = CIXVPU_DMA_SYNCED;
    CHECK(cixvpu_prepared_free(&out) == EBUSY && out == saved);
    dma->phase = CIXVPU_DMA_ALLOC;
    CHECK(cixvpu_dma_page(alias, 0, &page) == EBUSY);
    CHECK(cixvpu_dma_free(&alias) == EBUSY && alias == dma);
    dma->phase = CIXVPU_DMA_EXPOSED;
    CHECK(dma->ever_visible && native_calls == calls);
    /* Host fixture only: no hardware exists. End modeled object lifetimes
     * after guard assertions without an unmark or product recovery API. */
    cixvpu_dma_release(dma);
    kmem_free(out, out->bytes);
    CHECK(depth == 0 && live_kmem == 0);
}
#endif
#ifdef CIXVPU_BOOT_API
/* Breaks caught: wrong fixed seed, publish-before-mark, missing alias region,
 * stale whole-page PREWRITE, omitted RX acquire/return, early ACK and retry
 * after malformed input. Device bytes are independent native-service backing. */
struct boot_sync { unsigned int page; uint64_t offset, length; int ops; };
static struct boot_sync boot_events[128];
static unsigned int boot_nevents, boot_host, boot_mve, boot_rpc;
static bool boot_unstable, boot_append;
static unsigned int boot_alloc_change, boot_resize_change;
static void boot_observe(unsigned int i, bus_addr_t offset, bus_size_t length, int ops)
{
    CHECK(boot_nevents < __arraycount(boot_events));
    CHECK(i < sync_plan.backing_pages && offset <= 4096 && length <= 4096-offset);
    boot_events[boot_nevents++] = (struct boot_sync){i,offset,length,ops};
    if (ops & BUS_DMASYNC_POSTREAD) {
        if (boot_append && i==boot_mve && length==4096) {
            /* Native bounce memcpy may fetch payload before the producer.
             * Simulate valid FW append+commit between these two copy ranges. */
            memcpy((uint8_t *)resources[i].kva+16,device_memory[i]+16,4080);
            put32(device_memory[i]+16,0x000407d1); put32(device_memory[i]+20,0);
            device_memory[i][0]=2; put32(device_memory[i]+4,0x000407d1);
            put32(device_memory[i]+8,0x000407d1); put32(device_memory[i]+12,0x000407d1);
            memcpy(resources[i].kva,device_memory[i],16); boot_append=false;
        } else memcpy((uint8_t *)resources[i].kva+offset,device_memory[i]+offset,(size_t)length);
    }

    if (ops & BUS_DMASYNC_PREWRITE)
        memcpy(device_memory[i]+offset,(uint8_t *)resources[i].kva+offset,(size_t)length);
    if (boot_unstable && i==boot_mve && ops==BUS_DMASYNC_PREREAD && length==4096)
        device_memory[i][0] ^= 1;
    if (boot_alloc_change!=0 && i==boot_rpc && offset==12 && length==12 &&
        ops==BUS_DMASYNC_PREREAD) {
        unsigned int off=4*(boot_alloc_change-1);
        put32(device_memory[i]+off,word(device_memory[i],boot_alloc_change-1)^1);
        boot_alloc_change=0;
    }
    if (boot_resize_change!=0 && i==boot_rpc && ops==BUS_DMASYNC_PREREAD &&
        ((offset==12 && length==8) || (offset==0 && length==12 && boot_nevents>=2 &&
        boot_events[boot_nevents-2].length==20 && boot_events[boot_nevents-2].ops==BUS_DMASYNC_POSTREAD))) {
        unsigned int at=boot_resize_change-1;
        put32(device_memory[i]+4*at,word(device_memory[i],at)^1);
        boot_resize_change=0;
    }
}
static struct cixvpu_prepared *boot_begin(void)
{
    struct cixvpu_fw_plan plan;
    struct cixvpu_dma *owner=begin(1,&plan);
    memcpy(image+80,"r0p0-sum0005",12); memcpy(pinned_image,image,sizeof(image));
    CHECK(cixvpu_fw_plan_build(image,extent,1,&plan)==0);
    struct cixvpu_prepared *out=NULL;
    CHECK(cixvpu_prepare_boot_cpu(&owner,&plan,image,extent,fixture_pin,&out)==0);
    CHECK(owner==NULL && out!=NULL);
    for(unsigned int i=0;i<plan.backing_pages;i++) {
        struct cixvpu_fw_plan_page p;
        CHECK(cixvpu_fw_plan_page(&plan,i,&p)==0);
        if(p.role==CIXVPU_FW_PLAN_MSG_HOST) boot_host=i;
        if(p.role==CIXVPU_FW_PLAN_MSG_MVE) boot_mve=i;
        if(p.role==CIXVPU_FW_PLAN_RPC) boot_rpc=i;
    }
    const uint8_t *h=resources[boot_host].kva;
    CHECK(h[0]==0 && h[1]==0 && h[2]==2 && h[3]==0);
    CHECK(word(h,4)==1006 && word(h,5)==1006);
    CHECK(word(h,1)==0 && word(h,2)==0 && word(h,3)==1006);
    sync_fixture(&plan,true);
    CHECK(cixvpu_prepared_sync_initial(out)==0);
    CHECK(memcmp(device_memory[boot_host],h,4096)==0);
    boot_nevents=0; boot_alloc_change=boot_resize_change=0; boot_unstable=boot_append=false; sync_observer=boot_observe;
    return out;
}
static void boot_drop(struct cixvpu_prepared *out)
{
    /* Host-only quarantine teardown, never a production exposure reset. */
    sync_observer=NULL; unload_observer=NULL;
    cixvpu_dma_release(out->dma); kmem_free(out,out->bytes);
    CHECK(depth==0 && live_kmem==0);
}
static void boot_message(unsigned int pos, uint16_t code, uint16_t size, uint32_t payload, uint32_t sum)
{
    uint8_t *m=device_memory[boot_mve];
    put32(m+16+pos*4,(uint32_t)code|((uint32_t)size<<16));
    if(size!=0) put32(m+16+((pos+1)%1020)*4,payload);
    unsigned int end=(pos+1+(size+3)/4)%1020;
    m[0]=(uint8_t)end; m[1]=(uint8_t)(end>>8);
    put32(m+4,sum); put32(m+8,sum); put32(m+12,sum);
}
static unsigned int debug_changes;
static bool debug_churn;
static void debug_observe(unsigned int i, bus_addr_t offset, bus_size_t length, int ops)
{
    CHECK(i==boot_rpc && length<=12 && offset+length<=12);
    CHECK(!((ops & (BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE)) &&
        (ops & (BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE))));
    if(ops & BUS_DMASYNC_PREWRITE) {
        CHECK(offset==0 && length==4 && ops==(BUS_DMASYNC_PREWRITE|BUS_DMASYNC_PREREAD));
        CHECK(word(resources[i].kva,0)==2 && word(device_memory[i],0)==1);
        CHECK(boot_nevents>=5 && boot_events[boot_nevents-2].ops==BUS_DMASYNC_POSTWRITE);
        CHECK(boot_events[boot_nevents-1].offset==4 && boot_events[boot_nevents-1].length==8);
    }
    if(ops==BUS_DMASYNC_POSTWRITE || (offset==4 && ops==BUS_DMASYNC_PREREAD))
        CHECK(word(resources[i].kva,0)==1 && word(device_memory[i],0)==1);
    boot_observe(i,offset,length,ops);
    if(offset==0 && length==12 && ops==BUS_DMASYNC_PREREAD &&
        (debug_churn || debug_changes!=0)) {
        if(word(device_memory[i],0)==0) put32(device_memory[i],1);
        else put32(device_memory[i]+4,word(device_memory[i],1)^1);
        if(debug_changes!=0) debug_changes--;
    }
}
static struct cixvpu_prepared *debug_begin(uint32_t state, uint32_t call, uint32_t size)
{
    struct cixvpu_prepared *p=boot_begin(); uint32_t root;
    CHECK(cixvpu_prepared_publish_root(p,&root)==0);
    memset(device_memory[boot_rpc],0x6d,4096);
    memset(resources[boot_rpc].kva,0xa5,4096);
    put32(device_memory[boot_rpc],state); put32(device_memory[boot_rpc]+4,call);
    put32(device_memory[boot_rpc]+8,size);
    debug_changes=0; debug_churn=false; sync_observer=debug_observe;
    return p;
}
static void debugparam_tests(void)
{
    enum cixvpu_boot_debug_result result;
    struct cixvpu_prepared *p;
    uint8_t before[4096];
    const uint32_t sizes[]={0,1,1012};
    for(unsigned int i=0;i<__arraycount(sizes);i++) {
        p=debug_begin(1,1,sizes[i]); result=CIXVPU_BOOT_DEBUG_IDLE;
        memcpy(before,device_memory[boot_rpc],4096);
        unsigned int calls=native_calls, owners=depth, heaps=live_kmem;
        CHECK(cixvpu_prepared_boot_debugparam(p,&result)==0 && result==CIXVPU_BOOT_DEBUG_REPLIED);
        CHECK(native_calls==calls+6 && depth==owners && live_kmem==heaps);
        CHECK(word(device_memory[boot_rpc],0)==2 && boot_nevents==6);
        CHECK(boot_events[0].ops==BUS_DMASYNC_POSTREAD && boot_events[0].length==12);
        CHECK(boot_events[1].ops==BUS_DMASYNC_PREREAD && boot_events[1].length==12);
        CHECK(boot_events[2].ops==BUS_DMASYNC_POSTREAD && boot_events[2].length==12);
        CHECK(boot_events[3].offset==0 && boot_events[3].length==4);
        CHECK(memcmp(before+4,device_memory[boot_rpc]+4,4092)==0);
        CHECK(((uint8_t *)resources[boot_rpc].kva)[12]==0xa5);
        struct cixvpu_boot_rpc_header h;
        CHECK(cixvpu_prepared_boot_rpc_header(p,&h)==0 && h.valid && h.state==1 && h.callid==1 && h.size==sizes[i]);
        calls=native_calls; boot_nevents=0;
        CHECK(cixvpu_prepared_boot_debugparam(p,&result)==0 && result==CIXVPU_BOOT_DEBUG_PENDING);
        CHECK(native_calls==calls+4 && boot_nevents==4);
        CHECK(word(device_memory[boot_rpc],0)==2);
        put32(device_memory[boot_rpc],0); boot_nevents=0;
        CHECK(cixvpu_prepared_boot_debugparam(p,&result)==0 && result==CIXVPU_BOOT_DEBUG_IDLE);
        /* An identical newly committed request is a new transaction. */
        put32(device_memory[boot_rpc],1); boot_nevents=0;
        CHECK(cixvpu_prepared_boot_debugparam(p,&result)==0 && result==CIXVPU_BOOT_DEBUG_REPLIED);
        CHECK(cixvpu_prepared_free(&p)==EBUSY); boot_drop(p);
    }
    const struct { uint32_t state,call,size; int error; } bad[]={
        {1,1,1013,EPROTO},{1,1,UINT32_MAX,EPROTO},
        {1,2,0,ENOTSUP},{1,3,8,ENOTSUP},{1,4,4,ENOTSUP},
        {1,UINT32_MAX,0,ENOTSUP},{3,1,0,EPROTO},{UINT32_MAX,1,0,EPROTO}};
    for(unsigned int i=0;i<__arraycount(bad);i++) {
        p=debug_begin(bad[i].state,bad[i].call,bad[i].size);
        memcpy(before,device_memory[boot_rpc],4096); result=CIXVPU_BOOT_DEBUG_PENDING;
        CHECK(cixvpu_prepared_boot_debugparam(p,&result)==bad[i].error);
        CHECK(result==CIXVPU_BOOT_DEBUG_PENDING && boot_nevents==4);
        CHECK(boot_events[3].ops==BUS_DMASYNC_PREREAD && boot_events[3].length==12);
        CHECK(memcmp(before,device_memory[boot_rpc],4096)==0);
        unsigned int calls=native_calls;
        CHECK(cixvpu_prepared_boot_debugparam(p,&result)==bad[i].error && native_calls==calls);
        boot_drop(p);
    }
    for(unsigned int state=0;state<=2;state+=2) {
        p=debug_begin(state,UINT32_MAX,UINT32_MAX);
        memcpy(before,device_memory[boot_rpc],4096);
        CHECK(cixvpu_prepared_boot_debugparam(p,&result)==0);
        CHECK(result==(state==0 ? CIXVPU_BOOT_DEBUG_IDLE : CIXVPU_BOOT_DEBUG_PENDING));
        CHECK(memcmp(before,device_memory[boot_rpc],4096)==0 && boot_nevents==4); boot_drop(p);
    }
    p=debug_begin(0,1,4); debug_changes=1;
    CHECK(cixvpu_prepared_boot_debugparam(p,&result)==0 && result==CIXVPU_BOOT_DEBUG_REPLIED);
    CHECK(boot_nevents==10 && word(device_memory[boot_rpc],0)==2); boot_drop(p);
    p=debug_begin(1,0,1); debug_changes=1;
    CHECK(cixvpu_prepared_boot_debugparam(p,&result)==0 && result==CIXVPU_BOOT_DEBUG_REPLIED);
    CHECK(boot_nevents==10 && word(device_memory[boot_rpc],0)==2); boot_drop(p);
    p=debug_begin(1,1,1); debug_churn=true; result=CIXVPU_BOOT_DEBUG_IDLE;
    memcpy(before,device_memory[boot_rpc],4096);
    CHECK(cixvpu_prepared_boot_debugparam(p,&result)==EAGAIN && result==CIXVPU_BOOT_DEBUG_IDLE);
    CHECK(boot_nevents==16 && word(device_memory[boot_rpc],0)==1 && p->boot.error==0);
    debug_churn=false; put32(device_memory[boot_rpc]+4,1); boot_nevents=0;
    CHECK(cixvpu_prepared_boot_debugparam(p,&result)==0 && result==CIXVPU_BOOT_DEBUG_REPLIED); boot_drop(p);
    p=debug_begin(1,1,4); unsigned int calls=native_calls;
    CHECK(cixvpu_prepared_boot_debugparam(NULL,&result)==EINVAL);
    CHECK(cixvpu_prepared_boot_debugparam(p,NULL)==EINVAL);
    CHECK(cixvpu_prepared_boot_debugparam(p,(void *)&p->plan)==EINVAL);
    CHECK(cixvpu_prepared_boot_debugparam(p,resources[boot_rpc].kva)==EINVAL);
    CHECK(cixvpu_prepared_boot_debugparam(p,(void *)((uint8_t *)&result+1))==EINVAL);
    intr=true; CHECK(cixvpu_prepared_boot_debugparam(p,&result)==EWOULDBLOCK); intr=false;
    softintr=true; CHECK(cixvpu_prepared_boot_debugparam(p,&result)==EWOULDBLOCK); softintr=false;
    CHECK(native_calls==calls && p->boot.error==0);
    p->dma->owners[0].map->dm_nsegs=2; result=CIXVPU_BOOT_DEBUG_PENDING;
    CHECK(cixvpu_prepared_boot_debugparam(p,&result)==EIO && result==CIXVPU_BOOT_DEBUG_PENDING);
    p->dma->owners[0].map->dm_nsegs=1;
    CHECK(cixvpu_prepared_boot_debugparam(p,&result)==EIO && native_calls==calls); boot_drop(p);
    p=debug_begin(1,1,4); void *aligned=p->dma->owners[boot_rpc].kva;
    p->dma->owners[boot_rpc].kva=(uint8_t *)aligned+1; calls=native_calls;
    result=CIXVPU_BOOT_DEBUG_PENDING;
    CHECK(cixvpu_prepared_boot_debugparam(p,&result)==EINVAL && native_calls==calls && result==CIXVPU_BOOT_DEBUG_PENDING);
    p->dma->owners[boot_rpc].kva=aligned;
    CHECK(cixvpu_prepared_boot_debugparam(p,&result)==EINVAL && native_calls==calls); boot_drop(p);
    for(unsigned int state=1;state<=2;state++) {
        p=debug_begin(state,1,4); struct cixvpu_boot_reply reply;
        sync_observer=boot_observe;
        CHECK(cixvpu_prepared_boot_receive(p,&reply)==ENOTSUP);
        calls=native_calls; result=CIXVPU_BOOT_DEBUG_IDLE;
        CHECK(cixvpu_prepared_boot_debugparam(p,&result)==ENOTSUP && native_calls==calls && result==CIXVPU_BOOT_DEBUG_IDLE);
        p->dma->owners[0].map->dm_nsegs=2;
        CHECK(cixvpu_prepared_boot_debugparam(p,&result)==ENOTSUP && p->boot.error==ENOTSUP && native_calls==calls);
        CHECK(cixvpu_prepared_boot_debugparam(p,(void *)&p->plan)==ENOTSUP && p->boot.error==ENOTSUP && native_calls==calls);
        p->dma->owners[0].map->dm_nsegs=1;
        CHECK(word(device_memory[boot_rpc],0)==state); boot_drop(p);

    }
    p=boot_begin(); calls=native_calls; result=CIXVPU_BOOT_DEBUG_PENDING;
    CHECK(cixvpu_prepared_boot_debugparam(p,&result)==EBUSY && native_calls==calls && result==CIXVPU_BOOT_DEBUG_PENDING);
    sync_observer=observe_sync;
    CHECK(cixvpu_prepared_free(&p)==0);
}
/* Literal observed-size shape; the real planner/allocator/preparer stay linked. */
static struct cixvpu_dma *rpc_reserve_begin(struct cixvpu_fw_plan *plan)
{
    struct cixvpu_dma *owner=NULL;
    reset();
    memset(rpc_image,0x79,sizeof(rpc_image));
    memset(rpc_image,0,CIXVPU_FW_HEADER_SIZE);
    rpc_image[4]=5; rpc_image[5]=3;
    memcpy(rpc_image+80,"r0p0-sum0005",12);
    put32(rpc_image+96,420736); put32(rpc_image+100,0x70000);
    put32(rpc_image+104,15); put32(rpc_image+108,0x7fff);
    put32(rpc_image+172,0x7f000); put32(rpc_image+176,0);
    memcpy(rpc_pinned,rpc_image,sizeof(rpc_image));
    CHECK(cixvpu_fw_plan_build_boot_rpc(rpc_image,sizeof(rpc_image),1,plan)==0);
    CHECK(plan->backing_pages==390 && plan->mappings==385 && plan->boot_rpc==1);
    CHECK(cixvpu_dma_alloc(&tag32,&tag64,plan,&owner)==0);
    for(unsigned int i=0;i<390;i++) memset(resources[i].kva,0xa5,4096);
    hashes=page_calls=map_calls=link_calls=encode_calls=meta_calls=0;
    fail_meta=fail_page=fail_map=fail_link=fail_encode=view_fault=duplicate_map=rpc_table_fault=0;
    fail_free=fail_seal=false;
    return owner;
}
static void rpc_reserve_tests(void)
{
    static const uint8_t zero[4096]={0};
    static uint8_t canary[4096];
    memset(canary,0xa5,sizeof(canary));
    for(unsigned int mode=0;mode<2;mode++) {
        struct cixvpu_fw_plan plan;
        struct cixvpu_dma *owner=rpc_reserve_begin(&plan),*old=owner;
        struct cixvpu_prepared *out=NULL;
        CHECK(cixvpu_prepare_boot_cpu(&owner,&plan,rpc_image,sizeof(rpc_image),fixture_pin,&out)==0);
        CHECK(owner==NULL && out!=NULL && out->plan.boot_rpc==1 && out->count==389);
        CHECK(map_calls==385 && link_calls==4);
        for(unsigned int i=0;i<103;i++) {
            size_t n=i==102 ? 2944 : 4096;
            CHECK(memcmp(resources[i].kva,rpc_image+i*4096,n)==0);
            CHECK(memcmp((uint8_t *)resources[i].kva+n,zero,4096-n)==0);
        }
        for(unsigned int i=253;i<385;i++) CHECK(memcmp(resources[i].kva,zero,4096)==0);
        const void *root=resources[385].kva,*leaf=resources[388].kva;
        CHECK(word(root,0)==((uint32_t)(bus_address(386)>>12)<<2|1));
        CHECK(word(root,64)==((uint32_t)(bus_address(387)>>12)<<2|1));
        CHECK(word(root,128)==((uint32_t)(bus_address(388)>>12)<<2|1));
        for(unsigned int i=0;i<1024;i++) {
            CHECK(word(leaf,i)==(i<128 ? (uint32_t)(bus_address(253+i)>>12)<<2|0xc0000003U : 0));
            if(i!=0 && i!=64 && i!=128 && i!=144) CHECK(word(root,i)==0);
        }
        struct cixvpu_dma_page view;
        CHECK(word(root,144)==((uint32_t)(bus_address(389)>>12)<<2|1));
        const void *second=resources[389].kva;
        for(unsigned int i=0;i<1024;i++)
            CHECK(word(second,i)==(i<4 ? (uint32_t)(bus_address(381+i)>>12)<<2|0xc0000003U : 0));
        CHECK(cixvpu_dma_page(old,253,&view)==EBUSY);
        sync_fixture(&plan,mode!=0);
        /* A late new data-map fault must prevent every native sync. */
        old->owners[384].map->dm_segs[0].ds_addr++;
        unsigned int calls=native_calls;
        CHECK(cixvpu_prepared_sync_initial(out)==ERANGE && sync_calls==0 && native_calls==calls);
        old->owners[384].map->dm_segs[0].ds_addr--;
        CHECK(cixvpu_prepared_sync_initial(out)==0 && sync_calls==390);
        for(unsigned int i=253;i<385;i++) {
            CHECK(pres[i]==5 && posts[i]==0);
            CHECK(memcmp(device_bytes(i),zero,4096)==0);
        }
        calls=native_calls;
        CHECK(cixvpu_prepared_sync_initial(out)==EBUSY && native_calls==calls);
        if(mode==0) {
            unload_observer=observe_unload;
            CHECK(cixvpu_prepared_free(&out)==0 && out==NULL && sync_calls==780);
        } else {
            uint32_t address=0xa5a5a5a5;
            CHECK(cixvpu_prepared_publish_root(out,&address)==0);
            CHECK(address==((uint32_t)(bus_address(385)>>12)<<2|3));
            calls=native_calls;
            CHECK(cixvpu_prepared_free(&out)==EBUSY && out!=NULL && native_calls==calls);
            boot_drop(out); /* Host-only quarantine teardown, never production. */
        }
        CHECK(depth==0 && live_kmem==0);
    }
    const unsigned int arenas[]={253,381,384};
    for(unsigned int arena=0;arena<3;arena++) for(unsigned int fault=0;fault<9;fault++) {
        unsigned int at=arenas[arena];
        struct cixvpu_fw_plan plan;
        struct cixvpu_dma *owner=rpc_reserve_begin(&plan),*saved=owner;
        struct cixvpu_prepared *out=NULL;
        struct cixvpu_dma_owner original=owner->owners[at];
        if(fault==0) plan.boot_rpc=0;
        if(fault==1) plan.boot_rpc=2;
        if(fault==2) plan.mappings--;
        if(fault==3) owner->owners[at].page.role=CIXVPU_FW_PLAN_PRINT;
        if(fault==4) owner->owners[at].bus_address=bus_address(0);
        if(fault==5) owner->owners[at].kva=resources[0].kva;
        if(fault==6) rpc_table_fault=1;
        if(fault==7) rpc_table_fault=2;
        if(fault==8) rpc_image[4000]^=1;
        CHECK(cixvpu_prepare_boot_cpu(&owner,&plan,rpc_image,sizeof(rpc_image),fixture_pin,&out)!=0);
        CHECK(owner==saved && out==NULL && owner->phase==CIXVPU_DMA_ALLOC);
        if(fault<6 || fault==8) for(unsigned int i=0;i<390;i++)
            CHECK(memcmp(resources[i].kva,canary,4096)==0);
        owner->owners[at]=original; rpc_table_fault=0;
        CHECK(cixvpu_dma_free(&owner)==0 && depth==0 && live_kmem==0);
    }
    /* Each new gather/map/link boundary retains a reclaimable stopped owner. */
    for(unsigned int stage=0;stage<3;stage++) {
        unsigned int first=stage==0 ? 382 : stage==1 ? 382 : 4;
        unsigned int last=stage==0 ? 391 : stage==1 ? 385 : 4;
        for(unsigned int at=first;at<=last;at++) {
            struct cixvpu_fw_plan plan;
            struct cixvpu_dma *owner=rpc_reserve_begin(&plan),*saved=owner;
            struct cixvpu_prepared *out=NULL;
            if(stage==0) fail_page=at;
            if(stage==1) fail_map=at;
            if(stage==2) fail_link=at;
            CHECK(cixvpu_prepare_boot_cpu(&owner,&plan,rpc_image,sizeof(rpc_image),fixture_pin,&out)==EIO);
            CHECK(owner==saved && out==NULL && !owner->ever_visible);
            CHECK(cixvpu_dma_free(&owner)==0 && depth==0 && live_kmem==0);
        }
    }
    struct cixvpu_fw_plan plan;
    struct cixvpu_dma *owner=rpc_reserve_begin(&plan);
    struct cixvpu_prepared *out=NULL;
    CHECK(cixvpu_prepare_cpu(&owner,&plan,rpc_image,sizeof(rpc_image),fixture_pin,&out)==0 && owner==NULL);
    CHECK(cixvpu_prepared_free(&out)==0 && out==NULL && depth==0 && live_kmem==0);
    /* Unknown selector with otherwise valid plain-plan counts cannot fall back. */
    owner=begin(1,&plan); struct cixvpu_dma *saved=owner;
    plan.boot_rpc=2;
    CHECK(cixvpu_prepare_cpu(&owner,&plan,image,extent,fixture_pin,&out)==EINVAL);
    CHECK(owner==saved && out==NULL && page_calls==0);
    CHECK(cixvpu_dma_free(&owner)==0 && depth==0 && live_kmem==0);

}
static bool rpc_cacheline, rpc_churn;
static struct cixvpu_prepared *rpc_active;
static void rpc_observe(unsigned int i, bus_addr_t off, bus_size_t len, int ops)
{
    CHECK(i==boot_rpc && off+len<=24);
    uint8_t *cpu=resources[i].kva,*dev=device_memory[i];
    if(ops==BUS_DMASYNC_POSTWRITE && off==0) {
        if(word(dev,1)==2) CHECK(word(dev,4)==16384 ? rpc_active->dma->rpc_second_alloc_used : rpc_active->dma->rpc_alloc_used);
        if(word(dev,1)==3) CHECK(rpc_active->dma->rpc_resize_used && !rpc_active->dma->rpc_resize_committed);
        if(word(dev,1)==1) CHECK(rpc_active->dma->rpc_debug_count>0 && rpc_active->dma->rpc_debug_count<=16);
    }
    if(ops & BUS_DMASYNC_PREWRITE) {
        CHECK(ops==5 && ((off==8 && len==8) || (off==0 && len==4)));
        CHECK(word(dev,0)==1);
        if(off==8) {
            CHECK(word(cpu,0)==1 && (word(cpu,1)==2 || word(cpu,1)==3) && word(cpu,2)==4 && word(cpu,3)==(word(dev,4)==16384 ? 0x24000000U : 0x20000000U));
            CHECK(memcmp(cpu+16,dev+16,rpc_cacheline ? 48 : word(cpu,1)==3 ? 4 : 8)==0);
        } else {
            CHECK(word(cpu,0)==2);
            if(word(dev,1)==2 || word(dev,1)==3) CHECK(word(dev,2)==4 && word(dev,3)==(word(dev,4)==16384 ? 0x24000000U : 0x20000000U));
            CHECK(memcmp(cpu+4,dev+4,rpc_cacheline ? 60 : 8)==0);
        }
    }
    if(ops==BUS_DMASYNC_PREREAD && off!=0)
        CHECK(word(cpu,0)==1 && word(dev,0)==1);
    if(rpc_cacheline && (ops & BUS_DMASYNC_POSTREAD)) memcpy(cpu,dev,64);
    boot_observe(i,off,len,ops);
    if(rpc_cacheline && (ops & BUS_DMASYNC_PREWRITE)) memcpy(dev,cpu,64);
    if(rpc_cacheline && (ops & BUS_DMASYNC_PREREAD)) memcpy(cpu,dev,64);
    if(rpc_churn && off==0 && len==12 && ops==BUS_DMASYNC_PREREAD)
        put32(dev+4,word(dev,1)^1);
}
static void rpc_request(uint32_t state,uint32_t call,uint32_t size)
{
    uint8_t *rpc=device_memory[boot_rpc];
    memset(rpc,0x6d,4096); memset(resources[boot_rpc].kva,0xa5,4096);
    put32(rpc,state); put32(rpc+4,call); put32(rpc+8,size);
    put32(rpc+12,131072); put32(rpc+16,67108864); rpc[20]=0; rpc[21]=22;
    rpc[22]=0xc3; rpc[23]=0xd4; boot_nevents=0;
}
static struct cixvpu_prepared *rpc_protocol_begin(bool publish)
{
    struct cixvpu_fw_plan plan;
    struct cixvpu_dma *owner=rpc_reserve_begin(&plan);
    struct cixvpu_prepared *out=NULL;
    CHECK(cixvpu_prepare_boot_cpu(&owner,&plan,rpc_image,sizeof(rpc_image),fixture_pin,&out)==0);
    sync_fixture(&plan,true);
    CHECK(cixvpu_prepared_sync_initial(out)==0);
    boot_host=118; boot_mve=119; boot_rpc=124; boot_nevents=0;
    rpc_cacheline=rpc_churn=false; rpc_active=out; sync_observer=rpc_observe;
    if(publish) { uint32_t root; CHECK(cixvpu_prepared_publish_root(out,&root)==0); }
    return out;
}
static void rpc_protocol_tests(void)
{
    struct cixvpu_prepared *p;
    enum cixvpu_boot_rpc_result result;
    struct cixvpu_boot_rpc_header h;
    struct cixvpu_boot_reply reply,before;
    uint8_t original[4096];
    for(unsigned int mode=0;mode<2;mode++) {
        p=rpc_protocol_begin(true); rpc_cacheline=mode!=0;
        rpc_request(1,2,12); memcpy(original,device_memory[boot_rpc],4096);
        result=CIXVPU_BOOT_RPC_IDLE; unsigned int calls=native_calls,heap=live_kmem,owners=depth;
        CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==0 && result==CIXVPU_BOOT_RPC_ALLOC_REPLIED);
        CHECK(native_calls==calls+9 && heap==live_kmem && owners==depth && boot_nevents==9);
        static const unsigned int offsets[]={0,0,0,0,8,4,16,8,0};
        static const unsigned int lengths[]={12,12,24,4,8,4,8,8,4};
        static const int operations[]={2,1,2,8,8,1,1,5,5};
        for(unsigned int i=0;i<9;i++) CHECK(boot_events[i].offset==offsets[i] && boot_events[i].length==lengths[i] && boot_events[i].ops==operations[i]);
        CHECK(word(device_memory[boot_rpc],0)==2 && word(device_memory[boot_rpc],2)==4 && word(device_memory[boot_rpc],3)==0x20000000);
        CHECK(memcmp(original+4,device_memory[boot_rpc]+4,4)==0 && memcmp(original+16,device_memory[boot_rpc]+16,4080)==0);
        calls=native_calls;
        CHECK(cixvpu_prepared_boot_rpc_header(p,&h)==0 && h.valid && h.alloc_valid && h.state==1 && h.callid==2 && h.size==12);
        CHECK(h.alloc_size==131072 && h.alloc_max_size==67108864 && h.alloc_region==0 && h.alloc_log2_alignment==22 && native_calls==calls);
        boot_nevents=0; result=CIXVPU_BOOT_RPC_IDLE;
        CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==0 && result==CIXVPU_BOOT_RPC_PENDING && boot_nevents==4);
        for(unsigned int i=0;i<4;i++) CHECK(!(boot_events[i].ops & BUS_DMASYNC_PREWRITE));
        rpc_request(1,2,12); result=CIXVPU_BOOT_RPC_PENDING;
        CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==EBUSY && result==CIXVPU_BOOT_RPC_PENDING);
        CHECK(word(device_memory[boot_rpc],0)==1);
        calls=native_calls; p->dma->owners[0].map->dm_nsegs=2;
        CHECK(cixvpu_prepared_boot_rpc_step(p,(void *)&p->plan)==EBUSY && native_calls==calls && p->boot.error==EBUSY);
        memset(&reply,0xa5,sizeof(reply)); before=reply;
        CHECK(cixvpu_prepared_boot_receive_rpc(p,&reply)==EBUSY && memcmp(&reply,&before,sizeof(reply))==0 && native_calls==calls);
        p->dma->owners[0].map->dm_nsegs=1; boot_drop(p);
    }
    for(unsigned int fault=0;fault<12;fault++) {
        p=rpc_protocol_begin(true); rpc_request(1,2,12);
        int want=ENOTSUP;
        if(fault==0) put32(device_memory[boot_rpc]+12,131071);
        if(fault==1) put32(device_memory[boot_rpc]+16,UINT32_MAX);
        if(fault==2) device_memory[boot_rpc][20]=255;
        if(fault==3) device_memory[boot_rpc][21]=255;
        if(fault==4) {put32(device_memory[boot_rpc]+8,13);want=EPROTO;}
        if(fault==5) {put32(device_memory[boot_rpc]+4,3);want=EPROTO;}
        if(fault==6) put32(device_memory[boot_rpc]+4,4);
        if(fault==7) {put32(device_memory[boot_rpc],3);want=EPROTO;}
        if(fault==8) {put32(device_memory[boot_rpc]+4,1);put32(device_memory[boot_rpc]+8,1013);want=EPROTO;}
        if(fault==9) put32(device_memory[boot_rpc]+12,UINT32_MAX);
        if(fault==10) put32(device_memory[boot_rpc]+16,0);
        if(fault==11) put32(device_memory[boot_rpc]+8,UINT32_MAX),want=EPROTO;
        memcpy(original,device_memory[boot_rpc],4096); result=CIXVPU_BOOT_RPC_ALLOC_REPLIED;
        CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==want && result==CIXVPU_BOOT_RPC_ALLOC_REPLIED);
        CHECK(memcmp(original,device_memory[boot_rpc],4096)==0 && p->boot.error==want);
        unsigned int calls=native_calls;
        CHECK(cixvpu_prepared_boot_rpc_header(p,&h)==0 && h.valid && h.state==word(original,0) && h.callid==word(original,1) && h.size==word(original,2));
        bool valid=fault<4 || fault==9 || fault==10;
        CHECK(h.alloc_valid==valid && h.alloc_size==(valid ? word(original,3) : 0) && h.alloc_max_size==(valid ? word(original,4) : 0));
        CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==want && native_calls==calls);
        boot_drop(p);
    }
    p=rpc_protocol_begin(true);
    for(unsigned int i=0;i<16;i++) {
        rpc_request(1,1,i==15 ? 1012 : 0); memcpy(original,device_memory[boot_rpc],4096);
        CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==0 && result==CIXVPU_BOOT_RPC_DEBUG_REPLIED && boot_nevents==6);
        CHECK(word(device_memory[boot_rpc],0)==2 && memcmp(original+4,device_memory[boot_rpc]+4,4092)==0);
    }
    rpc_request(1,2,12);
    CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==0 && result==CIXVPU_BOOT_RPC_ALLOC_REPLIED);
    rpc_request(1,1,1); result=CIXVPU_BOOT_RPC_IDLE;
    CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==E2BIG && result==CIXVPU_BOOT_RPC_IDLE && word(device_memory[boot_rpc],0)==1);
    boot_drop(p);
    p=rpc_protocol_begin(true); rpc_request(1,2,12); rpc_churn=true;
    result=CIXVPU_BOOT_RPC_PENDING;
    CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==EAGAIN && result==CIXVPU_BOOT_RPC_PENDING && p->boot.error==0 && boot_nevents==16);
    rpc_churn=false; rpc_request(1,2,12);
    CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==0 && result==CIXVPU_BOOT_RPC_ALLOC_REPLIED); boot_drop(p);
    for(unsigned int state=0;state<=2;state+=2) {
        p=rpc_protocol_begin(true); rpc_request(state,UINT32_MAX,UINT32_MAX);
        result=CIXVPU_BOOT_RPC_ALLOC_REPLIED;
        CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==0 && result==(state==0 ? CIXVPU_BOOT_RPC_IDLE : CIXVPU_BOOT_RPC_PENDING));
        boot_message(0,2009,0,0,2009); boot_nevents=0; sync_observer=boot_observe;
        CHECK(cixvpu_prepared_boot_receive_rpc(p,&reply)==0 && reply.code==2009 && p->boot.cursor==1);
        boot_drop(p);
    }
    p=rpc_protocol_begin(true); rpc_request(0,0,0);
    CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==0 && result==CIXVPU_BOOT_RPC_IDLE);
    rpc_request(1,2,12); boot_message(0,2009,0,0,2009); sync_observer=boot_observe;
    memset(&reply,0xa5,sizeof(reply)); before=reply;
    CHECK(cixvpu_prepared_boot_receive_rpc(p,&reply)==EAGAIN && p->boot.error==0 && p->boot.cursor==0 && boot_nevents==2 && memcmp(&reply,&before,sizeof(reply))==0);
    sync_observer=rpc_observe;
    CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==0 && result==CIXVPU_BOOT_RPC_ALLOC_REPLIED);
    sync_observer=boot_observe;
    CHECK(cixvpu_prepared_boot_receive_rpc(p,&reply)==0 && reply.code==2009); boot_drop(p);
    p=rpc_protocol_begin(false); unsigned int calls=native_calls;
    CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==EBUSY && native_calls==calls);
    CHECK(cixvpu_prepared_boot_receive_rpc(p,&reply)==EBUSY && native_calls==calls); boot_drop(p);
    p=rpc_protocol_begin(true); rpc_request(1,2,12); calls=native_calls;
    CHECK(cixvpu_prepared_boot_rpc_step(NULL,&result)==EINVAL);
    CHECK(cixvpu_prepared_boot_rpc_step(p,NULL)==EINVAL);
    CHECK(cixvpu_prepared_boot_receive_rpc(NULL,&reply)==EINVAL);
    CHECK(cixvpu_prepared_boot_receive_rpc(p,NULL)==EINVAL);
    intr=true;
    CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==EWOULDBLOCK);
    CHECK(cixvpu_prepared_boot_receive_rpc(p,&reply)==EWOULDBLOCK);
    intr=false; softintr=true;
    CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==EWOULDBLOCK);
    CHECK(cixvpu_prepared_boot_receive_rpc(p,&reply)==EWOULDBLOCK);
    softintr=false;
    void *aliases[]={p,&p->plan,p->dma,&p->dma->owners[0],resources[0].kva};
    for(unsigned int i=0;i<__arraycount(aliases);i++) {
        CHECK(cixvpu_prepared_boot_rpc_step(p,aliases[i])==EINVAL);
        CHECK(cixvpu_prepared_boot_receive_rpc(p,aliases[i])==EINVAL);
    }
    CHECK(native_calls==calls && boot_nevents==0 && p->boot.error==0);
    p->dma->owners[0].map->dm_nsegs=2;
    result=CIXVPU_BOOT_RPC_PENDING;
    CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==EIO && result==CIXVPU_BOOT_RPC_PENDING && native_calls==calls);
    CHECK(!p->boot.snapshot.rpc_valid && !p->boot.snapshot.rpc_alloc_valid);
    p->dma->owners[0].map->dm_nsegs=1;
    CHECK(cixvpu_prepared_boot_receive_rpc(p,&reply)==EIO && native_calls==calls); boot_drop(p);
    p=rpc_protocol_begin(true); rpc_request(2,2,4); sync_observer=boot_observe;
    CHECK(cixvpu_prepared_boot_receive(p,&reply)==ENOTSUP && p->boot.error==ENOTSUP);
    calls=native_calls;
    CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==ENOTSUP && native_calls==calls);
    CHECK(cixvpu_prepared_boot_receive_rpc(p,&reply)==ENOTSUP && native_calls==calls); boot_drop(p);
    p=boot_begin(); uint32_t root; CHECK(cixvpu_prepared_publish_root(p,&root)==0); calls=native_calls;
    CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==ENOTSUP && native_calls==calls);
    CHECK(cixvpu_prepared_boot_receive_rpc(p,&reply)==ENOTSUP && native_calls==calls); boot_drop(p);
}
/* Breaks caught: lost/swapped resize words, unchecked recheck, broad payload
 * reads, writes/responses, stale cache validity and callbacks after sticky86. */
#ifdef CIXVPU_BOOT_BRIDGE
/* Device injection only. The composed native fixture owns its policy. */
void boot_fixture_rpc_resize(uint32_t address,uint32_t size)
{
    uint8_t *rpc=device_memory[boot_rpc];
    put32(rpc,1);put32(rpc+4,3);put32(rpc+8,8);
    put32(rpc+12,address);put32(rpc+16,size);
}
#endif
static void rpc_resize_request(uint32_t address,uint32_t size)
{
    rpc_request(1,3,8);
    put32(device_memory[boot_rpc]+12,address);
    put32(device_memory[boot_rpc]+16,size);
}
static void rpc_second_request(void)
{
    rpc_request(1,2,12);
    put32(device_memory[boot_rpc]+12,16384);
    put32(device_memory[boot_rpc]+16,16384);
    device_memory[boot_rpc][21]=12;
}
/* Breaks caught: absent second reply, early/duplicate admission, broad tuples,
 * first-reserve collision, wrong response ordering and live allocation. */
static void boot_rpc_second_tests(void)
{
    enum cixvpu_boot_rpc_result result;
    uint8_t original[4096];
    for(unsigned int mode=0;mode<2;mode++) {
        struct cixvpu_prepared *p=rpc_protocol_begin(true);
        rpc_cacheline=mode!=0;
        rpc_request(1,2,12);
        CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==0);
        rpc_resize_request(0x20000000,524288);
        CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==0);
        rpc_second_request(); memcpy(original,device_memory[boot_rpc],4096);
        unsigned int calls=native_calls,heap=live_kmem,owners=depth;
        CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==0);
        CHECK(result==CIXVPU_BOOT_RPC_SECOND_ALLOC_REPLIED);
        CHECK(native_calls==calls+9 && heap==live_kmem && owners==depth && boot_nevents==9);
        CHECK(word(device_memory[boot_rpc],0)==2 && word(device_memory[boot_rpc],2)==4 && word(device_memory[boot_rpc],3)==0x24000000);
        CHECK(memcmp(original+4,device_memory[boot_rpc]+4,4)==0 && memcmp(original+16,device_memory[boot_rpc]+16,4080)==0);
        rpc_second_request(); result=CIXVPU_BOOT_RPC_IDLE;
        CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==EBUSY && result==CIXVPU_BOOT_RPC_IDLE);
        calls=native_calls;
        CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==EBUSY && native_calls==calls);
        CHECK(cixvpu_prepared_free(&p)==EBUSY && p!=NULL);
        boot_drop(p);
    }
    for(unsigned int fault=0;fault<10;fault++) {
        struct cixvpu_prepared *p=rpc_protocol_begin(true);
        if(fault!=0) {
            rpc_request(1,2,12); CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==0);
            if(fault!=1 && fault!=9) {
                rpc_resize_request(0x20000000,524288);
                CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==0);
            }
        }
        if(fault==9) p->dma->rpc_resize_used=true; /* Consumed is not committed. */
        rpc_second_request();
        int want=ENOTSUP;
        if(fault==2) put32(device_memory[boot_rpc]+12,16383);
        if(fault==3) put32(device_memory[boot_rpc]+16,16385);
        if(fault==4) device_memory[boot_rpc][20]=1;
        if(fault==5) device_memory[boot_rpc][21]=13;
        if(fault==6) put32(device_memory[boot_rpc]+12,UINT32_MAX);
        if(fault==7) {put32(device_memory[boot_rpc]+8,11);want=EPROTO;}
        if(fault==8) {put32(device_memory[boot_rpc],3);want=EPROTO;}
        memcpy(original,device_memory[boot_rpc],4096);
        unsigned int calls,heap=live_kmem,owners=depth;
        result=CIXVPU_BOOT_RPC_IDLE;
        CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==want && result==CIXVPU_BOOT_RPC_IDLE);
        CHECK(p->boot.error==want && heap==live_kmem && owners==depth);
        CHECK(memcmp(original,device_memory[boot_rpc],4096)==0);
        for(unsigned int i=0;i<boot_nevents;i++) CHECK(!(boot_events[i].ops & BUS_DMASYNC_PREWRITE));
        calls=native_calls;
        CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==want && native_calls==calls);
        boot_drop(p);
    }
}
static void boot_rpc_resize_runtime_tests(void)
{
    for(unsigned int mode=0;mode<2;mode++) {
        struct cixvpu_prepared *p=rpc_protocol_begin(true);
        enum cixvpu_boot_rpc_result result=CIXVPU_BOOT_RPC_IDLE;
        struct cixvpu_boot_rpc_header header;
        rpc_cacheline=mode!=0;
        rpc_request(1,2,12);
        CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==0 && result==CIXVPU_BOOT_RPC_ALLOC_REPLIED);
        rpc_resize_request(0x20000000,524288);
        uint8_t original[4096];memcpy(original,device_memory[boot_rpc],4096);
        unsigned int calls=native_calls,heap=live_kmem,owners=depth;
        CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==0 && result==CIXVPU_BOOT_RPC_RESIZE_REPLIED);
        CHECK(p->dma->rpc_resize_used && p->dma->rpc_resize_committed && native_calls==calls+11 && boot_nevents==11);
        CHECK(heap==live_kmem && owners==depth);
        static const unsigned int offsets[]={0,0,0,0,0,0,8,4,16,8,0};
        static const unsigned int lengths[]={12,12,20,12,12,4,8,4,4,8,4};
        static const int ops[]={2,1,2,1,2,8,8,1,1,5,5};
        for(unsigned int i=0;i<11;i++) CHECK(boot_events[i].page==boot_rpc && boot_events[i].offset==offsets[i] && boot_events[i].length==lengths[i] && boot_events[i].ops==ops[i]);
        CHECK(word(device_memory[boot_rpc],0)==2 && word(device_memory[boot_rpc],2)==4 && word(device_memory[boot_rpc],3)==0x20000000);
        CHECK(memcmp(original+4,device_memory[boot_rpc]+4,4)==0 && memcmp(original+16,device_memory[boot_rpc]+16,4080)==0);
        calls=native_calls;
        CHECK(cixvpu_prepared_boot_rpc_header(p,&header)==0 && header.resize_valid && header.resize_address==0x20000000 && header.resize_new_size==524288 && native_calls==calls);
        boot_nevents=0;
        CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==0 && result==CIXVPU_BOOT_RPC_PENDING && boot_nevents==4);
        for(unsigned int i=0;i<4;i++) CHECK(!(boot_events[i].ops & BUS_DMASYNC_PREWRITE));
        rpc_resize_request(0x20000000,524288);
        result=CIXVPU_BOOT_RPC_IDLE;
        CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==EBUSY && result==CIXVPU_BOOT_RPC_IDLE && word(device_memory[boot_rpc],0)==1);
        calls=native_calls;p->dma->owners[0].map->dm_nsegs=2;
        CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==EBUSY && native_calls==calls && p->boot.error==EBUSY);
        p->dma->owners[0].map->dm_nsegs=1;
        CHECK(cixvpu_prepared_free(&p)==EBUSY && p!=NULL && native_calls==calls);
        boot_drop(p);
    }
    for(unsigned int fault=0;fault<8;fault++) {
        struct cixvpu_prepared *p=rpc_protocol_begin(true);
        enum cixvpu_boot_rpc_result result=CIXVPU_BOOT_RPC_IDLE;
        if(fault!=0) {
            rpc_request(1,2,12);
            CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==0);
        }
        rpc_resize_request(0x20000000,524288);
        int expected=ENOTSUP;
        if(fault==1) put32(device_memory[boot_rpc]+12,0x20001000);
        if(fault==2) put32(device_memory[boot_rpc]+16,524287);
        if(fault==3) put32(device_memory[boot_rpc]+16,UINT32_MAX);
        if(fault==4) {put32(device_memory[boot_rpc]+8,7);expected=EPROTO;}
        if(fault==5) {put32(device_memory[boot_rpc],3);expected=EPROTO;}
        if(fault==6) {boot_resize_change=1;expected=EPROTO;}
        if(fault==7) {boot_resize_change=3;expected=EPROTO;}
        uint8_t original[4096];memcpy(original,device_memory[boot_rpc],4096);
        unsigned int heap=live_kmem,owners=depth;
        result=CIXVPU_BOOT_RPC_PENDING;
        CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==expected && result==CIXVPU_BOOT_RPC_PENDING && p->boot.error==expected);
        CHECK(!p->dma->rpc_resize_used && heap==live_kmem && owners==depth);
        for(unsigned int i=0;i<boot_nevents;i++) CHECK(!(boot_events[i].ops & (BUS_DMASYNC_PREWRITE|BUS_DMASYNC_POSTWRITE)));
        if(fault<6) CHECK(memcmp(original,device_memory[boot_rpc],4096)==0);
        unsigned int calls=native_calls;
        CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==expected && native_calls==calls);
        boot_drop(p);
    }
}
static void boot_rpc_resize_tests(void)
{
    const uint32_t addresses[]={0x20000000,0,UINT32_MAX};
    const uint32_t sizes[]={131072,67108864,UINT32_MAX};
    for(unsigned int service=0;service<2;service++)for(unsigned int change=0;change<6;change++) {
        struct cixvpu_prepared *p=service ? rpc_protocol_begin(true) : boot_begin();
        uint32_t root=0;
        enum cixvpu_boot_rpc_result result=CIXVPU_BOOT_RPC_PENDING;
        struct cixvpu_boot_reply reply,old_reply;
        struct cixvpu_boot_rpc_header h,old;
        if(service) {
            rpc_request(1,2,12);
            CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==0 && result==CIXVPU_BOOT_RPC_ALLOC_REPLIED);
            CHECK(p->dma->rpc_alloc_used);
        } else CHECK(cixvpu_prepared_publish_root(p,&root)==0);
        unsigned int tuple=change%3;
        rpc_request(1,3,8);
#ifdef CIXVPU_BOOT_BRIDGE
        boot_fixture_rpc_resize(addresses[tuple],sizes[tuple]);
#else
        put32(device_memory[boot_rpc]+12,addresses[tuple]);
        put32(device_memory[boot_rpc]+16,sizes[tuple]);
#endif
        uint8_t before[4096];memcpy(before,device_memory[boot_rpc],4096);
        boot_resize_change=change<3 ? 0 : change-2;boot_nevents=0;
        memset(&reply,0xa5,sizeof(reply));memcpy(&old_reply,&reply,sizeof(old_reply));result=CIXVPU_BOOT_RPC_IDLE;
        int error=service ? cixvpu_prepared_boot_rpc_step(p,&result) : cixvpu_prepared_boot_receive(p,&reply);
        int expected=service && change>=3 ? EPROTO : ENOTSUP;
        CHECK(error==expected && p->boot.error==expected && result==CIXVPU_BOOT_RPC_IDLE);
        CHECK(memcmp(&reply,&old_reply,sizeof(reply))==0 && boot_nevents==6);
        const unsigned int offsets[]={0,0,12,12,0,0};
        const unsigned int lengths[]={12,12,8,8,12,12};
        const unsigned int step_lengths[]={12,12,20,12,12,20};
        for(unsigned int i=0;i<boot_nevents;i++) {
            CHECK(boot_events[i].page==boot_rpc && boot_events[i].offset==(service ? 0u : offsets[i]));
            CHECK(boot_events[i].length==(service ? step_lengths[i] : lengths[i]));
            CHECK(boot_events[i].ops==(i%2==0 ? BUS_DMASYNC_POSTREAD : BUS_DMASYNC_PREREAD));
        }
        CHECK(((uint8_t *)resources[boot_rpc].kva)[20]==0xa5);
        CHECK(memcmp(device_memory[boot_rpc]+12,before+12,4084)==0);
        if(change<3)CHECK(memcmp(device_memory[boot_rpc],before,4096)==0);
        unsigned int calls=native_calls,depth_before=depth,heap=live_kmem;
        CHECK(cixvpu_prepared_boot_rpc_header(p,&h)==0 && h.valid && h.state==1 && h.callid==3 && h.size==8);
        CHECK(h.resize_valid==(change<3) && !h.alloc_valid);
        CHECK(h.resize_address==(change<3 ? addresses[tuple] : 0) && h.resize_new_size==(change<3 ? sizes[tuple] : 0));
        CHECK(native_calls==calls && depth==depth_before && live_kmem==heap);
        old=h; memset(device_memory[boot_rpc],0,4096);
        CHECK((service ? cixvpu_prepared_boot_rpc_step(p,&result) : cixvpu_prepared_boot_receive(p,&reply))==expected);
        /* Successful getters define members, not structure padding. */
        CHECK(cixvpu_prepared_boot_rpc_header(p,&h)==0 &&
            h.valid==old.valid && h.state==old.state && h.callid==old.callid && h.size==old.size &&
            h.alloc_valid==old.alloc_valid && h.alloc_size==old.alloc_size && h.alloc_max_size==old.alloc_max_size &&
            h.alloc_region==old.alloc_region && h.alloc_log2_alignment==old.alloc_log2_alignment &&
            h.resize_valid==old.resize_valid && h.resize_address==old.resize_address && h.resize_new_size==old.resize_new_size &&
            native_calls==calls);
        memset(&h,0x5a,sizeof(h));memcpy(&old,&h,sizeof(old));
        CHECK(cixvpu_prepared_boot_rpc_header(p,(void *)&p->plan)==EINVAL && memcmp(&h,&old,sizeof(h))==0);
        p->dma->owners[0].map->dm_nsegs=2;
        CHECK(cixvpu_prepared_boot_rpc_header(p,&h)==EIO && memcmp(&h,&old,sizeof(h))==0 && native_calls==calls && p->boot.error==expected);
        if(service)CHECK(cixvpu_prepared_boot_rpc_step(p,&result)==expected && p->boot.error==expected && native_calls==calls);
        p->dma->owners[0].map->dm_nsegs=1;boot_drop(p);
    }
    const uint32_t shapes[][3]={{1,3,0},{1,3,7},{1,3,9},{1,3,UINT32_MAX},{1,4,8}};
    for(unsigned int service=0;service<2;service++)for(unsigned int n=0;n<__arraycount(shapes);n++) {
        struct cixvpu_prepared *p=service ? rpc_protocol_begin(true) : boot_begin();
        if(!service){uint32_t root;CHECK(cixvpu_prepared_publish_root(p,&root)==0);}
        rpc_request(shapes[n][0],shapes[n][1],shapes[n][2]);
        enum cixvpu_boot_rpc_result r;struct cixvpu_boot_reply reply;struct cixvpu_boot_rpc_header h;
        CHECK((service ? cixvpu_prepared_boot_rpc_step(p,&r) : cixvpu_prepared_boot_receive(p,&reply))==(service && n<4 ? EPROTO : ENOTSUP));
        CHECK(boot_nevents==(service ? 4u : 2u));
        CHECK(cixvpu_prepared_boot_rpc_header(p,&h)==0 && h.valid && !h.resize_valid && h.resize_address==0 && h.resize_new_size==0);
        CHECK(((uint8_t *)resources[boot_rpc].kva)[12]==0xa5);boot_drop(p);
    }
    struct cixvpu_prepared *p=rpc_protocol_begin(true);enum cixvpu_boot_rpc_result r;
    struct cixvpu_boot_rpc_header h;unsigned int calls=native_calls;
    p->boot.snapshot.rpc_resize_valid=true; /* Stale-cache fault injection. */
    p->dma->owners[0].map->dm_nsegs=2;
    CHECK(cixvpu_prepared_boot_rpc_step(p,&r)==EIO && native_calls==calls);
    p->dma->owners[0].map->dm_nsegs=1;
    CHECK(cixvpu_prepared_boot_rpc_header(p,&h)==0 && !h.valid && !h.resize_valid);boot_drop(p);
}

static void boot_rpc_alloc_tests(void)
{
    static const uint32_t tuples[][3]={
        {1,2,12}, {1,2,12}, {1,2,12}, {1,2,12}, {1,2,12},
        {2,2,12}, {1,1,12}, {1,2,0}, {1,2,8}, {1,2,13}, {1,2,UINT32_MAX}
    };
    for(unsigned int n=0;n<__arraycount(tuples);n++) {
        struct cixvpu_prepared *out=boot_begin();
        struct cixvpu_boot_reply reply, canary;
        struct cixvpu_boot_rpc_header h, old;
        uint8_t device_before[4096];
        uint32_t root=0;
        CHECK(cixvpu_prepared_publish_root(out,&root)==0);
        memset(device_memory[boot_rpc],0x6d,4096);
        memset(resources[boot_rpc].kva,0xa5,4096);
        put32(device_memory[boot_rpc],tuples[n][0]);
        put32(device_memory[boot_rpc]+4,tuples[n][1]);
        put32(device_memory[boot_rpc]+8,tuples[n][2]);
        uint32_t size=n==1 ? UINT32_MAX : 0x12345678;
        uint32_t max=n==1 ? 0 : 0xfedcba98;
        put32(device_memory[boot_rpc]+12,size);
        put32(device_memory[boot_rpc]+16,max);
        device_memory[boot_rpc][20]=n==1 ? 255 : 1;
        device_memory[boot_rpc][21]=n==1 ? 255 : 31;
        device_memory[boot_rpc][22]=0xc3; device_memory[boot_rpc][23]=0xd4;
        memcpy(device_before,device_memory[boot_rpc],sizeof(device_before));
        memset(&reply,0xa5,sizeof(reply)); canary=reply;
        boot_nevents=0;
        if(n>=2 && n<=4) boot_alloc_change=n-1;
        CHECK(cixvpu_prepared_boot_receive(out,&reply)==ENOTSUP);
        CHECK(memcmp(&reply,&canary,sizeof(reply))==0);
        bool exact=n<5, valid=n<2;
        CHECK(boot_nevents==(exact ? 6U : 2U));
        static const uint64_t offsets[]={0,0,12,12,0,0};
        for(unsigned int i=0;i<boot_nevents;i++) {
            CHECK(boot_events[i].page==boot_rpc && boot_events[i].offset==offsets[i]);
            CHECK(boot_events[i].length==12);
            CHECK(boot_events[i].ops==(i%2==0 ? BUS_DMASYNC_POSTREAD : BUS_DMASYNC_PREREAD));
        }
        unsigned int calls=native_calls;
        CHECK(cixvpu_prepared_boot_rpc_header(out,&h)==0 && h.valid);
        CHECK(h.state==tuples[n][0] && h.callid==tuples[n][1] && h.size==tuples[n][2]);
        CHECK(h.alloc_valid==valid);
        CHECK(h.alloc_size==(valid ? size : 0) && h.alloc_max_size==(valid ? max : 0));
        CHECK(h.alloc_region==(valid ? (n==1 ? 255 : 1) : 0));
        CHECK(h.alloc_log2_alignment==(valid ? (n==1 ? 255 : 31) : 0));
        if(exact) {
            CHECK(memcmp((uint8_t *)resources[boot_rpc].kva+12,device_before+12,12)==0);
            CHECK(((uint8_t *)resources[boot_rpc].kva)[24]==0xa5);
        } else CHECK(((uint8_t *)resources[boot_rpc].kva)[12]==0xa5);
        if(n<2 || n>4) CHECK(memcmp(device_memory[boot_rpc],device_before,4096)==0);
        CHECK(device_memory[boot_host][0]==0 && native_calls==calls);
        old=h;
        memset(device_memory[boot_rpc],0,4096);
        CHECK(cixvpu_prepared_boot_receive(out,&reply)==ENOTSUP);
        CHECK(cixvpu_prepared_boot_rpc_header(out,&h)==0);
        CHECK(h.alloc_valid==old.alloc_valid && h.alloc_size==old.alloc_size &&
            h.alloc_max_size==old.alloc_max_size && h.alloc_region==old.alloc_region &&
            h.alloc_log2_alignment==old.alloc_log2_alignment && native_calls==calls);
        memset(&h,0xa5,sizeof(h)); memcpy(&old,&h,sizeof(old));
        CHECK(cixvpu_prepared_boot_rpc_header(out,(void *)&out->plan)==EINVAL);
        CHECK(memcmp(&h,&old,sizeof(h))==0 && native_calls==calls);
        /* Faulted retained metadata invalidates both cached observations. */
        out->dma->owners[out->dma->count-1].map->dm_nsegs=0;
        CHECK(cixvpu_prepared_boot_receive(out,&reply)==EIO);
        CHECK(!out->boot.snapshot.rpc_valid && !out->boot.snapshot.rpc_alloc_valid);
        out->dma->owners[out->dma->count-1].map->dm_nsegs=1;
        CHECK(cixvpu_prepared_boot_rpc_header(out,&h)==0 && !h.valid && !h.alloc_valid);
        CHECK(h.alloc_size==0 && h.alloc_max_size==0 && h.alloc_region==0 && h.alloc_log2_alignment==0);
        boot_drop(out);
    }
}
static void boot_rpc_header_tests(void)
{
    struct cixvpu_prepared *out=boot_begin();
    struct cixvpu_boot_rpc_header h, canary;
    struct cixvpu_boot_reply reply, old_reply;
    uint32_t root=0;
    unsigned int calls=native_calls;
    memset(&h,0xa5,sizeof(h));
    CHECK(cixvpu_prepared_boot_rpc_header(out,&h)==0 && !h.valid);
    CHECK(h.state==0 && h.callid==0 && h.size==0 && native_calls==calls);
    memset(&h,0xa5,sizeof(h)); memcpy(&canary,&h,sizeof(canary));
    CHECK(cixvpu_prepared_boot_rpc_header(NULL,&h)==EINVAL);
    CHECK(memcmp(&h,&canary,sizeof(h))==0);
    CHECK(cixvpu_prepared_boot_rpc_header(out,NULL)==EINVAL);
    CHECK(cixvpu_prepared_boot_rpc_header(out,(void *)&out->plan)==EINVAL);
    CHECK(cixvpu_prepared_boot_rpc_header(out,resources[boot_rpc].kva)==EINVAL);
    intr=true; CHECK(cixvpu_prepared_boot_rpc_header(out,&h)==EWOULDBLOCK); intr=false;
    softintr=true; CHECK(cixvpu_prepared_boot_rpc_header(out,&h)==EWOULDBLOCK); softintr=false;
    CHECK(memcmp(&h,&canary,sizeof(h))==0 && native_calls==calls);
    CHECK(cixvpu_prepared_publish_root(out,&root)==0);
    put32(device_memory[boot_rpc]+4,0x12345678);
    put32(device_memory[boot_rpc]+8,0xfedcba98);
    CHECK(cixvpu_prepared_boot_receive(out,&reply)==EAGAIN);
    CHECK(cixvpu_prepared_boot_rpc_header(out,&h)==0 && h.valid && h.state==0);
    CHECK(h.callid==0x12345678 && h.size==0xfedcba98);
    put32(device_memory[boot_rpc],1);
    memset(&reply,0xa5,sizeof(reply)); old_reply=reply;
    boot_nevents=0;
    CHECK(cixvpu_prepared_boot_receive(out,&reply)==ENOTSUP);
    CHECK(memcmp(&reply,&old_reply,sizeof(reply))==0 && device_memory[boot_host][0]==0);
    CHECK(boot_nevents==2 && boot_events[0].page==boot_rpc && boot_events[0].offset==0);
    CHECK(boot_events[0].length==12 && boot_events[0].ops==BUS_DMASYNC_POSTREAD);
    CHECK(boot_events[1].page==boot_rpc && boot_events[1].offset==0 &&
        boot_events[1].length==12 && boot_events[1].ops==BUS_DMASYNC_PREREAD);
    calls=native_calls;
    CHECK(cixvpu_prepared_boot_rpc_header(out,&h)==0 && h.valid && h.state==1);
    CHECK(h.callid==0x12345678 && h.size==0xfedcba98);
    CHECK(cixvpu_prepared_boot_receive(out,(void *)&out->plan)==EINVAL);
    CHECK(cixvpu_prepared_boot_rpc_header(out,&h)==0 && h.valid && h.state==1);
    put32(device_memory[boot_rpc],0); /* A sticky retry must not observe this. */
    CHECK(cixvpu_prepared_boot_receive(out,&reply)==ENOTSUP);
    CHECK(cixvpu_prepared_boot_rpc_header(out,&h)==0 && h.valid && h.state==1);
    CHECK(h.callid==0x12345678 && h.size==0xfedcba98 && native_calls==calls && boot_nevents==2);
    boot_drop(out);

    static const struct { uint32_t state, callid, size; } vectors[]={
        {2,3,1012}, {99,0x87654321,1013}, {UINT32_MAX,0x80000000,UINT32_MAX}
    };
    for(unsigned int i=0;i<__arraycount(vectors);i++) {
        out=boot_begin(); CHECK(cixvpu_prepared_publish_root(out,&root)==0);
        put32(device_memory[boot_rpc],vectors[i].state);
        put32(device_memory[boot_rpc]+4,vectors[i].callid);
        put32(device_memory[boot_rpc]+8,vectors[i].size);
        memset(&reply,0xa5,sizeof(reply)); old_reply=reply;
        boot_nevents=0;
        CHECK(cixvpu_prepared_boot_receive(out,&reply)==ENOTSUP);
        CHECK(memcmp(&reply,&old_reply,sizeof(reply))==0 && device_memory[boot_host][0]==0);
        calls=native_calls;
        CHECK(cixvpu_prepared_boot_rpc_header(out,&h)==0 && h.valid);
        CHECK(h.state==vectors[i].state && h.callid==vectors[i].callid && h.size==vectors[i].size);
        CHECK(boot_nevents==2 && native_calls==calls);
        CHECK(boot_events[0].page==boot_rpc && boot_events[0].length==12 && boot_events[0].ops==BUS_DMASYNC_POSTREAD);
        CHECK(boot_events[1].page==boot_rpc && boot_events[1].length==12 && boot_events[1].ops==BUS_DMASYNC_PREREAD);
        boot_drop(out);
    }
    out=boot_begin(); CHECK(cixvpu_prepared_publish_root(out,&root)==0);
    CHECK(cixvpu_prepared_boot_receive(out,&reply)==EAGAIN);
    CHECK(cixvpu_prepared_boot_rpc_header(out,&h)==0 && h.valid);
    calls=native_calls;
    out->dma->owners[out->dma->count-1].map->dm_nsegs=0;
    CHECK(cixvpu_prepared_boot_receive(out,&reply)==EIO);
    memset(&h,0xa5,sizeof(h)); memcpy(&canary,&h,sizeof(canary));
    CHECK(cixvpu_prepared_boot_rpc_header(out,&h)==EIO && memcmp(&h,&canary,sizeof(h))==0);
    out->dma->owners[out->dma->count-1].map->dm_nsegs=1;
    CHECK(cixvpu_prepared_boot_rpc_header(out,&h)==0 && !h.valid);
    CHECK(h.state==0 && h.callid==0 && h.size==0 && native_calls==calls);
    boot_drop(out);
}
static void boot_api_tests(void)
{
    struct cixvpu_prepared *out=boot_begin();
    uint32_t root=0xa5a5a5a5;
    void *bad[]={out,&out->plan,out->dma,&out->dma->owners[0],resources[0].kva,
        out->dma->owners[0].map,(uint8_t *)out+out->bytes-4};
    for(unsigned int i=0;i<__arraycount(bad);i++) {
        CHECK(cixvpu_prepared_publish_root(out,bad[i])==EINVAL);
        CHECK(!out->dma->ever_visible && boot_nevents==0);
    }
    CHECK(cixvpu_prepared_publish_root(out,(void *)(UINTPTR_MAX-3))==EINVAL);
    intr=true; CHECK(cixvpu_prepared_publish_root(out,&root)==EWOULDBLOCK); intr=false;
    CHECK(root==0xa5a5a5a5 && !out->dma->ever_visible);
    CHECK(cixvpu_prepared_publish_root(out,&root)==0 && root==out->root_entry);
    CHECK(out->dma->ever_visible && boot_nevents==0);
    root=0x12345678;
    CHECK(cixvpu_prepared_publish_root(out,&root)==EBUSY && root==0x12345678);
    CHECK(cixvpu_prepared_free(&out)==EBUSY && out!=NULL);
    struct cixvpu_boot_reply reply,before;
    memset(&reply,0xa5,sizeof(reply)); before=reply;
    CHECK(cixvpu_prepared_boot_receive(out,(void *)&out->plan)==EINVAL && boot_nevents==0);
    CHECK(cixvpu_prepared_boot_receive(out,&reply)==EAGAIN);
    CHECK(memcmp(&reply,&before,sizeof(reply))==0);
    boot_nevents=0;
    boot_message(0,2001,4,0,0x000407d1);
    CHECK(cixvpu_prepared_boot_receive(out,&reply)==0 && reply.code==2001 && reply.core==0);
    CHECK(device_memory[boot_host][0]==2 && device_memory[boot_host][2]==2);
    CHECK(word(device_memory[boot_host],4)==1006 && word(device_memory[boot_host],5)==1006);
    CHECK(boot_nevents==10);
    CHECK(boot_events[0].page==boot_rpc && boot_events[0].ops==BUS_DMASYNC_POSTREAD && boot_events[0].length==12);
    CHECK(boot_events[1].ops==BUS_DMASYNC_PREREAD);
    CHECK(boot_events[2].page==boot_mve && boot_events[2].ops==BUS_DMASYNC_POSTREAD && boot_events[2].length==16);
    CHECK(boot_events[3].ops==BUS_DMASYNC_PREREAD);
    CHECK(boot_events[4].page==boot_mve && boot_events[4].ops==BUS_DMASYNC_POSTREAD && boot_events[4].length==4096);
    CHECK(boot_events[5].ops==BUS_DMASYNC_PREREAD);
    CHECK(boot_events[6].page==boot_mve && boot_events[6].ops==BUS_DMASYNC_POSTREAD && boot_events[6].length==16);
    CHECK(boot_events[7].ops==BUS_DMASYNC_PREREAD);
    CHECK(boot_events[8].page==boot_host && boot_events[8].ops==BUS_DMASYNC_POSTWRITE && boot_events[8].offset==0 && boot_events[8].length==2);
    CHECK(boot_events[9].page==boot_host && boot_events[9].ops==BUS_DMASYNC_PREWRITE && boot_events[9].offset==0 && boot_events[9].length==2);
    boot_nevents=0; boot_message(2,2009,0,0,0x00040faa);
    CHECK(cixvpu_prepared_boot_receive(out,&reply)==0 && reply.code==2009);
    CHECK(device_memory[boot_host][0]==3 && boot_nevents==10);
    boot_drop(out);
    for(unsigned int f=0;f<7;f++) {
        out=boot_begin(); CHECK(cixvpu_prepared_publish_root(out,&root)==0);
        boot_message(0,2001,4,0,0x000407d1);
        if(f==0) put32(device_memory[boot_mve]+4,1),put32(device_memory[boot_mve]+8,2),put32(device_memory[boot_mve]+12,3);
        if(f==1) boot_message(0,2001,4,1,0x000407d2);
        if(f==2) boot_message(0,2001,0,0,2001);
        if(f==3) boot_message(0,2009,4,0,0x000407d9);
        if(f==4) boot_message(0,2010,0,0,2010);
        if(f==5) put32(device_memory[boot_rpc],1);
        if(f==6) put32(device_memory[boot_rpc],99);
        memset(&reply,0xa5,sizeof(reply)); before=reply;
        CHECK(cixvpu_prepared_boot_receive(out,&reply)!=0);
        CHECK(memcmp(&reply,&before,sizeof(reply))==0 && device_memory[boot_host][0]==0);
        unsigned int calls=native_calls;
        CHECK(cixvpu_prepared_boot_receive(out,&reply)!=0 && native_calls==calls);
        CHECK(cixvpu_prepared_free(&out)==EBUSY); boot_drop(out);
    }
    out=boot_begin(); CHECK(cixvpu_prepared_publish_root(out,&root)==0);
    boot_unstable=true;
    CHECK(cixvpu_prepared_boot_receive(out,&reply)==EAGAIN && boot_nevents==26);
    CHECK(device_memory[boot_host][0]==0);
    boot_unstable=false; boot_nevents=0;
    CHECK(cixvpu_prepared_boot_receive(out,&reply)==EAGAIN); boot_drop(out);
    out=boot_begin(); CHECK(cixvpu_prepared_publish_root(out,&root)==0);
    boot_append=true;
    CHECK(cixvpu_prepared_boot_receive(out,&reply)==0 && reply.code==2001 && reply.core==0);
    CHECK(device_memory[boot_host][0]==2 && !boot_append);
    CHECK(cixvpu_prepared_boot_receive(out,&reply)==EAGAIN);
    boot_drop(out);
    out=boot_begin(); CHECK(cixvpu_prepared_publish_root(out,&root)==0);
    out->boot.cursor=1019; out->boot.checksum.sum=0;
    device_memory[boot_host][0]=0xfb; device_memory[boot_host][1]=3;
    boot_message(1019,2001,4,0,0x000407d1);
    CHECK(cixvpu_prepared_boot_receive(out,&reply)==0 && reply.code==2001);
    CHECK(device_memory[boot_host][0]==1 && device_memory[boot_host][1]==0);
    boot_drop(out);
    out=boot_begin(); CHECK(cixvpu_prepared_publish_root(out,&root)==0);
    unsigned int before_calls=native_calls;
    out->dma->owners[0].map->dm_nsegs=2;
    CHECK(cixvpu_prepared_boot_receive(out,&reply)!=0 && native_calls==before_calls);
    out->dma->owners[0].map->dm_nsegs=1;
    CHECK(cixvpu_prepared_boot_receive(out,&reply)!=0 && native_calls==before_calls);
    boot_drop(out);
    struct cixvpu_fw_plan plan;
    struct cixvpu_dma *owner=begin(4,&plan),*saved=owner; out=NULL;

    CHECK(cixvpu_prepare_boot_cpu(&owner,&plan,image,extent,fixture_pin,&out)==ENOTSUP && owner==saved && out==NULL);
    CHECK(cixvpu_dma_free(&owner)==0);
    for(unsigned int f=0;f<3;f++) {
        owner=begin(1,&plan); saved=owner; out=NULL;
        memcpy(image+80,"r0p0-sum0005",12);
        if(f==0) image[4]=4;
        if(f==1) image[80]='x';
        memcpy(pinned_image,image,sizeof(image));
        CHECK(cixvpu_fw_plan_build(image,extent,1,&plan)==0);
        if(f!=2) {
            CHECK(cixvpu_prepare_boot_cpu(&owner,&plan,image,extent,fixture_pin,&out)==ENOTSUP && owner==saved && out==NULL);
            CHECK(cixvpu_dma_free(&owner)==0);
        } else {
            CHECK(cixvpu_prepare_boot_cpu(&owner,&plan,image,extent,fixture_pin,&out)==0);
            root=0x12345678; unsigned int calls=native_calls;
            CHECK(cixvpu_prepared_publish_root(out,&root)==EBUSY && root==0x12345678 && native_calls==calls);
            CHECK(cixvpu_prepared_boot_receive(out,&reply)==EBUSY && native_calls==calls);
            CHECK(cixvpu_prepared_free(&out)==0 && out==NULL && depth==0 && live_kmem==0);
        }
    }
    out=boot_begin();
    CHECK(cixvpu_dma_mark_exposed(out->dma,out)==0);
    root=0x12345678;
    CHECK(cixvpu_prepared_publish_root(out,&root)==EBUSY && root==0x12345678);
    CHECK(!out->boot.published && out->dma->ever_visible); boot_drop(out);
    out=prepared_begin(1,&plan); root=0x12345678;
    CHECK(cixvpu_prepared_publish_root(out,&root)==ENOTSUP && root==0x12345678);
    CHECK(cixvpu_prepared_free(&out)==0 && out==NULL);

}
#endif

#endif
#if defined(CIXVPU_PREPARE_API) && defined(CIXVPU_FW_PLAN_FRAME_ADDRESS)
/* Ordinary preparation must never erase an unsupported slot request. */
static void closed_slot_profile_tests(void)
{
    const struct cixvpu_fw_slot_request request = {2, 4, 4097, 2051, 4097};
    struct cixvpu_fw_plan base, slots;
    struct cixvpu_dma *owner = rpc_reserve_begin(&base);
    static uint8_t canary[4096];
    memset(canary, 0xa5, sizeof(canary));
    CHECK(cixvpu_fw_plan_build_frame_slots(rpc_image, sizeof(rpc_image), 1,
        &request, &slots) == 0);
    CHECK(cixvpu_dma_free(&owner) == 0);
    reset();
    CHECK(cixvpu_dma_alloc(&tag32, &tag64, &slots, &owner) == EINVAL);
    CHECK(owner == NULL && native_calls == 0 && live_kmem == 0 && depth == 0);
    for (unsigned int boot = 0; boot < 2; boot++) {
        for (unsigned int fault = 0; fault < 9; fault++) {
            owner = rpc_reserve_begin(&base);
            struct cixvpu_dma *saved = owner;
            struct cixvpu_prepared *out = NULL;
            struct cixvpu_fw_plan bad = base;
            if (fault == 0) { bad.slots_enabled = 1; bad.slots = request; }
            if (fault == 1) bad = slots; /* Valid but unsupported profile. */
            if (fault == 2) bad.slots_enabled = UINT32_MAX;
            if (fault == 3) bad.slots.width = 2;
            if (fault == 4) bad.slots.height = 4;
            if (fault == 5) bad.slots.stride_y = 4097;
            if (fault == 6) bad.slots.stride_uv = 2051;
            if (fault == 7) bad.slots.bitstream_bytes = 4097;
            if (fault == 8) bad.slots = request; /* Disabled with all data set. */
            unsigned int calls = native_calls;
            int error = boot ? cixvpu_prepare_boot_cpu(&owner, &bad, rpc_image,
                sizeof(rpc_image), fixture_pin, &out) :
                cixvpu_prepare_cpu(&owner, &bad, rpc_image, sizeof(rpc_image),
                fixture_pin, &out);
            CHECK(error == EINVAL && owner == saved && out == NULL);
            CHECK(hashes == 0 && meta_calls == 0 && page_calls == 0 &&
                map_calls == 0 && link_calls == 0 && encode_calls == 0 &&
                native_calls == calls && owner->phase == CIXVPU_DMA_ALLOC &&
                !owner->ever_visible);
            for (unsigned int i = 0; i < 390; i++)
                CHECK(memcmp(resources[i].kva, canary, 4096) == 0);
            CHECK(cixvpu_dma_free(&owner) == 0 && depth == 0 && live_kmem == 0);
        }
    }
}
#endif
int main(void)

{
#ifdef CIXVPU_PREPARE_API
#ifdef CIXVPU_FW_PLAN_FRAME_ADDRESS
    closed_slot_profile_tests();
#endif
    debugparam_tests(); success_tests(); failure_tests(); handle_alias_tests(); argument_tests(); boundary_tests();
#ifdef CIXVPU_SYNC_API
    sync_success_tests(); sync_failure_tests(); sync_owner_tests(); exposure_tests();
#ifdef CIXVPU_BOOT_API
    rpc_reserve_tests(); rpc_protocol_tests(); boot_rpc_second_tests(); boot_rpc_resize_runtime_tests(); boot_rpc_resize_tests(); boot_rpc_alloc_tests(); boot_rpc_header_tests(); boot_api_tests();
#else
    fprintf(stderr, "FAIL: no typed boot seed/publication/receive API\n"); return 1;
#endif
#else
    fprintf(stderr, "FAIL: no enforced initial-sync/exposure boundary\n");
    return 1;
#endif
#else
    (void)fixture_pin; (void)extent;
    struct cixvpu_fw_plan plan = make_plan(4);
    struct cixvpu_dma *owner = NULL;
    reset(); CHECK(cixvpu_dma_alloc(&tag32, &tag64, &plan, &owner) == 0);
    /* Actual allocation-only source still has zero text; it is not prepared. */
    if (memcmp(resources[0].kva, image, 4096) == 0) return 2;
    fprintf(stderr, "FAIL: actual allocator has no text/PTE prepare-and-seal operation\n");
    CHECK(cixvpu_dma_free(&owner) == 0);
    return 1;
#endif
    printf("cixvpu-prepare: %u checks, 0 failures\n", checks);
    return 0;
}
