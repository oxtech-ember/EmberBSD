/* Origin: EmberBSD bounded BCM2712 V3D MMU fault recovery experiment, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

/*
 * Register and recovery facts: raspberrypi/linux
 * 43c132e8863c3bff3647033b6a7d2bf87b15501c (v3d_irq.c hub handler:
 * fault-bit acknowledgement, MMU_VIO_ID/MMU_VIO_ADDR diagnostics and the
 * verbatim MMU_CTL write-back that clears the latched write-1-to-clear
 * fault bits; v3d_mmu.c flush), Mesa 26.2.4 v3dx_tfu.c for the accepted
 * payload. V3D 7.1 AXI client IDs are from torvalds/linux
 * a5ebb76233b79db01e82e061173cb73c6d2b5c6b, v3d_irq.c: use the low
 * seven bits, TFU IDs 0x7c..0x7d; the old 4.1 table does not apply.
 * Its v3d_drv.h documents invalid MMU reads/writes redirected to scratch.
 * Also uses the wire-evidence HUB line identity from the accepted
 * interrupt experiment (_CRS index 0 / GSI 282). This native
 * implementation does not import that code. Only the explicit
 * BCM2712_V3D_FAULT_PROBE configuration includes this file, after a
 * completed takeover and with no other V3D probe enabled: one
 * deliberately faulting TFU job (invalid destination PTE), the pinned
 * recovery, then the identical good job must complete and verify on the
 * same GPU and MMU. Handlers self-mask under a level-line storm;
 * everything GPU-exposed is retained until reboot. Actual-source host
 * contracts are checked; one physical invalid-PTE recovery passed on
 * CM5 on 2026-10-11. Keep this bounded experiment opt-in: it provides
 * neither production job lifetime management nor a userspace interface.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <sys/xcall.h>

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
#define FV3D_SCRATCH_SIZE	(3 * PAGE_SIZE)
#define FV3D_SINK_OFFSET	PAGE_SIZE
/* Fixed-payload CM5 observation: the final 128-byte burst reaches the
 * illegal-address sink. Other fault write patterns remain unaccepted. */
#define FV3D_FAULT_SINK_WORDS	32
#define FV3D_DATA_PAGES		4

#define FV3D_WAIT_SLICES	5
#define FV3D_WAIT_SLICE_MS	100
#define FV3D_STORM_LIMIT	4


enum fv3d_object {
	FV3D_OBJ_PT, FV3D_OBJ_SCRATCH,
	FV3D_OBJ_TFU_ASRC, FV3D_OBJ_TFU_ADST, FV3D_OBJ_TFU_LSRC,
	FV3D_OBJ_TFU_LDST, FV3D_OBJ_COUNT
};

enum fv3d_phase {
	FV3D_FAULT_RETIRED, FV3D_RECOVERED, FV3D_GOOD_RETIRED
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
	unsigned int va_width;
	void *hub_ih;
	bool hub_established;
	/* Single HUB producer: cumulative atomics, events published last. */
	volatile uint32_t delivered;
	volatile uint32_t spurious;
	volatile unsigned int events, fault_events, tfuc_events;
	volatile unsigned int irq_error;
	uint32_t fault_bits, vio_id, vio_addr;
	uint32_t fault_hub_bits;
	unsigned int fault_tfuc_events;
	uint32_t tfu_cs_after_fault, tfu_cs_before_good;
	bool after_fault_valid, before_good_valid;
} fv3d;

int bcmv3d_fault_probe(device_t, bus_dma_tag_t, ACPI_HANDLE);

/* --- interrupt handlers: ack only what we handle, then wake -------- */

static void
fv3d_irq_fail(int error)
{

	atomic_cas_uint(&fv3d.irq_error, 0, error);
	/* Best effort even when the register window itself is failing. */
	if (bcmv3d_takeover_hub_poke(FV3D_MSK_SET,
	    FV3D_HUB_IRQS_ALL) != 0)
		atomic_cas_uint(&fv3d.irq_error, 0, EIO);
	wakeup(&fv3d.delivered);
}

static int
fv3d_hub_intr(void *arg)
{
	uint32_t status, ours;
	int error;

	(void)arg;
	error = bcmv3d_takeover_hub_peek(FV3D_INT_STS, &status);
	if (error != 0) {
		fv3d_irq_fail(error);
		return 0;
	}
	ours = status & (FV3D_HUB_TFUC | FV3D_HUB_MMU_FAULTS);
	if ((status & ~ours) != 0) {
		fv3d_irq_fail(EIO);
		return 0;
	}
	if (ours == 0) {
		if (atomic_inc_uint_nv(&fv3d.spurious) > FV3D_STORM_LIMIT)
			fv3d_irq_fail(EIO);
		return 0;
	}
	/* Only one event is expected per job. Mask in the handler before
	 * acknowledging: a stuck level or later duplicate cannot livelock
	 * the CPU. The waiting thread checks raw status before rearming. */
	error = bcmv3d_takeover_hub_poke(FV3D_MSK_SET, FV3D_HUB_IRQS_ALL);
	if (error == 0)
		error = bcmv3d_takeover_hub_poke(FV3D_INT_CLR, ours);
	if (error != 0) {
		fv3d_irq_fail(error);
		return 0;
	}
	atomic_or_uint(&fv3d.delivered, ours);
	if ((ours & FV3D_HUB_MMU_FAULTS) != 0)
		atomic_inc_uint(&fv3d.fault_events);
	if ((ours & FV3D_HUB_TFUC) != 0)
		atomic_inc_uint(&fv3d.tfuc_events);
	membar_producer();
	atomic_inc_uint(&fv3d.events);
	wakeup(&fv3d.delivered);
	return 1;
}

/* The argument is an event count, never an OR of already delivered bits. */
static int
fv3d_wait_events(unsigned int expected)
{
	int slice;

	for (slice = 0; slice < FV3D_WAIT_SLICES; slice++) {
		if (atomic_load_acquire(&fv3d.irq_error) != 0)
			return atomic_load_relaxed(&fv3d.irq_error);
		if (atomic_load_acquire(&fv3d.events) >= expected) {
			membar_consumer();
			return 0;
		}
		tsleep(&fv3d.delivered, PWAIT, "v3dfault",
		    MAX(1, mstohz(FV3D_WAIT_SLICE_MS)));
	}
	if (atomic_load_acquire(&fv3d.irq_error) != 0)
		return atomic_load_relaxed(&fv3d.irq_error);
	membar_consumer();
	return atomic_load_acquire(&fv3d.events) >= expected ? 0 : ETIMEDOUT;
}

/* --- packet packing (as in the queue experiment) ------------------- */

/* --- deterministic patterns (as in the DMA experiment) ------------- */

static uint32_t
fv3d_pattern(enum fv3d_object id, uint32_t page, uint32_t word)
{

	switch (id) {
	case FV3D_OBJ_SCRATCH:
		return page == 1 ? 0 : 0x6b710000u + page * 0x1000u + word;
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

static void
fv3d_fill_scratch(void)
{
	uint32_t *image = fv3d.obj[FV3D_OBJ_SCRATCH].kva;
	size_t word;

	for (word = 0; word < FV3D_SCRATCH_SIZE / 4; word++)
		image[word] = fv3d_pattern(FV3D_OBJ_SCRATCH,
		    word / (PAGE_SIZE / 4), word % (PAGE_SIZE / 4));
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
	    (fv3d.obj[FV3D_OBJ_SCRATCH].map->dm_segs[0].ds_addr +
	    FV3D_SINK_OFFSET) >>
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

static int
fv3d_remask_all(void)
{

	/* Restore the takeover's masks on every exit path. */
	return bcmv3d_takeover_hub_poke(FV3D_MSK_SET, FV3D_HUB_IRQS_ALL);
}

static void
fv3d_disestablish(void)
{

	if (fv3d.hub_established) {
		/* ARM disestablish frees the PIC source without draining an
		 * ISR already running on another CPU. Stop new deliveries,
		 * then wait below hardware IPL before freeing its cookie. */
		acpi_intr_mask(fv3d.hub_ih);
		xc_barrier(0);
		acpi_intr_disestablish(fv3d.hub_ih);
		fv3d.hub_established = false;
	}
}

/* Only the first recovery may acknowledge the captured fault. CM5 can
 * retain its HUB level and report TFUC after that failed conversion has
 * retired. Clear the source before acknowledging these owned indications.
 * The later PTE flush must never consume a new fault. */
static uint32_t
fv3d_retired_status(void)
{

	return fv3d.fault_hub_bits |
	    (fv3d.fault_tfuc_events == 0 ? FV3D_HUB_TFUC : 0);
}

static int
fv3d_recover_mmu(bool acknowledge)
{
	uint32_t value, status, id, addr;
	unsigned int i;
	int error;

	error = bcmv3d_takeover_hub_peek(FV3D_MMU_CTL, &value);
	if (error != 0)
		return error;
	if (acknowledge) {
		if (value != (FV3D_MMU_CTL_VALUE | fv3d.fault_bits))
			return EIO;
		error = bcmv3d_takeover_hub_peek(FV3D_MMU_VIO_ID, &id);
		if (error != 0)
			return error;
		error = bcmv3d_takeover_hub_peek(FV3D_MMU_VIO_ADDR, &addr);
		if (error != 0)
			return error;
		if (id != fv3d.vio_id || addr != fv3d.vio_addr)
			return EIO;
		error = bcmv3d_takeover_hub_poke(FV3D_MMU_CTL, value);
		if (error != 0)
			return error;
		error = bcmv3d_takeover_hub_peek(FV3D_MMU_CTL, &value);
		if (error != 0)
			return error;
	}
	if ((value & FV3D_MMU_CTL_FAULTS) != 0)
		return EIO;
	if (acknowledge) {
		error = bcmv3d_takeover_hub_peek(FV3D_INT_STS, &status);
		if (error != 0)
			return error;
		if ((status & ~fv3d_retired_status()) != 0)
			return EIO;
		error = bcmv3d_takeover_hub_poke(FV3D_INT_CLR, status);
		if (error != 0)
			return error;
	}
	error = bcmv3d_takeover_hub_peek(FV3D_INT_STS, &status);
	if (error != 0)
		return error;
	if (status != 0)
		return EIO;
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

/* Keep device ownership paired across both jobs, including read-only PT. */
static void
fv3d_sync_all(bool before)
{
	enum fv3d_object id;
	int ops;

	for (id = 0; id < FV3D_OBJ_COUNT; id++) {
		ops = before ? BUS_DMASYNC_PREWRITE : BUS_DMASYNC_POSTWRITE;
		if (id != FV3D_OBJ_PT)
			ops |= before ? BUS_DMASYNC_PREREAD : BUS_DMASYNC_POSTREAD;
		bus_dmamap_sync(fv3d.dmat, fv3d.obj[id].map, 0,
		    fv3d.obj[id].size, ops);
	}
}

static int
fv3d_verify_images(bool copied)
{
	enum fv3d_object id;
	size_t word;
	int error = 0;

	for (id = FV3D_OBJ_SCRATCH; id < FV3D_OBJ_COUNT; id++) {
		const uint32_t *image = fv3d.obj[id].kva;
		size_t bad = 0, first = 0, last = 0;
		uint32_t first_value = 0, first_expected = 0;
		uint32_t last_value = 0, last_expected = 0;

		for (word = 0; word < fv3d.obj[id].size / 4; word++) {
			enum fv3d_object pattern_id = id;
			uint32_t expected;

			if (copied && id == FV3D_OBJ_TFU_ADST &&
			    word < FV3D_OUTPUT_DATA / 4)
				pattern_id = FV3D_OBJ_TFU_ASRC;
			expected = fv3d_pattern(pattern_id, word / (PAGE_SIZE / 4),
			    word % (PAGE_SIZE / 4));
			if (!copied && id == FV3D_OBJ_SCRATCH &&
			    word >= FV3D_SINK_OFFSET / 4 &&
			    word < FV3D_SINK_OFFSET / 4 + FV3D_FAULT_SINK_WORDS)
				expected = fv3d_pattern(FV3D_OBJ_TFU_ASRC,
				    FV3D_DATA_PAGES - 1, PAGE_SIZE / 4 -
				    FV3D_FAULT_SINK_WORDS + word -
				    FV3D_SINK_OFFSET / 4);
			if (image[word] != expected) {
				if (bad++ == 0) {
					first = word;
					first_value = image[word];
					first_expected = expected;
				}
				last = word;
				last_value = image[word];
				last_expected = expected;
				fv3d.verdict = id == FV3D_OBJ_TFU_ADST ?
				    "destination image or canary modified" :
				    "protected source, alias or scratch modified";
				error = EIO;
			}
		}
		if (bad != 0)
			aprint_normal_dev(fv3d.dev, "image difference: copied=%u "
			    "object=%u words=%zu first=%zu value=%#x expected=%#x "
			    "last=%zu value=%#x expected=%#x\n",
			    (unsigned int)copied, (unsigned int)id, bad, first,
			    first_value, first_expected, last, last_value,
			    last_expected);
	}
	return error;
}

/* Drain the failed job's terminal indication before job 2 exists. A late
 * TFUC from job 1 must not impersonate job 2's completion after rearm. */
static int
fv3d_wait_fault_terminal(void)
{
	uint32_t status;
	int error, slice;

	/* Read once more after the last permitted sleep interval. */
	for (slice = 0; slice <= FV3D_WAIT_SLICES; slice++) {
		error = bcmv3d_takeover_hub_peek(FV3D_INT_STS, &status);
		if (error != 0)
			return error;
		if ((status & ~fv3d_retired_status()) != 0)
			return EIO;
		if (fv3d.fault_tfuc_events != 0 ||
		    (status & FV3D_HUB_TFUC) != 0)
			return 0;
		if (slice == FV3D_WAIT_SLICES)
			break;
		tsleep(&fv3d.delivered, PWAIT, "v3dretire",
		    MAX(1, mstohz(FV3D_WAIT_SLICE_MS)));
	}
	return ETIMEDOUT;
}

/* Check the hardware and handler observations together, with masks set.
 * On V3D 7.1 CVTCT also counts the failed conversion (physical CM5).
 * Require one retirement for the fault, two after the good job. The failed
 * conversion may also signal TFUC, before or after its fault interrupt.
 * Only the initial phase permits those owned indications under the mask;
 * recovery and good completion require clean HUB and MMU state. */
static int
fv3d_check_event(enum fv3d_phase phase, uint32_t *snapshot, bool *valid)
{
	const bool completed = phase == FV3D_GOOD_RETIRED;
	uint32_t status, cs, ctl, allowed = 0;
	int error;

	error = fv3d_remask_all();
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_peek(FV3D_INT_STS, &status);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_peek(FV3D_TFU_CS, &cs);
	if (error != 0)
		return error;
	if (snapshot != NULL) {
		*snapshot = cs;
		*valid = true;
	}
	if (atomic_load_acquire(&fv3d.irq_error) != 0)
		return atomic_load_relaxed(&fv3d.irq_error);
	if (phase == FV3D_FAULT_RETIRED)
		allowed = fv3d_retired_status();
	if ((status & ~allowed) != 0 || fv3d.fault_tfuc_events > 1 ||
	    atomic_load_acquire(&fv3d.events) != (completed ? 2u : 1u) ||
	    atomic_load_relaxed(&fv3d.fault_events) != 1 ||
	    atomic_load_relaxed(&fv3d.tfuc_events) !=
	    fv3d.fault_tfuc_events + (completed ? 1u : 0u) ||
	    (uint8_t)((cs >> FV3D_TFU_CVTCT_SHIFT) -
	    fv3d.tfu_cvt_before) != (completed ? 2 : 1)) {
		aprint_normal_dev(fv3d.dev, "event snapshot: good=%u "
		    "HUB_STS=%#x TFU_CS=%#x CVTCT baseline=%u "
		    "events=%u fault-events=%u TFUC-events=%u\n",
		    (unsigned int)completed, status, cs,
		    (unsigned int)fv3d.tfu_cvt_before,
		    atomic_load_acquire(&fv3d.events),
		    atomic_load_relaxed(&fv3d.fault_events),
		    atomic_load_relaxed(&fv3d.tfuc_events));
		fv3d.verdict = "unexpected fault/completion event or counter";
		return EIO;
	}
	if (phase != FV3D_FAULT_RETIRED) {
		error = bcmv3d_takeover_hub_peek(FV3D_MMU_CTL, &ctl);
		if (error != 0)
			return error;
		if ((ctl & FV3D_MMU_CTL_FAULTS) != 0) {
			fv3d.verdict = "MMU fault latched after recovery";
			return EIO;
		}
	}
	if ((cs & FV3D_TFU_BUSY) != 0) {
		fv3d.verdict = "TFU remains busy; resubmission is forbidden";
		return EBUSY;
	}
	return 0;
}

/* Capture raw post-mortem values even if a later read fails. Errors are
 * printed beside each value so unavailable registers are not evidence. */
static void
fv3d_diagnostics(void)
{
	uint32_t ctl = 0, cs = 0, id = 0, addr = 0;
	int ctl_error, cs_error, id_error, addr_error;

	ctl_error = bcmv3d_takeover_hub_peek(FV3D_MMU_CTL, &ctl);
	cs_error = bcmv3d_takeover_hub_peek(FV3D_TFU_CS, &cs);
	id_error = bcmv3d_takeover_hub_peek(FV3D_MMU_VIO_ID, &id);
	addr_error = bcmv3d_takeover_hub_peek(FV3D_MMU_VIO_ADDR, &addr);
	aprint_normal_dev(fv3d.dev, "fault post-mortem: MMU_CTL=%#x "
	    "(read error %d) TFU_CS=%#x (read error %d) VIO_ID=%#x "
	    "(read error %d) VIO_ADDR=%#x (read error %d); "
	    "events=%u fault-events=%u TFUC-events=%u; after-fault "
	    "TFU_CS=%#x (valid %u) before-good TFU_CS=%#x (valid %u)\n",
	    ctl, ctl_error, cs, cs_error, id, id_error, addr, addr_error,
	    atomic_load_relaxed(&fv3d.events),
	    atomic_load_relaxed(&fv3d.fault_events),
	    atomic_load_relaxed(&fv3d.tfuc_events), fv3d.tfu_cs_after_fault,
	    (unsigned int)fv3d.after_fault_valid, fv3d.tfu_cs_before_good,
	    (unsigned int)fv3d.before_good_valid);
}

/* Reissue every field for every job, as in pinned v3d_tfu_job_run().
 * A new ICFG alone does not establish a new complete TFU input payload. */
static int
fv3d_setup_tfu(uint32_t lsrc, uint32_t ldst)
{
	const struct { bus_size_t reg; uint32_t value; } setup[] = {
		{ FV3D_TFU_IIA, lsrc }, { FV3D_TFU_IIS, FV3D_DIM },
		{ FV3D_TFU_ICA, 0 }, { FV3D_TFU_IUA, 0 },
		{ FV3D_TFU_IOA, ldst },
		{ FV3D_TFU_IOC, FV3D_TFU_IOC_VALUE },
		{ FV3D_TFU_IOS, FV3D_TFU_IOS_VALUE },
		{ FV3D_TFU_COEF(0), 0 }, { FV3D_TFU_COEF(1), 0 },
		{ FV3D_TFU_COEF(2), 0 }, { FV3D_TFU_COEF(3), 0 }
	};
	size_t i;
	int error;

	for (i = 0; i < __arraycount(setup); i++) {
		error = bcmv3d_takeover_hub_poke(setup[i].reg, setup[i].value);
		if (error != 0)
			return error;
	}
	aprint_normal_dev(fv3d.dev, "TFU payload prepared: source=%#x "
	    "destination=%#x\n", lsrc, ldst);
	return 0;
}

int
bcmv3d_fault_probe(device_t dev, bus_dma_tag_t dmat, ACPI_HANDLE handle)
{
	struct acpi_resources res;
	struct acpi_irq *hub_irq;
	uint32_t debug, lsrc, ldst;
	uint32_t vio_id, vio_addr, ctl;
	uint64_t fault_va;
	size_t i, words;
	int error, mask_error;

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
	/* Pinned Linux v3d_drv.c and v3d_irq.c decode, V3D 7.1. */
	fv3d.va_width = 30 + __SHIFTOUT(debug, FV3D_MMU_VA_WIDTH);
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
	fv3d_fill_scratch();
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
	fv3d_sync_all(true);
	fv3d.stage = "MMU publication";
	error = fv3d_publish_mmu();
	if (error != 0)
		goto out;

	/* Stage 1: the faulting job. No unchecked setup writes. */
	fv3d.stage = "faulting TFU submission";
	error = bcmv3d_takeover_hub_peek(FV3D_TFU_CS, &ctl);
	if (error != 0)
		goto out;
	if ((ctl & FV3D_TFU_BUSY) != 0) {
		fv3d.verdict = "TFU busy before initial submission";
		error = EBUSY;
		goto out;
	}
	fv3d.tfu_cvt_before = (ctl >> FV3D_TFU_CVTCT_SHIFT) & 0xff;
	error = fv3d_setup_tfu(lsrc, ldst);
	if (error != 0)
		goto out;
	fv3d.stage = "fault interrupt wait";
	error = bcmv3d_takeover_hub_poke(FV3D_MSK_CLR,
	    FV3D_HUB_TFUC | FV3D_HUB_MMU_FAULTS);
	if (error != 0)
		goto out;
	error = bcmv3d_takeover_hub_poke(FV3D_TFU_ICFG,
	    FV3D_TFU_ICFG_VALUE /* starts the faulting copy */);
	if (error != 0)
		goto out;
	error = fv3d_wait_events(1);
	if (error != 0) {
		fv3d.verdict = "MMU fault missing or handler failed";
		goto out;
	}
	/* Freeze handler evidence before saving the first-job baseline. */
	acpi_intr_mask(fv3d.hub_ih);
	xc_barrier(0);
	fv3d.fault_hub_bits = atomic_load_relaxed(&fv3d.delivered) &
	    FV3D_HUB_MMU_FAULTS;
	fv3d.fault_tfuc_events = atomic_load_relaxed(&fv3d.tfuc_events);
	if (fv3d.fault_hub_bits == 0) {
		fv3d.verdict = "faulting job retired without a fault interrupt";
		error = EIO;
		goto out;
	}
	/* Decode the fault identity before attempting any recovery. */
	error = bcmv3d_takeover_hub_peek(FV3D_MMU_VIO_ID, &vio_id);
	if (error != 0)
		goto out;
	error = bcmv3d_takeover_hub_peek(FV3D_MMU_VIO_ADDR, &vio_addr);
	if (error != 0)
		goto out;
	error = bcmv3d_takeover_hub_peek(FV3D_MMU_CTL, &ctl);
	if (error != 0)
		goto out;
	if ((ctl & FV3D_MMU_CTL_FAULTS) == 0) {
		fv3d.verdict = "fault interrupt without MMU_CTL latch";
		error = EIO;
		goto out;
	}
	fault_va = (uint64_t)vio_addr << (fv3d.va_width - 32);
	if (((vio_id & 0x7f) != 0x7c && (vio_id & 0x7f) != 0x7d) ||
	    fault_va < ldst ||
	    fault_va >= (uint64_t)ldst + FV3D_IMAGE_SIZE) {
		fv3d.verdict = "fault is not TFU at the destination alias";
		error = EIO;
		goto out;
	}
	fv3d.fault_bits = ctl & FV3D_MMU_CTL_FAULTS;
	fv3d.vio_id = vio_id;
	fv3d.vio_addr = vio_addr;
	error = fv3d_wait_fault_terminal();
	if (error != 0) {
		fv3d.verdict = "fault terminal indication missing or foreign";
		goto out;
	}
	error = fv3d_check_event(FV3D_FAULT_RETIRED, &fv3d.tfu_cs_after_fault,
	    &fv3d.after_fault_valid);
	if (error != 0)
		goto out;
	/* Only an idle engine permits ending device ownership. */
	fv3d_sync_all(false);
	error = fv3d_verify_images(false);
	if (error != 0)
		goto out;

	fv3d.stage = "MMU fault recovery";
	error = fv3d_recover_mmu(true);
	if (error != 0) {
		fv3d.verdict = "recovery left the MMU unhealthy";
		goto out;
	}

	/* Stage 2: fix the CPU-owned table and rearm every allocation. */
	fv3d.stage = "valid destination alias";
	/* Verify before clearing: never erase evidence of a bad fault write.
	 * The next valid job must leave the sink and both canaries intact. */
	fv3d_fill_scratch();
	if ((error = fv3d_map_alias(FV3D_OBJ_TFU_LDST, FV3D_OBJ_TFU_ADST,
	    true)) != 0)
		goto out;
	fv3d_sync_all(true);
	error = fv3d_recover_mmu(false);	/* flush only, no new fault W1C */
	if (error != 0) {
		fv3d.verdict = "post-fixup flush failed";
		goto out;
	}
	fv3d.stage = "good TFU submission";
	/* Revalidate immediately before resubmission, including CVTCT. */
	error = fv3d_check_event(FV3D_RECOVERED, &fv3d.tfu_cs_before_good,
	    &fv3d.before_good_valid);
	if (error != 0)
		goto out;
	error = fv3d_setup_tfu(lsrc, ldst);
	if (error != 0)
		goto out;
	acpi_intr_unmask(fv3d.hub_ih);
	error = bcmv3d_takeover_hub_poke(FV3D_MSK_CLR,
	    FV3D_HUB_TFUC | FV3D_HUB_MMU_FAULTS);
	if (error != 0)
		goto out;
	error = bcmv3d_takeover_hub_poke(FV3D_TFU_ICFG,
	    FV3D_TFU_ICFG_VALUE /* starts the good copy */);
	if (error != 0)
		goto out;
	fv3d.stage = "good completion wait";
	error = fv3d_wait_events(2);
	if (error != 0) {
		fv3d.verdict = "good job missing or handler failed after recovery";
		goto out;
	}
	error = fv3d_check_event(FV3D_GOOD_RETIRED, NULL, NULL);
	if (error != 0)
		goto out;
	fv3d.stage = "post-completion synchronization";
	fv3d_sync_all(false);
	fv3d.stage = "verification";
	error = fv3d_verify_images(true);
out:
	mask_error = fv3d_remask_all();
	if (mask_error != 0) {
		if (error == 0)
			error = mask_error;
		fv3d.verdict = "could not restore HUB interrupt masks";
	}
	fv3d_disestablish();
	if (error == 0) {
		/* No handler can change the final evidence after this point. */
		fv3d.stage = "final masked acceptance boundary";
		error = fv3d_check_event(FV3D_GOOD_RETIRED, NULL, NULL);
	}
	if (error != 0)
		fv3d_diagnostics();
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
		aprint_normal_dev(dev, "fault experiment stopped at %s: "
		    "error %d; %s; delivered=%#x; %s\n", fv3d.stage, error,
		    fv3d.verdict != NULL ? fv3d.verdict :
		    "no verdict recorded", atomic_load_relaxed(&fv3d.delivered),
		    fv3d.published ? "GPU-exposed allocations retained until "
		    "reboot; no retry" :
		    "nothing published; DMA allocations released");
	}
	return error;
}
