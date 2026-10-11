/* Origin: EmberBSD bounded BCM2712 V3D interrupt experiment, 2026-10-10. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

/*
 * Register, packet and interrupt facts: raspberrypi/linux
 * 43c132e8863c3bff3647033b6a7d2bf87b15501c (v3d_{irq,regs,sched,mmu}.c,
 * v3d_gem.c), Mesa 26.2.4 v3d_packet.xml, and the stand firmware's own
 * GPU0 interrupt map (hub GSI 282 at _CRS index 0, core GSI 281 at
 * index 1). This native implementation does not import that code. Only
 * the explicit BCM2712_V3D_IRQ_PROBE configuration includes this file,
 * after a completed takeover and with no other V3D probe enabled: the
 * experiment owns the job lifecycle because a shared polling probe
 * would race the handlers for the same latched status bits. Two
 * handlers, the accepted TFU and clear-and-store payloads, ack-then-wake
 * with bounded waits, storm detection, remask and disestablish on every
 * exit path. Contracts and physical acceptance are pending; this file
 * must not ship in an installable configuration before they pass.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/xcall.h>

#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpi_intr.h>
#include <arm/broadcom/bcm2712_v3d_takeover.h>

/* HUB offsets (0x50.. block, MMU as before). */
#define IV3D_MASK_STS		0x5c
#define IV3D_HUB_IRQS_ALL	0x0000007f
#define IV3D_INT_STS		0x50
#define IV3D_INT_CLR		0x58
#define IV3D_MSK_SET		0x60
#define IV3D_MSK_CLR		0x64
#define IV3D_HUB_TFUC		__BIT(1)
#define IV3D_CORE_FLDONE	__BIT(1)
#define IV3D_CORE_FRDONE	__BIT(0)
#define IV3D_CORE_DONE		(IV3D_CORE_FLDONE | IV3D_CORE_FRDONE)
#define IV3D_MMU_DEBUG		0x1238
#define IV3D_MMU_PA_WIDTH	__BITS(11, 8)
#define IV3D_MMU_VA_WIDTH	__BITS(7, 4)
#define IV3D_MMUC_CTL		0x1000
#define IV3D_MMUC_ENABLE	__BIT(0)
#define IV3D_MMUC_FLUSH		__BIT(1)
#define IV3D_MMUC_FLUSHING	__BIT(2)
#define IV3D_MMU_CTL		0x1200
#define IV3D_MMU_CTL_TLB_CLEAR	__BIT(2)
#define IV3D_MMU_CTL_TLB_CLEARING __BIT(7)
#define IV3D_MMU_CTL_FAULTS	(__BIT(27) | __BIT(20) | __BIT(12))
#define IV3D_MMU_CTL_VALUE	UINT64_C(0x060d0c01)
#define IV3D_MMU_PT_BASE	0x1204
#define IV3D_MMU_ILLEGAL	0x1230
#define IV3D_MMU_ILLEGAL_ENABLE	__BIT(31)
#define IV3D_PAGE_SHIFT		12
#define IV3D_PTE_VALID		__BIT(28)
#define IV3D_PTE_WRITEABLE	__BIT(29)
#define IV3D_PTE_PFN_LIMIT	__BIT(24)

/* CORE submission registers and caches (as in the queue experiment). */
#define IV3D_CTL_SLCACTL	0x24
#define IV3D_SLCACTL_INVALIDATE	UINT64_C(0x0f0f0f0f)
#define IV3D_CTL_L2TCACTL	0x30
#define IV3D_L2TCACTL_FLUSH	UINT64_C(0x00000001)
#define IV3D_CLE_CT0QTS		0x15c
#define IV3D_CT0QTS_ENABLE	__BIT(1)
#define IV3D_CLE_CT0QBA		0x160
#define IV3D_CLE_CT1QBA		0x164
#define IV3D_CLE_CT0QEA		0x168
#define IV3D_CLE_CT1QEA		0x16c
#define IV3D_CLE_CT0QMA		0x170
#define IV3D_CLE_CT0QMS		0x174
#define IV3D_PTB_BPOS		0x30c
#define IV3D_TFU_CS		0x700
#define IV3D_TFU_CVTCT_SHIFT	16
#define IV3D_TFU_BUSY		__BIT(0)
#define IV3D_TFU_ICFG		0x708
#define IV3D_TFU_ICFG_VALUE	UINT64_C(0x001d0001)
#define IV3D_TFU_IIA		0x70c
#define IV3D_TFU_ICA		0x710
#define IV3D_TFU_IIS		0x714
#define IV3D_TFU_IUA		0x718
#define IV3D_TFU_IOC		0x71c
#define IV3D_TFU_IOC_VALUE	UINT64_C(0x00400000)
#define IV3D_TFU_IOA		0x720
#define IV3D_TFU_IOS		0x724
#define IV3D_TFU_IOS_VALUE	UINT64_C(0x00400040)
#define IV3D_TFU_COEF(n)	(0x728 + (n) * 4)

/* Job sizes (identical to the accepted experiments). */
#define IV3D_DIM		64
#define IV3D_LOG2_TILE		3
#define IV3D_CLEAR_COLOR	UINT64_C(0x305e7b4c)
#define IV3D_IMAGE_PAGES	5
#define IV3D_IMAGE_SIZE		(IV3D_IMAGE_PAGES * PAGE_SIZE)
#define IV3D_OUTPUT_DATA	(IV3D_DIM * IV3D_DIM * 4)
#define IV3D_TILE_ALLOC_SIZE	12288
#define IV3D_TILE_STATE_SIZE	PAGE_SIZE
#define IV3D_BCL_SIZE		PAGE_SIZE
#define IV3D_RCL_SIZE		(2 * PAGE_SIZE)
#define IV3D_PT_SIZE		(4 * 1024 * 1024)
#define IV3D_SCRATCH_SIZE	PAGE_SIZE
#define IV3D_DATA_PAGES		4

#define IV3D_WAIT_SLICES	5
#define IV3D_WAIT_SLICE_MS	100
#define IV3D_STORM_LIMIT	4

/* Control-list opcodes (pin: Mesa 26.2.4 v3d_packet.xml). */
#define IV3D_OP_END_OF_RENDERING	13
#define IV3D_OP_FLUSH			4
#define IV3D_OP_START_TILE_BINNING	6
#define IV3D_OP_RETURN			18
#define IV3D_OP_FLUSH_VCD		19
#define IV3D_OP_GENERIC_TILE_LIST	20
#define IV3D_OP_BRANCH_IMPLICIT		21
#define IV3D_OP_SUPERTILE_COORDS	23
#define IV3D_OP_CLEAR_RTS		25
#define IV3D_OP_END_OF_LOADS		26
#define IV3D_OP_END_OF_TILE		27
#define IV3D_OP_STORE			29
#define IV3D_OP_SET_INSTANCEID		54
#define IV3D_OP_PRIM_LIST_FORMAT	56
#define IV3D_OP_OQ_COUNTER		92
#define IV3D_OP_BINNING_CFG		120
#define IV3D_OP_RENDER_CFG		121
#define IV3D_OP_SUPERTILE_CFG		122
#define IV3D_OP_TILE_LIST_BASE		123
#define IV3D_OP_TILE_COORDS		124
#define IV3D_OP_TILE_COORDS_IMPLICIT	125
#define IV3D_OP_TILE_LIST_BLOCK_SIZE	126
#define IV3D_SUB_COMMON			0
#define IV3D_SUB_ZS_CLEAR		1
#define IV3D_SUB_RT_PART1		2
#define IV3D_STORE_BUFFER_RT0		0
#define IV3D_STORE_BUFFER_NONE		8
#define IV3D_STORE_FORMAT_RGBA8		27

enum iv3d_object {
	IV3D_OBJ_PT, IV3D_OBJ_SCRATCH,
	IV3D_OBJ_TFU_ASRC, IV3D_OBJ_TFU_ADST, IV3D_OBJ_TFU_LSRC,
	IV3D_OBJ_TFU_LDST,
	IV3D_OBJ_TILE_ALLOC, IV3D_OBJ_TILE_STATE, IV3D_OBJ_BCL,
	IV3D_OBJ_RCL, IV3D_OBJ_OUTPUT, IV3D_OBJ_COUNT
};

struct iv3d_buffer {
	bus_dma_segment_t seg;
	bus_dmamap_t map;
	void *kva;
	bus_size_t size;
	bool allocated, mapped, loaded;
};

static struct {
	device_t dev;
	bus_dma_tag_t dmat;
	struct iv3d_buffer obj[IV3D_OBJ_COUNT];
	uint8_t bcl_copy[IV3D_BCL_SIZE], rcl_copy[IV3D_RCL_SIZE];
	bool attempted, published;
	const char *stage, *verdict;
	uint8_t tfu_cvt_before;
	void *hub_ih, *core_ih;
	bool hub_established, core_established;
	/* Shared with MPSAFE interrupt handlers, protected at IPL_VM. */
	kmutex_t lock;
	uint32_t hub_delivered, core_delivered;
	uint32_t hub_spurious, core_spurious;
	unsigned int tfuc_count, fldone_count, frdone_count;
	int irq_error;
} iv3d;

int bcmv3d_irq_probe(device_t, bus_dma_tag_t, ACPI_HANDLE);

/* --- interrupt handlers: ack only what we handle, then wake -------- */

static int
iv3d_hub_intr(void *arg)
{
	uint32_t status;
	int error, handled = 0;

	(void)arg;
	mutex_enter(&iv3d.lock);
	error = bcmv3d_takeover_hub_peek(IV3D_INT_STS, &status);
	if (error != 0) {
		iv3d.irq_error = error;
		goto done;
	}
	if ((status & IV3D_HUB_TFUC) == 0) {
		if (++iv3d.hub_spurious > IV3D_STORM_LIMIT)
			iv3d.irq_error = EIO;
		goto done;
	}
	/* Ack only the handled bit, then publish the event and wake. */
	error = bcmv3d_takeover_hub_poke(IV3D_INT_CLR, IV3D_HUB_TFUC);
	if (error != 0)
		iv3d.irq_error = error;
	if (++iv3d.tfuc_count != 1)
		iv3d.irq_error = EIO;
	iv3d.hub_delivered |= IV3D_HUB_TFUC;
	handled = 1;
done:
	/* Bound both spurious lines and repeated handled completions. */
	if (iv3d.irq_error != 0)
		bcmv3d_takeover_hub_poke(IV3D_MSK_SET, IV3D_HUB_TFUC);
	wakeup(&iv3d.hub_delivered);
	mutex_exit(&iv3d.lock);
	return handled;
}

static int
iv3d_core_intr(void *arg)
{
	uint32_t status, ours;
	int error, handled = 0;

	(void)arg;
	mutex_enter(&iv3d.lock);
	error = bcmv3d_takeover_core_peek(IV3D_INT_STS, &status);
	if (error != 0) {
		iv3d.irq_error = error;
		goto done;
	}
	ours = status & IV3D_CORE_DONE;
	if (ours == 0) {
		if (++iv3d.core_spurious > IV3D_STORM_LIMIT)
			iv3d.irq_error = EIO;
		goto done;
	}
	error = bcmv3d_takeover_core_poke(IV3D_INT_CLR, ours);
	if (error != 0)
		iv3d.irq_error = error;
	if ((ours & IV3D_CORE_FLDONE) != 0 && ++iv3d.fldone_count != 1)
		iv3d.irq_error = EIO;
	if ((ours & IV3D_CORE_FRDONE) != 0 && ++iv3d.frdone_count != 1)
		iv3d.irq_error = EIO;
	iv3d.core_delivered |= ours;
	handled = 1;
done:
	if (iv3d.irq_error != 0)
		bcmv3d_takeover_core_poke(IV3D_MSK_SET, IV3D_CORE_DONE);
	wakeup(&iv3d.core_delivered);
	mutex_exit(&iv3d.lock);
	return handled;
}

/* mtsleep registers the waiter before releasing the ISR's spin mutex. */
static int
iv3d_wait_delivered(uint32_t *delivered, uint32_t bit)
{
	int slice, error = ETIMEDOUT;

	mutex_enter(&iv3d.lock);
	for (slice = 0; slice <= IV3D_WAIT_SLICES; slice++) {
		/* An error wins even if a completion was also delivered. */
		if (iv3d.irq_error != 0) {
			error = iv3d.irq_error;
			break;
		}
		if ((*delivered & bit) != 0) {
			error = 0;
			break;
		}
		if (slice == IV3D_WAIT_SLICES)
			break;
		(void)mtsleep(delivered, PWAIT, "v3dirq",
		    MAX(1, mstohz(IV3D_WAIT_SLICE_MS)), &iv3d.lock);
	}
	mutex_exit(&iv3d.lock);
	return error;
}

static int
iv3d_check_events(uint32_t expected_core)
{
	int error;

	mutex_enter(&iv3d.lock);
	error = iv3d.irq_error;
	if (error == 0 && (iv3d.tfuc_count != 1 ||
	    iv3d.hub_delivered != IV3D_HUB_TFUC ||
	    iv3d.core_delivered != expected_core ||
	    iv3d.fldone_count != ((expected_core & IV3D_CORE_FLDONE) != 0) ||
	    iv3d.frdone_count != ((expected_core & IV3D_CORE_FRDONE) != 0)))
		error = EIO;
	mutex_exit(&iv3d.lock);
	return error;
}

static int
iv3d_check_tfu(void)
{
	uint32_t value;
	uint8_t expected = (iv3d.tfu_cvt_before + 1) & 0xff;
	int error;

	error = bcmv3d_takeover_hub_peek(IV3D_TFU_CS, &value);
	if (error != 0)
		return error;
	if ((value & IV3D_TFU_BUSY) != 0 ||
	    ((value >> IV3D_TFU_CVTCT_SHIFT) & 0xff) != expected)
		return EIO;
	error = bcmv3d_takeover_hub_peek(IV3D_MMU_CTL, &value);
	if (error == 0 && (value & IV3D_MMU_CTL_FAULTS) != 0)
		error = EFAULT;
	return error;
}

/* --- packet packing (as in the queue experiment) ------------------- */

struct iv3d_cl {
	uint8_t *base;
	unsigned int bits;
};

static void
iv3d_field(struct iv3d_cl *cl, unsigned int at, unsigned int size,
    uint64_t value)
{
	unsigned int i;

	KASSERT(size <= 64 && (size == 64 || value >> size == 0));
	for (i = 0; i < size; i++)
		if ((value & (UINT64_C(1) << i)) != 0)
			cl->base[(at + i) / 8] |= 1 << ((at + i) % 8);
}

static unsigned int
iv3d_packet(struct iv3d_cl *cl, unsigned int opcode,
    unsigned int payload_bytes)
{
	unsigned int base = cl->bits;

	KASSERT(payload_bytes <= 15 && opcode <= 126);
	cl->base[base / 8] = (uint8_t)opcode;
	memset(cl->base + base / 8 + 1, 0, payload_bytes);
	cl->bits = base + 8 + payload_bytes * 8;
	return base + 8;
}

static void
iv3d_set(struct iv3d_cl *cl, unsigned int origin, unsigned int start,
    unsigned int size, uint64_t value)
{

	iv3d_field(cl, origin + start, size, value);
}

static unsigned int
iv3d_length(const struct iv3d_cl *cl)
{

	return cl->bits / 8;
}

static void
iv3d_build_bcl(struct iv3d_cl *cl)
{
	unsigned int o;

	o = iv3d_packet(cl, IV3D_OP_BINNING_CFG, 8);
	iv3d_set(cl, o, 32, 16, IV3D_DIM - 1);
	iv3d_set(cl, o, 48, 16, IV3D_DIM - 1);
	iv3d_set(cl, o, 8, 3, IV3D_LOG2_TILE);
	iv3d_set(cl, o, 11, 3, IV3D_LOG2_TILE);
	iv3d_set(cl, o, 4, 2, 1);
	iv3d_set(cl, o, 2, 2, 1);
	iv3d_packet(cl, IV3D_OP_FLUSH_VCD, 0);
	o = iv3d_packet(cl, IV3D_OP_OQ_COUNTER, 4);
	iv3d_set(cl, o, 0, 32, 0);
	iv3d_packet(cl, IV3D_OP_START_TILE_BINNING, 0);
	iv3d_packet(cl, IV3D_OP_FLUSH, 0);
}

static void
iv3d_build_rcl(struct iv3d_cl *cl, uint32_t rcl_va, uint32_t output_va,
    uint32_t tile_alloc_va)
{
	unsigned int o, generic_start, generic_end;

	o = iv3d_packet(cl, IV3D_OP_RENDER_CFG, 8);
	iv3d_set(cl, o, 0, 3, IV3D_SUB_COMMON);
	iv3d_set(cl, o, 4, 4, 0);
	iv3d_set(cl, o, 8, 16, IV3D_DIM);
	iv3d_set(cl, o, 24, 16, IV3D_DIM);
	iv3d_set(cl, o, 44, 1, 1);
	iv3d_set(cl, o, 46, 1, 1);
	iv3d_set(cl, o, 52, 3, IV3D_LOG2_TILE);
	iv3d_set(cl, o, 55, 3, IV3D_LOG2_TILE);

	o = iv3d_packet(cl, IV3D_OP_RENDER_CFG, 8);
	iv3d_set(cl, o, 0, 3, IV3D_SUB_RT_PART1);
	iv3d_set(cl, o, 3, 3, 0);
	iv3d_set(cl, o, 7, 11, 0);
	iv3d_set(cl, o, 18, 7, 32 - 1);
	iv3d_set(cl, o, 25, 2, 0);
	iv3d_set(cl, o, 27, 5, 8);
	iv3d_set(cl, o, 32, 32, IV3D_CLEAR_COLOR);

	o = iv3d_packet(cl, IV3D_OP_RENDER_CFG, 8);
	iv3d_set(cl, o, 0, 4, IV3D_SUB_ZS_CLEAR);

	o = iv3d_packet(cl, IV3D_OP_TILE_LIST_BLOCK_SIZE, 1);
	iv3d_set(cl, o, 0, 2, 1);
	iv3d_set(cl, o, 2, 1, 1);

	o = iv3d_packet(cl, IV3D_OP_TILE_LIST_BASE, 4);
	iv3d_set(cl, o, 0, 4, 0);
	iv3d_set(cl, o, 6, 26, tile_alloc_va >> 6);

	o = iv3d_packet(cl, IV3D_OP_SUPERTILE_CFG, 8);
	iv3d_set(cl, o, 0, 8, 0);
	iv3d_set(cl, o, 8, 8, 0);
	iv3d_set(cl, o, 16, 8, 1);
	iv3d_set(cl, o, 24, 8, 1);
	iv3d_set(cl, o, 32, 12, 1);
	iv3d_set(cl, o, 44, 12, 1);
	iv3d_set(cl, o, 61, 3, 0);

	for (unsigned int pass = 0; pass < 2; pass++) {
		o = iv3d_packet(cl, IV3D_OP_TILE_COORDS, 3);
		iv3d_set(cl, o, 0, 12, 0);
		iv3d_set(cl, o, 12, 12, 0);
		iv3d_packet(cl, IV3D_OP_END_OF_LOADS, 0);
		o = iv3d_packet(cl, IV3D_OP_STORE, 12);
		iv3d_set(cl, o, 0, 4, IV3D_STORE_BUFFER_NONE);
		iv3d_packet(cl, IV3D_OP_CLEAR_RTS, 0);
		iv3d_packet(cl, IV3D_OP_END_OF_TILE, 0);
	}
	iv3d_packet(cl, IV3D_OP_FLUSH_VCD, 0);

	generic_start = iv3d_length(cl);
	iv3d_packet(cl, IV3D_OP_TILE_COORDS_IMPLICIT, 0);
	iv3d_packet(cl, IV3D_OP_END_OF_LOADS, 0);
	o = iv3d_packet(cl, IV3D_OP_PRIM_LIST_FORMAT, 1);
	iv3d_set(cl, o, 0, 6, 2);
	o = iv3d_packet(cl, IV3D_OP_SET_INSTANCEID, 4);
	iv3d_set(cl, o, 0, 32, 0);
	o = iv3d_packet(cl, IV3D_OP_BRANCH_IMPLICIT, 1);
	iv3d_set(cl, o, 0, 8, 0);
	o = iv3d_packet(cl, IV3D_OP_STORE, 12);
	iv3d_set(cl, o, 0, 4, IV3D_STORE_BUFFER_RT0);
	iv3d_set(cl, o, 4, 3, 0);
	iv3d_set(cl, o, 12, 6, IV3D_STORE_FORMAT_RGBA8);
	iv3d_set(cl, o, 28, 20, IV3D_DIM * 4);
	iv3d_set(cl, o, 48, 16, IV3D_DIM);
	iv3d_set(cl, o, 64, 32, output_va);
	iv3d_packet(cl, IV3D_OP_END_OF_TILE, 0);
	iv3d_packet(cl, IV3D_OP_RETURN, 0);
	generic_end = iv3d_length(cl);

	o = iv3d_packet(cl, IV3D_OP_GENERIC_TILE_LIST, 8);
	iv3d_set(cl, o, 0, 32, rcl_va + generic_start);
	iv3d_set(cl, o, 32, 32, rcl_va + generic_end);

	o = iv3d_packet(cl, IV3D_OP_SUPERTILE_COORDS, 2);
	iv3d_set(cl, o, 0, 8, 0);
	iv3d_set(cl, o, 8, 8, 0);
	iv3d_packet(cl, IV3D_OP_END_OF_RENDERING, 0);
}

/* --- deterministic patterns (as in the DMA experiment) ------------- */

static uint32_t
iv3d_pattern(enum iv3d_object id, uint32_t page, uint32_t word)
{

	switch (id) {
	case IV3D_OBJ_TFU_ASRC:
		if (page >= IV3D_DATA_PAGES)
			return 0xcafe0000u + page * 0x1000u + word;
		return 0xa5a50000u + page * 0x1000u + word;
	case IV3D_OBJ_TFU_LSRC:
		if (page >= IV3D_DATA_PAGES)
			return 0xbeef0000u + page * 0x1000u + word;
		return 0x3c5c0000u + page * 0x4000u + word;
	case IV3D_OBJ_TFU_ADST:
		return 0xdead0000u + page * 0x2000u + word;
	case IV3D_OBJ_TFU_LDST:
		return 0xf00d0000u + page * 0x8000u + word;
	default:
		return 0;
	}
}

/* --- bus_dma objects and MMU publication --------------------------- */

static int
iv3d_allocate(void)
{
	static const bus_size_t sizes[IV3D_OBJ_COUNT] = {
		IV3D_PT_SIZE, IV3D_SCRATCH_SIZE, IV3D_IMAGE_SIZE,
		IV3D_IMAGE_SIZE, IV3D_IMAGE_SIZE, IV3D_IMAGE_SIZE,
		IV3D_TILE_ALLOC_SIZE, IV3D_TILE_STATE_SIZE, IV3D_BCL_SIZE,
		IV3D_RCL_SIZE, IV3D_IMAGE_SIZE
	};
	struct iv3d_buffer *object;
	enum iv3d_object id, other;
	int nsegs, error;

	for (id = 0; id < IV3D_OBJ_COUNT; id++) {
		object = &iv3d.obj[id];
		object->size = sizes[id];
		error = bus_dmamem_alloc(iv3d.dmat, object->size, PAGE_SIZE, 0,
		    &object->seg, 1, &nsegs, BUS_DMA_WAITOK);
		if (error != 0)
			return error;
		object->allocated = true;
		error = bus_dmamem_map(iv3d.dmat, &object->seg, 1,
		    object->size, &object->kva, BUS_DMA_WAITOK);
		if (error != 0)
			return error;
		object->mapped = true;
		error = bus_dmamap_create(iv3d.dmat, object->size, 1,
		    object->size, 0, BUS_DMA_WAITOK, &object->map);
		if (error != 0)
			return error;
		error = bus_dmamap_load(iv3d.dmat, object->map, object->kva,
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
	for (id = 0; id < IV3D_OBJ_COUNT; id++)
		for (other = id + 1; other < IV3D_OBJ_COUNT; other++)
			if (iv3d.obj[id].map->dm_segs[0].ds_addr <
			    iv3d.obj[other].map->dm_segs[0].ds_addr +
			    iv3d.obj[other].size &&
			    iv3d.obj[other].map->dm_segs[0].ds_addr <
			    iv3d.obj[id].map->dm_segs[0].ds_addr +
			    iv3d.obj[id].size)
				return EIO;
	return 0;
}

static void
iv3d_release_unpublished(void)
{
	struct iv3d_buffer *object;
	enum iv3d_object id;

	for (id = 0; id < IV3D_OBJ_COUNT; id++) {
		object = &iv3d.obj[id];
		if (object->loaded)
			bus_dmamap_unload(iv3d.dmat, object->map);
		if (object->map != NULL)
			bus_dmamap_destroy(iv3d.dmat, object->map);
		if (object->mapped)
			bus_dmamem_unmap(iv3d.dmat, object->kva, object->size);
		if (object->allocated)
			bus_dmamem_free(iv3d.dmat, &object->seg, 1);
		memset(object, 0, sizeof(*object));
	}
}

static int
iv3d_map_alias(enum iv3d_object alias, enum iv3d_object actual,
    bool writeable)
{
	uint32_t *pt = iv3d.obj[IV3D_OBJ_PT].kva;
	bus_addr_t va = iv3d.obj[alias].map->dm_segs[0].ds_addr;
	bus_addr_t pa = iv3d.obj[actual].map->dm_segs[0].ds_addr;
	unsigned int i;

	for (i = 0; i < IV3D_IMAGE_PAGES; i++) {
		uint32_t pfn = (uint32_t)(pa >> IV3D_PAGE_SHIFT) + i;

		if (pfn + IV3D_IMAGE_PAGES >= IV3D_PTE_PFN_LIMIT)
			return EIO;
		pt[(va >> IV3D_PAGE_SHIFT) + i] = pfn | IV3D_PTE_VALID |
		    (writeable ? IV3D_PTE_WRITEABLE : 0);
	}
	return 0;
}

static int
iv3d_map_identity(enum iv3d_object id, bool writeable, unsigned int pages)
{
	uint32_t *pt = iv3d.obj[IV3D_OBJ_PT].kva;
	bus_addr_t va = iv3d.obj[id].map->dm_segs[0].ds_addr;
	unsigned int i;

	for (i = 0; i < pages; i++) {
		uint32_t pfn = (uint32_t)(va >> IV3D_PAGE_SHIFT) + i;

		if (pfn + pages >= IV3D_PTE_PFN_LIMIT)
			return EIO;
		pt[(va >> IV3D_PAGE_SHIFT) + i] = pfn | IV3D_PTE_VALID |
		    (writeable ? IV3D_PTE_WRITEABLE : 0);
	}
	return 0;
}

static int
iv3d_publish_mmu(void)
{
	uint32_t pt_pfn =
	    iv3d.obj[IV3D_OBJ_PT].map->dm_segs[0].ds_addr >> IV3D_PAGE_SHIFT;
	uint32_t scratch_pfn =
	    iv3d.obj[IV3D_OBJ_SCRATCH].map->dm_segs[0].ds_addr >>
	    IV3D_PAGE_SHIFT;
	uint32_t value;
	unsigned int i;
	int error;

	iv3d.published = true;
	error = bcmv3d_takeover_hub_poke(IV3D_MMU_PT_BASE, pt_pfn);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(IV3D_MMU_CTL, IV3D_MMU_CTL_VALUE);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(IV3D_MMU_ILLEGAL,
	    scratch_pfn | IV3D_MMU_ILLEGAL_ENABLE);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(IV3D_MMUC_CTL, IV3D_MMUC_ENABLE);
	if (error != 0)
		return error;
	error = bcmv3d_takeover_hub_poke(IV3D_MMUC_CTL,
	    IV3D_MMUC_ENABLE | IV3D_MMUC_FLUSH);
	if (error != 0)
		return error;
	for (i = 0; i < 1000; i++) {
		error = bcmv3d_takeover_hub_peek(IV3D_MMUC_CTL, &value);
		if (error != 0)
			return error;
		if ((value & IV3D_MMUC_FLUSHING) == 0)
			break;
		delay(100);
	}
	if (i == 1000)
		return ETIMEDOUT;
	error = bcmv3d_takeover_hub_poke(IV3D_MMU_CTL,
	    IV3D_MMU_CTL_VALUE | IV3D_MMU_CTL_TLB_CLEAR);
	if (error != 0)
		return error;
	for (i = 0; i < 1000; i++) {
		error = bcmv3d_takeover_hub_peek(IV3D_MMU_CTL, &value);
		if (error != 0)
			return error;
		if ((value & IV3D_MMU_CTL_TLB_CLEARING) == 0)
			break;
		delay(100);
	}
	if (i == 1000)
		return ETIMEDOUT;
	error = bcmv3d_takeover_hub_peek(IV3D_MMU_CTL, &value);
	if (error != 0)
		return error;
	return value == IV3D_MMU_CTL_VALUE ? 0 : EIO;
}

static int
iv3d_invalidate_caches(void)
{
	int error;

	error = bcmv3d_takeover_core_poke(IV3D_CTL_L2TCACTL,
	    IV3D_L2TCACTL_FLUSH);
	if (error != 0)
		return error;
	return bcmv3d_takeover_core_poke(IV3D_CTL_SLCACTL,
	    IV3D_SLCACTL_INVALIDATE);
}

/* --- the probe ------------------------------------------------------ */

static int
iv3d_remask_all(void)
{
	int hub_error, core_error;

	/* Attempt both blocks even when one write fails. */
	hub_error = bcmv3d_takeover_hub_poke(IV3D_MSK_SET, IV3D_HUB_IRQS_ALL);
	core_error = bcmv3d_takeover_core_poke(IV3D_MSK_SET, IV3D_CORE_DONE);
	return hub_error != 0 ? hub_error : core_error;
}

static void
iv3d_disestablish(void)
{

	/* ARM PIC disestablish does not wait for an in-flight dispatcher. */
	if (iv3d.hub_established)
		acpi_intr_mask(iv3d.hub_ih);
	if (iv3d.core_established)
		acpi_intr_mask(iv3d.core_ih);
	/* Wait below IPL_VM, without the IRQ mutex, before freeing handlers. */
	if (iv3d.hub_established || iv3d.core_established)
		xc_barrier(0);
	if (iv3d.hub_established) {
		acpi_intr_disestablish(iv3d.hub_ih);
		iv3d.hub_established = false;
	}
	if (iv3d.core_established) {
		acpi_intr_disestablish(iv3d.core_ih);
		iv3d.core_established = false;
	}
}

int
bcmv3d_irq_probe(device_t dev, bus_dma_tag_t dmat, ACPI_HANDLE handle)
{
	struct acpi_resources res;
	struct acpi_irq *core_irq, *hub_irq;
	struct iv3d_cl bcl, rcl;
	uint32_t debug, tfu_cs;
	uint32_t lsrc, ldst, out_va, tile_alloc_va;
	uint32_t bcl_va, rcl_va, tile_state_va;
	const uint32_t *image;
	size_t words, i;
	int error, mask_error;

	if (iv3d.attempted)
		return EBUSY;
	iv3d.attempted = true;
	iv3d.dev = dev;
	iv3d.dmat = dmat;
	iv3d.stage = "awaiting completed takeover";
	if (!bcmv3d_takeover_complete())
		return EPERM;
	mutex_init(&iv3d.lock, MUTEX_DEFAULT, IPL_VM);
	iv3d.stage = "ACPI interrupt resources";
	if (ACPI_FAILURE(acpi_resource_parse(dev, handle, "_CRS", &res,
	    &acpi_resource_parse_ops_quiet))) {
		iv3d.verdict = "cannot parse _CRS";
		error = ENXIO;
		goto out;
	}
	/*
	 * The firmware header names index 0 (GSI 282) "core" and index 1
	 * (GSI 281) "hub", but the wire says otherwise: the TFU completion
	 * (a HUB-only event) arrives on GSI 282. Register by evidence:
	 * hub handler on index 0 / GSI 282, core handler on index 1 / 281.
	 */
	hub_irq = acpi_res_irq(&res, 0);
	core_irq = acpi_res_irq(&res, 1);
	if (core_irq == NULL || hub_irq == NULL ||
	    hub_irq->ar_irq != 282 || core_irq->ar_irq != 281 ||
	    core_irq->ar_type != ACPI_LEVEL_SENSITIVE ||
	    hub_irq->ar_type != ACPI_LEVEL_SENSITIVE) {
		iv3d.verdict = "unexpected GPU0 interrupt resources";
		error = ENXIO;
		acpi_resource_cleanup(&res);
		goto out;
	}
	iv3d.stage = "establishing handlers";
	iv3d.hub_ih = acpi_intr_establish_irq(dev, hub_irq, IPL_VM, true,
	    iv3d_hub_intr, NULL, "v3d hub");
	if (iv3d.hub_ih != NULL)
		iv3d.hub_established = true;
	iv3d.core_ih = acpi_intr_establish_irq(dev, core_irq, IPL_VM, true,
	    iv3d_core_intr, NULL, "v3d core");
	if (iv3d.core_ih != NULL)
		iv3d.core_established = true;
	if (!iv3d.core_established || !iv3d.hub_established) {
		/* Whatever was established is disestablished on exit. */
		iv3d.verdict = "could not establish a handler";
		error = ENXIO;
		acpi_resource_cleanup(&res);
		goto out;
	}
	acpi_resource_cleanup(&res);

	iv3d.stage = "MMU geometry";
	error = bcmv3d_takeover_hub_peek(IV3D_MMU_DEBUG, &debug);
	if (error != 0)
		goto out;
	if (__SHIFTOUT(debug, IV3D_MMU_PA_WIDTH) < 5 ||
	    __SHIFTOUT(debug, IV3D_MMU_VA_WIDTH) < 5) {
		iv3d.verdict = "unsuitable MMU geometry";
		error = EOPNOTSUPP;
		goto out;
	}
	iv3d.stage = "clean interrupt status";
	{
		uint32_t hub_sts, core_sts;

		error = bcmv3d_takeover_hub_peek(IV3D_INT_STS, &hub_sts);
		if (error == 0)
			error = bcmv3d_takeover_core_peek(IV3D_INT_STS,
			    &core_sts);
		if (error != 0)
			goto out;
		if (hub_sts != 0 || core_sts != 0) {
			iv3d.verdict = "stale latched interrupt status";
			error = EBUSY;
			goto out;
		}
	}
	iv3d.stage = "eleven bounded DMA allocations";
	error = iv3d_allocate();
	if (error != 0)
		goto out;
	lsrc = iv3d.obj[IV3D_OBJ_TFU_LSRC].map->dm_segs[0].ds_addr;
	ldst = iv3d.obj[IV3D_OBJ_TFU_LDST].map->dm_segs[0].ds_addr;
	out_va = iv3d.obj[IV3D_OBJ_OUTPUT].map->dm_segs[0].ds_addr;
	tile_alloc_va =
	    iv3d.obj[IV3D_OBJ_TILE_ALLOC].map->dm_segs[0].ds_addr;
	tile_state_va =
	    iv3d.obj[IV3D_OBJ_TILE_STATE].map->dm_segs[0].ds_addr;
	bcl_va = iv3d.obj[IV3D_OBJ_BCL].map->dm_segs[0].ds_addr;
	rcl_va = iv3d.obj[IV3D_OBJ_RCL].map->dm_segs[0].ds_addr;

	iv3d.stage = "patterns, control lists and page table";
	memset(iv3d.obj[IV3D_OBJ_PT].kva, 0, IV3D_PT_SIZE);
	memset(iv3d.obj[IV3D_OBJ_SCRATCH].kva, 0, IV3D_SCRATCH_SIZE);
	memset(iv3d.obj[IV3D_OBJ_OUTPUT].kva, 0, IV3D_IMAGE_SIZE);
	memset(iv3d.obj[IV3D_OBJ_TILE_ALLOC].kva, 0, IV3D_TILE_ALLOC_SIZE);
	memset(iv3d.obj[IV3D_OBJ_TILE_STATE].kva, 0, IV3D_TILE_STATE_SIZE);
	for (i = 0; i < IV3D_IMAGE_SIZE / 4; i++) {
		uint32_t page = i / (PAGE_SIZE / 4);

		((uint32_t *)iv3d.obj[IV3D_OBJ_TFU_ASRC].kva)[i] =
		    iv3d_pattern(IV3D_OBJ_TFU_ASRC, page, i % (PAGE_SIZE / 4));
		((uint32_t *)iv3d.obj[IV3D_OBJ_TFU_LSRC].kva)[i] =
		    iv3d_pattern(IV3D_OBJ_TFU_LSRC, page, i % (PAGE_SIZE / 4));
		((uint32_t *)iv3d.obj[IV3D_OBJ_TFU_ADST].kva)[i] =
		    iv3d_pattern(IV3D_OBJ_TFU_ADST, page, i % (PAGE_SIZE / 4));
		((uint32_t *)iv3d.obj[IV3D_OBJ_TFU_LDST].kva)[i] =
		    iv3d_pattern(IV3D_OBJ_TFU_LDST, page, i % (PAGE_SIZE / 4));
	}
	memset(iv3d.obj[IV3D_OBJ_BCL].kva, 0, IV3D_BCL_SIZE);
	memset(iv3d.obj[IV3D_OBJ_RCL].kva, 0, IV3D_RCL_SIZE);
	bcl.base = iv3d.obj[IV3D_OBJ_BCL].kva;
	bcl.bits = 0;
	iv3d_build_bcl(&bcl);
	rcl.base = iv3d.obj[IV3D_OBJ_RCL].kva;
	rcl.bits = 0;
	iv3d_build_rcl(&rcl, rcl_va, out_va, tile_alloc_va);
	KASSERT(iv3d_length(&bcl) <= IV3D_BCL_SIZE);
	KASSERT(iv3d_length(&rcl) <= IV3D_RCL_SIZE);
	memcpy(iv3d.bcl_copy, bcl.base, sizeof(iv3d.bcl_copy));
	memcpy(iv3d.rcl_copy, rcl.base, sizeof(iv3d.rcl_copy));
	/* TFU alias entries and queue identity entries share one table. */
	if ((error = iv3d_map_alias(IV3D_OBJ_TFU_LSRC, IV3D_OBJ_TFU_ASRC,
	    false)) != 0 ||
	    (error = iv3d_map_alias(IV3D_OBJ_TFU_LDST, IV3D_OBJ_TFU_ADST,
	    true)) != 0 ||
	    (error = iv3d_map_identity(IV3D_OBJ_BCL, false, 1)) != 0 ||
	    (error = iv3d_map_identity(IV3D_OBJ_RCL, false, 2)) != 0 ||
	    (error = iv3d_map_identity(IV3D_OBJ_TILE_ALLOC, true, 3)) != 0 ||
	    (error = iv3d_map_identity(IV3D_OBJ_TILE_STATE, true, 1)) != 0 ||
	    (error = iv3d_map_identity(IV3D_OBJ_OUTPUT, true,
	    IV3D_IMAGE_PAGES)) != 0)
		goto out;
	iv3d.stage = "pre-publication synchronization";
	for (enum iv3d_object id = 0; id < IV3D_OBJ_COUNT; id++)
		bus_dmamap_sync(iv3d.dmat, iv3d.obj[id].map, 0,
		    iv3d.obj[id].size,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	iv3d.stage = "MMU publication";
	error = iv3d_publish_mmu();
	if (error != 0)
		goto out;

	/* Stage 1: the accepted TFU copy, completion by hub interrupt. */
	iv3d.stage = "TFU submission";
	error = bcmv3d_takeover_hub_peek(IV3D_TFU_CS, &tfu_cs);
	if (error != 0)
		goto out;
	if ((tfu_cs & IV3D_TFU_BUSY) != 0) {
		iv3d.verdict = "TFU busy";
		error = EBUSY;
		goto out;
	}
	iv3d.tfu_cvt_before = (tfu_cs >> IV3D_TFU_CVTCT_SHIFT) & 0xff;
	{
		const struct {
			bus_size_t offset;
			uint32_t value;
		} setup[] = {
			{ IV3D_TFU_IIA, lsrc }, { IV3D_TFU_IIS, IV3D_DIM },
			{ IV3D_TFU_ICA, 0 }, { IV3D_TFU_IUA, 0 },
			{ IV3D_TFU_IOA, ldst }, { IV3D_TFU_IOC, IV3D_TFU_IOC_VALUE },
			{ IV3D_TFU_IOS, IV3D_TFU_IOS_VALUE },
			{ IV3D_TFU_COEF(0), 0 }, { IV3D_TFU_COEF(1), 0 },
			{ IV3D_TFU_COEF(2), 0 }, { IV3D_TFU_COEF(3), 0 }
		};

		for (i = 0; i < __arraycount(setup); i++) {
			error = bcmv3d_takeover_hub_poke(setup[i].offset,
			    setup[i].value);
			if (error != 0)
				goto out;
		}
	}
	iv3d.stage = "TFU interrupt wait";
	error = bcmv3d_takeover_hub_poke(IV3D_MSK_CLR, IV3D_HUB_TFUC);
	if (error != 0)
		goto out;
	error = bcmv3d_takeover_hub_poke(IV3D_TFU_ICFG,
	    IV3D_TFU_ICFG_VALUE /* starts the copy */);
	if (error != 0)
		goto out;
	error = iv3d_wait_delivered(&iv3d.hub_delivered, IV3D_HUB_TFUC);
	mask_error = bcmv3d_takeover_hub_poke(IV3D_MSK_SET, IV3D_HUB_TFUC);
	if (error == 0)
		error = mask_error;
	if (error == ETIMEDOUT) {
		iv3d.verdict = "hub TFUC never arrived";
		goto out;
	}
	if (error != 0) {
		iv3d.verdict = "hub interrupt delivery failed";
		goto out;
	}

	iv3d.stage = "TFU completion verification";
	error = iv3d_check_events(0);
	if (error == 0)
		error = iv3d_check_tfu();
	if (error != 0) {
		iv3d.verdict = "TFU completion state is invalid";
		goto out;
	}

	/* Stage 2: the accepted clear-and-store, FLDONE and FRDONE by IRQ. */
	iv3d.stage = "binner submission";
	error = bcmv3d_takeover_core_poke(IV3D_PTB_BPOS, 0);
	if (error != 0)
		goto out;
	error = iv3d_invalidate_caches();
	if (error != 0)
		goto out;
	{
		const struct {
			bus_size_t offset;
			uint32_t value;
		} setup[] = {
			{ IV3D_CLE_CT0QMA, tile_alloc_va },
			{ IV3D_CLE_CT0QMS, IV3D_TILE_ALLOC_SIZE },
			{ IV3D_CLE_CT0QTS, IV3D_CT0QTS_ENABLE | tile_state_va },
			{ IV3D_CLE_CT0QBA, bcl_va }
		};

		for (i = 0; i < __arraycount(setup); i++) {
			error = bcmv3d_takeover_core_poke(setup[i].offset,
			    setup[i].value);
			if (error != 0)
				goto out;
		}
	}
	error = bcmv3d_takeover_core_poke(IV3D_CLE_CT0QEA,
	    bcl_va + iv3d_length(&bcl));
	if (error != 0)
		goto out;
	iv3d.stage = "FLDONE interrupt wait";
	error = bcmv3d_takeover_core_poke(IV3D_MSK_CLR, IV3D_CORE_DONE);
	if (error != 0)
		goto out;
	error = iv3d_wait_delivered(&iv3d.core_delivered, IV3D_CORE_FLDONE);
	if (error == ETIMEDOUT) {
		iv3d.verdict = "core FLDONE never arrived";
		goto out;
	}
	if (error != 0) {
		iv3d.verdict = "binner interrupt delivery failed";
		goto out;
	}
	error = iv3d_check_events(IV3D_CORE_FLDONE);
	if (error != 0) {
		iv3d.verdict = "unexpected completion before rendering";
		goto out;
	}
	iv3d.stage = "render submission";
	error = iv3d_invalidate_caches();
	if (error != 0)
		goto out;
	error = bcmv3d_takeover_core_poke(IV3D_CLE_CT1QBA, rcl_va);
	if (error != 0)
		goto out;
	error = bcmv3d_takeover_core_poke(IV3D_CLE_CT1QEA,
	    rcl_va + iv3d_length(&rcl));
	if (error != 0)
		goto out;
	iv3d.stage = "FRDONE interrupt wait";
	error = iv3d_wait_delivered(&iv3d.core_delivered, IV3D_CORE_FRDONE);
	mask_error = bcmv3d_takeover_core_poke(IV3D_MSK_SET, IV3D_CORE_DONE);
	if (error == 0)
		error = mask_error;
	if (error == ETIMEDOUT) {
		iv3d.verdict = "core FRDONE never arrived";
		goto out;
	}
	if (error != 0) {
		iv3d.verdict = "render interrupt delivery failed";
		goto out;
	}
	iv3d.stage = "post-completion synchronization";
	for (enum iv3d_object id = 0; id < IV3D_OBJ_COUNT; id++)
		bus_dmamap_sync(iv3d.dmat, iv3d.obj[id].map, 0,
		    iv3d.obj[id].size,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	iv3d.stage = "verification";
	if (memcmp(iv3d.bcl_copy, iv3d.obj[IV3D_OBJ_BCL].kva,
	    sizeof(iv3d.bcl_copy)) != 0 ||
	    memcmp(iv3d.rcl_copy, iv3d.obj[IV3D_OBJ_RCL].kva,
	    sizeof(iv3d.rcl_copy)) != 0) {
		iv3d.verdict = "control list or padding modified by the GPU";
		error = EIO;
		goto out;
	}
	/* Both sources, alias destination and TFU padding must be unchanged. */
	for (enum iv3d_object id = IV3D_OBJ_TFU_ASRC;
	    id <= IV3D_OBJ_TFU_LDST; id++) {
		image = iv3d.obj[id].kva;
		i = id == IV3D_OBJ_TFU_ADST ? IV3D_OUTPUT_DATA / 4 : 0;
		for (; i < IV3D_IMAGE_SIZE / 4; i++) {
			uint32_t page = i / (PAGE_SIZE / 4);

			if (image[i] != iv3d_pattern(id, page, i % (PAGE_SIZE / 4))) {
				iv3d.verdict = "protected TFU buffer modified";
				error = EIO;
				goto out;
			}
		}
	}
	image = iv3d.obj[IV3D_OBJ_SCRATCH].kva;
	for (i = 0; i < IV3D_SCRATCH_SIZE / 4; i++) {
		if (image[i] != 0) {
			iv3d.verdict = "scratch page modified";
			error = EIO;
			goto out;
		}
	}
	image = iv3d.obj[IV3D_OBJ_TFU_ADST].kva;
	for (i = 0; i < IV3D_OUTPUT_DATA / 4; i++) {
		uint32_t page = i / (PAGE_SIZE / 4);

		if (image[i] != iv3d_pattern(IV3D_OBJ_TFU_ASRC, page,
		    i % (PAGE_SIZE / 4))) {
			iv3d.verdict = "TFU image mismatch";
			error = EIO;
			goto out;
		}
	}
	image = iv3d.obj[IV3D_OBJ_OUTPUT].kva;
	words = IV3D_OUTPUT_DATA / 4;
	for (i = 0; i < words; i++)
		if (image[i] != (uint32_t)IV3D_CLEAR_COLOR) {
			iv3d.verdict = "cleared image mismatch";
			error = EIO;
			goto out;
		}
	for (i = words; i < IV3D_IMAGE_SIZE / 4; i++)
		if (image[i] != 0) {
			iv3d.verdict = "output canary modified";
			error = EIO;
			goto out;
		}
	{
		uint32_t value;

		error = bcmv3d_takeover_hub_peek(IV3D_MMU_CTL, &value);
		if (error == 0 && (value & IV3D_MMU_CTL_FAULTS) != 0) {
			iv3d.verdict = "MMU fault after completion";
			error = EFAULT;
		}
	}
out:
	mask_error = iv3d_remask_all();
	if (error == 0 && mask_error != 0) {
		error = mask_error;
		iv3d.verdict = "could not restore interrupt masks";
	}
	iv3d_disestablish();
	/* No handler can race the final exactly-once verdict. */
	if (error == 0) {
		error = iv3d_check_events(IV3D_CORE_DONE);
		if (error != 0)
			iv3d.verdict = "interrupt lifecycle verification failed";
	}
	if (error == 0) {
		uint32_t hub_sts, core_sts;

		/* No completion or masked error may remain after the jobs. */
		error = bcmv3d_takeover_hub_peek(IV3D_INT_STS, &hub_sts);
		if (error == 0)
			error = bcmv3d_takeover_core_peek(IV3D_INT_STS, &core_sts);
		if (error == 0 && (hub_sts != 0 || core_sts != 0))
			error = EIO;
		if (error != 0)
			iv3d.verdict = "unexpected interrupt status after masking";
	}
	if (error == 0) {
		error = iv3d_check_tfu();
		if (error != 0)
			iv3d.verdict = "TFU state changed after verification";
	}
	mutex_destroy(&iv3d.lock);

	if (!iv3d.published)
		iv3d_release_unpublished();
	if (error == 0) {
		aprint_normal_dev(dev, "IRQ PASS: hub TFUC and core "
		    "FLDONE+FRDONE delivered as interrupts; both images "
		    "verified; handlers disestablished; masks restored; counts "
		    "TFUC=%u FLDONE=%u FRDONE=%u\n", iv3d.tfuc_count,
		    iv3d.fldone_count, iv3d.frdone_count);
	} else {
		uint32_t hub_sts = 0, core_sts = 0, tfu = 0;

		/* Post-mortem for the receipt: did the job finish and the
		 * bit latch while the line never reached the handler? */
		(void)bcmv3d_takeover_hub_peek(IV3D_INT_STS, &hub_sts);
		(void)bcmv3d_takeover_core_peek(IV3D_INT_STS, &core_sts);
		(void)bcmv3d_takeover_hub_peek(IV3D_TFU_CS, &tfu);
		aprint_normal_dev(dev, "IRQ experiment stopped at %s: error "
		    "%d; %s; delivered hub=%#x core=%#x; latched hub=%#x "
		    "core=%#x spurious hub=%u core=%u TFU_CS=%#x; "
		    "counts TFUC=%u FLDONE=%u FRDONE=%u; %s\n",
		    iv3d.stage, error,
		    iv3d.verdict != NULL ? iv3d.verdict :
		    "no verdict recorded", iv3d.hub_delivered,
		    iv3d.core_delivered, hub_sts, core_sts,
		    iv3d.hub_spurious, iv3d.core_spurious, tfu,
		    iv3d.tfuc_count, iv3d.fldone_count, iv3d.frdone_count,
		    iv3d.published ? "GPU-exposed allocations retained until "
		    "reboot; no retry" :
		    "nothing published; DMA allocations released");
	}
	return error;
}
