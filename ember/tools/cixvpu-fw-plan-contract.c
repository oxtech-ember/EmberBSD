/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted real-source tests. */
/* This contract uses no firmware blob, allocator, MMIO or VPU device. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cixvpu_fw_plan.h"

static unsigned int checks;

#define CHECK(c) do { \
    checks++; \
    if (!(c)) { \
        fprintf(stderr, "FAIL line %u: %s\n", (unsigned int)__LINE__, #c); \
        exit(1); \
    } \
} while (0)

static void
put32(uint8_t *p, uint32_t n)
{

    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
    p[2] = (uint8_t)(n >> 16);
    p[3] = (uint8_t)(n >> 24);
}

/* Logical image extent is larger; the actual parser reads only this header. */
static void
fixture(uint8_t *p)
{

    memset(p, 0, CIXVPU_FW_HEADER_SIZE);
    p[4] = 5;
    p[5] = 3;
    p[6] = 8;
    p[7] = 8;
    put32(p + 96, 4097);       /* Two text pages, last has one byte. */
    put32(p + 100, 0x8000);
    put32(p + 104, 6);
    put32(p + 108, 0x23);      /* Private BSS0,1,5. BSS2,3 shared;4 hole. */
    put32(p + 172, 0xa000);
    put32(p + 176, 4097);
}

static void
build_error(const uint8_t *p, size_t size, unsigned int cores)
{
    struct cixvpu_fw_plan out, before;

    memset(&out, 0xa5, sizeof(out));
    before = out;
    CHECK(cixvpu_fw_plan_build(p, size, cores, &out) != 0);
    CHECK(memcmp(&out, &before, sizeof(out)) == 0);
}

static void
tampered(const struct cixvpu_fw_plan *p)
{
    struct cixvpu_fw_plan_page page, old_page;
    struct cixvpu_fw_plan_mapping map, old_map;

    memset(&page, 0x5a, sizeof(page));
    old_page = page;
    memset(&map, 0xa5, sizeof(map));
    old_map = map;
    CHECK(cixvpu_fw_plan_page(p, 0, &page) != 0);
    CHECK(memcmp(&page, &old_page, sizeof(page)) == 0);
    CHECK(cixvpu_fw_plan_mapping(p, 0, &map) != 0);
    CHECK(memcmp(&map, &old_map, sizeof(map)) == 0);
}

/* Synthetic bus addresses occur only in this software MMU composition test. */
static uint64_t
test_bus(unsigned int backing)
{

    return UINT64_C(0x40000000) + (uint64_t)backing * 4096;
}

static void
compose(const struct cixvpu_fw_plan *plan)
{
    uint8_t root[4096] = { 0 }, leaves[5][4096] = { { 0 } };
    unsigned int references[CIXVPU_FW_PLAN_MAX_PAGES] = { 0 };
    struct cixvpu_fw_plan_page page;
    struct cixvpu_fw_plan_page leaf;
    struct cixvpu_fw_plan_mapping map;
    uint32_t root_owner = UINT32_MAX, first_leaf = UINT32_MAX;
    uint32_t value, encoded, leaf_count = 0, i;

    CHECK(plan->backing_pages <= CIXVPU_FW_PLAN_MAX_PAGES);
    for (i = 0; i < plan->backing_pages; i++) {
        CHECK(cixvpu_fw_plan_page(plan, i, &page) == 0);
        CHECK(page.backing_index == i);
        if (page.role != CIXVPU_FW_PLAN_TEXT)
            CHECK(page.image_offset == 0 && page.image_bytes == 0);
        if (page.role == CIXVPU_FW_PLAN_ROOT) {
            CHECK(root_owner == UINT32_MAX);
            root_owner = i;
            CHECK(page.attribute == CIXVPU_MMU_PRIVATE &&
                page.permission == CIXVPU_MMU_WRITE);
            CHECK(cixvpu_mmu_encode(test_bus(i), page.attribute,
                page.permission, &encoded) == 0);
        } else if (page.role == CIXVPU_FW_PLAN_LEAF) {
            if (first_leaf == UINT32_MAX)
                first_leaf = i;
            CHECK(leaf_count < 5);
            CHECK(cixvpu_mmu_link(root, sizeof(root), page.leaf_address,
                test_bus(i)) == 0);
            CHECK(cixvpu_mmu_lookup(root, sizeof(root),
                page.leaf_address >> 22, &value) == 0);
            CHECK(cixvpu_mmu_encode(test_bus(i), page.attribute,
                page.permission, &encoded) == 0);
            CHECK(value == encoded);
            leaf_count++;
        }
    }
    CHECK(root_owner != UINT32_MAX);
#ifdef CIXVPU_FW_PLAN_BOOT_RPC_ADDRESS
    CHECK(leaf_count == plan->cores + 1 + plan->boot_rpc);
#else
    CHECK(leaf_count == plan->cores + 1);
#endif
    for (i = 0; i < plan->mappings; i++) {
        CHECK(cixvpu_fw_plan_mapping(plan, i, &map) == 0);
        CHECK(map.backing_index < root_owner);
        CHECK(map.leaf_backing_index >= first_leaf);
        CHECK(map.leaf_backing_index - first_leaf < leaf_count);
        CHECK(cixvpu_fw_plan_page(plan, map.leaf_backing_index, &leaf) == 0);
        CHECK((map.address >> 22) == (leaf.leaf_address >> 22));
        CHECK(cixvpu_fw_plan_page(plan, map.backing_index, &page) == 0);
        if (page.role == CIXVPU_FW_PLAN_PRIVATE)
            CHECK(page.core == leaf.core);
        if (leaf.core != CIXVPU_FW_PLAN_NO_CORE)
            CHECK(map.address % 0x01000000 < 0x100000);
        CHECK(map.role == page.role);
        CHECK(map.attribute == page.attribute);
        CHECK(map.permission == page.permission);
        CHECK(cixvpu_mmu_map(leaves[map.leaf_backing_index - first_leaf],
            4096, map.address, test_bus(map.backing_index), map.attribute,
            map.permission) == 0);  /* Duplicate VA fails in the real MMU. */
        references[map.backing_index]++;
    }
#ifdef CIXVPU_FW_PLAN_BOOT_RPC_ADDRESS
    if (plan->boot_rpc) {
        CHECK(leaf_count==3);
        for (i=32;i<1024;i++) {
            CHECK(cixvpu_mmu_lookup(leaves[2],4096,i,&value)==0);
            CHECK(value==0); /* Tail is invalid, not a dummy physical page. */
        }
        for (i=129;i<144;i++) {
            CHECK(cixvpu_mmu_lookup(root,4096,i,&value)==0 && value==0);
        }
    }
#endif
    for (i = 0; i < root_owner; i++) {
        CHECK(cixvpu_fw_plan_page(plan, i, &page) == 0);
        CHECK(references[i] == (page.role == CIXVPU_FW_PLAN_TEXT ||
            page.role == CIXVPU_FW_PLAN_SHARED ? plan->cores : 1));
    }
}

#ifdef CIXVPU_FW_PLAN_BOOT_RPC_ADDRESS
#define build_boot_rpc cixvpu_fw_plan_build_boot_rpc
#else
/* Missing API is a runtime RED boundary, not a duplicated planner. */
static int
build_boot_rpc(const void *image, size_t size, unsigned int cores,
    struct cixvpu_fw_plan *out)
{
    (void)image; (void)size; (void)cores; (void)out;
    return ENOTSUP;
}
#endif

static void
boot_rpc_tests(void)
{
    uint8_t image[CIXVPU_FW_HEADER_SIZE];
    struct cixvpu_fw_plan old, plan;

    fixture(image);
    put32(image+96,420736);
    put32(image+100,0x70000);
    put32(image+104,15);
    put32(image+108,0x7fff);
    put32(image+172,0x7f000);
    put32(image+176,0);
    CHECK(cixvpu_fw_plan_build(image,420736,1,&old)==0);
    CHECK(old.backing_pages==256 && old.mappings==253);
    CHECK(build_boot_rpc(image,420736,1,&plan)==0);
    CHECK(plan.backing_pages==289 && plan.mappings==285);
#ifdef CIXVPU_FW_PLAN_BOOT_RPC_ADDRESS
    CHECK(old.boot_rpc==0 && plan.boot_rpc==1);
    CHECK(CIXVPU_FW_PLAN_BOOT_RPC_ADDRESS==0x20000000);
    CHECK(CIXVPU_FW_PLAN_BOOT_RPC_INITIAL_SIZE==131072);
    CHECK(CIXVPU_FW_PLAN_BOOT_RPC_CAPACITY==67108864);
    CHECK(CIXVPU_FW_PLAN_BOOT_RPC_REGION==0 &&
        CIXVPU_FW_PLAN_BOOT_RPC_LOG2_ALIGNMENT==22 &&
        CIXVPU_FW_PLAN_BOOT_RPC_PAGES==32);
    struct cixvpu_fw_plan_page before, after;
    struct cixvpu_fw_plan_mapping m, previous;
    /* Existing data records and VAs are byte-for-field equal, before tables. */
    for(unsigned int i=0;i<253;i++) {
        CHECK(cixvpu_fw_plan_page(&old,i,&before)==0);
        CHECK(cixvpu_fw_plan_page(&plan,i,&after)==0);
        CHECK(before.backing_index==after.backing_index && before.role==after.role &&
            before.core==after.core && before.ordinal==after.ordinal &&
            before.image_offset==after.image_offset && before.image_bytes==after.image_bytes &&
            before.attribute==after.attribute && before.permission==after.permission);
        CHECK(cixvpu_fw_plan_mapping(&old,i,&previous)==0);
        CHECK(cixvpu_fw_plan_mapping(&plan,i,&m)==0);
        CHECK(previous.address==m.address && previous.backing_index==m.backing_index &&
            previous.role==m.role && previous.attribute==m.attribute && previous.permission==m.permission);
        CHECK(m.leaf_backing_index==previous.leaf_backing_index+32);
    }
    for(unsigned int i=0;i<32;i++) {
        CHECK(cixvpu_fw_plan_page(&plan,253+i,&after)==0);
        CHECK(after.role==CIXVPU_FW_PLAN_RPC_DATA && after.ordinal==i &&
            after.core==CIXVPU_FW_PLAN_NO_CORE && after.image_bytes==0 && after.image_offset==0 &&
            after.attribute==CIXVPU_MMU_SHARED_RW && after.permission==CIXVPU_MMU_WRITE);
        CHECK(cixvpu_fw_plan_mapping(&plan,253+i,&m)==0);
        CHECK(m.address==0x20000000+i*4096 && m.backing_index==253+i &&
            m.leaf_backing_index==288 && m.role==CIXVPU_FW_PLAN_RPC_DATA &&
            m.attribute==CIXVPU_MMU_SHARED_RW && m.permission==CIXVPU_MMU_WRITE);
    }
    CHECK(cixvpu_fw_plan_page(&old,253,&before)==0 && before.role==CIXVPU_FW_PLAN_ROOT);
    CHECK(cixvpu_fw_plan_page(&plan,285,&after)==0 && after.role==CIXVPU_FW_PLAN_ROOT);
    static const uint32_t leaf_addresses[]={0,0x10000000,0x20000000};
    for(unsigned int i=0;i<3;i++) {
        CHECK(cixvpu_fw_plan_page(&plan,286+i,&after)==0);
        CHECK(after.role==CIXVPU_FW_PLAN_LEAF && after.ordinal==i &&
            after.leaf_address==leaf_addresses[i] && after.attribute==CIXVPU_MMU_PRIVATE &&
            after.permission==CIXVPU_MMU_READ && (i==0 ? after.core==0 : after.core==CIXVPU_FW_PLAN_NO_CORE));
    }
    compose(&plan);
    struct cixvpu_fw_plan bad, canary;
    memset(&canary,0xa5,sizeof(canary));
    unsigned int cores[]={0,2,4,UINT32_MAX};
    for(unsigned int i=0;i<sizeof(cores)/sizeof(cores[0]);i++) {
        bad=canary; CHECK(build_boot_rpc(image,420736,cores[i],&bad)==EINVAL);
        CHECK(memcmp(&bad,&canary,sizeof(bad))==0);
    }
    bad=canary; image[4]=6;
    CHECK(build_boot_rpc(image,420736,1,&bad)==EINVAL && memcmp(&bad,&canary,sizeof(bad))==0);
    CHECK(cixvpu_fw_plan_build(image,420736,1,&bad)==0 && bad.boot_rpc==0);
    image[4]=5;
    bad=canary; CHECK(build_boot_rpc(NULL,420736,1,&bad)==EINVAL);
    CHECK(memcmp(&bad,&canary,sizeof(bad))==0);
    CHECK(build_boot_rpc(image,179,1,&bad)==EINVAL && memcmp(&bad,&canary,sizeof(bad))==0);
    image[5]=4; bad=canary;
    CHECK(build_boot_rpc(image,420736,1,&bad)==EINVAL && memcmp(&bad,&canary,sizeof(bad))==0);
    image[5]=3;
    CHECK(build_boot_rpc(image,420736,1,NULL)==EINVAL);
    CHECK(build_boot_rpc(image,SIZE_MAX,1,&bad)==0 && bad.boot_rpc==1);
    CHECK(cixvpu_fw_plan_page(&plan,289,&after)==ERANGE);
    CHECK(cixvpu_fw_plan_mapping(&plan,285,&m)==ERANGE);
    bad=plan; bad.boot_rpc=2; tampered(&bad);
    bad=plan; bad.boot_rpc=2; bad.backing_pages=322; bad.mappings=317; tampered(&bad);
    bad=plan; bad.boot_rpc=UINT32_MAX; tampered(&bad);
    bad=plan; bad.boot_rpc=0; tampered(&bad);
    bad=old; bad.boot_rpc=1; tampered(&bad);
    bad=plan; bad.cores=2; tampered(&bad);
    bad=plan; bad.info.minor=4; tampered(&bad);
    bad=plan; bad.backing_pages=UINT32_MAX; tampered(&bad);
    bad=plan; bad.mappings=UINT32_MAX; tampered(&bad);
    bad=plan; bad.backing_pages--; tampered(&bad);
    bad=plan; bad.mappings--; tampered(&bad);
#endif

}

int
main(void)
{
    uint8_t image[CIXVPU_FW_HEADER_SIZE];
    struct cixvpu_fw_info parsed;
    struct cixvpu_fw_plan plan, bad;
    struct cixvpu_fw_plan_page page, old_page;
    struct cixvpu_fw_plan_mapping map, old_map;
    unsigned int n, i;
    static const enum cixvpu_fw_plan_role common_roles[] = {
        CIXVPU_FW_PLAN_MSG_HOST, CIXVPU_FW_PLAN_MSG_MVE,
        CIXVPU_FW_PLAN_BUF_IN_HOST, CIXVPU_FW_PLAN_BUF_IN_MVE,
        CIXVPU_FW_PLAN_BUF_OUT_HOST, CIXVPU_FW_PLAN_BUF_OUT_MVE,
        CIXVPU_FW_PLAN_RPC
    };

    fixture(image);
    CHECK(cixvpu_fw_plan_build(image, 8192, 2, &plan) == 0);
    CHECK(plan.backing_pages == 149); /* 2+2*3+2+7+128+1+3. */
    CHECK(plan.mappings == 149);     /* 2*(2+3+2)+7+128. */
    for (i = 0; i < 7; i++) {
        CHECK(cixvpu_fw_plan_mapping(&plan, 14 + i, &map) == 0);
        CHECK(map.address == 0x10079000 + i * 4096 &&
            map.role == common_roles[i]);
        CHECK(cixvpu_fw_plan_page(&plan, map.backing_index, &page) == 0);
        CHECK(page.ordinal == 0 && page.core == CIXVPU_FW_PLAN_NO_CORE);
    }
    CHECK(cixvpu_fw_plan_page(&plan, 0, &page) == 0);
    CHECK(page.image_offset == 0 && page.image_bytes == 4096);
    CHECK(cixvpu_fw_plan_page(&plan, 1, &page) == 0);
    CHECK(page.role == CIXVPU_FW_PLAN_TEXT && page.image_offset == 4096 &&
        page.image_bytes == 1 && page.attribute == CIXVPU_MMU_PRIVATE &&
        page.permission == CIXVPU_MMU_EXECUTE);
    CHECK(cixvpu_fw_plan_mapping(&plan, 0, &map) == 0);
    CHECK(map.address == 0x1000 && map.backing_index == 0);
    CHECK(cixvpu_fw_plan_mapping(&plan, 7, &map) == 0);
    CHECK(map.address == 0x01001000 && map.backing_index == 0);
    CHECK(cixvpu_fw_plan_mapping(&plan, 4, &map) == 0);
    CHECK(map.address == 0xa000 && map.backing_index == 8 &&
        map.role == CIXVPU_FW_PLAN_SHARED &&
        map.attribute == CIXVPU_MMU_PRIVATE &&
        map.permission == CIXVPU_MMU_WRITE);
    CHECK(cixvpu_fw_plan_mapping(&plan, 11, &map) == 0);
    CHECK(map.address == 0x0100a000 && map.backing_index == 8);
    CHECK(cixvpu_fw_plan_mapping(&plan, 6, &map) == 0);
    CHECK(map.address == 0xd000 && map.backing_index == 4);
    CHECK(cixvpu_fw_plan_mapping(&plan, 14, &map) == 0);
    CHECK(map.address == 0x10079000 &&
        map.role == CIXVPU_FW_PLAN_MSG_HOST &&
        map.attribute == CIXVPU_MMU_COHERENT &&
        map.permission == CIXVPU_MMU_WRITE);
    CHECK(cixvpu_fw_plan_mapping(&plan, 20, &map) == 0);
    CHECK(map.address == 0x1007f000 && map.role == CIXVPU_FW_PLAN_RPC);
    CHECK(cixvpu_fw_plan_mapping(&plan, 21, &map) == 0);
    CHECK(map.address == 0x10100000 && map.role == CIXVPU_FW_PLAN_PRINT &&
        map.attribute == CIXVPU_MMU_SHARED_RW &&
        map.permission == CIXVPU_MMU_WRITE);
    CHECK(cixvpu_fw_plan_mapping(&plan, 148, &map) == 0);
    CHECK(map.address == 0x1017f000);
    for (n = 1; n <= 4; n++) {
        CHECK(cixvpu_fw_plan_build(image, 8192, n, &plan) == 0);
        compose(&plan);
    }
    /* Exhaustive six-bit BSS patterns exercise shared override and holes. */
    for (i = 0; i < 64; i++) {
        put32(image + 108, i);
        for (n = 1; n <= 4; n++) {
            CHECK(cixvpu_fw_plan_build(image, 8192, n, &plan) == 0);
            compose(&plan);
        }
    }
    fixture(image);
    put32(image + 100, 8192);
    put32(image + 172, 16384);
    CHECK(cixvpu_fw_parse(image, 8192, &parsed) == CIXVPU_FW_OK);
    build_error(image, 8192, 1); /* Parser accepts; +0x1000 overlaps BSS. */
    fixture(image);
    put32(image + 96, 1048576);
    put32(image + 100, 1048576);
    put32(image + 104, 0);
    put32(image + 172, 1048576);
    put32(image + 176, 0);
    CHECK(cixvpu_fw_parse(image, 1048576, &parsed) == CIXVPU_FW_OK);
    build_error(image, 1048576, 1); /* Text starts after the region's first page. */
    fixture(image);
    build_error(image, 8192, 0);
    build_error(image, 8192, 5);
    build_error(image, 8192, UINT32_MAX);
    build_error(image, 179, 1);
    build_error(NULL, 8192, 1);
    CHECK(cixvpu_fw_plan_build(image, 8192, 1, NULL) != 0);
    CHECK(cixvpu_fw_plan_build(image, SIZE_MAX, 1, &plan) == 0);
    CHECK(cixvpu_fw_plan_build(image, 8192, 1, &plan) == 0);
    memset(&page, 0x5a, sizeof(page));
    old_page = page;
    memset(&map, 0xa5, sizeof(map));
    old_map = map;
    CHECK(cixvpu_fw_plan_page(&plan, plan.backing_pages, &page) != 0);
    CHECK(memcmp(&page, &old_page, sizeof(page)) == 0);
    CHECK(cixvpu_fw_plan_mapping(&plan, plan.mappings, &map) != 0);
    CHECK(memcmp(&map, &old_map, sizeof(map)) == 0);
    CHECK(cixvpu_fw_plan_page(&plan, UINT32_MAX, &page) != 0);
    CHECK(cixvpu_fw_plan_mapping(&plan, UINT32_MAX, &map) != 0);
    CHECK(cixvpu_fw_plan_page(&plan, 0, NULL) != 0);
    CHECK(cixvpu_fw_plan_mapping(&plan, 0, NULL) != 0);
    tampered(NULL);
#define BAD(field, value) do { bad = plan; bad.field = (value); tampered(&bad); } while (0)
    BAD(cores, UINT32_MAX);
    BAD(cores, 0);
    BAD(backing_pages, UINT32_MAX);
    BAD(mappings, UINT32_MAX);
    BAD(info.major, 2);
    BAD(info.text_size, UINT32_MAX);
    BAD(info.text_size, 0);
    BAD(info.text_pages, UINT32_MAX);
    BAD(info.bss_pages, UINT32_MAX);
    BAD(info.bss_address, UINT32_MAX);
    BAD(info.bss_address, 0);
    BAD(info.shared_address, UINT32_MAX);
    BAD(info.shared_size, UINT32_MAX);
    BAD(info.shared_pages, UINT32_MAX);
    BAD(info.private_pages, UINT32_MAX);
    bad = plan;
    bad.info.bitmap[0] ^= 1;
    tampered(&bad);
    memset(&bad, 0xff, sizeof(bad));
    tampered(&bad);
    memset(&bad, 0, sizeof(bad));
    tampered(&bad);
    /* Word boundary: private bits31/33, shared bit32, even when set. */
    fixture(image);
    put32(image + 96, 180);
    put32(image + 100, 0x2000);
    put32(image + 104, 64);
    put32(image + 108, UINT32_C(1) << 31);
    put32(image + 112, 3);
    put32(image + 172, 0x22000);
    put32(image + 176, 1);
    CHECK(cixvpu_fw_plan_build(image, 180, 2, &plan) == 0);
    CHECK(plan.backing_pages == 145 && plan.mappings == 143);
    CHECK(cixvpu_fw_plan_mapping(&plan, 1, &map) == 0);
    CHECK(map.address == 0x21000 && map.backing_index == 1);
    CHECK(cixvpu_fw_plan_mapping(&plan, 2, &map) == 0);
    CHECK(map.address == 0x22000 && map.backing_index == 5);
    CHECK(cixvpu_fw_plan_mapping(&plan, 3, &map) == 0);
    CHECK(map.address == 0x23000 && map.backing_index == 2);
    compose(&plan);
    /* Maximal private BSS, no shared pages, and ignored bitmap tail bits. */
    fixture(image);
    put32(image + 96, 180);
    put32(image + 100, 0x2000);
    put32(image + 104, 254);
    for (i = 0; i < 16; i++)
        put32(image + 108 + i * 4, UINT32_MAX);
    put32(image + 172, 0x100000);
    put32(image + 176, 0);
    CHECK(cixvpu_fw_plan_build(image, 180, 4, &plan) == 0);
    CHECK(plan.backing_pages == 1158 && plan.mappings == 1155);
    compose(&plan);
    /* Empty BSS is valid; maximal text still leaves the required first page. */
    fixture(image);
    put32(image + 96, 1044480);
    put32(image + 100, 1048576);
    put32(image + 104, 0);
    put32(image + 172, 1048576);
    put32(image + 176, 0);
    CHECK(cixvpu_fw_plan_build(image, 1044480, 4, &plan) == 0);
    CHECK(plan.backing_pages == 396); /* 255+135+1+5. */
    CHECK(plan.mappings == 1155);    /* 4*255+135. */
    compose(&plan);
    boot_rpc_tests();
    printf("CIX VPU firmware plan: %u checks, 0 failures\n", checks);
    return 0;
}
