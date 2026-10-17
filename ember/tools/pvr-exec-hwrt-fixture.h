/* Extracted from pvr_exec_hwrt.c; geometry formulas only, host-side. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>

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
#define HWRT_VHEAP_BYTES 1536
#define HWRT_RTC_BYTES 768
#define HWRT_MTA_BYTES 128

struct hwrt_spans_args { uint32_t width, height, samples, layers; };
struct hwrt_spans_out {
	uint64_t region_span, tpc_span;
	uint32_t config_tpc_size, config_tpc_stride;
};

static void
hwrt_tiles(uint32_t width, uint32_t height, uint32_t samples,
    uint32_t *tiles_x, uint32_t *tiles_y)
{
	uint32_t sx, sy, tx, ty;

	switch (samples) {
	case 2: sx = 1; sy = 2; break;
	case 4: sx = 2; sy = 2; break;
	default: sx = 1; sy = 1; break;
	}
	tx = (width + HWRT_TILE - 1) / HWRT_TILE;
	ty = (height + HWRT_TILE - 1) / HWRT_TILE;
	tx = ((tx + 7) / 8) * 2 * sx;
	ty = ((ty + 7) / 8) * 2 * sy;
	*tiles_x = HWRT_MACROTILES_XY * tx;
	*tiles_y = HWRT_MACROTILES_XY * ty;
}

static uint32_t
hwrt_next_pow2(uint32_t v)
{

	if (v == 0)
		return 1;
	v--;
	v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
	return v + 1;
}

static int
hwrt_spans_raw(const struct hwrt_spans_args *args,
    uint32_t *config_tpc_size, uint32_t *config_tpc_stride, uint64_t *span)
{
	uint32_t tiles_x, tiles_y, mtiles, tpc;

	hwrt_tiles(args->width, args->height, args->samples, &tiles_x,
	    &tiles_y);
	if (tiles_x == 0 || tiles_y == 0)
		return EINVAL;
	span[0] = ((uint64_t)(tiles_x / HWRT_GROUP) *
	    (tiles_y / HWRT_GROUP) * HWRT_REGION_HEADER_BYTES +
	    HWRT_REGION_ALIGN - 1) & ~(uint64_t)(HWRT_REGION_ALIGN - 1);
	if (span[0] == 0 || span[0] > UINT32_MAX)
		return EINVAL;
	*config_tpc_stride = 0;
	if (args->layers > 1) {
		span[0] = ((span[0] + HWRT_REGION_STRIDE_UNIT - 1) &
		    ~(uint64_t)(HWRT_REGION_STRIDE_UNIT - 1)) * args->layers;
		if (span[0] > UINT32_MAX)
			return EINVAL;
	}
	mtiles = hwrt_next_pow2(tiles_x > tiles_y ? tiles_x : tiles_y);
	tpc = (uint64_t)mtiles * mtiles / (HWRT_GROUP * HWRT_GROUP) *
	    HWRT_TAIL_POINTER;
	if (tpc == 0 || tpc > UINT32_MAX)
		return EINVAL;
	*config_tpc_size = tpc;
	if (args->layers > 1) {
		uint64_t layered = ((uint64_t)tpc + HWRT_PM_PAGE_SIZE - 1) &
		    ~(uint64_t)(HWRT_PM_PAGE_SIZE - 1);

		*config_tpc_stride = (uint32_t)(layered / HWRT_PM_PAGE_SIZE);
		layered *= args->layers;
		if (layered > UINT32_MAX)
			return EINVAL;
		span[1] = layered;
	} else {
		span[1] = ((uint64_t)tpc + HWRT_TPC_CACHE_LINE - 1) &
		    ~(uint64_t)(HWRT_TPC_CACHE_LINE - 1);
	}
	span[2] = HWRT_VHEAP_BYTES;
	span[3] = args->layers > 1 ? HWRT_RTC_BYTES : 0;
	span[4] = HWRT_MTA_BYTES;
	return 0;
}

static uint64_t
hwrt_mlist_fixture(uint32_t local_pages, uint32_t global_pages)
{
	uint64_t total, pte, pde, pce;

	total = (uint64_t)local_pages + global_pages;
	pte = (total + 0x200 - 1) / 0x200;
	pde = (pte + 0x200 - 1) / 0x200;
	pce = (pde + 0x400 - 1) / 0x400;
	total = (pte + pde + pce) * 2 * 4;
	return (total + HWRT_PM_PAGE_SIZE - 1) &
	    ~(uint64_t)(HWRT_PM_PAGE_SIZE - 1);
}

static int
hwrt_spans_fixture(uint32_t width, uint32_t height, uint32_t samples,
    uint32_t layers, struct hwrt_spans_out *out)
{
	struct hwrt_spans_args args = { width, height, samples, layers };
	uint64_t span[5] = { 0, 0, 0, 0, 0 };
	int error;

	if (out == NULL)
		return 22;
	error = hwrt_spans_raw(&args, &out->config_tpc_size,
	    &out->config_tpc_stride, span);
	if (error == 0) {
		out->region_span = span[0];
		out->tpc_span = span[1];
	}
	return error;
}
