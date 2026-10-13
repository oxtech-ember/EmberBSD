/* Origin: EmberBSD actual-source V3D bin/render queue contract, 2026-10-10. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

/*
 * Host contract for the bounded BCM2712 V3D first bin/render experiment.
 * It compiles the production probe body and substitutes only host
 * services: the sealed takeover window (HUB and CORE banks), bus_dma and
 * a fake GPU whose control-list engine walks the submitted BCL/RCL
 * packet by packet, validates the pinned V3D 7.1 fields, executes the
 * clear-and-store through the fake MMU (separate CPU/GPU memory views)
 * and latches FLDONE/FRDONE with delivery masked. This cannot establish
 * physical V3D behavior; it checks the probe's packet encodings,
 * submission order, completion rules, fault priority and retention.
 */

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __BIT
#define __BIT(n) (UINT32_C(1) << (n))
#endif
#ifndef __BITS
#define __BITS(hi, lo) (((UINT64_C(1) << ((hi) + 1)) - 1) & \
    ~((UINT64_C(1) << (lo)) - 1))
#endif
#ifndef __SHIFTOUT
#define __SHIFTOUT(x, mask) (((x) & (mask)) / ((mask) & (~(mask) + 1)))
#endif
#ifndef __arraycount
#define __arraycount(a) (sizeof((a)) / sizeof((a)[0]))
#endif

#define PAGE_SIZE 4096
#define KASSERT(v) assert(v)

#define BUS_DMA_WAITOK 0x0
#define BUS_DMASYNC_PREREAD 0x01
#define BUS_DMASYNC_POSTREAD 0x02
#define BUS_DMASYNC_PREWRITE 0x04
#define BUS_DMASYNC_POSTWRITE 0x08

typedef uint64_t bus_addr_t;
typedef size_t bus_size_t;
typedef struct fake_dma_tag *bus_dma_tag_t;
typedef struct device { unsigned int id; } *device_t;

struct fake_segment {
	bus_addr_t ds_addr;
	bus_size_t ds_len;
};
typedef struct fake_segment bus_dma_segment_t;

struct fake_map {
	int dm_nsegs;
	bus_dma_segment_t dm_segs[2];
	int object;
	bool destroyed;
};
typedef struct fake_map *bus_dmamap_t;

struct fake_allocation {
	uint8_t *cpu;		/* what the CPU reads and writes */
	uint8_t *dev;		/* what the GPU reads and writes */
	bus_size_t size;
	bus_addr_t base;	/* the DMA address seen by hardware */
	bus_dmamap_t map;
	bool live;
};

struct fake_dma_tag {
	int unused;
};

/* --- production body ---------------------------------------------- */

static void fake_aprint(device_t, const char *, ...);
static void delay(unsigned int);
static bool fake_takeover_complete;
static unsigned int fail_peek_at, fail_poke_at, mmio_events;
static unsigned int fail_hub_poke_at, hub_poke_events;
static unsigned int fail_core_poke_at, core_poke_events;
static unsigned int allocs, maps, creates, loads;
static unsigned int frees, unmaps, destroys, unloads;
static int current_object;
static bool inside_driver;

static int fake_bus_dmamem_alloc(bus_dma_tag_t, bus_size_t, bus_size_t,
    bus_size_t, bus_dma_segment_t *, int, int *, int);
static int fake_bus_dmamem_map(bus_dma_tag_t, bus_dma_segment_t *, int,
    bus_size_t, void **, int);
static void fake_bus_dmamem_unmap(bus_dma_tag_t, void *, bus_size_t);
static void fake_bus_dmamem_free(bus_dma_tag_t, bus_dma_segment_t *, int);
static int fake_bus_dmamap_create(bus_dma_tag_t, bus_size_t, int,
    bus_size_t, int, int, bus_dmamap_t *);
static void fake_bus_dmamap_destroy(bus_dma_tag_t, bus_dmamap_t);
static int fake_bus_dmamap_load(bus_dma_tag_t, bus_dmamap_t, void *,
    bus_size_t, void *, int);
static void fake_bus_dmamap_unload(bus_dma_tag_t, bus_dmamap_t);
static void fake_bus_dmamap_sync(bus_dma_tag_t, bus_dmamap_t, bus_addr_t,
    bus_size_t, int);

#define bus_dmamem_alloc fake_bus_dmamem_alloc
#define bus_dmamem_map fake_bus_dmamem_map
#define bus_dmamem_unmap fake_bus_dmamem_unmap
#define bus_dmamem_free fake_bus_dmamem_free
#define bus_dmamap_create fake_bus_dmamap_create
#define bus_dmamap_destroy fake_bus_dmamap_destroy
#define bus_dmamap_load fake_bus_dmamap_load
#define bus_dmamap_unload fake_bus_dmamap_unload
#define bus_dmamap_sync fake_bus_dmamap_sync
#define aprint_normal_dev fake_aprint
#define bcmv3d_takeover_hub_peek fake_hub_peek
#define bcmv3d_takeover_hub_poke fake_hub_poke
#define bcmv3d_takeover_core_peek fake_core_peek
#define bcmv3d_takeover_core_poke fake_core_poke
static int fake_hub_peek(bus_size_t, uint32_t *);
static int fake_hub_poke(bus_size_t, uint32_t);
static int fake_core_peek(bus_size_t, uint32_t *);
static int fake_core_poke(bus_size_t, uint32_t);
bool bcmv3d_takeover_complete(void);

#include "queue-body.h"

/* --- register banks ------------------------------------------------ */

enum {
	H_MASK_STS = 0x5c, H_MMU_DEBUG = 0x1238, H_MMUC_CTL = 0x1000,
	H_MMU_CTL = 0x1200, H_PT_BASE = 0x1204, H_ILLEGAL = 0x1230
};
enum {
	C_INT_STS = 0x50, C_INT_CLR = 0x58, C_SLCACTL = 0x24,
	C_L2TCACTL = 0x30, C_CT0QTS = 0x15c, C_CT0QBA = 0x160,
	C_CT1QBA = 0x164, C_CT0QEA = 0x168, C_CT1QEA = 0x16c,
	C_CT0QMA = 0x170, C_CT0QMS = 0x174, C_PTB_BPOS = 0x30c
};

static struct {
	uint32_t mask_sts, mmu_debug, mmuc_ctl, mmu_ctl, pt_base, illegal;
} hub;
static struct {
	uint32_t int_sts, int_clr_count, slcactl, l2tcactl, ct0qts;
	uint32_t ct0qba, ct0qea, ct1qba, ct1qea, ct0qma, ct0qms, ptb_bpos;
} core;

static unsigned int hub_order[16], hub_pokes, core_order[32], core_pokes;
static bool publication_seen, tlb_cleared;
static unsigned int invalidate_rounds;

enum gpu_mode {
	GPU_GOOD,
	GPU_BIN_STALL,		/* the binner never reports done */
	GPU_RENDER_STALL,	/* the render never reports done */
	GPU_BIN_FAULT,		/* an MMU fault replaces bin completion */
	GPU_RENDER_FAULT,	/* an MMU fault replaces render completion */
	GPU_INT_FAILURE,	/* OUTOMEM latches instead of done */
	GPU_WRONG_COLOR,	/* the store writes a different color */
	GPU_PARTIAL_CLEAR,	/* the store writes only half the tile */
	GPU_TOUCH_CANARY,	/* the store also touches the canary page */
	GPU_TOUCH_SCRATCH,	/* the store also touches the scratch */
	GPU_TOUCH_CL,		/* the binner also writes into the BCL */
	GPU_SPILLUSE		/* SPILLUSE latches instead of done */
};

static struct fixture {
	enum gpu_mode mode;
	bool fail_alloc[QV3D_OBJ_COUNT];
	int fail_map, fail_create, fail_load;
	int short_load, split_load, misalign_obj, above4g_obj, overlap_obj;
	unsigned int delays;
	char last_message[512];
	const char *case_name;
} fx;

static struct device dev = { 1 };
static struct fake_dma_tag tag;

static void
fake_aprint(device_t dv, const char *fmt, ...)
{
	va_list ap;

	(void)dv;
	va_start(ap, fmt);
	vsnprintf(fx.last_message, sizeof(fx.last_message), fmt, ap);
	va_end(ap);
}

static void
delay(unsigned int us)
{
	(void)us;
	fx.delays++;
}

bool
bcmv3d_takeover_complete(void);
bool
bcmv3d_takeover_complete(void)
{

	return fake_takeover_complete;
}

/* --- fake bus_dma (CPU/GPU views as in the DMA contract) ---------- */

static struct fake_allocation allocations[QV3D_OBJ_COUNT];
static bus_addr_t default_bases[QV3D_OBJ_COUNT] = {
	0x10000000, 0x10400000, 0x10401000, 0x10404000, 0x10405000,
	0x10406000, 0x10408000
};

static struct fake_allocation *
allocation_by_seg(const bus_dma_segment_t *seg)
{
	int i;

	for (i = 0; i < QV3D_OBJ_COUNT; i++)
		if (seg == (bus_dma_segment_t *)&qv3d.obj[i].seg)
			return &allocations[i];
	return NULL;
}

static struct fake_allocation *
allocation_containing(bus_addr_t addr)
{
	int i;

	for (i = 0; i < QV3D_OBJ_COUNT; i++)
		if (allocations[i].live && addr >= allocations[i].base &&
		    addr < allocations[i].base + allocations[i].size)
			return &allocations[i];
	return NULL;
}

static int
fake_bus_dmamem_alloc(bus_dma_tag_t t, bus_size_t size, bus_size_t align,
    bus_size_t boundary, bus_dma_segment_t *segs, int nsegs, int *rsegs,
    int flags)
{
	struct fake_allocation *allocation = allocation_by_seg(segs);

	(void)t;
	(void)align;
	(void)boundary;
	(void)nsegs;
	(void)flags;
	assert(inside_driver);
	assert(allocation != NULL);
	current_object = (int)(allocation - allocations);
	if (fx.fail_alloc[current_object]) {
		*rsegs = 0;
		return ENOMEM;
	}
	memset(allocation, 0, sizeof(*allocation));
	allocation->cpu = calloc(1, size);
	allocation->dev = calloc(1, size);
	allocation->size = size;
	allocation->base = default_bases[current_object];
	if (fx.overlap_obj == current_object)
		allocation->base = default_bases[QV3D_OBJ_OUTPUT] + PAGE_SIZE;
	if (fx.above4g_obj == current_object)
		allocation->base = 0xffffe000;
	if (fx.misalign_obj == current_object)
		allocation->base += 0x123;
	allocation->live = true;
	segs->ds_addr = allocation->base;
	segs->ds_len = size;
	*rsegs = 1;
	allocs++;
	return 0;
}

static int
fake_bus_dmamem_map(bus_dma_tag_t t, bus_dma_segment_t *segs, int nsegs,
    bus_size_t size, void **kvap, int flags)
{
	struct fake_allocation *allocation = allocation_by_seg(segs);

	(void)t;
	(void)nsegs;
	(void)size;
	(void)flags;
	assert(allocation != NULL && allocation->live);
	if (fx.fail_map == (int)(allocation - allocations))
		return ENOMEM;
	*kvap = allocation->cpu;
	maps++;
	return 0;
}

static void
fake_bus_dmamem_unmap(bus_dma_tag_t t, void *kva, bus_size_t size)
{
	int i;

	(void)t;
	(void)size;
	assert(!qv3d.published);
	for (i = 0; i < QV3D_OBJ_COUNT; i++)
		if (allocations[i].live && allocations[i].cpu == kva)
			allocations[i].map = NULL;
	unmaps++;
}

static void
fake_bus_dmamem_free(bus_dma_tag_t t, bus_dma_segment_t *segs, int nsegs)
{
	struct fake_allocation *allocation = allocation_by_seg(segs);

	(void)t;
	(void)nsegs;
	assert(!qv3d.published);
	assert(allocation != NULL);
	free(allocation->cpu);
	free(allocation->dev);
	memset(allocation, 0, sizeof(*allocation));
	frees++;
}

static int
fake_bus_dmamap_create(bus_dma_tag_t t, bus_size_t size, int nsegs,
    bus_size_t maxsegsz, int boundary, int flags, bus_dmamap_t *map)
{
	struct fake_map *m;

	(void)t;
	(void)size;
	(void)nsegs;
	(void)maxsegsz;
	(void)boundary;
	(void)flags;
	assert(inside_driver);
	if (fx.fail_create == current_object)
		return ENOMEM;
	m = calloc(1, sizeof(*m));
	m->object = current_object;
	*map = m;
	allocations[current_object].map = m;
	creates++;
	return 0;
}

static void
fake_bus_dmamap_destroy(bus_dma_tag_t t, bus_dmamap_t map)
{
	(void)t;
	assert(!qv3d.published);
	assert(map != NULL && !map->destroyed);
	map->destroyed = true;
	if (allocations[map->object].map == map)
		allocations[map->object].map = NULL;
	free(map);
	destroys++;
}

static int
fake_bus_dmamap_load(bus_dma_tag_t t, bus_dmamap_t map, void *kva,
    bus_size_t size, void *proc, int flags)
{
	struct fake_allocation *allocation = NULL;
	int i;

	(void)t;
	(void)proc;
	(void)flags;
	for (i = 0; i < QV3D_OBJ_COUNT; i++)
		if (allocations[i].live && allocations[i].cpu == kva)
			allocation = &allocations[i];
	assert(allocation != NULL);
	if (fx.fail_load == (int)(allocation - allocations))
		return ENOMEM;
	loads++;
	map->object = (int)(allocation - allocations);
	map->dm_nsegs = fx.split_load == map->object ? 2 : 1;
	map->dm_segs[0].ds_addr = allocation->base;
	map->dm_segs[0].ds_len =
	    fx.short_load == map->object ? size - PAGE_SIZE : size;
	if (map->dm_nsegs == 2) {
		map->dm_segs[1].ds_addr = allocation->base + size / 2;
		map->dm_segs[1].ds_len = size / 2;
	}
	return 0;
}

static void
fake_bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t map)
{
	(void)t;
	assert(!qv3d.published);
	unloads++;
}

static void
fake_bus_dmamap_sync(bus_dma_tag_t t, bus_dmamap_t map, bus_addr_t offset,
    bus_size_t length, int ops)
{
	struct fake_allocation *allocation = &allocations[map->object];

	(void)t;
	if ((ops & BUS_DMASYNC_PREWRITE) != 0)
		memcpy(allocation->dev + offset, allocation->cpu + offset, length);
	if ((ops & BUS_DMASYNC_POSTREAD) != 0)
		memcpy(allocation->cpu + offset, allocation->dev + offset, length);
}

/* --- fake MMU ------------------------------------------------------ */

static bool
fake_translate(bus_addr_t va, bus_addr_t *pa)
{
	struct fake_allocation *pt = allocation_containing(
	    (bus_addr_t)hub.pt_base << QV3D_PAGE_SHIFT);
	uint32_t pte;

	if (!tlb_cleared)
		return false;
	assert(pt != NULL && pt->dev != NULL);
	if ((va >> QV3D_PAGE_SHIFT) >= pt->size / 4)
		return false;
	memcpy(&pte, pt->dev + (va >> QV3D_PAGE_SHIFT) * 4, sizeof(pte));
	if ((pte & QV3D_PTE_VALID) == 0)
		return false;
	*pa = ((bus_addr_t)(pte & 0x00ffffff) << QV3D_PAGE_SHIFT) |
	    (va & (PAGE_SIZE - 1));
	return true;
}

/* --- fake control-list engine -------------------------------------- */

static uint64_t
cl_get(const uint8_t *stream, unsigned int payload_bit, unsigned int size)
{
	uint64_t value = 0;
	unsigned int i;

	for (i = 0; i < size; i++)
		if ((stream[1 + (payload_bit + i) / 8] >>
		    ((payload_bit + i) % 8)) & 1)
			value |= UINT64_C(1) << i;
	return value;
}

/* The fixture's own expectation for the BCL stream. */
static bool
fake_gpu_check_bcl(const uint8_t *stream, size_t length)
{
	size_t at = 0;
	unsigned int o;

	if (length != 9 + 1 + 5 + 1 + 1)
		return false;
	if (stream[at] != 120)
		return false;
	o = 0;
	if (cl_get(stream + at, o + 32, 16) != QV3D_DIM - 1 ||
	    cl_get(stream + at, o + 48, 16) != QV3D_DIM - 1 ||
	    cl_get(stream + at, o + 8, 3) != 3 ||
	    cl_get(stream + at, o + 11, 3) != 3 ||
	    cl_get(stream + at, o + 4, 2) != 1 ||
	    cl_get(stream + at, o + 2, 2) != 1)
		return false;
	at += 9;
	if (stream[at++] != 19)
		return false;
	if (stream[at++] != 92 || cl_get(stream + at - 1, 0, 32) != 0)
		return false;
	at += 4;
	if (stream[at++] != 6)
		return false;
	if (stream[at++] != 4)
		return false;
	return at == length;
}

/* The fixture's own independent RCL encoder: every packet of the pinned
 * stream, byte for byte, with local constants only. Any deviation in the
 * production packing fails the following comparison. */
static size_t
fixture_expected_rcl(uint8_t *out, uint32_t rcl_va)
{
	size_t at = 0;
	unsigned int o, generic_start, generic_end;

	memset(out, 0, QV3D_RCL_SIZE);
#define P(op, payload) do { \
	out[at] = (op); o = (at + 1) * 8; \
	memset(out + at + 1, 0, (payload)); at += 1 + (payload); \
} while (0)
#define F(start, size, value) do { \
	unsigned int _b = o + (start), _v = 0; _v = (unsigned)(value); \
	for (unsigned int _i = 0; _i < (size); _i++) \
		if ((_v >> _i) & 1) \
			out[(_b + _i) / 8] |= 1 << ((_b + _i) % 8); \
} while (0)
	P(121, 8);		/* COMMON */
	F(0, 3, 0); F(4, 4, 0); F(8, 16, 64); F(24, 16, 64);
	F(44, 1, 1); F(46, 1, 1); F(52, 3, 3); F(55, 3, 3);
	P(121, 8);		/* RENDER TARGET PART1 */
	F(0, 3, 2); F(3, 3, 0); F(7, 11, 0); F(18, 7, 31);
	F(25, 2, 0); F(27, 5, 8); F(32, 32, 0x305e7b4c);
	P(121, 8);		/* ZS CLEAR VALUES */
	F(0, 4, 1);
	P(126, 1);		/* TILE LIST INITIAL BLOCK SIZE */
	F(0, 2, 1); F(2, 1, 1);
	P(123, 5 - 1);		/* MULTICORE TILE LIST SET BASE */
	F(0, 4, 0); F(6, 26, default_bases[QV3D_OBJ_TILE_ALLOC] >> 6);
	P(122, 8);		/* MULTICORE SUPERTILE CFG */
	F(0, 8, 0); F(8, 8, 0); F(16, 8, 1); F(24, 8, 1);
	F(32, 12, 1); F(44, 12, 1); F(61, 3, 0);
	for (unsigned int pass = 0; pass < 2; pass++) {
		P(124, 3);	/* TILE COORDINATES */
		F(0, 12, 0); F(12, 12, 0);
		P(26, 0);	/* END OF LOADS */
		P(29, 12);	/* STORE NONE */
		F(0, 4, 8);
		P(25, 0);	/* CLEAR RENDER TARGETS */
		P(27, 0);	/* END OF TILE MARKER */
	}
	P(19, 0);		/* FLUSH VCD CACHE */
	generic_start = at;
	P(125, 0);		/* TILE COORDINATES IMPLICIT */
	P(26, 0);		/* END OF LOADS */
	P(56, 1);		/* PRIM LIST FORMAT: list triangles */
	F(0, 6, 2);
	P(54, 4);		/* SET INSTANCEID zero */
	F(0, 32, 0);
	P(21, 1);		/* BRANCH TO IMPLICIT TILE LIST */
	F(0, 8, 0);
	P(29, 12);		/* STORE RT0 raster rgba8 */
	F(0, 4, 0); F(4, 3, 0); F(12, 6, 27);
	F(28, 20, 256); F(48, 16, 64);
	F(64, 32, default_bases[QV3D_OBJ_OUTPUT]);
	P(27, 0);		/* END OF TILE MARKER */
	P(18, 0);		/* RETURN FROM SUB LIST */
	generic_end = at;
	P(20, 8);		/* START ADDRESS OF GENERIC TILE LIST */
	F(0, 32, rcl_va + generic_start);
	F(32, 32, rcl_va + generic_end);
	P(23, 2);		/* SUPERTILE COORDINATES, 8-bit fields */
	F(0, 8, 0); F(8, 8, 0);
	P(13, 0);		/* END OF RENDERING */
#undef P
#undef F
	return at;
}

/*
 * Validate the RCL byte for byte against the independent expectation
 * and perform the store. Any deviation leaves the tile unstored and
 * no done bit latches.
 */
static bool
fake_gpu_run_rcl(const uint8_t *stream, size_t length)
{
	static uint8_t expected[QV3D_RCL_SIZE];
	size_t expected_length;
	bus_addr_t pa;
	struct fake_allocation *out;
	size_t words = QV3D_OUTPUT_DATA / 4;
	size_t limit = fx.mode == GPU_PARTIAL_CLEAR ? words / 2 : words;
	int i;

	expected_length = fixture_expected_rcl(expected,
	    default_bases[QV3D_OBJ_RCL]);
	if (length != expected_length ||
	    memcmp(stream, expected, length) != 0)
		return false;

	/* Perform the store through the fake MMU into the output image. */
	if (!fake_translate(default_bases[QV3D_OBJ_OUTPUT], &pa))
		return false;
	out = allocation_containing(pa & ~(bus_addr_t)(PAGE_SIZE - 1));
	if (out == NULL || out->base != default_bases[QV3D_OBJ_OUTPUT])
		return false;
	for (i = 0; i < (int)limit; i++)
		memcpy(out->dev + (pa - out->base) + i * 4,
		    &(uint32_t){ (uint32_t)(fx.mode == GPU_WRONG_COLOR ?
		    0xdeadbeef : 0x305e7b4c) }, 4);
	if (fx.mode == GPU_TOUCH_CANARY)
		out->dev[QV3D_OUTPUT_DATA] = 1;
	if (fx.mode == GPU_TOUCH_SCRATCH) {
		struct fake_allocation *scratch =
		    allocation_containing(
		    (bus_addr_t)(hub.illegal &
		    ~QV3D_MMU_ILLEGAL_ENABLE) << QV3D_PAGE_SHIFT);

		assert(scratch != NULL);
		scratch->dev[0] = 1;
	}
	return true;
}

static bool
caches_invalidated_for_round(void)
{

	return invalidate_rounds > 0;
}

/* Byte offset inside the BCL payload used by GPU_TOUCH_CL. */
static size_t
qv3d_cl_len_probe(void)
{

	return 5;	/* inside the binning-config payload */
}

/* --- sealed HUB/CORE windows --------------------------------------- */

static int
fake_hub_peek(bus_size_t offset, uint32_t *value)
{

	if (fail_peek_at != 0 && ++mmio_events == fail_peek_at)
		return EIO;
	switch (offset) {
	case H_MASK_STS:
		*value = hub.mask_sts;
		break;
	case H_MMU_DEBUG:
		*value = hub.mmu_debug;
		break;
	case H_MMUC_CTL:
		*value = hub.mmuc_ctl;
		break;
	case H_MMU_CTL:
		*value = hub.mmu_ctl;
		break;
	case H_PT_BASE:
		*value = hub.pt_base;
		break;
	case H_ILLEGAL:
		*value = hub.illegal;
		break;
	default:
		assert(false);
		break;
	}
	return 0;
}

static int
fake_hub_poke(bus_size_t offset, uint32_t value)
{

	if (fail_hub_poke_at != 0 && ++hub_poke_events == fail_hub_poke_at)
		return EIO;
	assert(hub_pokes < __arraycount(hub_order));
	hub_order[hub_pokes++] = (unsigned int)offset;
	switch (offset) {
	case H_PT_BASE:
		publication_seen = true;
		hub.pt_base = value;
		break;
	case H_MMU_CTL:
		/* Full-value writes only; fault bits are never reflected. */
		assert((value & ~(QV3D_MMU_CTL_TLB_CLEAR)) ==
		    QV3D_MMU_CTL_VALUE);
		assert((value & QV3D_MMU_CTL_FAULTS) == 0);
		hub.mmu_ctl = value & ~QV3D_MMU_CTL_TLB_CLEAR;
		if ((value & QV3D_MMU_CTL_TLB_CLEAR) != 0)
			tlb_cleared = true;
		break;
	case H_ILLEGAL:
		hub.illegal = value;
		break;
	case H_MMUC_CTL:
		assert(value == 1 || value == 3);
		hub.mmuc_ctl = value & 3;
		break;
	default:
		assert(false);
		break;
	}
	return 0;
}

static int
fake_core_peek(bus_size_t offset, uint32_t *value)
{

	switch (offset) {
	case C_INT_STS:
		*value = core.int_sts;
		break;
	default:
		assert(false);
		break;
	}
	return 0;
}

static int
fake_core_poke(bus_size_t offset, uint32_t value)
{

	if (fail_core_poke_at != 0 && ++core_poke_events == fail_core_poke_at)
		return EIO;
	assert(core_pokes < __arraycount(core_order));
	core_order[core_pokes++] = (unsigned int)offset;
	switch (offset) {
	case C_PTB_BPOS:
		assert(value == 0);
		core.ptb_bpos = value;
		break;
	case C_L2TCACTL:
		assert(value == QV3D_L2TCACTL_FLUSH);
		core.l2tcactl = value;
		invalidate_rounds++;
		break;
	case C_SLCACTL:
		assert(value == 0x0f0f0f0f);
		core.slcactl = value;
		break;
	case C_CT0QMA:
		core.ct0qma = value;
		break;
	case C_CT0QMS:
		core.ct0qms = value;
		break;
	case C_CT0QTS:
		core.ct0qts = value;
		break;
	case C_CT0QBA:
		core.ct0qba = value;
		break;
	case C_CT0QEA: {
		struct fake_allocation *bcl;
		bool ok;

		/* The binner starts; caches must have been invalidated. */
		assert(publication_seen);
		if (!caches_invalidated_for_round())
			return 0;	/* stalled hardware */
		bcl = allocation_containing(core.ct0qba);
		ok = bcl != NULL &&
		    fake_gpu_check_bcl(bcl->dev + (core.ct0qba - bcl->base),
		    value - core.ct0qba);
		core.ct0qea = value;
		if (!ok)
			return 0;
		if (fx.mode == GPU_BIN_STALL)
			return 0;
		if (fx.mode == GPU_BIN_FAULT) {
			hub.mmu_ctl |= __BIT(20);
			return 0;
		}
		if (fx.mode == GPU_INT_FAILURE) {
			core.int_sts |= __BIT(2);	/* OUTOMEM */
			return 0;
		}
		if (fx.mode == GPU_TOUCH_CL)
			bcl->dev[qv3d_cl_len_probe()] ^= 0xff;
		/* Tile-list evidence: the binner writes into tile_alloc. */
		{
			struct fake_allocation *tile_alloc =
			    allocation_containing(core.ct0qma);

			assert(tile_alloc != NULL);
			tile_alloc->dev[0] = 1;
		}
		core.int_sts |= __BIT(1);	/* FLDONE */
		break;
	}
	case C_INT_CLR:
		/* Only FLDONE is cleared, exactly once, after bin done. */
		assert(value == QV3D_INT_FLDONE);
		core.int_clr_count++;
		core.int_sts &= ~value;
		break;
	case C_CT1QBA:
		core.ct1qba = value;
		break;
	case C_CT1QEA: {
		struct fake_allocation *rcl;
		bool ok;

		/* A new invalidation round must precede the render. */
		assert(invalidate_rounds >= 2);
		rcl = allocation_containing(core.ct1qba);
		ok = rcl != NULL &&
		    fake_gpu_run_rcl(rcl->dev + (core.ct1qba - rcl->base),
		    value - core.ct1qba);
		core.ct1qea = value;
		if (!ok)
			return 0;
		if (fx.mode == GPU_RENDER_STALL)
			return 0;
		if (fx.mode == GPU_RENDER_FAULT) {
			hub.mmu_ctl |= __BIT(20);
			return 0;
		}
		if (fx.mode == GPU_INT_FAILURE) {
			core.int_sts |= __BIT(2);	/* OUTOMEM */
			return 0;
		}
		if (fx.mode == GPU_SPILLUSE) {
			core.int_sts |= __BIT(3);	/* SPILLUSE */
			return 0;
		}
		core.int_sts |= __BIT(0);	/* FRDONE */
		break;
	}
	default:
		assert(false);
		break;
	}
	return 0;
}

/* --- cases ---------------------------------------------------------- */

static unsigned int cases, checks;

#define CHECK(cond) do {						\
	checks++;							\
	if (!(cond)) {							\
		printf("FAIL case '%s' line %d: %s (last: %s)\n",	\
		    fx.case_name, __LINE__, #cond, fx.last_message);	\
		exit(1);						\
	}								\
} while (0)

static void
reset_fixture(void)
{
	int i;

	if (qv3d.published)
		for (i = 0; i < QV3D_OBJ_COUNT; i++)
			if (allocations[i].live) {
				free(allocations[i].cpu);
				free(allocations[i].dev);
				if (allocations[i].map != NULL &&
				    !allocations[i].map->destroyed)
					free(allocations[i].map);
			}
	memset(&qv3d, 0, sizeof(qv3d));
	memset(allocations, 0, sizeof(allocations));
	memset(&hub, 0, sizeof(hub));
	memset(&core, 0, sizeof(core));
	memset(&fx, 0, sizeof(fx));
	for (i = 0; i < QV3D_OBJ_COUNT; i++)
		fx.fail_alloc[i] = false;
	fx.fail_map = fx.fail_create = fx.fail_load = -1;
	fx.short_load = fx.split_load = fx.misalign_obj = -1;
	fx.above4g_obj = fx.overlap_obj = -1;
	hub.mask_sts = 0x0000007f;
	hub.mmu_debug = 0x20804664;
	fake_takeover_complete = true;
	hub_pokes = core_pokes = 0;
	mmio_events = 0;
	fail_peek_at = fail_poke_at = 0;
	fail_hub_poke_at = fail_core_poke_at = 0;
	hub_poke_events = core_poke_events = 0;
	publication_seen = false;
	tlb_cleared = false;
	invalidate_rounds = 0;
	allocs = maps = creates = loads = 0;
	frees = unmaps = destroys = unloads = 0;
	inside_driver = false;
}

static int
run_case(const char *name, int expected)
{
	int error;

	fx.case_name = name;
	cases++;
	inside_driver = true;
	error = bcmv3d_queue_probe(&dev, &tag);
	inside_driver = false;
	CHECK(error == expected);
	return error;
}

static void
assert_retained(void)
{
	int i;

	CHECK(qv3d.published);
	CHECK(frees == 0 && unmaps == 0 && destroys == 0 && unloads == 0);
	for (i = 0; i < QV3D_OBJ_COUNT; i++)
		CHECK(qv3d.obj[i].allocated && allocations[i].live);
}

static void
assert_released(void)
{

	CHECK(!qv3d.published);
	CHECK(frees == allocs && unmaps == maps && destroys == creates &&
	    unloads == loads);
}

int
main(void)
{
	int i;

	/* Without a completed takeover there is no queue experiment. */
	reset_fixture();
	fake_takeover_complete = false;
	run_case("no completed takeover", EPERM);

	/* The pinned happy path, once per boot. */
	reset_fixture();
	run_case("clear and store one tile", 0);
	run_case("second attempt", EBUSY);
	CHECK(strstr(fx.last_message, "bin/render PASS") != NULL);
	assert_retained();
	/* The pinned hub publication order. */
	CHECK(hub_pokes == 6);
	CHECK(hub_order[0] == H_PT_BASE);
	/* The pinned core submission order. */
	CHECK(core_pokes == 13);
	CHECK(core_order[0] == C_PTB_BPOS);
	CHECK(core_order[1] == C_L2TCACTL);
	CHECK(core_order[2] == C_SLCACTL);
	CHECK(core_order[3] == C_CT0QMA);
	CHECK(core_order[4] == C_CT0QMS);
	CHECK(core_order[5] == C_CT0QTS);
	CHECK(core_order[6] == C_CT0QBA);
	CHECK(core_order[7] == C_CT0QEA);
	CHECK(core_order[8] == C_INT_CLR);
	CHECK(core_order[9] == C_L2TCACTL);
	CHECK(core_order[10] == C_SLCACTL);
	CHECK(core_order[11] == C_CT1QBA);
	CHECK(core_order[12] == C_CT1QEA);
	/* The binner really ran: tile_alloc carries its output. */
	CHECK(allocations[QV3D_OBJ_TILE_ALLOC].dev[0] == 1);
	CHECK(core.int_clr_count == 1);

	/* Both done waits are bounded. */
	reset_fixture();
	fx.mode = GPU_BIN_STALL;
	run_case("binner timeout", ETIMEDOUT);
	assert_retained();
	CHECK(fx.delays > 0);

	reset_fixture();
	fx.mode = GPU_RENDER_STALL;
	run_case("render timeout", ETIMEDOUT);
	assert_retained();

	/* MMU faults win over completion on both queues. */
	reset_fixture();
	fx.mode = GPU_BIN_FAULT;
	run_case("binner MMU fault", EFAULT);
	assert_retained();

	reset_fixture();
	fx.mode = GPU_RENDER_FAULT;
	run_case("render MMU fault", EFAULT);
	assert_retained();

	/* Latched OUTOMEM is a failure, never completion. */
	reset_fixture();
	fx.mode = GPU_INT_FAILURE;
	run_case("OUTOMEM latched", EIO);
	CHECK(strstr(fx.last_message, "latched failure") != NULL);
	assert_retained();

	/* Verification catches wrong, partial and out-of-bounds stores. */
	reset_fixture();
	fx.mode = GPU_WRONG_COLOR;
	run_case("wrong clear color", EIO);
	CHECK(strstr(fx.last_message, "output word mismatch") != NULL);
	assert_retained();

	reset_fixture();
	fx.mode = GPU_PARTIAL_CLEAR;
	run_case("partial clear", EIO);
	assert_retained();

	reset_fixture();
	fx.mode = GPU_TOUCH_CANARY;
	run_case("canary touched", EIO);
	CHECK(strstr(fx.last_message, "canary") != NULL);
	assert_retained();

	reset_fixture();
	fx.mode = GPU_TOUCH_SCRATCH;
	run_case("scratch touched", EIO);
	CHECK(strstr(fx.last_message, "scratch") != NULL);
	assert_retained();

	/* Unsuitable geometry and lost masks. */
	reset_fixture();
	hub.mmu_debug = 0x20804644;
	run_case("narrow VA", EOPNOTSUPP);

	reset_fixture();
	hub.mask_sts = 0;
	run_case("unmasked interrupts", EIO);

	/* Allocation failures release cleanly before exposure. */
	for (i = 0; i < QV3D_OBJ_COUNT; i++) {
		reset_fixture();
		fx.fail_alloc[i] = true;
		run_case("allocation failure", ENOMEM);
		assert_released();
	}
	reset_fixture();
	fx.fail_map = QV3D_OBJ_PT;
	run_case("mapping failure", ENOMEM);
	assert_released();
	reset_fixture();
	fx.fail_create = QV3D_OBJ_TILE_STATE;
	run_case("map create failure", ENOMEM);
	assert_released();
	reset_fixture();
	fx.fail_load = QV3D_OBJ_RCL;
	run_case("map load failure", ENOMEM);
	assert_released();

	/* Rejected layouts. */
	reset_fixture();
	fx.split_load = QV3D_OBJ_RCL;
	run_case("split segment", EIO);
	assert_released();
	reset_fixture();
	fx.short_load = QV3D_OBJ_OUTPUT;
	run_case("short segment", EIO);
	assert_released();
	reset_fixture();
	fx.misalign_obj = QV3D_OBJ_BCL;
	run_case("misaligned address", EIO);
	assert_released();
	reset_fixture();
	fx.above4g_obj = QV3D_OBJ_TILE_ALLOC;
	run_case("above 4 GiB", EIO);
	assert_released();
	reset_fixture();
	fx.overlap_obj = QV3D_OBJ_TILE_STATE;
	run_case("overlapping ranges", EIO);
	assert_released();

	/* MMIO faults at decisive points retain what was published. */
	reset_fixture();
	fail_peek_at = 1;
	run_case("debug read fault", EIO);

	reset_fixture();
	fail_peek_at = 2;
	run_case("mask read fault", EIO);

	/* Stale latched status must stop the experiment before submission. */
	reset_fixture();
	core.int_sts = __BIT(0);
	run_case("stale FRDONE", EBUSY);
	CHECK(strstr(fx.last_message, "stale latched") != NULL);

	/* SPILLUSE is a failure like OUTOMEM. */
	reset_fixture();
	fx.mode = GPU_SPILLUSE;
	run_case("SPILLUSE latched", EIO);
	assert_retained();

	/* A GPU write into a control list is detected after the sync. */
	reset_fixture();
	fx.mode = GPU_TOUCH_CL;
	run_case("BCL modified", EIO);
	CHECK(strstr(fx.last_message, "control list modified") != NULL);
	assert_retained();

	/* Publishing and submission writes can fail like any MMIO. */
	reset_fixture();
	fail_hub_poke_at = 2;	/* the MMU control write */
	run_case("MMU write fault", EIO);
	assert_retained();

	reset_fixture();
	fail_core_poke_at = 8;	/* the CT0QEA submit */
	run_case("binner submit fault", EIO);
	assert_retained();

	reset_fixture();
	fail_core_poke_at = 9;	/* the INT_CLR ack */
	run_case("INT_CLR fault", EIO);
	assert_retained();

	printf("PASS: %u cases, %u checks\n", cases, checks);
	return 0;
}
