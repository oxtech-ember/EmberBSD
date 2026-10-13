/* Origin: EmberBSD bounded BCM2712 V3D first bin/render experiment, 2026-10-10. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

/*
 * Register, sequence and packet facts: raspberrypi/linux
 * 43c132e8863c3bff3647033b6a7d2bf87b15501c (v3d_sched.c, v3d_gem.c,
 * v3d_regs.h) and Mesa 26.2.4 (v3d_packet.xml min_ver=71 variants,
 * v3dx_{draw,job,rcl}.c, v3d_util.c). This native implementation does
 * not import that code. Only the explicit BCM2712_V3D_QUEUE_PROBE
 * configuration includes this file, after a completed takeover and the
 * accepted DMA experiment. One clear-and-store bin/render job on a
 * single 64x64 tile; completion is polled latched interrupt status with
 * delivery still masked; no IRQ handler, no DRM, no user submission,
 * no draws or shaders. Every allocation exposed to the GPU is retained
 * until reboot. Contracts and physical acceptance are pending; this
 * file must not ship in an installable configuration before they pass.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <arm/broadcom/bcm2712_v3d_takeover.h>

/* HUB offsets shared with the takeover inventory. */
#define QV3D_MASK_STS		0x5c
#define QV3D_HUB_IRQS		0x0000007f
#define QV3D_MMU_DEBUG		0x1238
#define QV3D_MMU_PA_WIDTH	__BITS(11, 8)
#define QV3D_MMU_VA_WIDTH	__BITS(7, 4)
#define QV3D_MMUC_CTL		0x1000
#define QV3D_MMUC_ENABLE	__BIT(0)
#define QV3D_MMUC_FLUSH		__BIT(1)
#define QV3D_MMUC_FLUSHING	__BIT(2)
#define QV3D_MMU_CTL		0x1200
#define QV3D_MMU_CTL_TLB_CLEAR	__BIT(2)
#define QV3D_MMU_CTL_TLB_CLEARING __BIT(7)
#define QV3D_MMU_CTL_FAULTS	(__BIT(27) | __BIT(20) | __BIT(12))
#define QV3D_MMU_CTL_VALUE	UINT64_C(0x060d0c01)
#define QV3D_MMU_PT_BASE	0x1204
#define QV3D_MMU_ILLEGAL	0x1230
#define QV3D_MMU_ILLEGAL_ENABLE	__BIT(31)
#define QV3D_PAGE_SHIFT		12
#define QV3D_PTE_VALID		__BIT(28)
#define QV3D_PTE_WRITEABLE	__BIT(29)
#define QV3D_PTE_PFN_LIMIT	__BIT(24)

/* CORE control-list and interrupt registers (pin: v3d_regs.h). */
#define QV3D_CTL_INT_STS	0x50
#define QV3D_CTL_INT_CLR	0x58
#define QV3D_INT_FRDONE		__BIT(0)
#define QV3D_INT_FLDONE		__BIT(1)
#define QV3D_INT_FAILURES	(__BIT(2) | __BIT(3) | __BIT(4) | __BIT(5))
#define QV3D_CTL_SLCACTL	0x24
#define QV3D_SLCACTL_INVALIDATE	UINT64_C(0x0f0f0f0f)
#define QV3D_CTL_L2TCACTL	0x30
#define QV3D_L2TCACTL_FLUSH	UINT64_C(0x00000001)
#define QV3D_CLE_CT0QTS		0x15c
#define QV3D_CT0QTS_ENABLE	__BIT(1)
#define QV3D_CLE_CT0QBA		0x160
#define QV3D_CLE_CT1QBA		0x164
#define QV3D_CLE_CT0QEA		0x168
#define QV3D_CLE_CT1QEA		0x16c
#define QV3D_CLE_CT0QMA		0x170
#define QV3D_CLE_CT0QMS		0x174
#define QV3D_PTB_BPOS		0x30c

/* One 64x64 RGBA8 tile: log2 tile size 3, one tile, one layer. */
#define QV3D_DIM		64
#define QV3D_LOG2_TILE		3
#define QV3D_CLEAR_COLOR	UINT64_C(0x305e7b4c)
#define QV3D_OUTPUT_DATA	(QV3D_DIM * QV3D_DIM * 4)
#define QV3D_OUTPUT_PAGES	5
#define QV3D_OUTPUT_SIZE	(QV3D_OUTPUT_PAGES * PAGE_SIZE)
/* v3d_tile_alloc_sizes(1, 1, 1, 0 draws, 4096): align(128, 4096) + 8192. */
#define QV3D_TILE_ALLOC_SIZE	12288
#define QV3D_TILE_STATE_SIZE	PAGE_SIZE
#define QV3D_BCL_SIZE		PAGE_SIZE
#define QV3D_RCL_SIZE		(2 * PAGE_SIZE)
#define QV3D_PT_SIZE		(4 * 1024 * 1024)
#define QV3D_SCRATCH_SIZE	PAGE_SIZE

#define QV3D_MMU_WAIT_COUNT	1000
#define QV3D_MMU_WAIT_US	100
#define QV3D_POLL_COUNT		5000
#define QV3D_POLL_US		100

/* V3D 7.1 control-list packet opcodes (pin: v3d_packet.xml). */
#define QV3D_OP_END_OF_RENDERING	13
#define QV3D_OP_FLUSH			4
#define QV3D_OP_START_TILE_BINNING	6
#define QV3D_OP_RETURN			18
#define QV3D_OP_FLUSH_VCD		19
#define QV3D_OP_GENERIC_TILE_LIST	20
#define QV3D_OP_BRANCH_IMPLICIT		21
#define QV3D_OP_SUPERTILE_COORDS	23
#define QV3D_OP_CLEAR_RTS		25
#define QV3D_OP_END_OF_LOADS		26
#define QV3D_OP_END_OF_TILE		27
#define QV3D_OP_STORE			29
#define QV3D_OP_SET_INSTANCEID		54
#define QV3D_OP_PRIM_LIST_FORMAT	56
#define QV3D_OP_OQ_COUNTER		92
#define QV3D_OP_BINNING_CFG		120
#define QV3D_OP_RENDER_CFG		121
#define QV3D_OP_SUPERTILE_CFG		122
#define QV3D_OP_TILE_LIST_BASE		123
#define QV3D_OP_TILE_COORDS		124
#define QV3D_OP_TILE_COORDS_IMPLICIT	125
#define QV3D_OP_TILE_LIST_BLOCK_SIZE	126
/* Render-config sub-ids of opcode 121 and store decode. */
#define QV3D_SUB_COMMON			0
#define QV3D_SUB_ZS_CLEAR		1
#define QV3D_SUB_RT_PART1		2
#define QV3D_STORE_BUFFER_RT0		0
#define QV3D_STORE_BUFFER_NONE		8
#define QV3D_STORE_FORMAT_RGBA8		27

enum qv3d_object {
	QV3D_OBJ_PT, QV3D_OBJ_SCRATCH, QV3D_OBJ_TILE_ALLOC, QV3D_OBJ_TILE_STATE,
	QV3D_OBJ_BCL, QV3D_OBJ_RCL, QV3D_OBJ_OUTPUT, QV3D_OBJ_COUNT
};

struct qv3d_buffer {
	bus_dma_segment_t seg;
	bus_dmamap_t map;
	void *kva;
	bus_size_t size;
	bool allocated, mapped, loaded;
};

/* One physical V3D, one attempt. Everything exposed is kept until reboot. */
static struct {
	device_t dev;
	bus_dma_tag_t dmat;
	struct qv3d_buffer obj[QV3D_OBJ_COUNT];
	uint8_t bcl_copy[QV3D_BCL_SIZE], rcl_copy[QV3D_RCL_SIZE];
	unsigned int bcl_bytes, rcl_bytes;
	bool attempted, published;
	const char *stage, *verdict;
} qv3d;

int bcmv3d_queue_probe(device_t, bus_dma_tag_t);

/* --- little-endian bit packing per the packet XML -----------------
 * A packet is its 8-bit opcode byte followed by payload fields whose
 * XML "start" positions count from the first payload bit.            */

struct qv3d_cl {
	uint8_t *base;
	unsigned int bits;	/* absolute write cursor in bits */
};

static void
qv3d_field(struct qv3d_cl *cl, unsigned int at, unsigned int size,
    uint64_t value)
{
	unsigned int i;

	KASSERT(size <= 64 && (size == 64 || value >> size == 0));
	for (i = 0; i < size; i++)
		if ((value & (UINT64_C(1) << i)) != 0)
			cl->base[(at + i) / 8] |= 1 << ((at + i) % 8);
}

static unsigned int
qv3d_packet(struct qv3d_cl *cl, unsigned int opcode,
    unsigned int payload_bytes)
{
	unsigned int base = cl->bits;

	KASSERT(payload_bytes <= 15 && opcode <= 126);
	cl->base[base / 8] = (uint8_t)opcode;
	memset(cl->base + base / 8 + 1, 0, payload_bytes);
	cl->bits = base + 8 + payload_bytes * 8;
	/* Return the payload bit origin for qv3d_set field writes. */
	return base + 8;
}

static void
qv3d_set(struct qv3d_cl *cl, unsigned int origin, unsigned int start,
    unsigned int size, uint64_t value)
{

	qv3d_field(cl, origin + start, size, value);
}

static unsigned int
qv3d_length(const struct qv3d_cl *cl)
{

	return cl->bits / 8;
}

static void
qv3d_build_bcl(struct qv3d_cl *cl)
{
	unsigned int o;

	/* TILE_BINNING_MODE_CFG: 64x64 frame, one 64x64 tile, 128b blocks. */
	o = qv3d_packet(cl, QV3D_OP_BINNING_CFG, 8);
	qv3d_set(cl, o, 32, 16, QV3D_DIM - 1);	/* width - 1 */
	qv3d_set(cl, o, 48, 16, QV3D_DIM - 1);	/* height - 1 */
	qv3d_set(cl, o, 8, 3, QV3D_LOG2_TILE);	/* log2 tile width */
	qv3d_set(cl, o, 11, 3, QV3D_LOG2_TILE);	/* log2 tile height */
	qv3d_set(cl, o, 4, 2, 1);		/* overflow block 128b */
	qv3d_set(cl, o, 2, 2, 1);		/* initial block 128b */

	/* There is definitely nothing in the VCD cache we want. */
	qv3d_packet(cl, QV3D_OP_FLUSH_VCD, 0);

	/* Disable any leftover occlusion-query state (address 0). */
	o = qv3d_packet(cl, QV3D_OP_OQ_COUNTER, 4);
	qv3d_set(cl, o, 0, 32, 0);

	/* Binning lists must have this item after any prefix state. */
	qv3d_packet(cl, QV3D_OP_START_TILE_BINNING, 0);

	/* Epilogue: cap the bin CLs with a return. */
	qv3d_packet(cl, QV3D_OP_FLUSH, 0);
}

static void
qv3d_build_rcl(struct qv3d_cl *cl, uint32_t rcl_va, uint32_t output_va,
    uint32_t tile_alloc_va)
{
	unsigned int o, generic_start, generic_end;

	/* TILE_RENDERING_MODE_CFG_COMMON: 64x64, one RT, 64x64 tile. */
	o = qv3d_packet(cl, QV3D_OP_RENDER_CFG, 8);
	qv3d_set(cl, o, 0, 3, QV3D_SUB_COMMON);
	qv3d_set(cl, o, 4, 4, 0);		/* render targets - 1 */
	qv3d_set(cl, o, 8, 16, QV3D_DIM);	/* image width */
	qv3d_set(cl, o, 24, 16, QV3D_DIM);	/* image height */
	qv3d_set(cl, o, 44, 1, 1);		/* depth buffer disable */
	qv3d_set(cl, o, 46, 1, 1);		/* early-Z disable */
	qv3d_set(cl, o, 52, 3, QV3D_LOG2_TILE);	/* log2 tile width */
	qv3d_set(cl, o, 55, 3, QV3D_LOG2_TILE);	/* log2 tile height */

	/* RENDER_TARGET_PART1: RGBA8, 32bpp internal, raster stride 32. */
	o = qv3d_packet(cl, QV3D_OP_RENDER_CFG, 8);
	qv3d_set(cl, o, 0, 3, QV3D_SUB_RT_PART1);
	qv3d_set(cl, o, 3, 3, 0);		/* render target 0 */
	qv3d_set(cl, o, 7, 11, 0);		/* base address 0 */
	qv3d_set(cl, o, 18, 7, 32 - 1);		/* stride - 1, 128b rows */
	qv3d_set(cl, o, 25, 2, 0);		/* internal BPP 32 */
	qv3d_set(cl, o, 27, 5, 8);		/* RGBA8 type and clamp */
	qv3d_set(cl, o, 32, 32, QV3D_CLEAR_COLOR);

	/* ZS_CLEAR_VALUES: zeroes end the render configuration. */
	o = qv3d_packet(cl, QV3D_OP_RENDER_CFG, 8);
	qv3d_set(cl, o, 0, 4, QV3D_SUB_ZS_CLEAR);

	/* TILE_LIST_INITIAL_BLOCK_SIZE: auto-chained, 128b first block. */
	o = qv3d_packet(cl, QV3D_OP_TILE_LIST_BLOCK_SIZE, 1);
	qv3d_set(cl, o, 0, 2, 1);
	qv3d_set(cl, o, 2, 1, 1);

	/* MULTICORE_RENDERING_TILE_LIST_SET_BASE. */
	o = qv3d_packet(cl, QV3D_OP_TILE_LIST_BASE, 4);
	qv3d_set(cl, o, 0, 4, 0);		/* tile list set 0 */
	qv3d_set(cl, o, 6, 26, tile_alloc_va >> 6);

	/* MULTICORE_RENDERING_SUPERTILE_CFG: one tile, one supertile. */
	o = qv3d_packet(cl, QV3D_OP_SUPERTILE_CFG, 8);
	qv3d_set(cl, o, 0, 8, 0);		/* supertile width - 1 */
	qv3d_set(cl, o, 8, 8, 0);		/* supertile height - 1 */
	qv3d_set(cl, o, 16, 8, 1);		/* frame width in supertiles */
	qv3d_set(cl, o, 24, 8, 1);		/* frame height in supertiles */
	qv3d_set(cl, o, 32, 12, 1);		/* frame width in tiles */
	qv3d_set(cl, o, 44, 12, 1);		/* frame height in tiles */
	qv3d_set(cl, o, 61, 3, 0);		/* bin tile lists - 1 */

	/* Initial double clear of the tile buffer (GFXH-1742 style). */
	for (unsigned int pass = 0; pass < 2; pass++) {
		o = qv3d_packet(cl, QV3D_OP_TILE_COORDS, 3);
		qv3d_set(cl, o, 0, 12, 0);		/* column 0 */
		qv3d_set(cl, o, 12, 12, 0);		/* row 0 */
		qv3d_packet(cl, QV3D_OP_END_OF_LOADS, 0);
		o = qv3d_packet(cl, QV3D_OP_STORE, 12);
		qv3d_set(cl, o, 0, 4, QV3D_STORE_BUFFER_NONE);
		qv3d_packet(cl, QV3D_OP_CLEAR_RTS, 0);
		qv3d_packet(cl, QV3D_OP_END_OF_TILE, 0);
	}
	qv3d_packet(cl, QV3D_OP_FLUSH_VCD, 0);

	/* Generic tile list inline in this RCL buffer. */
	generic_start = qv3d_length(cl);
	qv3d_packet(cl, QV3D_OP_TILE_COORDS_IMPLICIT, 0);
	qv3d_packet(cl, QV3D_OP_END_OF_LOADS, 0);
	o = qv3d_packet(cl, QV3D_OP_PRIM_LIST_FORMAT, 1);
	qv3d_set(cl, o, 0, 6, 2);		/* list triangles */
	o = qv3d_packet(cl, QV3D_OP_SET_INSTANCEID, 4);
	qv3d_set(cl, o, 0, 32, 0);		/* PTB assumes zero */
	o = qv3d_packet(cl, QV3D_OP_BRANCH_IMPLICIT, 1);
	qv3d_set(cl, o, 0, 8, 0);		/* tile list set 0 */
	o = qv3d_packet(cl, QV3D_OP_STORE, 12);
	qv3d_set(cl, o, 0, 4, QV3D_STORE_BUFFER_RT0);
	qv3d_set(cl, o, 4, 3, 0);		/* memory format raster */
	qv3d_set(cl, o, 12, 6, QV3D_STORE_FORMAT_RGBA8);
	qv3d_set(cl, o, 28, 20, QV3D_DIM * 4);	/* raster byte stride */
	qv3d_set(cl, o, 48, 16, QV3D_DIM);	/* height */
	qv3d_set(cl, o, 64, 32, output_va);
	qv3d_packet(cl, QV3D_OP_END_OF_TILE, 0);
	qv3d_packet(cl, QV3D_OP_RETURN, 0);
	generic_end = qv3d_length(cl);

	o = qv3d_packet(cl, QV3D_OP_GENERIC_TILE_LIST, 8);
	qv3d_set(cl, o, 0, 32, rcl_va + generic_start);
	qv3d_set(cl, o, 32, 32, rcl_va + generic_end);

	o = qv3d_packet(cl, QV3D_OP_SUPERTILE_COORDS, 2);
	qv3d_set(cl, o, 0, 8, 0);		/* supertile column 0 */
	qv3d_set(cl, o, 8, 8, 0);		/* supertile row 0 */
	qv3d_packet(cl, QV3D_OP_END_OF_RENDERING, 0);
}

/* --- bus_dma objects and MMU publication (rules as the DMA probe) -- */

static int
qv3d_allocate(void)
{
	static const bus_size_t sizes[QV3D_OBJ_COUNT] = {
		QV3D_PT_SIZE, QV3D_SCRATCH_SIZE, QV3D_TILE_ALLOC_SIZE,
		QV3D_TILE_STATE_SIZE, QV3D_BCL_SIZE, QV3D_RCL_SIZE,
		QV3D_OUTPUT_SIZE
	};
	struct qv3d_buffer *object;
	enum qv3d_object id, other;
	int nsegs, error;

	for (id = 0; id < QV3D_OBJ_COUNT; id++) {
		object = &qv3d.obj[id];
		object->size = sizes[id];
		error = bus_dmamem_alloc(qv3d.dmat, object->size, PAGE_SIZE, 0,
		    &object->seg, 1, &nsegs, BUS_DMA_WAITOK);
		if (error != 0)
			return error;
		object->allocated = true;
		error = bus_dmamem_map(qv3d.dmat, &object->seg, 1,
		    object->size, &object->kva, BUS_DMA_WAITOK);
		if (error != 0)
			return error;
		object->mapped = true;
		error = bus_dmamap_create(qv3d.dmat, object->size, 1,
		    object->size, 0, BUS_DMA_WAITOK, &object->map);
		if (error != 0)
			return error;
		error = bus_dmamap_load(qv3d.dmat, object->map, object->kva,
		    object->size, NULL, BUS_DMA_WAITOK);
		if (error != 0)
			return error;
		object->loaded = true;
		if (object->map->dm_nsegs != 1 ||
		    object->map->dm_segs[0].ds_len != object->size ||
		    (object->map->dm_segs[0].ds_addr & (PAGE_SIZE - 1)) != 0 ||
		    object->map->dm_segs[0].ds_addr +
		    (uint64_t)object->size > UINT64_C(0x100000000))
			return EIO;
	}
	for (id = 0; id < QV3D_OBJ_COUNT; id++)
		for (other = id + 1; other < QV3D_OBJ_COUNT; other++)
			if (qv3d.obj[id].map->dm_segs[0].ds_addr <
			    qv3d.obj[other].map->dm_segs[0].ds_addr +
			    qv3d.obj[other].size &&
			    qv3d.obj[other].map->dm_segs[0].ds_addr <
			    qv3d.obj[id].map->dm_segs[0].ds_addr +
			    qv3d.obj[id].size)
				return EIO;
	return 0;
}

static void
qv3d_release_unpublished(void)
{
	struct qv3d_buffer *object;
	enum qv3d_object id;

	for (id = 0; id < QV3D_OBJ_COUNT; id++) {
		object = &qv3d.obj[id];
		if (object->loaded)
			bus_dmamap_unload(qv3d.dmat, object->map);
		if (object->map != NULL)
			bus_dmamap_destroy(qv3d.dmat, object->map);
		if (object->mapped)
			bus_dmamem_unmap(qv3d.dmat, object->kva, object->size);
		if (object->allocated)
			bus_dmamem_free(qv3d.dmat, &object->seg, 1);
		memset(object, 0, sizeof(*object));
	}
}

static int
qv3d_map_object(enum qv3d_object id, bool writeable, unsigned int pages)
{
	uint32_t *pt = qv3d.obj[QV3D_OBJ_PT].kva;
	bus_addr_t va = qv3d.obj[id].map->dm_segs[0].ds_addr;
	unsigned int i;

	/* This job maps each buffer at its own DMA address through the MMU. */
	for (i = 0; i < pages; i++) {
		uint32_t pfn = (uint32_t)(va >> QV3D_PAGE_SHIFT) + i;

		if (pfn + pages >= QV3D_PTE_PFN_LIMIT)
			return EIO;
		pt[(va >> QV3D_PAGE_SHIFT) + i] = pfn | QV3D_PTE_VALID |
		    (writeable ? QV3D_PTE_WRITEABLE : 0);
	}
	return 0;
}

static int
qv3d_publish_mmu(void)
{
	uint32_t pt_pfn =
	    qv3d.obj[QV3D_OBJ_PT].map->dm_segs[0].ds_addr >> QV3D_PAGE_SHIFT;
	uint32_t scratch_pfn =
	    qv3d.obj[QV3D_OBJ_SCRATCH].map->dm_segs[0].ds_addr >>
	    QV3D_PAGE_SHIFT;
	int error;

	/* From this point on, every allocation stays exposed until reboot. */
	qv3d.published = true;
	error = bcmv3d_takeover_hub_poke(QV3D_MMU_PT_BASE, pt_pfn);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(QV3D_MMU_CTL, QV3D_MMU_CTL_VALUE);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(QV3D_MMU_ILLEGAL,
	    scratch_pfn | QV3D_MMU_ILLEGAL_ENABLE);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(QV3D_MMUC_CTL, QV3D_MMUC_ENABLE);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(QV3D_MMUC_CTL,
	    QV3D_MMUC_ENABLE | QV3D_MMUC_FLUSH);
	if (error != 0)
		return error;
	/* Bounded waits as in the accepted DMA experiment. */
	{
		uint32_t value;
		unsigned int i;

		for (i = 0; i < QV3D_MMU_WAIT_COUNT; i++) {
			error = bcmv3d_takeover_hub_peek(QV3D_MMUC_CTL,
			    &value);
			if (error != 0)
				return error;
			if ((value & QV3D_MMUC_FLUSHING) == 0)
				break;
			delay(QV3D_MMU_WAIT_US);
		}
		if (i == QV3D_MMU_WAIT_COUNT)
			return ETIMEDOUT;
	}
	/* A full overwrite never reflects W1C fault bits back. */
	error = bcmv3d_takeover_hub_poke(QV3D_MMU_CTL,
	    QV3D_MMU_CTL_VALUE | QV3D_MMU_CTL_TLB_CLEAR);
	if (error != 0)
		return error;
	{
		uint32_t value;
		unsigned int i;

		for (i = 0; i < QV3D_MMU_WAIT_COUNT; i++) {
			error = bcmv3d_takeover_hub_peek(QV3D_MMU_CTL,
			    &value);
			if (error != 0)
				return error;
			if ((value & QV3D_MMU_CTL_TLB_CLEARING) == 0)
				break;
			delay(QV3D_MMU_WAIT_US);
		}
		if (i == QV3D_MMU_WAIT_COUNT)
			return ETIMEDOUT;
		error = bcmv3d_takeover_hub_peek(QV3D_MMU_CTL, &value);
		if (error != 0)
			return error;
		if (value != QV3D_MMU_CTL_VALUE)
			return EIO;
	}
	return 0;
}

static int
qv3d_invalidate_caches(void)
{
	int error;

	error = bcmv3d_takeover_core_poke(QV3D_CTL_L2TCACTL,
	    QV3D_L2TCACTL_FLUSH);
	if (error != 0)
		return error;
	return bcmv3d_takeover_core_poke(QV3D_CTL_SLCACTL,
	    QV3D_SLCACTL_INVALIDATE);
}

static int
qv3d_fault_pending(bool *pending)
{
	uint32_t value;
	int error;

	error = bcmv3d_takeover_hub_peek(QV3D_MMU_CTL, &value);
	if (error != 0)
		return error;
	*pending = (value & QV3D_MMU_CTL_FAULTS) != 0;
	return 0;
}

static int
qv3d_wait_done(uint32_t bit, uint32_t *failures)
{
	uint32_t status;
	bool pending;
	unsigned int i;
	int error;

	for (i = 0; i < QV3D_POLL_COUNT; i++) {
		error = bcmv3d_takeover_core_peek(QV3D_CTL_INT_STS, &status);
		if (error != 0)
			return error;
		if ((status & QV3D_INT_FAILURES) != 0) {
			*failures = status & QV3D_INT_FAILURES;
			return EIO;
		}
		error = qv3d_fault_pending(&pending);
		if (error != 0)
			return error;
		if (pending) {
			*failures = 0;
			return EFAULT;
		}
		if ((status & bit) != 0)
			return 0;
		delay(QV3D_POLL_US);
	}
	return ETIMEDOUT;
}

int
bcmv3d_queue_probe(device_t dev, bus_dma_tag_t dmat)
{
	struct qv3d_cl bcl, rcl;
	uint32_t debug, masks, failures = 0;
	uint32_t bcl_va, rcl_va, tile_alloc_va, tile_state_va, output_va;
	bool pending;
	int error;

	if (qv3d.attempted)
		return EBUSY;
	qv3d.attempted = true;
	qv3d.dev = dev;
	qv3d.dmat = dmat;
	qv3d.stage = "awaiting completed takeover";
	if (!bcmv3d_takeover_complete())
		return EPERM;
	qv3d.stage = "MMU geometry";
	error = bcmv3d_takeover_hub_peek(QV3D_MMU_DEBUG, &debug);
	if (error != 0)
		goto out;
	if (__SHIFTOUT(debug, QV3D_MMU_PA_WIDTH) < 5 ||
	    __SHIFTOUT(debug, QV3D_MMU_VA_WIDTH) < 5) {
		qv3d.verdict = "unsuitable MMU geometry";
		error = EOPNOTSUPP;
		goto out;
	}
	qv3d.stage = "retained interrupt masks";
	error = bcmv3d_takeover_hub_peek(QV3D_MASK_STS, &masks);
	if (error != 0)
		goto out;
	if ((masks & QV3D_HUB_IRQS) != QV3D_HUB_IRQS) {
		qv3d.verdict = "HUB interrupt masks lost";
		error = EIO;
		goto out;
	}
	qv3d.stage = "seven bounded DMA allocations";
	error = qv3d_allocate();
	if (error != 0)
		goto out;
	bcl_va = qv3d.obj[QV3D_OBJ_BCL].map->dm_segs[0].ds_addr;
	rcl_va = qv3d.obj[QV3D_OBJ_RCL].map->dm_segs[0].ds_addr;
	tile_alloc_va = qv3d.obj[QV3D_OBJ_TILE_ALLOC].map->dm_segs[0].ds_addr;
	tile_state_va = qv3d.obj[QV3D_OBJ_TILE_STATE].map->dm_segs[0].ds_addr;
	output_va = qv3d.obj[QV3D_OBJ_OUTPUT].map->dm_segs[0].ds_addr;
	qv3d.stage = "control lists and canaries";
	memset(qv3d.obj[QV3D_OBJ_PT].kva, 0, QV3D_PT_SIZE);
	memset(qv3d.obj[QV3D_OBJ_SCRATCH].kva, 0, QV3D_SCRATCH_SIZE);
	memset(qv3d.obj[QV3D_OBJ_TILE_ALLOC].kva, 0, QV3D_TILE_ALLOC_SIZE);
	memset(qv3d.obj[QV3D_OBJ_TILE_STATE].kva, 0, QV3D_TILE_STATE_SIZE);
	memset(qv3d.obj[QV3D_OBJ_OUTPUT].kva, 0, QV3D_OUTPUT_SIZE);
	memset(qv3d.obj[QV3D_OBJ_BCL].kva, 0, QV3D_BCL_SIZE);
	memset(qv3d.obj[QV3D_OBJ_RCL].kva, 0, QV3D_RCL_SIZE);
	bcl.base = qv3d.obj[QV3D_OBJ_BCL].kva;
	bcl.bits = 0;
	qv3d_build_bcl(&bcl);
	rcl.base = qv3d.obj[QV3D_OBJ_RCL].kva;
	rcl.bits = 0;
	qv3d_build_rcl(&rcl, rcl_va, output_va, tile_alloc_va);
	KASSERT(qv3d_length(&bcl) <= QV3D_BCL_SIZE);
	KASSERT(qv3d_length(&rcl) <= QV3D_RCL_SIZE);
	qv3d.bcl_bytes = qv3d_length(&bcl);
	qv3d.rcl_bytes = qv3d_length(&rcl);
	memcpy(qv3d.bcl_copy, qv3d.obj[QV3D_OBJ_BCL].kva, qv3d.bcl_bytes);
	memcpy(qv3d.rcl_copy, qv3d.obj[QV3D_OBJ_RCL].kva, qv3d.rcl_bytes);
	qv3d.stage = "identity page table for one job";
	if ((error = qv3d_map_object(QV3D_OBJ_BCL, false, 1)) != 0 ||
	    (error = qv3d_map_object(QV3D_OBJ_RCL, false, 2)) != 0 ||
	    (error = qv3d_map_object(QV3D_OBJ_TILE_ALLOC, true, 3)) != 0 ||
	    (error = qv3d_map_object(QV3D_OBJ_TILE_STATE, true, 1)) != 0 ||
	    (error = qv3d_map_object(QV3D_OBJ_OUTPUT, true, 5)) != 0)
		goto out;
	qv3d.stage = "pre-publication cache synchronization";
	for (enum qv3d_object id = 0; id < QV3D_OBJ_COUNT; id++)
		bus_dmamap_sync(qv3d.dmat, qv3d.obj[id].map, 0,
		    qv3d.obj[id].size,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	qv3d.stage = "MMU publication";
	error = qv3d_publish_mmu();
	if (error != 0)
		goto out;
	qv3d.stage = "clean interrupt status";
	{
		uint32_t status;

		error = bcmv3d_takeover_core_peek(QV3D_CTL_INT_STS, &status);
		if (error != 0)
			goto out;
		if (status != 0) {
			qv3d.verdict = "stale latched interrupt status";
			error = EBUSY;
			goto out;
		}
	}
	qv3d.stage = "binner submission";
	error = bcmv3d_takeover_core_poke(QV3D_PTB_BPOS, 0);
	if (error != 0)
		goto out;
	error = qv3d_invalidate_caches();
	if (error != 0)
		goto out;
	error = bcmv3d_takeover_core_poke(QV3D_CLE_CT0QMA, tile_alloc_va);
	if (error != 0)
		goto out;
	error = bcmv3d_takeover_core_poke(QV3D_CLE_CT0QMS,
	    QV3D_TILE_ALLOC_SIZE);
	if (error != 0)
		goto out;
	error = bcmv3d_takeover_core_poke(QV3D_CLE_CT0QTS,
	    QV3D_CT0QTS_ENABLE | tile_state_va);
	if (error != 0)
		goto out;
	error = bcmv3d_takeover_core_poke(QV3D_CLE_CT0QBA, bcl_va);
	if (error != 0)
		goto out;
	error = bcmv3d_takeover_core_poke(QV3D_CLE_CT0QEA,
	    bcl_va + qv3d_length(&bcl) /* this write starts the binner */);
	qv3d.stage = "binner completion watch";
	if (error != 0)
		goto out;
	error = qv3d_wait_done(QV3D_INT_FLDONE, &failures);
	if (error != 0)
		goto out;
	/* Clear the latched bit so only FRDONE ends the render watch. */
	error = bcmv3d_takeover_core_poke(QV3D_CTL_INT_CLR, QV3D_INT_FLDONE);
	if (error != 0)
		goto out;
	qv3d.stage = "render submission";
	error = qv3d_invalidate_caches();
	if (error != 0)
		goto out;
	error = bcmv3d_takeover_core_poke(QV3D_CLE_CT1QBA, rcl_va);
	if (error != 0)
		goto out;
	error = bcmv3d_takeover_core_poke(QV3D_CLE_CT1QEA,
	    rcl_va + qv3d_length(&rcl) /* this write starts the render */);
	qv3d.stage = "render completion watch";
	if (error != 0)
		goto out;
	error = qv3d_wait_done(QV3D_INT_FRDONE, &failures);
	if (error != 0)
		goto out;
	qv3d.stage = "post-completion cache synchronization";
	for (enum qv3d_object id = 0; id < QV3D_OBJ_COUNT; id++)
		bus_dmamap_sync(qv3d.dmat, qv3d.obj[id].map, 0,
		    qv3d.obj[id].size,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	qv3d.stage = "control-list integrity";
	if (memcmp(qv3d.bcl_copy, qv3d.obj[QV3D_OBJ_BCL].kva,
	    qv3d.bcl_bytes) != 0 ||
	    memcmp(qv3d.rcl_copy, qv3d.obj[QV3D_OBJ_RCL].kva,
	    qv3d.rcl_bytes) != 0) {
		qv3d.verdict = "control list modified by the GPU";
		error = EIO;
		goto out;
	}
	qv3d.stage = "cleared-image verification";
	{
		const uint32_t *out = qv3d.obj[QV3D_OBJ_OUTPUT].kva;
		size_t words = QV3D_OUTPUT_DATA / sizeof(*out);
		size_t canary_words = (QV3D_OUTPUT_SIZE - QV3D_OUTPUT_DATA) /
		    sizeof(*out);
		size_t i;

		for (i = 0; i < words; i++)
			if (out[i] != (uint32_t)QV3D_CLEAR_COLOR) {
				qv3d.verdict = "output word mismatch";
				error = EIO;
				goto out;
			}
		for (i = words; i < words + canary_words; i++)
			if (out[i] != 0) {
				qv3d.verdict = "output canary modified";
				error = EIO;
				goto out;
			}
	}
	{
		const uint32_t *scratch = qv3d.obj[QV3D_OBJ_SCRATCH].kva;
		size_t words = QV3D_SCRATCH_SIZE / sizeof(*scratch);

		for (size_t i = 0; i < words; i++)
			if (scratch[i] != 0) {
				qv3d.verdict = "scratch page modified";
				error = EIO;
				goto out;
			}
	}
	qv3d.stage = "final fault check";
	error = qv3d_fault_pending(&pending);
	if (error == 0 && pending) {
		qv3d.verdict = "MMU fault after completion";
		error = EFAULT;
	}
out:
	/* Allocations exposed to the GPU are never released. */
	if (!qv3d.published)
		qv3d_release_unpublished();
	if (error == 0) {
		aprint_normal_dev(dev, "bin/render PASS: one 64x64 tile "
		    "cleared to %#x and stored through both control lists; "
		    "allocations retained until reboot\n",
		    (unsigned int)QV3D_CLEAR_COLOR);
	} else {
		aprint_normal_dev(dev, "queue experiment stopped at %s: "
		    "error %d; %s%s; %s\n", qv3d.stage, error,
		    qv3d.verdict != NULL ? qv3d.verdict :
		    "no verdict recorded",
		    failures != 0 ? "; latched failure interrupts" : "",
		    qv3d.published ? "GPU-exposed allocations retained until "
		    "reboot; no retry" :
		    "nothing published; DMA allocations released");
	}
	return error;
}
