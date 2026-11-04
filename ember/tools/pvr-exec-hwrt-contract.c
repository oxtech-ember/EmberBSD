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
#define HWRT_GE8300_BVNC UINT64_C(0x0016006600360026)
#define HWRT_V1_REGION_HEADER_BYTES 5
#define HWRT_V1_RENDER_MAX 4096

#include "pvr-exec-hwrt-fixture.h"

struct drm_pvr_ioctl_create_hwrt_dataset_args {
	uint32_t width, height, samples, layers, region_header_size;
	struct { uint32_t tpc_size, tpc_stride; } geom_data_args;
};
struct pvr_hwrt_config {
	uint64_t bvnc;
	uint32_t region_header_size, tpc_size, tpc_stride;
};
#define hwrt_tiles production_hwrt_tiles
#define hwrt_tiles_v1 production_hwrt_tiles_v1
#define hwrt_next_pow2 production_hwrt_next_pow2
#include "hwrt-production.inc"
#undef hwrt_tiles
#undef hwrt_tiles_v1
#undef hwrt_next_pow2

static void
hwrt_region_units(void)
{
	struct drm_pvr_ioctl_create_hwrt_dataset_args args = {
		.width = 16, .height = 16, .samples = 1, .layers = 1,
		.region_header_size = 16, .geom_data_args = { .tpc_size = 128 }
	};
	struct pvr_hwrt_config config = { 0 };
	uint64_t span[5] = { 0 };

	CHECK(hwrt_spans(&args, &config, span) == 0);
	CHECK(config.region_header_size == 16);
	CHECK(span[0] == 128);
	args.region_header_size = 128;
	CHECK(hwrt_spans(&args, &config, span) == EINVAL);
	args.width = 128;
	args.height = 128;
	args.samples = 2;
	args.layers = 2;
	args.region_header_size = 32;
	args.geom_data_args.tpc_size = 512;
	args.geom_data_args.tpc_stride = 1;
	CHECK(hwrt_spans(&args, &config, span) == 0);
	CHECK(config.region_header_size == 32 && span[0] == 8192);
	args.region_header_size = 192;
	CHECK(hwrt_spans(&args, &config, span) == EINVAL);
}

/*
 * Parameter format v1 (GE8300): one macrotile covers the render, so region
 * headers and tail pointers count single tiles, each header has 5 bytes,
 * and no macrotile array exists. v2 rounding would change every result.
 */
static uint32_t next_pow2(uint32_t);

static void
expect_v1(uint32_t w, uint32_t h, uint32_t samples, uint32_t *headers,
    uint64_t *rgn, uint32_t *tpc)
{
	uint32_t sx = samples == 4 ? 2 : 1, sy = samples >= 2 ? 2 : 1;
	uint32_t tx = (w + 15) / 16 * sx, ty = (h + 15) / 16 * sy;
	uint32_t mt = next_pow2(tx > ty ? tx : ty);

	*headers = tx * ty;
	*rgn = ((uint64_t)*headers * 5 + 63) & ~63ULL;
	*tpc = mt * mt * 8;
}

static void
hwrt_v1_units(void)
{
	static const struct { uint32_t w, h, s; } table[] = {
		{ 256, 256, 1 }, { 1280, 800, 1 }, { 800, 1280, 1 },
		{ 100, 100, 1 }, { 17, 1, 1 }, { 4096, 4096, 1 },
	};
	struct drm_pvr_ioctl_create_hwrt_dataset_args args = { 0 };
	struct pvr_hwrt_config config = { .bvnc = HWRT_GE8300_BVNC };
	uint64_t span[5], rgn;
	uint32_t headers, tpc;
	size_t i;

	for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		expect_v1(table[i].w, table[i].h, table[i].s, &headers, &rgn,
		    &tpc);
		args.width = table[i].w;
		args.height = table[i].h;
		args.samples = table[i].s;
		args.layers = 1;
		args.region_header_size = headers;
		args.geom_data_args.tpc_size = tpc;
		args.geom_data_args.tpc_stride = 0;
		memset(span, 0xa5, sizeof(span));
		CHECK(hwrt_spans(&args, &config, span) == 0);
		CHECK(config.region_header_size == headers);
		CHECK(span[0] == rgn);
		CHECK(span[1] == ((tpc + 63) & ~63U));
		CHECK(span[4] == 0);
	}
	/* 100x100 has 7x7 tiles: 49 headers, not the v2 4x4 grid of groups. */
	args.width = args.height = 100;
	args.samples = 1;
	args.region_header_size = 49;
	args.geom_data_args.tpc_size = 512;
	CHECK(hwrt_spans(&args, &config, span) == 0 && span[0] == 256);
	args.region_header_size = 64;
	CHECK(hwrt_spans(&args, &config, span) == EINVAL);
	/* The v1 profile has no 8K screen support. */
	args.width = 4097;
	CHECK(hwrt_spans(&args, &config, span) == EOPNOTSUPP);
	/* An empty render is malformed, as on the other profile. */
	args.width = 0;
	CHECK(hwrt_spans(&args, &config, span) == EINVAL);
	/* Multisampled and layered v1 renders lack hardware acceptance. */
	args.width = 100;
	args.samples = 4;
	CHECK(hwrt_spans(&args, &config, span) == EOPNOTSUPP);
	args.samples = 1;
	args.layers = 2;
	CHECK(hwrt_spans(&args, &config, span) == EOPNOTSUPP);
}

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

struct drm_device { int unused; };
struct drm_file { int unused; };
struct pvr_backend { int unused; };
static struct pvr_backend fake_backend;
static uint64_t admission_bvnc = UINT64_C(0x00240038006800b7);
static unsigned admission_calls;
static uint64_t pvr_device_bvnc(struct drm_device *dev)
{ (void)dev; return admission_bvnc; }
static const struct pvr_backend *pvr_device_enter(struct drm_device *dev)
{ (void)dev; return &fake_backend; }
static void pvr_device_leave(struct drm_device *dev) { (void)dev; }
static int pvr_create_hwrt_locked(struct drm_device *dev, void *data,
    struct drm_file *file, const struct pvr_backend *be)
{ (void)dev; (void)data; (void)file; (void)be; admission_calls++; return 0; }
#include "hwrt-admission.inc"

static void
hwrt_profile_admission(void)
{
	struct drm_device dev = {0}; struct drm_file file = {0};
	(void)pvr_device_bvnc;
	CHECK(pvr_create_hwrt_dataset(&dev, NULL, &file) == 0);
	CHECK(admission_calls == 1);
	/* GE8300 geometry is checked by hwrt_spans under the device gate. */
	admission_bvnc = HWRT_GE8300_BVNC;
	CHECK(pvr_create_hwrt_dataset(&dev, NULL, &file) == 0);
	CHECK(admission_calls == 2);
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
	hwrt_profile_admission();
	hwrt_region_units();
	hwrt_v1_units();
	printf("PASS: %d HWRT geometry and admission checks\n", checks);
	return 0;
}
