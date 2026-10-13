/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted source tests. */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sun60i_a733_gpu_mips.h"

static unsigned int checks;
#define CHECK(expr) do { \
	checks++; \
	if (!(expr)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
		exit(1); \
	} \
} while (0)

#define COUNT 4096
static union {
	uint64_t align;
	uint8_t bytes[A733_GPU_MIPS_PT_SIZE + 32];
} storage;
static uint8_t before[sizeof(storage.bytes)];
static uint64_t pages[COUNT], pages_before[COUNT];
static uint64_t arena[COUNT + 8];
static uint8_t *const table = storage.bytes + 8;

static uint32_t
get32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void
put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static void
reset(void)
{
	size_t i;

	memset(storage.bytes, 0xa5, sizeof(storage.bytes));
	memset(table, 0, A733_GPU_MIPS_PT_SIZE);
	for (i = 0; i < COUNT; i++)
		pages[i] = UINT64_C(0x123450000) + i * 4096;
}

static void
fail_map(void *out, size_t size, uint64_t va, const uint64_t *in,
    size_t count, int expected)
{
	memcpy(before, storage.bytes, sizeof(before));
	memcpy(pages_before, pages, sizeof(pages));
	CHECK(sun60i_a733_gpu_mips_map(out, size, va, in, count) == expected);
	CHECK(memcmp(before, storage.bytes, sizeof(before)) == 0);
	CHECK(memcmp(pages_before, pages, sizeof(pages)) == 0);
}

static void
fail_boot(void *out, size_t size, uint64_t stack, uint64_t reg,
    const uint64_t *in, int expected)
{
	memcpy(before, storage.bytes, sizeof(before));
	memcpy(pages_before, pages, sizeof(pages));
	CHECK(sun60i_a733_gpu_mips_boot_data(out, size, stack, reg, in) ==
	    expected);
	CHECK(memcmp(before, storage.bytes, sizeof(before)) == 0);
	CHECK(memcmp(pages_before, pages, sizeof(pages)) == 0);
}

static void
map_tests(void)
{
	static const uint8_t golden[] = {
		0x17, 0x14, 0x8d, 0x04, /* PA 0x123450000. */
		0x57, 0x14, 0x8d, 0x04  /* PA 0x123451000. */
	};
	uint64_t base = A733_GPU_MIPS_HEAP_BASE;
	uint64_t end = A733_GPU_MIPS_HEAP_END;
	size_t i, j;

	reset();
	CHECK(sun60i_a733_gpu_mips_map(table, 16384, base, pages, 2) == 0);
	CHECK(memcmp(table, golden, sizeof(golden)) == 0);
	for (i = 8; i < 16384; i++)
		CHECK(table[i] == 0);
	for (i = 0; i < 8; i++)
		CHECK(storage.bytes[i] == 0xa5);
	for (i = 16392; i < sizeof(storage.bytes); i++)
		CHECK(storage.bytes[i] == 0xa5);
	fail_map(table, 16384, base, pages, 2, EEXIST);
	fail_map(table, 16384, base + 8192, pages, 1, EEXIST);
	/* Last prior entry is considered, and no first entry is committed. */
	reset();
	put32(table + 16380, 0x048d1457);
	fail_map(table, 16384, base, pages, 2, EEXIST);
	reset();
	put32(table + 4, 0x048d1497);
	fail_map(table, 16384, base, pages, 2, EEXIST);

	reset();
	fail_map(NULL, 16384, base, pages, 1, EINVAL);
	fail_map(table, 16384, base, NULL, 1, EINVAL);
	fail_map(table, 16384, base, pages, 0, EINVAL);
	fail_map(table, 16384, base, pages, 4097, EINVAL);
	fail_map(table, 16384, base, pages, SIZE_MAX, EINVAL);
	fail_map(table, 16383, base, pages, 1, EINVAL);
	fail_map(table, 16385, base, pages, 1, EINVAL);
	fail_map(table, SIZE_MAX, base, pages, 1, EINVAL);
	fail_map(table, 16384, base - 4096, pages, 1, EINVAL);
	fail_map(table, 16384, end, pages, 1, EINVAL);
	fail_map(table, 16384, UINT64_MAX, pages, 1, EINVAL);
	fail_map(table, 16384, UINT64_MAX - 4095, pages, 2, EINVAL);
	fail_map(table, 16384, end - 4096, pages, 2, EINVAL);
	for (i = 1; i < 4096; i++) {
		fail_map(table, 16384, base + i, pages, 1, EINVAL);
		pages[1] = UINT64_C(0x123451000) + i;
		fail_map(table, 16384, base, pages, 2, EINVAL);
	}
	for (i = 36; i < 64; i++) {
		pages[1] = UINT64_C(1) << i;
		fail_map(table, 16384, base, pages, 2, EINVAL);
	}
	reset();
	for (i = 0; i < 4; i++) {
		pages[1] = UINT64_C(0x1fc00000) + i * 4096;
		fail_map(table, 16384, base, pages, 2, EOPNOTSUPP);
	}
	pages[1] = pages[0];
	fail_map(table, 16384, base, pages, 2, EEXIST);

	/* Every unexpected flag combination is rejected in a prior entry. */
	for (i = 0; i < 64; i++) {
		if (i == 0x17)
			continue;
		reset();
		put32(table + 16380, 0x10000000 | (uint32_t)i);
		fail_map(table, 16384, base, pages, 1, EINVAL);
	}
	for (i = 30; i <= 31; i++) {
		reset();
		put32(table + 16380, (UINT32_C(1) << i) | 0x17);
		fail_map(table, 16384, base, pages, 1, EINVAL);
	}
	reset();
	put32(table + 16380, 0x007f0017); /* PA 0x1fc00000. */
	fail_map(table, 16384, base, pages, 1, EOPNOTSUPP);
	reset();
	fail_map(table, 16384, base, (const uint64_t *)table, 1, EINVAL);
	fail_map(table, 16384, base, (const uint64_t *)(table - 8), 2,
	    EINVAL);
	fail_map(table, 16384, base, (const uint64_t *)(table + 16376), 2,
	    EINVAL);
	fail_map(table, 16384, base, (const uint64_t *)(table + 1), 1,
	    EINVAL);
	fail_map((void *)(UINTPTR_MAX - 7), 16384, base, pages, 1, EINVAL);
	fail_map(table, 16384, base,
	    (const uint64_t *)(UINTPTR_MAX - 7), 2, EINVAL);

	/* Adjacent input and output spans do not overlap. */
	memset(arena, 0, sizeof(arena));
	arena[0] = 0x1000;
	CHECK(sun60i_a733_gpu_mips_map(arena + 1, 16384, base, arena, 1) == 0);
	CHECK(get32((uint8_t *)(arena + 1)) == 0x57);
	memset(arena, 0, sizeof(arena));
	arena[2048] = 0x2000;
	CHECK(sun60i_a733_gpu_mips_map(arena, 16384, base, arena + 2048,
	    1) == 0);
	CHECK(get32((uint8_t *)arena) == 0x97);

	/* Upper 36-bit boundary, PA zero, and both remap-window boundaries. */
	reset();
	pages[0] = UINT64_C(0xffffff000);
	CHECK(sun60i_a733_gpu_mips_map(table, 16384, end - 4096,
	    pages, 1) == 0);
	CHECK(get32(table + 16380) == 0x3fffffd7);
	pages[0] = 0;
	CHECK(sun60i_a733_gpu_mips_map(table, 16384, base, pages, 1) == 0);
	CHECK(get32(table) == 0x17);
	fail_map(table, 16384, base + 4096, pages, 1, EEXIST);
	pages[0] = 0x1fbff000;
	pages[1] = 0x1fc04000;
	CHECK(sun60i_a733_gpu_mips_map(table, 16384, base + 4096,
	    pages, 2) == 0);

	/* Byte buffer alignment is independent of firmware page alignment. */
	reset();
	memset(table + 1, 0, 16384);
	CHECK(sun60i_a733_gpu_mips_map(table + 1, 16384, base, pages, 2) == 0);
	CHECK(memcmp(table + 1, golden, sizeof(golden)) == 0);
	CHECK(table[0] == 0 && table[16385] == 0xa5);

	/* Last-page rejection and full-heap success exercise all 4096 PTEs. */
	reset();
	pages[4095]++;
	fail_map(table, 16384, base, pages, 4096, EINVAL);
	pages[4095]--;
	CHECK(sun60i_a733_gpu_mips_map(table, 16384, base, pages, 4096) == 0);
	for (i = 0; i < 4096; i++) {
		CHECK(get32(table + i * 4) ==
		    (UINT32_C(0x048d1400) + (uint32_t)i * 64 + 0x17));
	}
	/* Sparse subsequent requests preserve all entries outside their range. */
	reset();
	for (i = 0; i < 32; i++) {
		memcpy(before, storage.bytes, sizeof(before));
		CHECK(sun60i_a733_gpu_mips_map(table, 16384,
		    base + i * 3 * 4096, pages + i, 1) == 0);
		for (j = 0; j < 16384; j++) {
			if (j < i * 12 || j >= i * 12 + 4)
				CHECK(table[j] == before[j + 8]);
		}
	}
}

static void
boot_tests(void)
{
	static const uint8_t golden[64] = {
		0x00, 0xc0, 0xab, 0x89, 0x07, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x45, 0x23, 0x01, 0x00, 0x00, 0x00,
		0x00, 0x10, 0x45, 0x23, 0x01, 0x00, 0x00, 0x00,
		0x00, 0x20, 0x45, 0x23, 0x01, 0x00, 0x00, 0x00,
		0x00, 0x30, 0x45, 0x23, 0x01, 0x00, 0x00, 0x00,
		0x0c, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	uint64_t stack = UINT64_C(0x789abc000);
	uint64_t reg = A733_GPU_MIPS_REGISTER_PA;
	size_t i, j;

	reset();
	memset(storage.bytes, 0xa5, sizeof(storage.bytes));
	CHECK(sun60i_a733_gpu_mips_boot_data(table + 1, 64, stack, reg,
	    pages) == 0);
	CHECK(memcmp(table + 1, golden, 64) == 0);
	CHECK(table[0] == 0xa5 && table[65] == 0xa5);
	fail_boot(NULL, 64, stack, reg, pages, EINVAL);
	fail_boot(table, 64, stack, reg, NULL, EINVAL);
	fail_boot(table, 63, stack, reg, pages, EINVAL);
	fail_boot(table, 65, stack, reg, pages, EINVAL);
	fail_boot(table, SIZE_MAX, stack, reg, pages, EINVAL);
	fail_boot(table, 64, stack, 0, pages, EINVAL);
	fail_boot(table, 64, stack, reg + 4096, pages, EINVAL);
	fail_boot(table, 64, stack, reg | (UINT64_C(1) << 36), pages, EINVAL);
	for (i = 1; i < 4096; i++)
		fail_boot(table, 64, stack + i, reg, pages, EINVAL);
	for (i = 36; i < 64; i++)
		fail_boot(table, 64, UINT64_C(1) << i, reg, pages, EINVAL);
	for (i = 0; i < 4; i++) {
		fail_boot(table, 64, UINT64_C(0x1fc00000) + i * 4096, reg,
		    pages, EOPNOTSUPP);
		fail_boot(table, 64, pages[i], reg, pages, EEXIST);
		for (j = 0; j < i; j++) {
			reset();
			pages[i] = pages[j];
			fail_boot(table, 64, stack, reg, pages, EEXIST);
		}
		reset();
		pages[i]++;
		fail_boot(table, 64, stack, reg, pages, EINVAL);
		pages[i] = UINT64_C(1) << 36;
		fail_boot(table, 64, stack, reg, pages, EINVAL);
		pages[i] = 0x1fc03000;
		fail_boot(table, 64, stack, reg, pages, EOPNOTSUPP);
		reset();
	}
	fail_boot(table, 64, stack, reg, (const uint64_t *)table, EINVAL);
	fail_boot(table, 64, stack, reg, (const uint64_t *)(table - 8), EINVAL);
	fail_boot(table, 64, stack, reg, (const uint64_t *)(table + 56), EINVAL);
	fail_boot(table, 64, stack, reg, (const uint64_t *)(table + 1), EINVAL);
	fail_boot((void *)(UINTPTR_MAX - 7), 64, stack, reg, pages, EINVAL);
	fail_boot(table, 64, stack, reg,
	    (const uint64_t *)(UINTPTR_MAX - 7), EINVAL);
	/* Noncontiguous physical PT pages and the highest page are valid. */
	pages[0] = UINT64_C(0xffffff000);
	pages[2] = 0;
	pages[3] = 0x2000;
	CHECK(sun60i_a733_gpu_mips_boot_data(table, 64, stack, reg, pages) == 0);
	CHECK(get32(table + 16) == 0xfffff000 && get32(table + 20) == 0xf);
	CHECK(get32(table + 32) == 0 && get32(table + 36) == 0);
	CHECK(get32(table + 40) == 0x2000 && get32(table + 44) == 0);
	CHECK(get32(table + 48) == 12 && get32(table + 52) == 4);
	CHECK(get32(table + 56) == 0 && get32(table + 60) == 0);
}

static void
unmap_tests(void)
{
	uint64_t base = A733_GPU_MIPS_HEAP_BASE;
	size_t i;

	reset();
	CHECK(sun60i_a733_gpu_mips_map(table, 16384, base, pages, 4) == 0);
	memcpy(before, storage.bytes, sizeof(before));
	pages[3] += 4096;
	CHECK(sun60i_a733_gpu_mips_unmap(table, 16384, base, pages, 4) == ENOENT);
	CHECK(memcmp(before, storage.bytes, sizeof(before)) == 0);
	pages[3] -= 4096;
	CHECK(sun60i_a733_gpu_mips_unmap(table, 16384, base + 4096, pages + 1, 2) == 0);
	CHECK(get32(table) == 0x048d1417);
	CHECK(get32(table + 4) == 0 && get32(table + 8) == 0);
	CHECK(get32(table + 12) == 0x048d14d7);
	CHECK(sun60i_a733_gpu_mips_unmap(table, 16384, base + 4096, pages + 1, 2) == ENOENT);
	CHECK(sun60i_a733_gpu_mips_map(table, 16384, base + 4096, pages + 1, 2) == 0);
	CHECK(memcmp(before, storage.bytes, sizeof(before)) == 0);
	CHECK(sun60i_a733_gpu_mips_unmap(table, 16384, base, pages, 4) == 0);
	for (i = 0; i < 16384; i++)
		CHECK(table[i] == 0);
	for (i = 0; i < 8; i++)
		CHECK(storage.bytes[i] == 0xa5);
	for (i = 16392; i < sizeof(storage.bytes); i++)
		CHECK(storage.bytes[i] == 0xa5);
	CHECK(sun60i_a733_gpu_mips_map(table, 16384,
	    A733_GPU_MIPS_HEAP_END - 4096, pages, 1) == 0);
	CHECK(sun60i_a733_gpu_mips_unmap(table, 16384,
	    A733_GPU_MIPS_HEAP_END - 4096, pages, 1) == 0);
	CHECK(sun60i_a733_gpu_mips_unmap(NULL, 16384, base, pages, 1) == EINVAL);
	CHECK(sun60i_a733_gpu_mips_unmap(table, 16384, base, NULL, 1) == EINVAL);
	CHECK(sun60i_a733_gpu_mips_unmap(table, 16383, base, pages, 1) == EINVAL);
	CHECK(sun60i_a733_gpu_mips_unmap(table, 16384, base, pages, 0) == EINVAL);
	CHECK(sun60i_a733_gpu_mips_unmap(table, 16384, base, pages, SIZE_MAX) == EINVAL);
	CHECK(sun60i_a733_gpu_mips_unmap(table, 16384, base + 1, pages, 1) == EINVAL);
	CHECK(sun60i_a733_gpu_mips_unmap(table, 16384,
	    A733_GPU_MIPS_HEAP_END - 4096, pages, 2) == EINVAL);
	CHECK(sun60i_a733_gpu_mips_unmap(table, 16384, base,
	    (const uint64_t *)table, 1) == EINVAL);
	CHECK(sun60i_a733_gpu_mips_unmap((void *)(UINTPTR_MAX - 7), 16384,
	    base, pages, 1) == EINVAL);
	CHECK(sun60i_a733_gpu_mips_unmap(table, 16384, base,
	    (const uint64_t *)(UINTPTR_MAX - 7), 2) == EINVAL);
}

int
main(void)
{
	map_tests();
	boot_tests();
	unmap_tests();
	printf("a733-gpu-mips-contract: PASS %u checks\n", checks);
	return 0;
}
