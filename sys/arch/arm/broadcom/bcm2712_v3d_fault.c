/* Origin: EmberBSD bounded BCM2712 V3D MMU fault recovery experiment, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

/*
 * Register and recovery facts: raspberrypi/linux
 * 43c132e8863c3bff3647033b6a7d2bf87b15501c (v3d_irq.c hub handler:
 * fault-bit acknowledgement, MMU_VIO_ID/MMU_VIO_ADDR diagnostics and the
 * verbatim MMU_CTL write-back that clears the latched write-1-to-clear
 * fault bits; v3d_mmu.c flush), Mesa 26.2.4 v3dx_tfu.c for the accepted
 * payload, and the wire-evidence HUB line identity from the accepted
 * interrupt experiment (_CRS index 0 / GSI 282). This native
 * implementation does not import that code. Only the explicit
 * BCM2712_V3D_FAULT_PROBE configuration includes this file, after a
 * completed takeover and with no other V3D probe enabled: one
 * deliberately faulting TFU job (invalid destination PTE), the pinned
 * recovery, then the identical good job must complete and verify on the
 * same GPU and MMU. Handlers self-mask under a level-line storm;
 * everything GPU-exposed is retained until reboot. Contracts and
 * physical acceptance are pending; this file must not ship in an
 * installable configuration before they pass.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpi_intr.h>
#include <arm/broadcom/bcm2712_v3d_takeover.h>

/* HUB offsets (0x50.. block, MMU as before). */
#define FV3D_MASK_STS		0x5c
#define FV3D_HUB_IRQS_ALL	0x0000007f
#define FV3D_INT_STS		0x50
#define FV3D_INT_CLR		0x58
#define FV3D_MSK_SET		0x60
#define FV3D_MSK_CLR		0x64
#define FV3D_HUB_TFUC		__BIT(1)
#define FV3D_HUB_MMU_CAP	__BIT(3)
#define FV3D_HUB_MMU_PTI	__BIT(4)
#define FV3D_HUB_MMU_WRV	__BIT(5)
#define FV3D_HUB_MMU_FAULTS	(FV3D_HUB_MMU_CAP | FV3D_HUB_MMU_PTI | \
    FV3D_HUB_MMU_WRV)
#define FV3D_MMU_DEBUG		0x1238
#define FV3D_MMU_PA_WIDTH	__BITS(11, 8)
#define FV3D_MMU_VA_WIDTH	__BITS(7, 4)
#define FV3D_MMUC_CTL		0x1000
#define FV3D_MMUC_ENABLE	__BIT(0)
#define FV3D_MMUC_FLUSH		__BIT(1)
#define FV3D_MMUC_FLUSHING	__BIT(2)
#define FV3D_MMU_CTL		0x1200
#define FV3D_MMU_CTL_TLB_CLEAR	__BIT(2)
#define FV3D_MMU_CTL_TLB_CLEARING __BIT(7)
#define FV3D_MMU_CTL_FAULTS	(__BIT(27) | __BIT(20) | __BIT(12))
#define FV3D_MMU_CTL_VALUE	UINT64_C(0x060d0c01)
#define FV3D_MMU_PT_BASE	0x1204
#define FV3D_MMU_ILLEGAL	0x1230
#define FV3D_MMU_ILLEGAL_ENABLE	__BIT(31)
#define FV3D_MMU_VIO_ID		0x122c
#define FV3D_MMU_VIO_ADDR	0x1234
#define FV3D_PAGE_SHIFT		12
#define FV3D_PTE_VALID		__BIT(28)
#define FV3D_PTE_WRITEABLE	__BIT(29)
#define FV3D_PTE_PFN_LIMIT	__BIT(24)
#define FV3D_TFU_CS		0x700
#define FV3D_TFU_CVTCT_SHIFT	16
#define FV3D_TFU_BUSY		__BIT(0)
#define FV3D_TFU_ICFG		0x708
#define FV3D_TFU_ICFG_VALUE	UINT64_C(0x001d0001)
#define FV3D_TFU_IIA		0x70c
#define FV3D_TFU_ICA		0x710
#define FV3D_TFU_IIS		0x714
#define FV3D_TFU_IUA		0x718
#define FV3D_TFU_IOC		0x71c
#define FV3D_TFU_IOC_VALUE	UINT64_C(0x00400000)
#define FV3D_TFU_IOA		0x720
#define FV3D_TFU_IOS		0x724
#define FV3D_TFU_IOS_VALUE	UINT64_C(0x00400040)
#define FV3D_TFU_COEF(n)	(0x728 + (n) * 4)

/* Job sizes (identical to the accepted experiments). */
#define FV3D_DIM		64
#define FV3D_LOG2_TILE		3
#define FV3D_CLEAR_COLOR	UINT64_C(0x305e7b4c)
#define FV3D_IMAGE_PAGES	5
#define FV3D_IMAGE_SIZE		(FV3D_IMAGE_PAGES * PAGE_SIZE)
#define FV3D_OUTPUT_DATA	(FV3D_DIM * FV3D_DIM * 4)
#define FV3D_TILE_STATE_SIZE	PAGE_SIZE
#define FV3D_PT_SIZE		(4 * 1024 * 1024)
#define FV3D_SCRATCH_SIZE	PAGE_SIZE
#define FV3D_DATA_PAGES		4

#define FV3D_WAIT_SLICES	5
#define FV3D_WAIT_SLICE_MS	100
#define FV3D_STORM_LIMIT	4


enum fv3d_object {
	FV3D_OBJ_PT, FV3D_OBJ_SCRATCH,
	FV3D_OBJ_TFU_ASRC, FV3D_OBJ_TFU_ADST, FV3D_OBJ_TFU_LSRC,
	FV3D_OBJ_TFU_LDST, FV3D_OBJ_COUNT
};

struct fv3d_buffer {
	bus_dma_segment_t seg;
	bus_dmamap_t map;
	void *kva;
	bus_size_t size;
	bool allocated, mapped, loaded;
};

static struct {
	device_t dev;
	bus_dma_tag_t dmat;
	struct fv3d_buffer obj[FV3D_OBJ_COUNT];
	bool attempted, published;
	const char *stage, *verdict;
	uint8_t tfu_cvt_before;
	void *hub_ih;
	bool hub_established;
	/* Handler state: volatile, single-word, set at IPL_VM. */
	volatile uint32_t delivered;
	volatile uint32_t spurious;
	uint32_t fault_bits, vio_id, vio_addr;
	uint32_t tfu_cs_after_fault, tfu_cs_before_good;
} fv3d;

int bcmv3d_irq_probe(device_t, bus_dma_tag_t, ACPI_HANDLE);

int bcmv3d_fault_probe(device_t, bus_dma_tag_t, ACPI_HANDLE);

/* --- interrupt handlers: ack only what we handle, then wake -------- */

static int
fv3d_hub_intr(void *arg)
{
	uint32_t status;
	int error;

	(void)arg;
	error = bcmv3d_takeover_hub_peek(FV3D_INT_STS, &status);
	if (error != 0)
		return 0;
	{
		uint32_t ours = status & (FV3D_HUB_TFUC | FV3D_HUB_MMU_FAULTS);

		if (ours == 0) {
			/* A level line we cannot handle must be masked HERE:
			 * waiting-thread logic never runs during a livelock. */
			if (++fv3d.spurious > FV3D_STORM_LIMIT) {
				bcmv3d_takeover_hub_poke(FV3D_MSK_SET,
				    FV3D_HUB_TFUC | FV3D_HUB_MMU_FAULTS);
				wakeup(&fv3d.delivered);
			}
			return 0;
		}
		bcmv3d_takeover_hub_poke(FV3D_INT_CLR, ours);
		fv3d.delivered |= ours;
		wakeup(&fv3d.delivered);
		return 1;
	}
}

/* Wait for the handler to record `bit'; bounded, lost-wakeup safe. */
static int
fv3d_wait_delivered(uint32_t bits)
{
	int slice;

	for (slice = 0; slice < FV3D_WAIT_SLICES; slice++) {
		if ((fv3d.delivered & bits) != 0)
			return 0;
		if (fv3d.spurious > FV3D_STORM_LIMIT)
			return EIO;
		tsleep(&fv3d.delivered, PWAIT, "v3dfault",
		    FV3D_WAIT_SLICE_MS);
	}
	return (fv3d.delivered & bits) != 0 ? 0 : ETIMEDOUT;
}

/* --- packet packing (as in the queue experiment) ------------------- */

/* --- deterministic patterns (as in the DMA experiment) ------------- */

static uint32_t
fv3d_pattern(enum fv3d_object id, uint32_t page, uint32_t word)
{

	switch (id) {
	case FV3D_OBJ_TFU_ASRC:
		if (page >= FV3D_DATA_PAGES)
			return 0xcafe0000u + page * 0x1000u + word;
		return 0xa5a50000u + page * 0x1000u + word;
	case FV3D_OBJ_TFU_LSRC:
		if (page >= FV3D_DATA_PAGES)
			return 0xbeef0000u + page * 0x1000u + word;
		return 0x3c5c0000u + page * 0x4000u + word;
	case FV3D_OBJ_TFU_ADST:
		return 0xdead0000u + page * 0x2000u + word;
	case FV3D_OBJ_TFU_LDST:
		return 0xf00d0000u + page * 0x8000u + word;
	default:
		return 0;
	}
}

/* --- bus_dma objects and MMU publication --------------------------- */

static int
fv3d_allocate(void)
{
	static const bus_size_t sizes[FV3D_OBJ_COUNT] = {
		FV3D_PT_SIZE, FV3D_SCRATCH_SIZE, FV3D_IMAGE_SIZE,
		FV3D_IMAGE_SIZE, FV3D_IMAGE_SIZE, FV3D_IMAGE_SIZE
	};
	struct fv3d_buffer *object;
	enum fv3d_object id, other;
	int nsegs, error;

	for (id = 0; id < FV3D_OBJ_COUNT; id++) {
		object = &fv3d.obj[id];
		object->size = sizes[id];
		error = bus_dmamem_alloc(fv3d.dmat, object->size, PAGE_SIZE, 0,
		    &object->seg, 1, &nsegs, BUS_DMA_WAITOK);
		if (error != 0)
			return error;
		object->allocated = true;
		error = bus_dmamem_map(fv3d.dmat, &object->seg, 1,
		    object->size, &object->kva, BUS_DMA_WAITOK);
		if (error != 0)
			return error;
		object->mapped = true;
		error = bus_dmamap_create(fv3d.dmat, object->size, 1,
		    object->size, 0, BUS_DMA_WAITOK, &object->map);
		if (error != 0)
			return error;
		error = bus_dmamap_load(fv3d.dmat, object->map, object->kva,
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
	for (id = 0; id < FV3D_OBJ_COUNT; id++)
		for (other = id + 1; other < FV3D_OBJ_COUNT; other++)
			if (fv3d.obj[id].map->dm_segs[0].ds_addr <
			    fv3d.obj[other].map->dm_segs[0].ds_addr +
			    fv3d.obj[other].size &&
			    fv3d.obj[other].map->dm_segs[0].ds_addr <
			    fv3d.obj[id].map->dm_segs[0].ds_addr +
			    fv3d.obj[id].size)
				return EIO;
	return 0;
}

static void
fv3d_release_unpublished(void)
{
	struct fv3d_buffer *object;
	enum fv3d_object id;

	for (id = 0; id < FV3D_OBJ_COUNT; id++) {
		object = &fv3d.obj[id];
		if (object->loaded)
			bus_dmamap_unload(fv3d.dmat, object->map);
		if (object->map != NULL)
			bus_dmamap_destroy(fv3d.dmat, object->map);
		if (object->mapped)
			bus_dmamem_unmap(fv3d.dmat, object->kva, object->size);
		if (object->allocated)
			bus_dmamem_free(fv3d.dmat, &object->seg, 1);
		memset(object, 0, sizeof(*object));
	}
}

static int
fv3d_map_alias(enum fv3d_object alias, enum fv3d_object actual,
    bool writeable)
{
	uint32_t *pt = fv3d.obj[FV3D_OBJ_PT].kva;
	bus_addr_t va = fv3d.obj[alias].map->dm_segs[0].ds_addr;
	bus_addr_t pa = fv3d.obj[actual].map->dm_segs[0].ds_addr;
	unsigned int i;

	for (i = 0; i < FV3D_IMAGE_PAGES; i++) {
		uint32_t pfn = (uint32_t)(pa >> FV3D_PAGE_SHIFT) + i;

		if (pfn + FV3D_IMAGE_PAGES >= FV3D_PTE_PFN_LIMIT)
			return EIO;
		pt[(va >> FV3D_PAGE_SHIFT) + i] = pfn | FV3D_PTE_VALID |
		    (writeable ? FV3D_PTE_WRITEABLE : 0);
	}
	return 0;
}

static int
fv3d_publish_mmu(void)
{
	uint32_t pt_pfn =
	    fv3d.obj[FV3D_OBJ_PT].map->dm_segs[0].ds_addr >> FV3D_PAGE_SHIFT;
	uint32_t scratch_pfn =
	    fv3d.obj[FV3D_OBJ_SCRATCH].map->dm_segs[0].ds_addr >>
	    FV3D_PAGE_SHIFT;
	uint32_t value;
	unsigned int i;
	int error;

	fv3d.published = true;
	error = bcmv3d_takeover_hub_poke(FV3D_MMU_PT_BASE, pt_pfn);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(FV3D_MMU_CTL, FV3D_MMU_CTL_VALUE);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(FV3D_MMU_ILLEGAL,
	    scratch_pfn | FV3D_MMU_ILLEGAL_ENABLE);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(FV3D_MMUC_CTL, FV3D_MMUC_ENABLE);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(FV3D_MMUC_CTL,
	    FV3D_MMUC_ENABLE | FV3D_MMUC_FLUSH);
	if (error != 0)
		return error;
	for (i = 0; i < 1000; i++) {
		error = bcmv3d_takeover_hub_peek(FV3D_MMUC_CTL, &value);
		if (error != 0)
			return error;
		if ((value & FV3D_MMUC_FLUSHING) == 0)
			break;
		delay(100);
	}
	if (i == 1000)
		return ETIMEDOUT;
	error = bcmv3d_takeover_hub_poke(FV3D_MMU_CTL,
	    FV3D_MMU_CTL_VALUE | FV3D_MMU_CTL_TLB_CLEAR);
	if (error != 0)
		return error;
	for (i = 0; i < 1000; i++) {
		error = bcmv3d_takeover_hub_peek(FV3D_MMU_CTL, &value);
		if (error != 0)
			return error;
		if ((value & FV3D_MMU_CTL_TLB_CLEARING) == 0)
			break;
		delay(100);
	}
	if (i == 1000)
		return ETIMEDOUT;
	error = bcmv3d_takeover_hub_peek(FV3D_MMU_CTL, &value);
	if (error != 0)
		return error;
	return value == FV3D_MMU_CTL_VALUE ? 0 : EIO;
}

/* --- the probe ------------------------------------------------------ */

static void
fv3d_remask_all(void)
{

	/* Restore the takeover's masks on every exit path. */
	bcmv3d_takeover_hub_poke(FV3D_MSK_SET, FV3D_HUB_IRQS_ALL);
}

static void
fv3d_disestablish(void)
{

	if (fv3d.hub_established) {
		acpi_intr_disestablish(fv3d.hub_ih);
		fv3d.hub_established = false;
	}
}

/* The pinned recovery: verbatim MMU_CTL write-back clears the latched
 * W1C fault bits, then the bounded MMUC flush and full-value TLB clear. */
static int
fv3d_recover_mmu(void)
{
	uint32_t value;
	unsigned int i;
	int error;

	error = bcmv3d_takeover_hub_peek(FV3D_MMU_CTL, &value);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(FV3D_MMU_CTL, value);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(FV3D_MMUC_CTL,
	    FV3D_MMUC_ENABLE | FV3D_MMUC_FLUSH);
	if (error != 0)
		return error;
	for (i = 0; i < 1000; i++) {
		error = bcmv3d_takeover_hub_peek(FV3D_MMUC_CTL, &value);
		if (error != 0)
			return error;
		if ((value & FV3D_MMUC_FLUSHING) == 0)
			break;
		delay(100);
	}
	if (i == 1000)
		return ETIMEDOUT;
	error = bcmv3d_takeover_hub_poke(FV3D_MMU_CTL,
	    FV3D_MMU_CTL_VALUE | FV3D_MMU_CTL_TLB_CLEAR);
	if (error != 0)
		return error;
	for (i = 0; i < 1000; i++) {
		error = bcmv3d_takeover_hub_peek(FV3D_MMU_CTL, &value);
		if (error != 0)
			return error;
		if ((value & FV3D_MMU_CTL_TLB_CLEARING) == 0)
			break;
		delay(100);
	}
	if (i == 1000)
		return ETIMEDOUT;
	error = bcmv3d_takeover_hub_peek(FV3D_MMU_CTL, &value);
	if (error != 0)
		return error;
	return value == FV3D_MMU_CTL_VALUE ? 0 : EIO;
}

int
bcmv3d_fault_probe(device_t dev, bus_dma_tag_t dmat, ACPI_HANDLE handle)
{
	struct acpi_resources res;
	struct acpi_irq *hub_irq;
	uint32_t debug, lsrc, ldst;
	uint32_t vio_id = 0, vio_addr = 0, ctl;
	size_t i, words;
	int error;

	if (fv3d.attempted)
		return EBUSY;
	fv3d.attempted = true;
	fv3d.dev = dev;
	fv3d.dmat = dmat;
	fv3d.stage = "awaiting completed takeover";
	if (!bcmv3d_takeover_complete())
		return EPERM;
	fv3d.stage = "ACPI interrupt resource";
	if (ACPI_FAILURE(acpi_resource_parse(dev, handle, "_CRS", &res,
	    &acpi_resource_parse_ops_quiet))) {
		fv3d.verdict = "cannot parse _CRS";
		error = ENXIO;
		goto out;
	}
	hub_irq = acpi_res_irq(&res, 0);
	if (hub_irq == NULL || hub_irq->ar_irq != 282 ||
	    hub_irq->ar_type != ACPI_LEVEL_SENSITIVE) {
		fv3d.verdict = "unexpected GPU0 interrupt resource";
		error = ENXIO;
		acpi_resource_cleanup(&res);
		goto out;
	}
	fv3d.stage = "establishing handler";
	fv3d.hub_ih = acpi_intr_establish_irq(dev, hub_irq, IPL_VM, true,
	    fv3d_hub_intr, NULL, "v3d hub");
	if (fv3d.hub_ih != NULL)
		fv3d.hub_established = true;
	if (!fv3d.hub_established) {
		fv3d.verdict = "could not establish the hub handler";
		error = ENXIO;
		acpi_resource_cleanup(&res);
		goto out;
	}
	acpi_resource_cleanup(&res);

	fv3d.stage = "MMU geometry";
	error = bcmv3d_takeover_hub_peek(FV3D_MMU_DEBUG, &debug);
	if (error != 0)
		goto out;
	if (__SHIFTOUT(debug, FV3D_MMU_PA_WIDTH) < 5 ||
	    __SHIFTOUT(debug, FV3D_MMU_VA_WIDTH) < 5) {
		fv3d.verdict = "unsuitable MMU geometry";
		error = EOPNOTSUPP;
		goto out;
	}
	fv3d.stage = "clean hub interrupt status";
	error = bcmv3d_takeover_hub_peek(FV3D_INT_STS, &ctl);
	if (error != 0)
		goto out;
	if (ctl != 0) {
		fv3d.verdict = "stale latched interrupt status";
		error = EBUSY;
		goto out;
	}
	fv3d.stage = "six bounded DMA allocations";
	error = fv3d_allocate();
	if (error != 0)
		goto out;
	lsrc = fv3d.obj[FV3D_OBJ_TFU_LSRC].map->dm_segs[0].ds_addr;
	ldst = fv3d.obj[FV3D_OBJ_TFU_LDST].map->dm_segs[0].ds_addr;

	fv3d.stage = "patterns and faulting page table";
	memset(fv3d.obj[FV3D_OBJ_PT].kva, 0, FV3D_PT_SIZE);
	memset(fv3d.obj[FV3D_OBJ_SCRATCH].kva, 0, FV3D_SCRATCH_SIZE);
	words = FV3D_IMAGE_SIZE / 4;
	for (i = 0; i < words; i++) {
		uint32_t page = i / (PAGE_SIZE / 4);

		((uint32_t *)fv3d.obj[FV3D_OBJ_TFU_ASRC].kva)[i] =
		    fv3d_pattern(FV3D_OBJ_TFU_ASRC, page, i % (PAGE_SIZE / 4));
		((uint32_t *)fv3d.obj[FV3D_OBJ_TFU_LSRC].kva)[i] =
		    fv3d_pattern(FV3D_OBJ_TFU_LSRC, page, i % (PAGE_SIZE / 4));
		((uint32_t *)fv3d.obj[FV3D_OBJ_TFU_ADST].kva)[i] =
		    fv3d_pattern(FV3D_OBJ_TFU_ADST, page, i % (PAGE_SIZE / 4));
		((uint32_t *)fv3d.obj[FV3D_OBJ_TFU_LDST].kva)[i] =
		    fv3d_pattern(FV3D_OBJ_TFU_LDST, page, i % (PAGE_SIZE / 4));
	}
	/* Source alias translates; destination alias is deliberately
	 * invalid so the TFU write must fault. */
	if ((error = fv3d_map_alias(FV3D_OBJ_TFU_LSRC, FV3D_OBJ_TFU_ASRC,
	    false)) != 0)
		goto out;
	fv3d.stage = "pre-publication synchronization";
	for (enum fv3d_object id = 0; id < FV3D_OBJ_COUNT; id++)
		bus_dmamap_sync(fv3d.dmat, fv3d.obj[id].map, 0,
		    fv3d.obj[id].size,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	fv3d.stage = "MMU publication";
	error = fv3d_publish_mmu();
	if (error != 0)
		goto out;

	/* Stage 1: the faulting job. */
	fv3d.stage = "faulting TFU submission";
	(void)bcmv3d_takeover_hub_peek(FV3D_TFU_CS, &ctl);
	fv3d.tfu_cvt_before = (ctl >> FV3D_TFU_CVTCT_SHIFT) & 0xff;
	bcmv3d_takeover_hub_poke(FV3D_TFU_IIA, lsrc);
	bcmv3d_takeover_hub_poke(FV3D_TFU_IIS, FV3D_DIM);
	bcmv3d_takeover_hub_poke(FV3D_TFU_ICA, 0);
	bcmv3d_takeover_hub_poke(FV3D_TFU_IUA, 0);
	bcmv3d_takeover_hub_poke(FV3D_TFU_IOA, ldst);
	bcmv3d_takeover_hub_poke(FV3D_TFU_IOC, FV3D_TFU_IOC_VALUE);
	bcmv3d_takeover_hub_poke(FV3D_TFU_IOS, FV3D_TFU_IOS_VALUE);
	bcmv3d_takeover_hub_poke(FV3D_TFU_COEF(0), 0);
	bcmv3d_takeover_hub_poke(FV3D_TFU_COEF(1), 0);
	bcmv3d_takeover_hub_poke(FV3D_TFU_COEF(2), 0);
	bcmv3d_takeover_hub_poke(FV3D_TFU_COEF(3), 0);
	fv3d.stage = "fault interrupt wait";
	bcmv3d_takeover_hub_poke(FV3D_MSK_CLR,
	    FV3D_HUB_TFUC | FV3D_HUB_MMU_FAULTS);
	error = bcmv3d_takeover_hub_poke(FV3D_TFU_ICFG,
	    FV3D_TFU_ICFG_VALUE /* starts the faulting copy */);
	if (error != 0)
		goto out;
	error = fv3d_wait_delivered(FV3D_HUB_MMU_FAULTS | FV3D_HUB_TFUC);
	if (error == ETIMEDOUT) {
		fv3d.verdict = "MMU fault never arrived";
		goto out;
	}
	if (error == EIO) {
		fv3d.verdict = "hub handler storm";
		goto out;
	}
	if ((fv3d.delivered & FV3D_HUB_TFUC) != 0 &&
	    (fv3d.delivered & FV3D_HUB_MMU_FAULTS) == 0) {
		fv3d.verdict = "faulting job completed unexpectedly";
		error = EIO;
		goto out;
	}
	/* Fault diagnostics for the receipt. */
	(void)bcmv3d_takeover_hub_peek(FV3D_MMU_VIO_ID, &vio_id);
	(void)bcmv3d_takeover_hub_peek(FV3D_MMU_VIO_ADDR, &vio_addr);
	(void)bcmv3d_takeover_hub_peek(FV3D_MMU_CTL, &ctl);
	if ((ctl & FV3D_MMU_CTL_FAULTS) == 0) {
		fv3d.verdict = "fault interrupt without MMU_CTL latch";
		error = EIO;
		goto out;
	}
	fv3d.fault_bits = ctl & FV3D_MMU_CTL_FAULTS;
	fv3d.vio_id = vio_id;
	fv3d.vio_addr = vio_addr;
	(void)bcmv3d_takeover_hub_peek(FV3D_TFU_CS, &fv3d.tfu_cs_after_fault);
	/* A faulted job must not have written the destination. */
	bus_dmamap_sync(fv3d.dmat, fv3d.obj[FV3D_OBJ_TFU_ADST].map, 0,
	    fv3d.obj[FV3D_OBJ_TFU_ADST].size,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	{
		const uint32_t *image = fv3d.obj[FV3D_OBJ_TFU_ADST].kva;
		size_t w, n = FV3D_IMAGE_SIZE / 4;

		for (w = 0; w < n; w++) {
			uint32_t page = w / (PAGE_SIZE / 4);

			if (image[w] != fv3d_pattern(FV3D_OBJ_TFU_ADST, page,
			    w % (PAGE_SIZE / 4))) {
				fv3d.verdict = "destination canary modified";
				error = EIO;
				goto out;
			}
		}
	}

	/* Recovery exactly as pinned. */
	fv3d.stage = "MMU fault recovery";
	error = fv3d_recover_mmu();
	if (error != 0) {
		fv3d.verdict = "recovery left the MMU unhealthy";
		goto out;
	}

	/* Stage 2: the good job on the same GPU and MMU. */
	fv3d.stage = "valid destination alias";
	if ((error = fv3d_map_alias(FV3D_OBJ_TFU_LDST, FV3D_OBJ_TFU_ADST,
	    true)) != 0)
		goto out;
	bus_dmamap_sync(fv3d.dmat, fv3d.obj[FV3D_OBJ_PT].map, 0,
	    fv3d.obj[FV3D_OBJ_PT].size, BUS_DMASYNC_PREWRITE);
	error = fv3d_recover_mmu();	/* bounded flush after the fixup */
	if (error != 0) {
		fv3d.verdict = "post-fixup flush failed";
		goto out;
	}
	fv3d.stage = "good TFU submission";
	(void)bcmv3d_takeover_hub_peek(FV3D_TFU_CS, &ctl);
	fv3d.tfu_cs_before_good = ctl;
	if ((ctl & FV3D_TFU_BUSY) != 0) {
		fv3d.verdict = "TFU still busy after the aborted job";
		error = EBUSY;
		goto out;
	}
	fv3d.tfu_cvt_before = (ctl >> FV3D_TFU_CVTCT_SHIFT) & 0xff;
	fv3d.delivered = 0;
	error = bcmv3d_takeover_hub_poke(FV3D_TFU_ICFG,
	    FV3D_TFU_ICFG_VALUE /* starts the good copy */);
	if (error != 0)
		goto out;
	fv3d.stage = "good completion wait";
	error = fv3d_wait_delivered(FV3D_HUB_TFUC);
	bcmv3d_takeover_hub_poke(FV3D_MSK_SET,
	    FV3D_HUB_TFUC | FV3D_HUB_MMU_FAULTS);
	if (error == ETIMEDOUT) {
		fv3d.verdict = "good job never completed after recovery";
		goto out;
	}
	if (error == EIO) {
		fv3d.verdict = "hub handler storm (good job)";
		goto out;
	}
	if ((fv3d.delivered & FV3D_HUB_MMU_FAULTS) != 0) {
		fv3d.verdict = "good job faulted after recovery";
		error = EIO;
		goto out;
	}
	fv3d.stage = "post-completion synchronization";
	for (enum fv3d_object id = 0; id < FV3D_OBJ_COUNT; id++)
		bus_dmamap_sync(fv3d.dmat, fv3d.obj[id].map, 0,
		    fv3d.obj[id].size,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	fv3d.stage = "verification";
	{
		const uint32_t *image = fv3d.obj[FV3D_OBJ_TFU_ADST].kva;

		for (i = 0; i < FV3D_OUTPUT_DATA / 4; i++) {
			uint32_t page = i / (PAGE_SIZE / 4);

			if (image[i] != fv3d_pattern(FV3D_OBJ_TFU_ASRC, page,
			    i % (PAGE_SIZE / 4))) {
				fv3d.verdict = "good image mismatch";
				error = EIO;
				goto out;
			}
		}
		for (i = FV3D_OUTPUT_DATA / 4; i < words; i++) {
			uint32_t page = i / (PAGE_SIZE / 4);

			if (image[i] != fv3d_pattern(FV3D_OBJ_TFU_ADST, page,
			    i % (PAGE_SIZE / 4))) {
				fv3d.verdict = "destination canary modified";
				error = EIO;
				goto out;
			}
		}
	}
out:
	fv3d_remask_all();
	fv3d_disestablish();
	if (!fv3d.published)
		fv3d_release_unpublished();
	if (error == 0) {
		aprint_normal_dev(dev, "fault-recovery PASS: invalid "
		    "destination PTE faulted (MMU_CTL=%#x VIO_ID=%#x "
		    "VIO_ADDR=%#x), the pinned recovery cleared it, and the "
		    "identical good job completed and verified; handler "
		    "disestablished; masks restored\n", fv3d.fault_bits,
		    fv3d.vio_id, fv3d.vio_addr);
	} else {
		uint32_t ctl2 = 0, cs = 0;

		(void)bcmv3d_takeover_hub_peek(FV3D_MMU_CTL, &ctl2);
		(void)bcmv3d_takeover_hub_peek(FV3D_TFU_CS, &cs);
		aprint_normal_dev(dev, "fault experiment stopped at %s: "
		    "error %d; %s; delivered=%#x; postmortem MMU_CTL=%#x "
		    "TFU_CS=%#x after-fault TFU_CS=%#x before-good TFU_CS=%#x "
		    "VIO_ID=%#x VIO_ADDR=%#x; %s\n", fv3d.stage, error,
		    fv3d.verdict != NULL ? fv3d.verdict :
		    "no verdict recorded", fv3d.delivered, ctl2, cs,
		    fv3d.tfu_cs_after_fault, fv3d.tfu_cs_before_good,
		    fv3d.vio_id, fv3d.vio_addr,
		    fv3d.published ? "GPU-exposed allocations retained until "
		    "reboot; no retry" :
		    "nothing published; DMA allocations released");
	}
	return error;
}
