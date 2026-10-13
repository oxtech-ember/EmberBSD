/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted source tests. */
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sun60i_a733_gpu_mmu.h"
#include "sun60i_a733_gpu_fwinfo.h"
#ifdef MMU_UPSTREAM_ORACLE
#include "upstream.h"
#endif

static unsigned int checks;
#define CHECK(e) do { \
	checks++; \
	if (!(e)) { \
		fprintf(stderr, "%d: %s\n", __LINE__, #e); \
		exit(1); \
	} \
} while (0)

static void
valid_entries(uint64_t pa)
{
	uint32_t pc = 0;
	uint64_t pd = 0, pt = 0;
	unsigned int combination;

	CHECK(sun60i_a733_gpu_mmu_pce(pa, &pc) == 0);
	CHECK(sun60i_a733_gpu_mmu_pde(pa, &pd) == 0);
	/* Independent literal golden encoding, not the production constants. */
	CHECK(pc == ((pa / 4096) * 16 + 1));
	CHECK(pd == pa + 1);
#ifdef MMU_UPSTREAM_ORACLE
	struct pvr_page_table_l2_entry_raw upc;
	struct pvr_page_table_l1_entry_raw upd;

	pvr_page_table_l2_entry_raw_set(&upc, pa);
	pvr_page_table_l1_entry_raw_set(&upd, pa);
	CHECK(pc == upc.val && pd == upd.val);
#endif
	for (combination = 0; combination < 16; combination++) {
		uint64_t flags = (uint64_t)(combination & 7) * 2 |
		    (uint64_t)(combination >> 3) << 62;

		CHECK(sun60i_a733_gpu_mmu_pte(pa, flags, &pt) == 0);
		CHECK(pt == pa + flags + 1);
#ifdef MMU_UPSTREAM_ORACLE
		struct pvr_page_table_l0_entry_raw upt;
		struct pvr_page_flags_raw uf = pvr_page_flags_raw_create(
		    (combination & 1) != 0, (combination & 2) != 0,
		    (combination & 4) != 0, (combination & 8) != 0);

		pvr_page_table_l0_entry_raw_set(&upt, pa, uf);
		CHECK(pt == upt.val);
#endif
	}
}

static void
valid_indices(uint64_t va)
{
	struct sun60i_a733_gpu_mmu_indices indices;
	uint64_t reconstructed;

	CHECK(sun60i_a733_gpu_mmu_indices(va, &indices) == 0);
	CHECK(indices.pc < 1024 && indices.pd < 512 && indices.pt < 512);
	CHECK(indices.offset < 4096);
	reconstructed = (uint64_t)indices.pc * UINT64_C(1073741824) +
	    (uint64_t)indices.pd * UINT64_C(2097152) +
	    (uint64_t)indices.pt * 4096 + indices.offset;
	CHECK(reconstructed == va);
#ifdef MMU_UPSTREAM_ORACLE
	CHECK(indices.pc == pvr_page_table_l2_idx(va));
	CHECK(indices.pd == pvr_page_table_l1_idx(va));
	CHECK(indices.pt == pvr_page_table_l0_idx(va));
#endif
}

static void
invalid_pa(uint64_t pa)
{
	uint32_t pc = UINT32_C(0xdeadbeef);
	uint64_t pd = UINT64_C(0x0123456789abcdef), pt = pd;

	CHECK(sun60i_a733_gpu_mmu_pce(pa, &pc) == EINVAL);
	CHECK(pc == UINT32_C(0xdeadbeef));
	CHECK(sun60i_a733_gpu_mmu_pde(pa, &pd) == EINVAL);
	CHECK(pd == UINT64_C(0x0123456789abcdef));
	CHECK(sun60i_a733_gpu_mmu_pte(pa, 0, &pt) == EINVAL);
	CHECK(pt == UINT64_C(0x0123456789abcdef));
}

static void
boundaries(void)
{
	static const uint64_t valid_va[] = {
		0, 1, 4095, 4096, 2097151, 2097152, 1073741823,
		1073741824, UINT64_C(0xfffffff000), UINT64_C(0xffffffffff)
	};
	uint64_t out = UINT64_C(0x9876543210fedcba);
	struct sun60i_a733_gpu_mmu_indices indices, saved;
	uint64_t alignment_buffer[4];
	unsigned int bit, offset;

	CHECK(A733_GPU_MMU_PC_ENTRIES * sizeof(uint32_t) == 4096);
	CHECK(A733_GPU_MMU_PD_ENTRIES * sizeof(uint64_t) == 4096);
	CHECK(A733_GPU_MMU_PT_ENTRIES * sizeof(uint64_t) == 4096);
	CHECK(A733_GPU_MMU_PAGE_SIZE == 4096);
	CHECK(A733_GPU_MMU_PTE_READ_ONLY == 2);
	CHECK(A733_GPU_MMU_PTE_COHERENT == 4);
	CHECK(A733_GPU_MMU_PTE_SLC_BYPASS == 8);
	CHECK(A733_GPU_MMU_PTE_PM_FW_PROTECT == UINT64_C(0x4000000000000000));
	valid_entries(0);
	valid_entries(UINT64_C(0x100000000));
	valid_entries(UINT64_C(0xffffff000));
	for (offset = 1; offset < 4096; offset++)
		invalid_pa(UINT64_C(0x100000000) + offset);
	for (bit = 36; bit < 64; bit++) {
		invalid_pa(UINT64_C(1) << bit);
		invalid_pa((UINT64_C(1) << bit) | UINT64_C(0x12345000));
	}
	invalid_pa(UINT64_MAX);
	for (bit = 0; bit < 64; bit++) {
		if (bit == 1 || bit == 2 || bit == 3 || bit == 62)
			continue;
		CHECK(sun60i_a733_gpu_mmu_pte(0, UINT64_C(1) << bit, &out) == EINVAL);
		CHECK(out == UINT64_C(0x9876543210fedcba));
	}
	for (offset = 0; offset < sizeof(valid_va) / sizeof(valid_va[0]); offset++)
		valid_indices(valid_va[offset]);
	memset(&indices, 0xa5, sizeof(indices));
	saved = indices;
	for (bit = 40; bit < 64; bit++) {
		CHECK(sun60i_a733_gpu_mmu_indices(UINT64_C(1) << bit, &indices) == EINVAL);
		CHECK(memcmp(&indices, &saved, sizeof(indices)) == 0);
	}
	CHECK(sun60i_a733_gpu_mmu_indices(UINT64_MAX, &indices) == EINVAL);
	CHECK(memcmp(&indices, &saved, sizeof(indices)) == 0);
	CHECK(sun60i_a733_gpu_mmu_indices(0, NULL) == EINVAL);
	CHECK(sun60i_a733_gpu_mmu_pce(0, NULL) == EINVAL);
	CHECK(sun60i_a733_gpu_mmu_pde(0, NULL) == EINVAL);
	CHECK(sun60i_a733_gpu_mmu_pte(0, 0, NULL) == EINVAL);
	memset(alignment_buffer, 0x3c, sizeof(alignment_buffer));
	CHECK(sun60i_a733_gpu_mmu_indices(0,
	    (void *)((uint8_t *)alignment_buffer + 1)) == EINVAL);
	CHECK(sun60i_a733_gpu_mmu_pce(0,
	    (void *)((uint8_t *)alignment_buffer + 1)) == EINVAL);
	CHECK(sun60i_a733_gpu_mmu_pde(0,
	    (void *)((uint8_t *)alignment_buffer + 1)) == EINVAL);
	CHECK(sun60i_a733_gpu_mmu_pte(0, 0,
	    (void *)((uint8_t *)alignment_buffer + 1)) == EINVAL);
	for (offset = 0; offset < sizeof(alignment_buffer); offset++)
		CHECK(((uint8_t *)alignment_buffer)[offset] == 0x3c);
	CHECK(sun60i_a733_gpu_mmu_indices(0, (void *)(UINTPTR_MAX - 1)) == EINVAL);
}

static void
firmware_profile(const char *path)
{
	struct sun60i_a733_gpu_fwinfo info;
	uint8_t bytes[131072];
	uint64_t value = 0;
	FILE *input = fopen(path, "rb");

	CHECK(input != NULL);
	CHECK(fread(bytes, 1, sizeof(bytes), input) == sizeof(bytes));
	CHECK(fgetc(input) == EOF && !ferror(input));
	CHECK(fclose(input) == 0);
	CHECK(sun60i_a733_gpu_fwinfo_parse(bytes, sizeof(bytes),
	    UINT64_C(0x00240038006800b7), &info) == 0);
	CHECK(sun60i_a733_gpu_fwinfo_feature(&info,
	    A733_GPU_FW_FEATURE_PHYS_BUS_WIDTH, &value) == 0 && value == 36);
	CHECK(sun60i_a733_gpu_fwinfo_feature(&info,
	    A733_GPU_FW_FEATURE_VIRTUAL_ADDRESS_SPACE_BITS, &value) == 0 && value == 40);
	CHECK(info.page_size == 4096);
}

int
main(int argc, char **argv)
{
	uint64_t random = UINT64_C(0xa733123456789abc);
	unsigned int i;

	CHECK(argc == 1 || argc == 2);
	boundaries();
	for (i = 0; i < 4096; i++) {
		random = random * UINT64_C(6364136223846793005) + 1;
		valid_entries((random & UINT64_C(0xffffff)) * 4096);
		valid_indices(random & UINT64_C(0xffffffffff));
	}
	if (argc == 2)
		firmware_profile(argv[1]);
	printf("a733-gpu-mmu-contract: PASS %u checks%s\n", checks,
#ifdef MMU_UPSTREAM_ORACLE
	    " (executed pinned upstream encoders)"
#else
	    " (independent literal oracle)"
#endif
	    );
	return 0;
}
