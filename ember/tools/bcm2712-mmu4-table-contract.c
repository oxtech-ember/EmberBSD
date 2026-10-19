/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bcm2712_mmu4_tablevar.h"

static unsigned long checks;
#define CHECK(c) do { \
	checks++; \
	if (!(c)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
		abort(); \
	} \
} while (0)

static struct bcm2712_mmu4_pages pages, saved_pages;
static struct {
	uint64_t before;
	struct bcm2712_mmu4_image image;
	uint64_t after;
} output, saved_output;

static void
pages_init(void)
{
	unsigned int i;

	memset(&pages, 0, sizeof(pages));
	pages.debug = 0x20804774;
	pages.top_pa = 0x01000000;
	pages.l2_pa[0] = 0x01001000;
	pages.l2_pa[1] = 0x01002000;
	pages.default_pa = 0x01003000;
	for (i = 0; i < 2025; i++)
		pages.frame_pa[i] = UINT64_C(0x02000000) + i * 4096U;
}

static void
poison(unsigned int value)
{

	memset(&output, (int)value, sizeof(output));
	output.before = UINT64_C(0x123456789abcdef0);
	output.after = UINT64_C(0xfedcba9876543210);
	memcpy(&saved_output, &output, sizeof(saved_output));
	memcpy(&saved_pages, &pages, sizeof(saved_pages));
}

static void
unchanged(int expected)
{

	poison(0xa5);
	CHECK(bcm2712_mmu4_encode(&pages, &output.image) == expected);
	CHECK(memcmp(&pages, &saved_pages, sizeof(pages)) == 0);
	CHECK(memcmp(&output, &saved_output, sizeof(output)) == 0);
}

static void
word(const uint8_t *bytes, uint32_t expected)
{

	CHECK(bytes[0] == (expected & 255U));
	CHECK(bytes[1] == ((expected / 256U) & 255U));
	CHECK(bytes[2] == ((expected / 65536U) & 255U));
	CHECK(bytes[3] == (expected / 16777216U));
}

static void
sequential(void)
{
	unsigned int i, p;

	for (p = 0; p < 3; p++) {
		pages_init();
		poison(p == 0 ? 0 : p == 1 ? 0x80 : 0xff);
		CHECK(bcm2712_mmu4_encode(&pages, &output.image) == 0);
		CHECK(memcmp(&pages, &saved_pages, sizeof(pages)) == 0);
		CHECK(output.before == saved_output.before);
		CHECK(output.after == saved_output.after);
		word(output.image.top, 0x10001001);
		word(output.image.top + 4, 0x10001002);
		for (i = 8; i < 4096; i++)
			CHECK(output.image.top[i] == 0);
		for (i = 0; i < 1024; i++)
			word(output.image.l2[0] + i * 4, 0x10002000 + i);
		for (i = 0; i < 1001; i++)
			word(output.image.l2[1] + i * 4, 0x10002400 + i);
		for (i = 4004; i < 4096; i++)
			CHECK(output.image.l2[1][i] == 0);
		for (i = 0; i < 4096; i++)
			CHECK(output.image.default_page[i] == 0);
		word(output.image.l2[0] + 4092, 0x100023ff);
		word(output.image.l2[1], 0x10002400);
		word(output.image.l2[1] + 4000, 0x100027e8);
		CHECK(output.image.l2[0][0] == 0x00);
		CHECK(output.image.l2[0][1] == 0x20);
		CHECK(output.image.l2[0][2] == 0x00);
		CHECK(output.image.l2[0][3] == 0x10);
		memcpy(&saved_output, &output, sizeof(saved_output));
		CHECK(bcm2712_mmu4_encode(&pages, &output.image) == 0);
		CHECK(memcmp(&output, &saved_output, sizeof(output)) == 0);
	}
}

static void
scatter(void)
{
	const uint64_t pa[] = { 0, UINT64_C(0xffffff000),
	    UINT64_C(0x800000000), UINT64_C(0x123456000) };
	const uint32_t entry[] = { 0x10000000, 0x10ffffff,
	    0x10800000, 0x10123456 };
	unsigned int i;

	pages_init();
	pages.top_pa = UINT64_C(0xffffff000);
	pages.l2_pa[0] = UINT64_C(0xfffffe000);
	pages.l2_pa[1] = UINT64_C(0xfffffd000);
	pages.default_pa = UINT64_C(0xfffffc000);
	for (i = 0; i < 2025; i++)
		pages.frame_pa[i] = pa[i % 4] == pages.top_pa ?
		    UINT64_C(0x100000000) : pa[i % 4];
	poison(0x55);
	CHECK(bcm2712_mmu4_encode(&pages, &output.image) == 0);
	word(output.image.top, 0x10fffffe);
	word(output.image.top + 4, 0x10fffffd);
	for (i = 0; i < 2025; i++)
		word(output.image.l2[i / 1024] + (i % 1024) * 4,
		    i % 4 == 1 ? 0x10100000 : entry[i % 4]);
	CHECK(memcmp(&pages, &saved_pages, sizeof(pages)) == 0);
	/* Repeated frame PA 0 and the maximum full-span PA are valid. */
	pages_init();
	for (i = 0; i < 2025; i++)
		pages.frame_pa[i] = i % 2 == 0 ? 0 : UINT64_C(0xffffff000);
	CHECK(bcm2712_mmu4_encode(&pages, &output.image) == 0);
	for (i = 0; i < 2025; i++)
		word(output.image.l2[i / 1024] + (i % 1024) * 4,
		    i % 2 == 0 ? 0x10000000 : 0x10ffffff);
}

static void
invalid_debug(void)
{
	unsigned int i, shift;

	for (i = 0; i < 16; i++) {
		pages_init();
		pages.debug = (pages.debug & ~15U) | i;
		if (i != 4)
			unchanged(EOPNOTSUPP);
	}
	for (shift = 4; shift <= 8; shift += 4) {
		for (i = 0; i < 16; i++) {
			pages_init();
			pages.debug = (pages.debug & ~(15U << shift)) |
			    (i << shift);
			if (i < 6)
				unchanged(EOPNOTSUPP);
			else
				CHECK(bcm2712_mmu4_encode(&pages,
				    &output.image) == 0);
		}
	}
	pages_init();
	pages.debug &= ~0x20000000U;
	unchanged(EOPNOTSUPP);
	pages.debug = 0xffffffffU;
	pages.top_pa = 0;
	unchanged(EOPNOTSUPP);
	/* Unused DEBUG fields do not select large or writable leaves. */
	pages_init();
	pages.debug = 0xfffff664;
	CHECK(bcm2712_mmu4_encode(&pages, &output.image) == 0);
	word(output.image.l2[0], 0x10002000);
}

static uint64_t *
control(unsigned int i)
{

	switch (i) {
	case 0: return &pages.top_pa;
	case 1: return &pages.l2_pa[0];
	case 2: return &pages.l2_pa[1];
	default: return &pages.default_pa;
	}
}

static void
invalid_pages(void)
{
	const uint64_t bad[] = { UINT64_C(0x1000000000),
	    UINT64_C(0xfffffffffffff000) };
	unsigned int i, j, k;

	for (i = 0; i < 4; i++) {
		pages_init();
		*control(i) = 0;
		unchanged(EINVAL);
		for (j = 0; j < 12; j++) {
			pages_init();
			*control(i) |= UINT64_C(1) << j;
			unchanged(EINVAL);
		}
		for (j = 0; j < 2; j++) {
			pages_init();
			*control(i) = bad[j];
			unchanged(ERANGE);
		}
		for (j = i + 1; j < 4; j++) {
			pages_init();
			*control(j) = *control(i);
			unchanged(EINVAL);
		}
	}
	for (i = 0; i < 2025; i++) {
		for (j = 0; j < 12; j++) {
			pages_init();
			pages.frame_pa[i] |= UINT64_C(1) << j;
			unchanged(EINVAL);
		}
		for (j = 0; j < 2; j++) {
			pages_init();
			pages.frame_pa[i] = bad[j];
			unchanged(ERANGE);
		}
		for (k = 0; k < 4; k++) {
			pages_init();
			pages.frame_pa[i] = *control(k);
			unchanged(EINVAL);
		}
	}
	/* A late error must preserve even the first output byte. */
	pages_init();
	pages.frame_pa[2024] = UINT64_MAX;
	unchanged(EINVAL);
}

static void
cpu_objects(void)
{
	const struct bcm2712_mmu4_pages *in;
	struct bcm2712_mmu4_image *out;
	unsigned char *arena, *saved;
	size_t size, offset;

	pages_init();
	poison(0xcc);
	CHECK(bcm2712_mmu4_encode(NULL, NULL) == EINVAL);
	CHECK(bcm2712_mmu4_encode(NULL, &output.image) == EINVAL);
	CHECK(memcmp(&output, &saved_output, sizeof(output)) == 0);
	CHECK(bcm2712_mmu4_encode(&pages, NULL) == EINVAL);
	CHECK(bcm2712_mmu4_encode((const void *)(UINTPTR_MAX &
	    ~(uintptr_t)7), &output.image) == EINVAL);
	CHECK(bcm2712_mmu4_encode(&pages, (void *)UINTPTR_MAX) == EINVAL);
	CHECK(memcmp(&output, &saved_output, sizeof(output)) == 0);
	size = sizeof(pages) + sizeof(output.image) + 16;
	arena = malloc(size);
	saved = malloc(size);
	CHECK(arena != NULL && saved != NULL);
	memset(arena, 0x7b, size);
	memcpy(saved, arena, size);
	in = (const void *)arena;
	for (offset = 0; offset < sizeof(pages); offset++) {
		out = (void *)(arena + offset);
		CHECK(bcm2712_mmu4_encode(in, out) == EINVAL);
	}
	out = (void *)arena;
	for (offset = 0; offset < sizeof(output.image); offset += 8) {
		in = (const void *)(arena + offset);
		CHECK(bcm2712_mmu4_encode(in, out) == EINVAL);
	}
	CHECK(memcmp(arena, saved, size) == 0);
	CHECK(bcm2712_mmu4_encode((const void *)(arena + 1),
	    &output.image) == EINVAL);
	CHECK(memcmp(&output, &saved_output, sizeof(output)) == 0);
	/* Exact adjacency is disjoint in either order. */
	memcpy(arena, &pages, sizeof(pages));
	out = (void *)(arena + sizeof(pages));
	CHECK(bcm2712_mmu4_encode((const void *)arena, out) == 0);
	word(out->top, 0x10001001);
	memcpy(arena + sizeof(*out), &pages, sizeof(pages));
	CHECK(bcm2712_mmu4_encode((const void *)(arena + sizeof(*out)),
	    (void *)arena) == 0);
	word(arena, 0x10001001);
	free(saved);
	free(arena);
}

int
main(void)
{

	CHECK(BCM2712_MMU4_PAGE_BYTES == 4096);
	CHECK(BCM2712_MMU4_FRAME_PAGES == 2025);
	CHECK(sizeof(struct bcm2712_mmu4_image) == 16384);
	sequential();
	scatter();
	invalid_debug();
	invalid_pages();
	cpu_objects();
	printf("MMU4 table contract: %lu checks passed\n", checks);
	return 0;
}
