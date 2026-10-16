/* Origin: EmberBSD; HWRT execution geometry and ownership contract. */
/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Extracts the geometry helpers and exercises them against the exact
 * pinned Mesa formulas, then runs ownership rules as a model. The UAPI
 * mapping itself stays with the kernel build.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); exit(1); } checks++; } while (0)

#define HWRT_TILE 16
#define HWRT_MACROTILES_XY 4
#define HWRT_GROUP 2
#define HWRT_REGION_HEADER_BYTES 6
#define HWRT_REGION_ALIGN 64
#define HWRT_REGION_STRIDE_UNIT 4096
#define HWRT_TAIL_POINTER 8
#define HWRT_TPC_CACHE_LINE 64
#define HWRT_PM_PAGE_SHIFT 12
#define HWRT_PM_PAGE_SIZE (1U << HWRT_PM_PAGE_SHIFT)
#define HWRT_PT_PER_PAGE 0x200
#define HWRT_PD_PER_PAGE 0x200
#define HWRT_PC_PER_PAGE 0x400
#define HWRT_PM_SPACES 2
#define HWRT_MLIST_STRIDE 4

#include "pvr-exec-hwrt-fixture.h"

static uint32_t
next_pow2(uint32_t v)
{
	if (v == 0) return 1;
	v--; v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
	return v + 1;
}

/* Independent recomputation straight from the Mesa formulas. */
static void
expect(uint32_t w, uint32_t h, uint32_t samples, uint32_t layers,
    uint64_t *rgn, uint64_t *tpc_size, uint32_t *tpc_stride)
{
	uint32_t sx, sy, tx, ty, tiles_x, tiles_y, mt;
	uint64_t rgn_base, tpc;

	switch (samples) {
	case 2: sx = 1; sy = 2; break;
	case 4: sx = 2; sy = 2; break;
	default: sx = 1; sy = 1; break;
	}
	tx = (w + 15) / 16; ty = (h + 15) / 16;
	tiles_x = 4 * ((tx + 7) / 8) * 2 * sx;
	tiles_y = 4 * ((ty + 7) / 8) * 2 * sy;
	rgn_base = (uint64_t)(tiles_x / 2) * (tiles_y / 2) * 6;
	rgn_base = (rgn_base + 63) & ~63ULL;
	mt = next_pow2(tiles_x > tiles_y ? tiles_x : tiles_y);
	tpc = (uint64_t)mt * mt / 4 * 8;
	if (layers > 1) {
		*rgn = ((rgn_base + 4095) & ~4095ULL) * layers;
		*tpc_size = ((tpc + 4095) & ~4095ULL) * layers;
		*tpc_stride = (uint32_t)((((tpc + 4095) & ~4095ULL)) / 4096);
	} else {
		*rgn = rgn_base;
		*tpc_size = (tpc + 63) & ~63ULL;
		*tpc_stride = 0;
	}
}

static uint64_t
expect_mlist(uint32_t local, uint32_t global)
{
	uint64_t total, pte, pde, pce, size;

	total = (uint64_t)local + global;
	pte = (total + 511) / 512;
	pde = (pte + 511) / 512;
	pce = (pde + 1023) / 1024;
	size = (pte + pde + pce) * 2 * 4;
	return (size + 4095) & ~4095ULL;
}

int
main(void)
{
	static const struct { uint32_t w, h, s, l; } table[] = {
		{ 1, 1, 1, 1 }, { 16, 16, 1, 1 }, { 17, 1, 1, 1 },
		{ 8192, 8192, 1, 1 }, { 128, 128, 2, 1 }, { 640, 480, 4, 1 },
		{ 1024, 768, 1, 4 }, { 4096, 4096, 4, 8 }, { 256, 256, 2, 2048 },
		{ 33, 77, 3, 1 }, /* unsupported samples collapse to 1x1 factor */
	};
	uint64_t rgn, tpc; uint32_t stride;
	size_t i;

	for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		struct hwrt_spans_out out;

		CHECK(hwrt_spans_fixture(table[i].w, table[i].h, table[i].s,
		    table[i].l, &out) == 0);
		expect(table[i].w, table[i].h,
		    table[i].s == 3 ? 1 : table[i].s, table[i].l, &rgn, &tpc,
		    &stride);
		CHECK(out.region_span == rgn);
		CHECK(out.tpc_span == tpc);
		CHECK(out.config_tpc_size != 0);
	}
	/* Boundary rejections. */
	CHECK(hwrt_spans_fixture(0, 16, 1, 1, NULL) == 22);
	{
		struct hwrt_spans_out out;

		CHECK(hwrt_spans_fixture(0, 16, 1, 1, &out) == 22);
	}
	/* MLIST sizes against the independent formula. */
	CHECK(hwrt_mlist_fixture(0, 0) == 0);
	CHECK(hwrt_mlist_fixture(1, 1) == 4096);
	CHECK(hwrt_mlist_fixture(524288, 524288) ==
	    expect_mlist(524288, 524288));
	CHECK(hwrt_mlist_fixture(1048576, 0) == expect_mlist(1048576, 0));
	printf("PASS: %d HWRT geometry checks\n", checks);
	return 0;
}
