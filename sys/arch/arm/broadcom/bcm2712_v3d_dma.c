/* Origin: EmberBSD bounded BCM2712 V3D first DMA/TFU experiment, 2026-10-10. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

/*
 * Register and sequence facts: raspberrypi/linux
 * 43c132e8863c3bff3647033b6a7d2bf87b15501c, v3d_{regs,mmu,sched}, and
 * Mesa 26.2.4 v3dx_tfu.c (generic R32F format 29, raster strides in
 * texels, IOC output stride 64 << 16). This native implementation does
 * not import Linux code. Only the explicit BCM2712_V3D_DMA_PROBE
 * configuration includes this file, and only after a completed takeover.
 * One bounded translated 64x64 R32F TFU copy; no DRM, no user command
 * submission, no IRQ handler, no frequency change, no reset retry.
 * Every allocation exposed to the GPU is retained until reboot.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <arm/broadcom/bcm2712_v3d_takeover.h>

/* HUB offsets shared with the takeover inventory. */
#define DV3D_MASK_STS		0x5c
#define DV3D_HUB_IRQS		0x0000007f
#define DV3D_MMU_DEBUG		0x1238
#define DV3D_MMU_PA_WIDTH	__BITS(11, 8)
#define DV3D_MMU_VA_WIDTH	__BITS(7, 4)
#define DV3D_MMUC_CTL		0x1000
#define DV3D_MMUC_ENABLE	__BIT(0)
#define DV3D_MMUC_FLUSH		__BIT(1)
#define DV3D_MMUC_FLUSHING	__BIT(2)
#define DV3D_MMU_CTL		0x1200
#define DV3D_MMU_CTL_TLB_CLEAR	__BIT(2)
#define DV3D_MMU_CTL_TLB_CLEARING __BIT(7)
#define DV3D_MMU_CTL_FAULTS	(__BIT(27) | __BIT(20) | __BIT(12))
/*
 * v3d_mmu.c: ENABLE plus ABORT|INT for PT_INVALID, WRITE_VIOLATION and
 * CAP_EXCEEDED. Fault bits are never written back after a read.
 */
#define DV3D_MMU_CTL_VALUE	UINT64_C(0x060d0c01)
#define DV3D_MMU_PT_BASE	0x1204
#define DV3D_MMU_ILLEGAL	0x1230
#define DV3D_MMU_ILLEGAL_ENABLE	__BIT(31)
#define DV3D_PAGE_SHIFT		12
#define DV3D_PTE_VALID		__BIT(28)
#define DV3D_PTE_WRITEABLE	__BIT(29)
#define DV3D_PTE_PFN_LIMIT	__BIT(24)

/* V7.1 HUB TFU block. */
#define DV3D_TFU_CS		0x700
#define DV3D_TFU_CVTCT_SHIFT	16
#define DV3D_TFU_CVTCT_MASK	UINT64_C(0x000000ff)
#define DV3D_TFU_BUSY		__BIT(0)
#define DV3D_TFU_ICFG		0x708
#define DV3D_TFU_ICFG_VALUE	UINT64_C(0x001d0001)	/* R32F 29 << 16 | IOC */
#define DV3D_TFU_IIA		0x70c
#define DV3D_TFU_ICA		0x710
#define DV3D_TFU_IIS		0x714
#define DV3D_TFU_IUA		0x718
#define DV3D_TFU_IOC		0x71c
#define DV3D_TFU_IOC_VALUE	UINT64_C(0x00400000)	/* stride 64 << 16 */
#define DV3D_TFU_IOA		0x720
#define DV3D_TFU_IOS		0x724
#define DV3D_TFU_IOS_VALUE	UINT64_C(0x00400040)	/* 64 << 16 | 64 */
#define DV3D_TFU_COEF(n)	(0x728 + (n) * 4)

/* One bounded copy: 64x64 R32F = 16 KiB of data per image. */
#define DV3D_TFU_DIM		64
#define DV3D_TFU_DATA		(DV3D_TFU_DIM * DV3D_TFU_DIM * 4)
/* Five pages per image: four data pages plus read-ahead/canary page. */
#define DV3D_DATA_PAGES		4
#define DV3D_IMAGE_PAGES	5
#define DV3D_IMAGE_SIZE		(DV3D_IMAGE_PAGES * PAGE_SIZE)
#define DV3D_PT_SIZE		(4 * 1024 * 1024)
#define DV3D_SCRATCH_SIZE	PAGE_SIZE

#define DV3D_CACHE_POLL_COUNT	1000
#define DV3D_CACHE_POLL_US	100
#define DV3D_TFU_POLL_COUNT	5000
#define DV3D_TFU_POLL_US	100

enum dv3d_object {
	DV3D_OBJ_PT, DV3D_OBJ_SCRATCH, DV3D_OBJ_ACTUAL_SRC, DV3D_OBJ_ACTUAL_DST,
	DV3D_OBJ_ALIAS_SRC, DV3D_OBJ_ALIAS_DST, DV3D_OBJ_COUNT
};

struct dv3d_buffer {
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
	struct dv3d_buffer obj[DV3D_OBJ_COUNT];
	bool attempted, published, have_mismatch;
	const char *stage, *verdict;
	uint32_t first_mismatch;
	uint8_t cvt_before, cvt_after;
} dv3d;

int bcmv3d_dma_probe(device_t, bus_dma_tag_t);

static uint32_t
dv3d_cvtct(uint32_t tfu_cs)
{

	return (tfu_cs >> DV3D_TFU_CVTCT_SHIFT) & DV3D_TFU_CVTCT_MASK;
}

/*
 * Deterministic per-image word patterns. The comparison after the copy
 * uses these definitions, never memory the GPU could have modified.
 */
static uint32_t
dv3d_pattern(enum dv3d_object id, uint32_t page, uint32_t word)
{

	switch (id) {
	case DV3D_OBJ_ACTUAL_SRC:
		/* The fifth page only serves the 64-byte read-ahead allowance. */
		if (page >= DV3D_DATA_PAGES)
			return 0xcafe0000u + page * 0x1000u + word;
		return 0xa5a50000u + page * 0x1000u + word;
	case DV3D_OBJ_ALIAS_SRC:
		if (page >= DV3D_DATA_PAGES)
			return 0xbeef0000u + page * 0x1000u + word;
		return 0x3c5c0000u + page * 0x4000u + word;
	case DV3D_OBJ_ACTUAL_DST:
		return 0xdead0000u + page * 0x2000u + word;
	case DV3D_OBJ_ALIAS_DST:
		return 0xf00d0000u + page * 0x8000u + word;
	default:
		return 0;
	}
}

static void
dv3d_fill(enum dv3d_object id)
{
	uint32_t *image = dv3d.obj[id].kva;
	uint32_t per_page = PAGE_SIZE / sizeof(*image);
	uint32_t page, word;

	for (page = 0; page < dv3d.obj[id].size / PAGE_SIZE; page++)
		for (word = 0; word < per_page; word++)
			image[page * per_page + word] =
			    dv3d_pattern(id, page, word);
}

static bool
dv3d_buffer_matches(enum dv3d_object id, uint32_t from_page, uint32_t pages,
    uint32_t *first_mismatch)
{
	const uint32_t *image = dv3d.obj[id].kva;
	uint32_t per_page = PAGE_SIZE / sizeof(*image);
	uint32_t page, word;

	for (page = from_page; page < from_page + pages; page++)
		for (word = 0; word < per_page; word++) {
			uint32_t index = page * per_page + word;

			if (image[index] != dv3d_pattern(id, page, word)) {
				*first_mismatch = index;
				return false;
			}
		}
	return true;
}

/* True when the actual-source data pattern landed in this image. */
static bool
dv3d_contains_actual_source(enum dv3d_object id, uint32_t from_page,
    uint32_t pages, uint32_t *first_mismatch)
{
	uint32_t *image = dv3d.obj[id].kva;
	uint32_t per_page = PAGE_SIZE / sizeof(*image);
	uint32_t page, word;

	for (page = from_page; page < from_page + pages; page++)
		for (word = 0; word < per_page; word++) {
			uint32_t index = page * per_page + word;

			if (image[index] != dv3d_pattern(DV3D_OBJ_ACTUAL_SRC,
			    page, word)) {
				*first_mismatch = index;
				return false;
			}
		}
	return true;
}

static bool
dv3d_scratch_clean(void)
{
	const uint32_t *scratch = dv3d.obj[DV3D_OBJ_SCRATCH].kva;
	size_t words = dv3d.obj[DV3D_OBJ_SCRATCH].size / sizeof(*scratch);
	size_t i;

	for (i = 0; i < words; i++)
		if (scratch[i] != 0)
			return false;
	return true;
}

static int
dv3d_allocate(void)
{
	static const bus_size_t sizes[DV3D_OBJ_COUNT] = {
		DV3D_PT_SIZE, DV3D_SCRATCH_SIZE, DV3D_IMAGE_SIZE,
		DV3D_IMAGE_SIZE, DV3D_IMAGE_SIZE, DV3D_IMAGE_SIZE
	};
	struct dv3d_buffer *object;
	enum dv3d_object id, other;
	int nsegs, error;

	for (id = 0; id < DV3D_OBJ_COUNT; id++) {
		object = &dv3d.obj[id];
		object->size = sizes[id];
		error = bus_dmamem_alloc(dv3d.dmat, object->size, PAGE_SIZE, 0,
		    &object->seg, 1, &nsegs, BUS_DMA_WAITOK);
		if (error != 0)
			return error;
		object->allocated = true;
		error = bus_dmamem_map(dv3d.dmat, &object->seg, 1,
		    object->size, &object->kva, BUS_DMA_WAITOK);
		if (error != 0)
			return error;
		object->mapped = true;
		error = bus_dmamap_create(dv3d.dmat, object->size, 1,
		    object->size, 0, BUS_DMA_WAITOK, &object->map);
		if (error != 0)
			return error;
		error = bus_dmamap_load(dv3d.dmat, object->map, object->kva,
		    object->size, NULL, BUS_DMA_WAITOK);
		if (error != 0)
			return error;
		object->loaded = true;
		/*
		 * Hardware addresses come only from the loaded segment:
		 * exactly one segment, full length, 4 KiB alignment and an
		 * end at or below the 32-bit DMA window for every object.
		 */
		if (object->map->dm_nsegs != 1 ||
		    object->map->dm_segs[0].ds_len != object->size ||
		    (object->map->dm_segs[0].ds_addr & (PAGE_SIZE - 1)) != 0 ||
		    object->map->dm_segs[0].ds_addr +
		    (uint64_t)object->size > UINT64_C(0x100000000))
			return EIO;
	}
	/* Pairwise range overlap is a rejected layout, not a retry input. */
	for (id = 0; id < DV3D_OBJ_COUNT; id++)
		for (other = id + 1; other < DV3D_OBJ_COUNT; other++)
			if (dv3d.obj[id].map->dm_segs[0].ds_addr <
			    dv3d.obj[other].map->dm_segs[0].ds_addr +
			    dv3d.obj[other].size &&
			    dv3d.obj[other].map->dm_segs[0].ds_addr <
			    dv3d.obj[id].map->dm_segs[0].ds_addr +
			    dv3d.obj[id].size)
				return EIO;
	return 0;
}

static void
dv3d_release_unpublished(void)
{
	struct dv3d_buffer *object;
	enum dv3d_object id;

	for (id = 0; id < DV3D_OBJ_COUNT; id++) {
		object = &dv3d.obj[id];
		if (object->loaded)
			bus_dmamap_unload(dv3d.dmat, object->map);
		if (object->map != NULL)
			bus_dmamap_destroy(dv3d.dmat, object->map);
		if (object->mapped)
			bus_dmamem_unmap(dv3d.dmat, object->kva, object->size);
		if (object->allocated)
			bus_dmamem_free(dv3d.dmat, &object->seg, 1);
		memset(object, 0, sizeof(*object));
	}
}

static int
dv3d_populate_page_table(void)
{
	uint32_t *pt = dv3d.obj[DV3D_OBJ_PT].kva;
	bus_addr_t alias_src =
	    dv3d.obj[DV3D_OBJ_ALIAS_SRC].map->dm_segs[0].ds_addr;
	bus_addr_t alias_dst =
	    dv3d.obj[DV3D_OBJ_ALIAS_DST].map->dm_segs[0].ds_addr;
	bus_addr_t actual_src =
	    dv3d.obj[DV3D_OBJ_ACTUAL_SRC].map->dm_segs[0].ds_addr;
	bus_addr_t actual_dst =
	    dv3d.obj[DV3D_OBJ_ACTUAL_DST].map->dm_segs[0].ds_addr;
	uint32_t i;

	/* All 1,048,576 little-endian PTEs start invalid. */
	memset(pt, 0, DV3D_PT_SIZE);
	/*
	 * TFU works on the alias virtual addresses; their PTEs translate to
	 * the separate actual pages. No big or superpages; the table and the
	 * scratch are not mapped as ordinary GPU data.
	 */
	for (i = 0; i < DV3D_IMAGE_PAGES; i++) {
		uint32_t source_pfn = (uint32_t)(actual_src >> DV3D_PAGE_SHIFT) + i;
		uint32_t dest_pfn = (uint32_t)(actual_dst >> DV3D_PAGE_SHIFT) + i;

		if (source_pfn + DV3D_IMAGE_PAGES >= DV3D_PTE_PFN_LIMIT ||
		    dest_pfn + DV3D_IMAGE_PAGES >= DV3D_PTE_PFN_LIMIT)
			return EIO;
		/* Sources are read-only for the GPU. */
		pt[(alias_src >> DV3D_PAGE_SHIFT) + i] =
		    source_pfn | DV3D_PTE_VALID;
		pt[(alias_dst >> DV3D_PAGE_SHIFT) + i] =
		    dest_pfn | DV3D_PTE_VALID | DV3D_PTE_WRITEABLE;
	}
	return 0;
}

static int
dv3d_wait_clear(bus_size_t offset, uint32_t bit)
{
	uint32_t value;
	unsigned int i;
	int error;

	for (i = 0; i < DV3D_CACHE_POLL_COUNT; i++) {
		error = bcmv3d_takeover_hub_peek(offset, &value);
		if (error != 0)
			return error;
		if ((value & bit) == 0)
			return 0;
		delay(DV3D_CACHE_POLL_US);
	}
	return ETIMEDOUT;
}

static int
dv3d_publish_mmu(void)
{
	uint32_t pt_pfn =
	    dv3d.obj[DV3D_OBJ_PT].map->dm_segs[0].ds_addr >> DV3D_PAGE_SHIFT;
	uint32_t scratch_pfn =
	    dv3d.obj[DV3D_OBJ_SCRATCH].map->dm_segs[0].ds_addr >>
	    DV3D_PAGE_SHIFT;
	uint32_t value;
	int error;

	/* From this point on, every allocation stays exposed until reboot. */
	dv3d.published = true;
	error = bcmv3d_takeover_hub_poke(DV3D_MMU_PT_BASE, pt_pfn);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(DV3D_MMU_CTL, DV3D_MMU_CTL_VALUE);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(DV3D_MMU_ILLEGAL,
	    scratch_pfn | DV3D_MMU_ILLEGAL_ENABLE);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_peek(DV3D_MMU_PT_BASE, &value);
	if (error != 0)
		return error;
	if (value != pt_pfn)
		return EIO;
	error = bcmv3d_takeover_hub_peek(DV3D_MMU_CTL, &value);
	if (error != 0)
		return error;
	/* Defined fields only: our value, no fault or clearing bits. */
	if (value != DV3D_MMU_CTL_VALUE)
		return EIO;
	error = bcmv3d_takeover_hub_peek(DV3D_MMU_ILLEGAL, &value);
	if (error != 0)
		return error;
	if (value != (scratch_pfn | DV3D_MMU_ILLEGAL_ENABLE))
		return EIO;
	/* Only the documented MMUC writes ENABLE=1 and FLUSH|ENABLE=3. */
	error = bcmv3d_takeover_hub_poke(DV3D_MMUC_CTL, DV3D_MMUC_ENABLE);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(DV3D_MMUC_CTL,
	    DV3D_MMUC_ENABLE | DV3D_MMUC_FLUSH);
	if (error != 0)
		return error;
	error = dv3d_wait_clear(DV3D_MMUC_CTL, DV3D_MMUC_FLUSHING);
	if (error != 0)
		return error;
	/* A full overwrite avoids reflecting W1C fault bits through RMW. */
	error = bcmv3d_takeover_hub_poke(DV3D_MMU_CTL,
	    DV3D_MMU_CTL_VALUE | DV3D_MMU_CTL_TLB_CLEAR);
	if (error != 0)
		return error;
	error = dv3d_wait_clear(DV3D_MMU_CTL, DV3D_MMU_CTL_TLB_CLEARING);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_peek(DV3D_MMU_CTL, &value);
	if (error != 0)
		return error;
	return value == DV3D_MMU_CTL_VALUE ? 0 : EIO;
}

static int
dv3d_submit_tfu(void)
{
	bus_addr_t alias_src =
	    dv3d.obj[DV3D_OBJ_ALIAS_SRC].map->dm_segs[0].ds_addr;
	bus_addr_t alias_dst =
	    dv3d.obj[DV3D_OBJ_ALIAS_DST].map->dm_segs[0].ds_addr;
	unsigned int i;
	int error;

	/* The pinned submission order; ICFG starts the copy and goes last. */
	error = bcmv3d_takeover_hub_poke(DV3D_TFU_IIA, alias_src);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(DV3D_TFU_IIS, DV3D_TFU_DIM);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(DV3D_TFU_ICA, 0);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(DV3D_TFU_IUA, 0);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(DV3D_TFU_IOA, alias_dst);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(DV3D_TFU_IOC, DV3D_TFU_IOC_VALUE);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(DV3D_TFU_IOS, DV3D_TFU_IOS_VALUE);
	if (error != 0)
		return error;
	for (i = 0; i < 4; i++) {
		error = bcmv3d_takeover_hub_poke(DV3D_TFU_COEF(i), 0);
		if (error != 0)
			return error;
	}
	return bcmv3d_takeover_hub_poke(DV3D_TFU_ICFG, DV3D_TFU_ICFG_VALUE);
}

static int
dv3d_fault_pending(bool *pending)
{
	uint32_t value;
	int error;

	error = bcmv3d_takeover_hub_peek(DV3D_MMU_CTL, &value);
	if (error != 0)
		return error;
	*pending = (value & DV3D_MMU_CTL_FAULTS) != 0;
	return 0;
}

static int
dv3d_watch_completion(void)
{
	uint32_t value;
	uint8_t expected = (dv3d.cvt_before + 1) & DV3D_TFU_CVTCT_MASK;
	bool pending;
	unsigned int i;
	int error;

	for (i = 0; i < DV3D_TFU_POLL_COUNT; i++) {
		error = bcmv3d_takeover_hub_peek(DV3D_TFU_CS, &value);
		if (error != 0)
			return error;
		if ((value & DV3D_TFU_BUSY) == 0 &&
		    dv3d_cvtct(value) == expected) {
			dv3d.cvt_after = dv3d_cvtct(value);
			/* A fault wins over simultaneous completion. */
			error = dv3d_fault_pending(&pending);
			if (error != 0)
				return error;
			return pending ? EFAULT : 0;
		}
		delay(DV3D_TFU_POLL_US);
	}
	return ETIMEDOUT;
}

static void
dv3d_note_mismatch(uint32_t mismatch)
{

	if (!dv3d.have_mismatch) {
		dv3d.have_mismatch = true;
		dv3d.first_mismatch = mismatch;
	}
}

static int
dv3d_verify_translation(void)
{
	uint32_t mismatch = 0;
	enum dv3d_object id;
	bool translated, identity;

	/*
	 * Translation PASS needs the actual-destination to hold the
	 * actual-source pattern while every other protected image keeps
	 * its original contents. Identity addressing copies through the
	 * alias source buffer instead and never reaches the actual
	 * destination; identity or mixed results are separate outcomes
	 * and never count as translation PASS.
	 */
	translated = dv3d_contains_actual_source(DV3D_OBJ_ACTUAL_DST, 0,
	    DV3D_DATA_PAGES, &mismatch);
	if (!translated)
		dv3d_note_mismatch(mismatch);
	identity = !dv3d_buffer_matches(DV3D_OBJ_ALIAS_DST, 0,
	    DV3D_DATA_PAGES, &mismatch);
	if (identity)
		dv3d_note_mismatch(mismatch);
	if (translated && identity)
		dv3d.verdict = "mixed actual and alias destination data";
	else if (identity)
		dv3d.verdict = "identity addressing observed, not translation";
	else if (!translated)
		dv3d.verdict = "actual destination missing source data";
	if (!translated || identity)
		return EIO;
	/* Both sources, the alias destination and the canary page stay. */
	for (id = DV3D_OBJ_ACTUAL_SRC; id <= DV3D_OBJ_ALIAS_DST; id++) {
		uint32_t from = id == DV3D_OBJ_ACTUAL_DST ? DV3D_DATA_PAGES : 0;
		uint32_t pages = id == DV3D_OBJ_ACTUAL_DST ? 1 : DV3D_IMAGE_PAGES;

		if (!dv3d_buffer_matches(id, from, pages, &mismatch)) {
			dv3d.verdict = "protected image modified";
			dv3d_note_mismatch(mismatch);
			return EIO;
		}
	}
	if (!dv3d_scratch_clean()) {
		dv3d.verdict = "scratch page modified";
		return EIO;
	}
	dv3d.verdict = NULL;
	return 0;
}

static const char *
hexfmt(uint32_t value)
{
	static char text[11];

	snprintf(text, sizeof(text), "%#x", value);
	return text;
}

int
bcmv3d_dma_probe(device_t dev, bus_dma_tag_t dmat)
{
	uint32_t debug, masks, tfu_cs;
	bool pending;
	int error;

	if (dv3d.attempted)
		return EBUSY;
	dv3d.attempted = true;
	dv3d.dev = dev;
	dv3d.dmat = dmat;
	dv3d.stage = "awaiting completed takeover";
	if (!bcmv3d_takeover_complete())
		return EPERM;
	dv3d.stage = "MMU geometry";
	error = bcmv3d_takeover_hub_peek(DV3D_MMU_DEBUG, &debug);
	if (error != 0)
		goto out;
	/* A width nibble n encodes 4*(n+3) bits; the probe needs at least 32. */
	if (__SHIFTOUT(debug, DV3D_MMU_PA_WIDTH) < 5 ||
	    __SHIFTOUT(debug, DV3D_MMU_VA_WIDTH) < 5) {
		aprint_normal_dev(dev, "unsuitable MMU geometry %#x\n", debug);
		error = EOPNOTSUPP;
		goto out;
	}
	aprint_normal_dev(dev, "MMU_DEBUG=%#x PA%u VA%u; using the 32-bit "
	    "DMA window only\n", debug,
	    (unsigned int)(4 * (__SHIFTOUT(debug, DV3D_MMU_PA_WIDTH) + 3)),
	    (unsigned int)(4 * (__SHIFTOUT(debug, DV3D_MMU_VA_WIDTH) + 3)));
	dv3d.stage = "retained interrupt masks";
	error = bcmv3d_takeover_hub_peek(DV3D_MASK_STS, &masks);
	if (error != 0)
		goto out;
	if ((masks & DV3D_HUB_IRQS) != DV3D_HUB_IRQS) {
		error = EIO;
		goto out;
	}
	dv3d.stage = "six bounded DMA allocations";
	error = dv3d_allocate();
	if (error != 0)
		goto out;
	dv3d.stage = "deterministic buffer patterns";
	dv3d_fill(DV3D_OBJ_ACTUAL_SRC);
	dv3d_fill(DV3D_OBJ_ALIAS_SRC);
	dv3d_fill(DV3D_OBJ_ACTUAL_DST);
	dv3d_fill(DV3D_OBJ_ALIAS_DST);
	memset(dv3d.obj[DV3D_OBJ_SCRATCH].kva, 0, DV3D_SCRATCH_SIZE);
	dv3d.stage = "translated page table";
	error = dv3d_populate_page_table();
	if (error != 0)
		goto out;
	dv3d.stage = "pre-publication cache synchronization";
	bus_dmamap_sync(dv3d.dmat, dv3d.obj[DV3D_OBJ_ACTUAL_SRC].map, 0,
	    dv3d.obj[DV3D_OBJ_ACTUAL_SRC].size,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(dv3d.dmat, dv3d.obj[DV3D_OBJ_ALIAS_SRC].map, 0,
	    dv3d.obj[DV3D_OBJ_ALIAS_SRC].size,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(dv3d.dmat, dv3d.obj[DV3D_OBJ_ACTUAL_DST].map, 0,
	    dv3d.obj[DV3D_OBJ_ACTUAL_DST].size,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(dv3d.dmat, dv3d.obj[DV3D_OBJ_ALIAS_DST].map, 0,
	    dv3d.obj[DV3D_OBJ_ALIAS_DST].size,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(dv3d.dmat, dv3d.obj[DV3D_OBJ_SCRATCH].map, 0,
	    dv3d.obj[DV3D_OBJ_SCRATCH].size,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(dv3d.dmat, dv3d.obj[DV3D_OBJ_PT].map, 0,
	    dv3d.obj[DV3D_OBJ_PT].size, BUS_DMASYNC_PREWRITE);
	dv3d.stage = "MMU publication";
	error = dv3d_publish_mmu();
	if (error != 0)
		goto out;
	dv3d.stage = "TFU readiness";
	error = bcmv3d_takeover_hub_peek(DV3D_TFU_CS, &tfu_cs);
	if (error != 0)
		goto out;
	if ((tfu_cs & DV3D_TFU_BUSY) != 0) {
		error = EBUSY;
		goto out;
	}
	dv3d.cvt_before = dv3d_cvtct(tfu_cs);
	dv3d.stage = "single translated TFU copy";
	error = dv3d_submit_tfu();
	if (error != 0)
		goto out;
	dv3d.stage = "bounded completion watch";
	error = dv3d_watch_completion();
	if (error != 0)
		goto out;
	dv3d.stage = "post-completion cache synchronization";
	/*
	 * POSTWRITE alone is a no-op in this backend; POSTREAD|POSTWRITE
	 * also exposes an unexpected GPU write hidden in the CPU cache.
	 */
	bus_dmamap_sync(dv3d.dmat, dv3d.obj[DV3D_OBJ_ACTUAL_SRC].map, 0,
	    dv3d.obj[DV3D_OBJ_ACTUAL_SRC].size,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(dv3d.dmat, dv3d.obj[DV3D_OBJ_ALIAS_SRC].map, 0,
	    dv3d.obj[DV3D_OBJ_ALIAS_SRC].size,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(dv3d.dmat, dv3d.obj[DV3D_OBJ_ACTUAL_DST].map, 0,
	    dv3d.obj[DV3D_OBJ_ACTUAL_DST].size,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(dv3d.dmat, dv3d.obj[DV3D_OBJ_ALIAS_DST].map, 0,
	    dv3d.obj[DV3D_OBJ_ALIAS_DST].size,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(dv3d.dmat, dv3d.obj[DV3D_OBJ_SCRATCH].map, 0,
	    dv3d.obj[DV3D_OBJ_SCRATCH].size,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(dv3d.dmat, dv3d.obj[DV3D_OBJ_PT].map, 0,
	    dv3d.obj[DV3D_OBJ_PT].size, BUS_DMASYNC_POSTWRITE);
	dv3d.stage = "translated-copy verification";
	error = dv3d_verify_translation();
	if (error != 0)
		goto out;
	dv3d.stage = "final fault check";
	error = dv3d_fault_pending(&pending);
	if (error == 0 && pending)
		error = EFAULT;
out:
	/* Allocations exposed to the GPU are never released. */
	if (!dv3d.published)
		dv3d_release_unpublished();
	if (error == 0) {
		aprint_normal_dev(dev, "translated DMA PASS: TFU copied the "
		    "actual source pattern through alias addresses; CVTCT "
		    "%u->%u; allocations retained until reboot\n",
		    dv3d.cvt_before, dv3d.cvt_after);
	} else {
		aprint_normal_dev(dev, "DMA experiment stopped at %s: error %d; "
		    "%s%s%s; %s\n", dv3d.stage, error,
		    dv3d.verdict != NULL ? dv3d.verdict :
		    "no verdict recorded",
		    dv3d.have_mismatch ? "; first mismatching word " : "",
		    dv3d.have_mismatch ? hexfmt(dv3d.first_mismatch) : "",
		    dv3d.published ? "GPU-exposed allocations retained until "
		    "reboot; no retry" :
		    "nothing published; DMA allocations released");
	}
	return error;
}
