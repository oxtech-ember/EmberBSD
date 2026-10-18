/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; independent MMU byte expectations. */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sun60i_a733_npu_mmu.h"
#include "sun60i_a733_npu_mmu.c"

static unsigned checks;
#define CHECK(c) do { \
	checks++; \
	if (!(c)) { \
		fprintf(stderr, "check %u failed at line %d: %s\n", \
		    checks, __LINE__, #c); \
		exit(1); \
	} \
} while (0)

static uint8_t test_mtlb[1024 + 16], test_stlb[16384 + 16];
static uint8_t saved_mtlb[sizeof(test_mtlb)], saved_stlb[sizeof(test_stlb)];
static struct sun60i_a733_npu_mmu_page test_pages[64];
static struct sun60i_a733_npu_mmu_plan test_plan;

static void
reset(void)
{
	memset(test_mtlb, 0xa5, sizeof(test_mtlb));
	memset(test_stlb, 0x5a, sizeof(test_stlb));
	for (size_t i = 0; i < 64; i++) {
		test_pages[i].physical = 0x12345000 + i * 0x3000;
		test_pages[i].flags = i & 1;
	}
	test_plan = (struct sun60i_a733_npu_mmu_plan){
		.mtlb_physical = 0x100000, .stlb_physical = 0x200000,
		.virtual_base = 0x83001000, .pages = test_pages, .page_count = 3
	};
}

static void
reject(const struct sun60i_a733_npu_mmu_plan *in,
    void *m, size_t mb, void *s, size_t sb)
{
	memcpy(saved_mtlb, test_mtlb, sizeof(test_mtlb));
	memcpy(saved_stlb, test_stlb, sizeof(test_stlb));
	CHECK(sun60i_a733_npu_mmu_build(in, m, mb, s, sb) == EINVAL);
	CHECK(memcmp(saved_mtlb, test_mtlb, sizeof(test_mtlb)) == 0);
	CHECK(memcmp(saved_stlb, test_stlb, sizeof(test_stlb)) == 0);
}

static void
byte_word(const uint8_t *bytes, uint32_t value)
{
	CHECK(bytes[0] == (value & 255));
	CHECK(bytes[1] == ((value >> 8) & 255));
	CHECK(bytes[2] == ((value >> 16) & 255));
	CHECK(bytes[3] == ((value >> 24) & 255));
}

static void
profile(size_t mtlb_bytes, size_t stlb_bytes, size_t max_pages, uint32_t writable)
{
	CHECK(mtlb_bytes == 1024);
	CHECK(stlb_bytes == 16384);
	CHECK(max_pages == 64);
	CHECK(writable == 1);
}

static void
images(void)
{
	const uint32_t bases[] = { 0x1000, 0x83001000, 0xffffc000 };

	profile(A733_NPU_MMU_MTLB_BYTES, A733_NPU_MMU_STLB_BYTES,
	    A733_NPU_MMU_MAX_PAGES, A733_NPU_MMU_WRITABLE);
	for (size_t sample = 0; sample < 3; sample++) {
		reset();
		test_plan.virtual_base = bases[sample];
		test_pages[1].physical = 0xa0bcd000;
		test_pages[2].physical = 0xfffff000;
		CHECK(sun60i_a733_npu_mmu_build(&test_plan, test_mtlb, sizeof(test_mtlb),
		    test_stlb, sizeof(test_stlb)) == 0);
		for (size_t i = 0; i < 256; i++)
			byte_word(test_mtlb + i * 4,
			    i == (bases[sample] >> 24) ? 0x00200001 : 2);
		for (size_t i = 0; i < 4096; i++) {
			uint32_t expected = 2;
			uint32_t first = (bases[sample] >> 12) & 4095;
			if (i == first)
				expected = 0x12345003;
			if (i == first + 1)
				expected = 0xa0bcd007;
			if (i == first + 2)
				expected = 0xfffff003;
			byte_word(test_stlb + i * 4, expected);
		}
		for (size_t i = 1024; i < sizeof(test_mtlb); i++)
			CHECK(test_mtlb[i] == 0xa5);
		for (size_t i = 16384; i < sizeof(test_stlb); i++)
			CHECK(test_stlb[i] == 0x5a);
	}
	reset();
	test_plan.page_count = 64;
	CHECK(sun60i_a733_npu_mmu_build(&test_plan, test_mtlb + 1, 1024,
	    test_stlb + 1, 16384) == 0);
	CHECK(test_mtlb[0] == 0xa5 && test_mtlb[1025] == 0xa5);
	CHECK(test_stlb[0] == 0x5a && test_stlb[16385] == 0x5a);
	for (size_t i = 0; i < 64; i++)
		byte_word(test_stlb + 1 + (i + 1) * 4,
		    0x12345000 + (uint32_t)i * 0x3000 + (i & 1 ? 7 : 3));
}

static void
invalid(void)
{
	reset();
	reject(NULL, test_mtlb, 1024, test_stlb, 16384);
	reject((void *)(UINTPTR_MAX - sizeof(test_plan) + 2),
	    test_mtlb, 1024, test_stlb, 16384);
	reject(&test_plan, NULL, 1024, test_stlb, 16384);
	reject(&test_plan, test_mtlb, 1024, NULL, 16384);
	reject(&test_plan, test_mtlb, 1023, test_stlb, 16384);
	reject(&test_plan, test_mtlb, 1024, test_stlb, 16383);
	reject(&test_plan, test_mtlb, 1024, test_mtlb, 16384);
	reject(&test_plan, test_stlb + 20, 1024, test_stlb, 16384);
	reject(&test_plan, (void *)(UINTPTR_MAX - 100), 1024, test_stlb, 16384);
	reject(&test_plan, test_mtlb, 1024, (void *)(UINTPTR_MAX - 100), 16384);
	test_plan.pages = NULL;
	reject(&test_plan, test_mtlb, 1024, test_stlb, 16384);
	test_plan.pages = (void *)(UINTPTR_MAX - 1);
	reject(&test_plan, test_mtlb, 1024, test_stlb, 16384);
	reset();
	test_plan.pages = (void *)test_mtlb;
	reject(&test_plan, test_mtlb, 1024, test_stlb, 16384);
	test_plan.pages = (void *)(test_stlb + 12);
	reject(&test_plan, test_mtlb, 1024, test_stlb, 16384);
	reset();
	/* A trusted test_plan can still alias output storage; it must not be changed. */
	memcpy(test_mtlb, &test_plan, sizeof(test_plan));
	reject((void *)test_mtlb, test_mtlb, 1024, test_stlb, 16384);
	memcpy(test_stlb + 8, &test_plan, sizeof(test_plan));
	reject((void *)(test_stlb + 8), test_mtlb, 1024, test_stlb, 16384);
	const size_t counts[] = { 0, 65, SIZE_MAX };
	for (size_t i = 0; i < 3; i++) {
		reset(); test_plan.page_count = counts[i];
		reject(&test_plan, test_mtlb, 1024, test_stlb, 16384);
	}
	const uint64_t roots[] = { 1, 0x100001, 0xfffff001,
	    0x100000000, UINT64_MAX };
	for (size_t i = 0; i < 5; i++) {
		reset(); test_plan.mtlb_physical = roots[i];
		reject(&test_plan, test_mtlb, 1024, test_stlb, 16384);
		reset(); test_plan.stlb_physical = roots[i];
		reject(&test_plan, test_mtlb, 1024, test_stlb, 16384);
	}
	reset(); test_plan.stlb_physical = 0xffffd000;
	reject(&test_plan, test_mtlb, 1024, test_stlb, 16384);
	reset(); test_plan.stlb_physical = test_plan.mtlb_physical;
	reject(&test_plan, test_mtlb, 1024, test_stlb, 16384);
	reset(); test_plan.mtlb_physical = test_plan.stlb_physical + 0x3000;
	reject(&test_plan, test_mtlb, 1024, test_stlb, 16384);
	const uint64_t bad_pages[] = { 1, 0x12345001, 0x100000000,
	    UINT64_MAX, 0x100000, 0x200000, 0x203000 };
	for (size_t i = 0; i < 7; i++) {
		reset(); test_pages[2].physical = bad_pages[i];
		reject(&test_plan, test_mtlb, 1024, test_stlb, 16384);
	}
	reset(); test_pages[2].flags = 2;
	reject(&test_plan, test_mtlb, 1024, test_stlb, 16384);
	reset(); test_pages[2].flags = UINT32_MAX;
	reject(&test_plan, test_mtlb, 1024, test_stlb, 16384);
	reset(); test_pages[2].physical = test_pages[0].physical;
	test_pages[2].flags = A733_NPU_MMU_WRITABLE;
	reject(&test_plan, test_mtlb, 1024, test_stlb, 16384);
	const uint32_t bad_va[] = { 0, 1, 0x1001, 0x83000000,
	    0x83ffd000, 0x83ffe000, 0x83fff000, 0xffffe000, UINT32_MAX };
	for (size_t i = 0; i < 9; i++) {
		reset(); test_plan.virtual_base = bad_va[i];
		reject(&test_plan, test_mtlb, 1024, test_stlb, 16384);
	}
	reset(); test_plan.page_count = 1; test_plan.virtual_base = 0xffffe000;
	test_plan.mtlb_physical = 0xfffff000; test_plan.stlb_physical = 0xffffb000;
	CHECK(sun60i_a733_npu_mmu_build(&test_plan, test_mtlb, 1024, test_stlb, 16384) == 0);
	byte_word(test_mtlb + 255 * 4, 0xffffb001);
	byte_word(test_stlb + 4094 * 4, 0x12345003);
}

static void
pd(void)
{
	const uint64_t addresses[] = { 0, 0x1000, 0x12345000, 0xfffff000 };
	const uint32_t expected[] = { 1, 0x11, 0x123451, 0xfffff1 };
	uint32_t word;
	for (size_t i = 0; i < 4; i++) {
		word = 0xdeadbeef;
		CHECK(sun60i_a733_npu_mmu_pd_entry(addresses[i], &word) == 0);
		CHECK(word == expected[i]);
	}
	const uint64_t bad[] = { 1, 0xfff, 0x100000000, UINT64_MAX };
	for (size_t i = 0; i < 4; i++) {
		word = 0xdeadbeef;
		CHECK(sun60i_a733_npu_mmu_pd_entry(bad[i], &word) == EINVAL);
		CHECK(word == 0xdeadbeef);
	}
	CHECK(sun60i_a733_npu_mmu_pd_entry(0x1000, NULL) == EINVAL);
}

int
main(void)
{
	images();
	invalid();
	pd();
	printf("A733 NPU MMU32: %u checks passed\n", checks);
	return 0;
}
