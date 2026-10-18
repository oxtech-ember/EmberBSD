/* SPDX-License-Identifier: BSD-2-Clause */
/* Reuse actual allocator and its platform boundary, never a copied algorithm. */
#define main cixvpu_allocation_contract_main
#include "cixvpu-dma-contract.c"
#undef main
#include "cixvpu_mmu.h"

_Alignas(max_align_t) static uint8_t image[8192], pinned_image[8192];
static const uint8_t pin[32] = { 0x47, 0x21, 0x83, 0xb0 };
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
 * Match the complete immutable synthetic pin; native object uses libkern SHA. */
#define SHA256_DIGEST_LENGTH 32
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
typedef struct { unsigned int phase; bool matches; } SHA256_CTX;
static unsigned int hashes, page_calls, map_calls, link_calls, encode_calls;
static unsigned int meta_calls, fail_meta, fail_page, fail_map, fail_link, fail_encode;
static unsigned int view_fault, duplicate_map;
static unsigned int success_counts[4];
static bool fail_free, top_bus;
static int SHA256_Init(SHA256_CTX *ctx)
{ ctx->phase = 1; ctx->matches = false; return 1; }
static int SHA256_Update(SHA256_CTX *ctx, const uint8_t *p, size_t n)
{ CHECK(ctx->phase == 1 && p == image && n == extent); ctx->matches = memcmp(p, pinned_image, n) == 0; ctx->phase = 2; hashes++; return 1; }
static int SHA256_Final(uint8_t out[32], SHA256_CTX *ctx)
{ CHECK(ctx->phase == 2); memcpy(out, pin, 32); if (!ctx->matches) out[0] ^= 1; return 1; }
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
    if (error == 0 && map_calls == duplicate_map) return cixvpu_mmu_map(table, n, va, bus, attr, perm);
    return error;
}
static int prepare_link(void *table, size_t n, uint32_t va, uint64_t bus)
{ if (++link_calls == fail_link) return EIO; return cixvpu_mmu_link(table, n, va, bus); }
static int prepare_encode(uint64_t bus, unsigned int attr, unsigned int perm, uint32_t *out)
{ if (++encode_calls == fail_encode) return EIO; return cixvpu_mmu_encode(bus, attr, perm, out); }
static int prepare_free(struct cixvpu_dma **owner)
{ if (fail_free) return EIO; return cixvpu_dma_free(owner); }
#include "prepare-header.inc"
#define kmem_zalloc prepare_kmem
#define cixvpu_dma_page(...) prepare_page(__VA_ARGS__)
#define cixvpu_dma_free prepare_free
#define cixvpu_mmu_map prepare_map
#define cixvpu_mmu_link prepare_link
#define cixvpu_mmu_encode prepare_encode
#include "prepare-source.inc"
#undef kmem_zalloc
#undef cixvpu_dma_page
#undef cixvpu_dma_free
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
        struct cixvpu_dma *owner = begin(cores, &plan);
        struct cixvpu_prepared *out = NULL;
        CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, pin, &out) == 0);
        CHECK(owner == NULL && out != NULL && hashes == 1);
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
        CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, pin, &out) == EINVAL && out == same);
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
        CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, pin, &out) != 0);
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
            CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, pin, &out) != 0);
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
            cixvpu_prepare_cpu(&owner, &plan, image, extent, pin, slot) :
            cixvpu_prepare_cpu(slot, &plan, image, extent, pin, &out);
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
    CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, pin, &out) == EBUSY && out == &canary);
    out = NULL;
    CHECK(cixvpu_prepare_cpu(NULL, &plan, image, extent, pin, &out) == EINVAL);
    CHECK(cixvpu_prepare_cpu(&owner, NULL, image, extent, pin, &out) == EINVAL);
    CHECK(cixvpu_prepare_cpu(&owner, &plan, NULL, extent, pin, &out) == EINVAL);
    CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, NULL, &out) == EINVAL);
    CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, pin, NULL) == EINVAL);
    CHECK(cixvpu_prepare_cpu(&owner, &plan, image, SIZE_MAX, pin, &out) == EINVAL);
    CHECK(cixvpu_prepare_cpu(&owner, &plan, (void *)(UINTPTR_MAX - 3), extent, pin, &out) == EINVAL);
    intr = true;
    CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, pin, &out) == EWOULDBLOCK);
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
        CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, pin, &out) == EINVAL);
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
    CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, pin, &out) == 0);
    for (unsigned int i = 0; i < plan.backing_pages; i++) {
        struct cixvpu_fw_plan_page p; CHECK(cixvpu_fw_plan_page(&plan, i, &p) == 0);
        if (p.role == CIXVPU_FW_PLAN_LEAF && p.leaf_address == 0)
            CHECK(word(resources[i].kva, 1) == 0x3ffffffe);
    }
    CHECK(cixvpu_prepared_free(&out) == 0 && out == NULL && depth == 0 && live_kmem == 0);
    CHECK(cixvpu_prepared_free(&out) == EINVAL);
    owner = begin(4, &plan); struct cixvpu_dma *saved = owner;
    view_fault = 8;
    CHECK(cixvpu_prepare_cpu(&owner, &plan, image, extent, pin, &out) == EINVAL);
    CHECK(owner == saved && out == NULL);
    for (unsigned int i = 0; i < plan.backing_pages; i++)
        for (unsigned int j = 0; j < 4096; j++)
            CHECK(((const uint8_t *)resources[i].kva)[j] == 0xa5);
    CHECK(cixvpu_dma_free(&owner) == 0 && depth == 0 && live_kmem == 0);
}
#endif
int main(void)
{
#ifdef CIXVPU_PREPARE_API
    success_tests(); failure_tests(); handle_alias_tests(); argument_tests(); boundary_tests();
#else
    (void)pin; (void)extent;
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
