/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. Origin: AI-assisted. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cixvpu_mmu.h"

static unsigned int checks;

#define CHECK(expr) do { \
	checks++; \
	if (!(expr)) { \
		fprintf(stderr, "line %d: %s\n", __LINE__, #expr); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

int
main(void)
{
	uint8_t guarded[CIXVPU_MMU_PAGE_BYTES + 2], saved[sizeof(guarded)];
	uint8_t *table = guarded + 1;
	uint32_t entry, output, current;
	unsigned int i, attr, permission;

	memset(guarded, 0, sizeof(guarded));
	guarded[0] = 0xa5;
	guarded[sizeof(guarded) - 1] = 0x5a;
	CHECK(cixvpu_mmu_encode(UINT64_C(0x1234567000), 2, 3,
	    &entry) == 0);
	CHECK(entry == UINT32_C(0x848d159f));
	CHECK(cixvpu_mmu_map(table, 4096, 0x1000,
	    UINT64_C(0x1234567000), 2, 3) == 0);
	CHECK(memcmp(table + 4, "\x9f\x15\x8d\x84", 4) == 0);
	CHECK(cixvpu_mmu_lookup(table, 4096, 1, &output) == 0);
	CHECK(output == entry);
	CHECK(cixvpu_mmu_remove(table, 4096, 1, entry) == 0);
	CHECK(cixvpu_mmu_remove(table, 4096, 1, entry) == ENOENT);

	/* Exercise both indices across all firmware-address table boundaries. */
	for (i = 0; i < CIXVPU_MMU_ENTRIES; i++) {
		CHECK(cixvpu_mmu_link(table, 4096, (uint32_t)i << 22,
		    UINT64_C(0x100000) + (uint64_t)i * 4096) == 0);
		CHECK(cixvpu_mmu_encode(UINT64_C(0x100000) +
		    (uint64_t)i * 4096, 0, 1, &entry) == 0);
		CHECK(cixvpu_mmu_lookup(table, 4096, i, &output) == 0);
		CHECK(output == entry);
	}
	for (i = 0; i < CIXVPU_MMU_ENTRIES; i++) {
		CHECK(cixvpu_mmu_lookup(table, 4096, i, &entry) == 0);
		memcpy(saved, guarded, sizeof(saved));
		CHECK(cixvpu_mmu_remove(table, 4096, i, entry + 4) == ESTALE);
		CHECK(memcmp(saved, guarded, sizeof(saved)) == 0);
		CHECK(cixvpu_mmu_remove(table, 4096, i, entry) == 0);
		CHECK(cixvpu_mmu_map(table, 4096,
		    UINT32_C(0xffc00000) + (uint32_t)i * 4096,
		    UINT64_C(0xfffffff000), 3, 3) == 0);
		CHECK(cixvpu_mmu_lookup(table, 4096, i, &output) == 0);
		CHECK(output == UINT32_MAX);
		CHECK(cixvpu_mmu_remove(table, 4096, i, output) == 0);
	}
	for (attr = 0; attr < 4; attr++) {
		for (permission = 1; permission <= 3; permission++) {
			CHECK(cixvpu_mmu_encode(0, attr, permission, &entry) == 0);
			CHECK(entry == (attr << 30 | permission));
			CHECK(cixvpu_mmu_map(table, 4096, 0, 0, attr,
			    permission) == 0);
			memcpy(saved, guarded, sizeof(saved));
			CHECK(cixvpu_mmu_map(table, 4096, 0, 4096, attr,
			    permission) == EEXIST);
			CHECK(memcmp(saved, guarded, sizeof(saved)) == 0);
			CHECK(cixvpu_mmu_remove(table, 4096, 0, entry) == 0);
		}
	}
	memcpy(saved, guarded, sizeof(saved));
	output = UINT32_C(0xdeadbeef);
	CHECK(cixvpu_mmu_encode(UINT64_C(1) << 40, 0, 1,
	    &output) == EINVAL);
	CHECK(cixvpu_mmu_encode(1, 0, 1, &output) == EINVAL);
	CHECK(cixvpu_mmu_encode(0, 4, 1, &output) == EINVAL);
	CHECK(cixvpu_mmu_encode(0, 0, 0, &output) == EINVAL);
	CHECK(cixvpu_mmu_encode(0, 0, 4, &output) == EINVAL);
	CHECK(output == UINT32_C(0xdeadbeef));
	CHECK(cixvpu_mmu_encode(0, 0, 1, NULL) == EINVAL);
	CHECK(cixvpu_mmu_map(table, 4096, 1, 4096, 0, 3) == EINVAL);
	CHECK(cixvpu_mmu_map(table, 4096, 0, 4097, 0, 3) == EINVAL);
	CHECK(cixvpu_mmu_map(table, 4096, 0, UINT64_C(1) << 40,
	    0, 3) == EINVAL);
	CHECK(cixvpu_mmu_map(table, 4095, 0, 4096, 0, 3) == EINVAL);
	CHECK(cixvpu_mmu_map(table, 4097, 0, 4096, 0, 3) == EINVAL);
	CHECK(cixvpu_mmu_map(NULL, 4096, 0, 4096, 0, 3) == EINVAL);
	CHECK(cixvpu_mmu_link(table, 4096, 0x1000, 4096) == EINVAL);
	CHECK(cixvpu_mmu_lookup(table, 4096, 1024, &output) == EINVAL);
	CHECK(cixvpu_mmu_lookup(NULL, 4096, 0, &output) == EINVAL);
	CHECK(cixvpu_mmu_lookup(table, 4096, 0, NULL) == EINVAL);
	CHECK(cixvpu_mmu_remove(table, 4096, 0, 0) == EINVAL);
	CHECK(cixvpu_mmu_remove(table, 4096, 0, 4) == EINVAL);
	CHECK(output == UINT32_C(0xdeadbeef));
	CHECK(memcmp(saved, guarded, sizeof(saved)) == 0);

	/* Refuse nonzero entries even when their permission bits are malformed. */
	table[0] = 4;
	memcpy(saved, guarded, sizeof(saved));
	CHECK(cixvpu_mmu_map(table, 4096, 0, 4096, 0, 3) == EEXIST);
	CHECK(memcmp(saved, guarded, sizeof(saved)) == 0);
	CHECK(cixvpu_mmu_lookup(table, 4096, 0, &current) == 0);
	CHECK(current == 4);
	CHECK(guarded[0] == 0xa5 && guarded[sizeof(guarded) - 1] == 0x5a);
	printf("CIX VPU MMU contract: %u checks passed\n", checks);
	return 0;
}
