/* Origin: EmberBSD actual-source V3D fault-recovery contract, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

/*
 * Host contract for the bounded BCM2712 V3D MMU fault experiment.
 * It compiles the production probe body and substitutes host services:
 * a fake ACPI (resource parse and interrupt establish), the sealed
 * takeover window, bus_dma with CPU/GPU memory views, the faulting and recovered
 * job engines, and a level-triggered fake interrupt controller that
 * invokes the registered handlers the moment the fake hardware latches
 * an unmasked bit, re-fires while a bit stays unacknowledged and bounds
 * the entries per latch. This cannot establish physical V3D behavior;
 * it checks the probe's resource validation, mask discipline,
 * ack-then-wake ordering, exactly-once delivery and retention rules.
 */

#include <stdarg.h>
#include <assert.h>
#include <errno.h>
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

#define atomic_load_acquire(p) (*(p))
#define atomic_load_relaxed(p) (*(p))
#define atomic_cas_uint(p, o, n) (*(p) == (o) ? (*(p) = (n), (o)) : *(p))
#define atomic_inc_uint_nv(p) (++*(p))
#define atomic_inc_uint(p) ((void)++*(p))
#define atomic_or_uint(p, bits) ((void)(*(p) |= (bits)))
#define membar_producer() ((void)0)
#define membar_consumer() ((void)0)
#define MAX(a, b) ((a) > (b) ? (a) : (b))
static unsigned int hz = 100;
#define mstohz(ms) (((ms) * hz) / 1000)

#define PAGE_SIZE 4096
#define KASSERT(v) assert(v)
#define ACPI_STATUS uint32_t
#define ACPI_FAILURE(s) ((s) != 0)
#define ACPI_OK 0
#define ACPI_LEVEL_SENSITIVE 1
#define ACPI_EDGE_SENSITIVE 2
typedef void *ACPI_HANDLE;
#define PWAIT 0
#define IPL_VM 0

struct acpi_irq {
	int ar_irq;
	int ar_type;
};
struct acpi_resources { int dummy; };
static int acpi_resource_parse_ops_quiet;
static int fx_parse_ok = 1, fx_core_gsi = 282, fx_hub_gsi = 281;

#define BUS_DMA_WAITOK 0x0
#define BUS_DMASYNC_PREREAD 0x01
#define BUS_DMASYNC_POSTREAD 0x02
#define BUS_DMASYNC_PREWRITE 0x04
#define BUS_DMASYNC_POSTWRITE 0x08

typedef uint64_t bus_addr_t;
typedef size_t bus_size_t;
struct fake_dma_tag { int unused; };
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
	uint8_t *cpu;
	uint8_t *dev;
	bus_size_t size;
	bus_addr_t base;
	bus_dmamap_t map;
	bool live;
	bool device_owned;
};

/* --- production body ---------------------------------------------- */

static void fake_aprint(device_t, const char *, ...);
static void delay(unsigned int);
static void _wakeup(void *);
static unsigned int wake_count;
#define wakeup(chan) _wakeup((void *)(uintptr_t)(chan))
static int _tsleep(void *, int, const char *, int);
#define tsleep(chan, pri, msg, timo) _tsleep((void *)(uintptr_t)(chan), pri, msg, timo)
static bool fake_takeover_complete;
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
#define acpi_intr_establish_irq fake_acpi_intr_establish_irq
#define acpi_intr_disestablish fake_acpi_intr_disestablish
#define acpi_intr_mask fake_acpi_intr_mask
#define xc_barrier fake_xc_barrier
static void *fake_acpi_intr_establish_irq(device_t, struct acpi_irq *, int,
    bool, int (*)(void *), void *, const char *);
static void fake_acpi_intr_disestablish(void *);
static void fake_acpi_intr_mask(void *);
static void fake_xc_barrier(unsigned int);
static ACPI_STATUS fake_acpi_resource_parse(device_t, ACPI_HANDLE,
    const char *, struct acpi_resources *, const void *);
static struct acpi_irq *fake_acpi_res_irq(struct acpi_resources *, int);
static void fake_acpi_resource_cleanup(struct acpi_resources *);
static void
fake_acpi_resource_cleanup(struct acpi_resources *res)
{

	(void)res;
}
static ACPI_STATUS
fake_acpi_resource_parse(device_t dev, ACPI_HANDLE h, const char *path,
    struct acpi_resources *res, const void *ops)
{

	(void)dev;
	(void)h;
	(void)path;
	(void)ops;
	res->dummy = 0;
	return fx_parse_ok ? ACPI_OK : 4;
}

#define acpi_resource_parse fake_acpi_resource_parse
#define acpi_res_irq fake_acpi_res_irq
#define acpi_resource_cleanup fake_acpi_resource_cleanup
static int fake_hub_peek(bus_size_t, uint32_t *);
static int fake_hub_poke(bus_size_t, uint32_t);
bool bcmv3d_takeover_complete(void);

#include "fault-body.h"

/* --- fake ACPI ------------------------------------------------------ */

struct fake_irq_handler {
	int (*fn)(void *);
	void *arg;
	bool established;
	bool controller_masked, drained;
};
static struct fake_irq_handler fake_handlers[2]; /* 0 = core, 1 = hub */
static unsigned int disestablish_count;
static bool establish_should_fail[2];
static bool unmasked_without_handler;


static struct acpi_irq *
fake_acpi_res_irq(struct acpi_resources *res, int index)
{
	static struct acpi_irq irqs[2];

	(void)res;
	if (index > 1)
		return NULL;
	irqs[0].ar_irq = fx_hub_gsi;	/* index 0 = 282 = hub line */
	irqs[0].ar_type = ACPI_LEVEL_SENSITIVE;
	irqs[1].ar_irq = fx_core_gsi;	/* index 1 = 281 = core line */
	irqs[1].ar_type = ACPI_LEVEL_SENSITIVE;
	return &irqs[index];
}


static void *
fake_acpi_intr_establish_irq(device_t dev, struct acpi_irq *irq, int ipl,
    bool mpsafe, int (*intr)(void *), void *arg, const char *xname)
{
	int slot;

	(void)dev;
	(void)ipl;
	(void)mpsafe;
	(void)xname;
	/* Wire evidence: GSI 282 carries the HUB line, 281 the CORE. */
	if (irq->ar_irq == 282)
		slot = 1;
	else if (irq->ar_irq == 281)
		slot = 0;
	else
		return NULL;
	if (fake_handlers[slot].established || establish_should_fail[slot])
		return NULL;
	fake_handlers[slot].fn = intr;
	fake_handlers[slot].arg = arg;
	fake_handlers[slot].established = true;
	return &fake_handlers[slot];
}

static void
fake_acpi_intr_disestablish(void *ih)
{

	assert(ih != NULL);
	assert(((struct fake_irq_handler *)ih)->controller_masked);
	assert(((struct fake_irq_handler *)ih)->drained);
	((struct fake_irq_handler *)ih)->established = false;
	((struct fake_irq_handler *)ih)->fn = NULL;
	disestablish_count++;
}

static struct fixture {
	bool fail_alloc[FV3D_OBJ_COUNT];
	int fail_create;
	bool job1_valid_dst, fault_touch_dst, good_image_bad;
	bool fault_never, good_never, sticky_latch;
	bool fault_with_tfuc, fault_with_count, duplicate_fault, duplicate_good;
	bool fault_busy, initial_busy, good_busy, pre_good_busy;
	bool good_no_count, good_two_counts, good_with_fault;
	bool good_fault_only, good_latch_only;
	unsigned int va_width_field;
	uint32_t vio_id;
	bool wrong_address, address_below, address_at_end;
	bool enforce_sync, sync_violation, missing_latch, sticky_irq;
	int corrupt_object, corrupt_job;
	unsigned int corrupt_offset;
	int fail_peek_offset, fail_poke_offset, fail_io_job;
	unsigned int jobs;
	unsigned int delays, sleeps, sleep_ticks;
	bool late_good_fault, late_good_completion;
	bool barrier_fault, barrier_completion, barrier_read_error;
	unsigned int barriers, controller_masks;
	char diagnostics[768];
	char last_message[512];
	const char *case_name;
} fx;

/* --- register banks and level-triggered interrupt model ------------ */

enum {
	R_INT_STS = 0x50, R_INT_CLR = 0x58, R_MSK_SET = 0x60, R_MSK_CLR = 0x64
};
static uint32_t hub_ctl_shadow, hub_pt_base, hub_illegal, fake_tfu_count;
static unsigned int mmuc_flush_writes;
static bool publication_seen;
static struct {
	uint32_t int_sts, mask;
} hub_bank;
static unsigned int hub_pokes;
static unsigned int hub_entries; /* handler invocations */
static unsigned int hub_latches; /* bits latched by jobs */

static bool fake_run_tfu(void);

/* Level model: re-enter only while the bit stays unmasked, latched
 * and unacknowledged; the guard bounds a storm for diagnostics. */
static void
fake_fire(struct fake_irq_handler *h, uint32_t *sts, uint32_t *mask,
    uint32_t bit, unsigned int *entries)
{
	int guard = 8;

	while (guard-- > 0 && (*sts & bit) != 0 && (*mask & bit) == 0 &&
	    h->established && h->fn != NULL) {
		(*entries)++;
		if (h->fn(h->arg) != 1)
			break;
	}
}

static void
fake_latch_hub(uint32_t bit)
{

	hub_bank.int_sts |= bit;
	hub_latches++;
	if ((hub_bank.mask & bit) == 0 && fake_handlers[1].established)
		fake_fire(&fake_handlers[1], &hub_bank.int_sts,
		    &hub_bank.mask, bit, &hub_entries);
}

static bool fake_run_tfu(void);


static void
fake_mask_changed_hub(void)
{
	uint32_t pending = hub_bank.int_sts & ~hub_bank.mask;

	if (pending == 0)
		return;
	if (!fake_handlers[1].established) {
		unmasked_without_handler = true;
		return;
	}
	while (pending != 0) {
		uint32_t bit = pending & -pending;

		fake_fire(&fake_handlers[1], &hub_bank.int_sts,
		    &hub_bank.mask, bit, &hub_entries);
		pending &= pending - 1;
	}
}

static int
fake_hub_peek(bus_size_t offset, uint32_t *value)
{

	if (fx.fail_peek_offset == (int)offset &&
	    (fx.fail_io_job < 0 || fx.jobs == (unsigned int)fx.fail_io_job))
		return EIO;
	switch (offset) {
	case R_INT_STS:
		*value = hub_bank.int_sts;
		break;
	case 0x5c: *value = hub_bank.mask; break;	/* MASK_STS: all masked */
	case 0x1238: *value = 0x20804604 | fx.va_width_field << 4; break;	/* MMU_DEBUG */
	case 0x1000: *value = 0; break;		/* MMUC_CTL */
	case 0x1200: *value = hub_ctl_shadow; break;
	case 0x1204: *value = hub_pt_base; break;
	case 0x1230: *value = hub_illegal; break;
	case FV3D_MMU_VIO_ID:
		*value = fx.vio_id;
		break;
	case FV3D_MMU_VIO_ADDR:
		/* Pinned Linux: VIO_ADDR stores the high 32 VA bits. */
		*value = (0x10410000 + (fx.address_at_end ? FV3D_IMAGE_SIZE :
		    fx.address_below ? -16 : 32)) >> (fx.va_width_field - 2);
		if (fx.wrong_address)
			*value = 0x01040100; /* source object, not destination alias */
		break;
	case FV3D_TFU_CS:
		*value = 0x100 | (fake_tfu_count << FV3D_TFU_CVTCT_SHIFT);
		if ((fx.initial_busy && fx.jobs == 0) ||
		    (fx.fault_busy && fx.jobs == 1) ||
		    (fx.pre_good_busy && fx.jobs == 1 && mmuc_flush_writes >= 3) ||
		    (fx.good_busy && fx.jobs == 2))
			*value |= FV3D_TFU_BUSY;
		break;
	default: *value = 0; break;
	}
	return 0;
}

static int
fake_hub_poke(bus_size_t offset, uint32_t value)
{

	hub_pokes++;
	if (fx.fail_poke_offset == (int)offset &&
	    (fx.fail_io_job < 0 || fx.jobs == (unsigned int)fx.fail_io_job))
		return EIO;
	if (offset == 0x1000 && (value & FV3D_MMUC_FLUSH) != 0)
		mmuc_flush_writes++;
	switch (offset) {
	case R_MSK_SET:
		hub_bank.mask |= value;
		break;
	case R_MSK_CLR:
		hub_bank.mask &= ~value;
		/* Unmasking before the handler exists is a violation. */
		if (!fake_handlers[1].established)
			unmasked_without_handler = true;
		fake_mask_changed_hub();
		break;
	case R_INT_CLR:
		if (!fx.sticky_irq)
			hub_bank.int_sts &= ~value;
		break;
	case 0x1200: {
		uint32_t latch = hub_ctl_shadow & FV3D_MMU_CTL_FAULTS;

		/* True write-1-to-clear: only fault bits written as 1 clear;
		 * a full-value write without them leaves the latch alone.
		 * Sticky mode never clears. */
		if (!fx.sticky_latch)
			latch &= ~value;
		hub_ctl_shadow = (value & ~FV3D_MMU_CTL_TLB_CLEAR &
		    ~FV3D_MMU_CTL_FAULTS) | latch;
		break;
	}
	case 0x1204:
		hub_pt_base = value;
		publication_seen = true;
		break;
	case 0x1230:
		hub_illegal = value;
		break;
	case FV3D_TFU_ICFG:
		/*
		 * The engine latches its own outcome (fault bit or TFUC);
		 * a stall latches nothing.
		 */
		(void)fake_run_tfu();
		break;
	default:
		break;
	}
	return 0;
}

/* The controller mask prevents new interrupts. The barrier must still
 * wait for an ISR that entered before that mask and runs on another CPU. */
static void
fake_acpi_intr_mask(void *ih)
{
	struct fake_irq_handler *handler = ih;

	assert(handler != NULL && handler->established);
	handler->controller_masked = true;
	handler->drained = false;
	fx.controller_masks++;
}

static void
fake_xc_barrier(unsigned int flags)
{
	struct fake_irq_handler *handler = &fake_handlers[1];

	assert(flags == 0); /* low priority, below the hardware ISR */
	assert(handler->established && handler->controller_masked);
	fx.barriers++;
	if (fx.jobs == 2 && (fx.barrier_fault || fx.barrier_completion ||
	    fx.barrier_read_error)) {
		if (fx.barrier_fault) {
			hub_ctl_shadow |= __BIT(20);
			hub_bank.int_sts |= FV3D_HUB_MMU_PTI;
		}
		if (fx.barrier_completion) {
			fake_tfu_count++;
			hub_bank.int_sts |= FV3D_HUB_TFUC;
		}
		if (fx.barrier_read_error) {
			fx.fail_peek_offset = FV3D_INT_STS;
			fx.fail_io_job = 2;
		}
		/* Already in flight: do not use the new-interrupt mask gate. */
		hub_entries++;
		(void)handler->fn(handler->arg);
	}
	handler->drained = true;
}

/* --- bus_dma (CPU/GPU views) --------------------------------------- */

static struct fake_allocation allocations[FV3D_OBJ_COUNT];
static bus_addr_t default_bases[FV3D_OBJ_COUNT] = {
	0x10000000, 0x10400000, 0x10401000, 0x10406000, 0x1040b000,
	0x10410000
};

static struct fake_allocation *
allocation_by_seg(const bus_dma_segment_t *seg)
{
	int i;

	for (i = 0; i < FV3D_OBJ_COUNT; i++)
		if (seg == (bus_dma_segment_t *)&fv3d.obj[i].seg)
			return &allocations[i];
	return NULL;
}

static struct fake_allocation *
allocation_containing(bus_addr_t addr)
{
	int i;

	for (i = 0; i < FV3D_OBJ_COUNT; i++)
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

	(void)t; (void)align; (void)boundary; (void)nsegs; (void)flags;
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

	(void)t; (void)nsegs; (void)size; (void)flags;
	assert(allocation != NULL && allocation->live);
	*kvap = allocation->cpu;
	maps++;
	return 0;
}

static void
fake_bus_dmamem_unmap(bus_dma_tag_t t, void *kva, bus_size_t size)
{
	int i;

	(void)t; (void)size;
	assert(!fv3d.published);
	for (i = 0; i < FV3D_OBJ_COUNT; i++)
		if (allocations[i].live && allocations[i].cpu == kva)
			allocations[i].map = NULL;
	unmaps++;
}

static void
fake_bus_dmamem_free(bus_dma_tag_t t, bus_dma_segment_t *segs, int nsegs)
{
	struct fake_allocation *allocation = allocation_by_seg(segs);

	(void)t; (void)nsegs;
	assert(!fv3d.published);
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

	(void)t; (void)size; (void)nsegs; (void)maxsegsz; (void)boundary;
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
	assert(!fv3d.published);
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

	(void)t; (void)proc; (void)flags;
	for (i = 0; i < FV3D_OBJ_COUNT; i++)
		if (allocations[i].live && allocations[i].cpu == kva)
			allocation = &allocations[i];
	assert(allocation != NULL);
	map->object = (int)(allocation - allocations);
	map->dm_nsegs = 1;
	map->dm_segs[0].ds_addr = allocation->base;
	map->dm_segs[0].ds_len = size;
	loads++;
	return 0;
}

static void
fake_bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t map)
{
	(void)t;
	assert(!fv3d.published);
	unloads++;
}

static void
fake_bus_dmamap_sync(bus_dma_tag_t t, bus_dmamap_t map, bus_addr_t offset,
    bus_size_t length, int ops)
{
	struct fake_allocation *allocation = &allocations[map->object];

	(void)t;
	if (fx.jobs == 2 && map->object == FV3D_OBJ_PT &&
	    (ops & BUS_DMASYNC_POSTWRITE) != 0) {
		if (fx.late_good_fault) {
			hub_ctl_shadow |= __BIT(20);
			fake_latch_hub(FV3D_HUB_MMU_PTI);
		}
		if (fx.late_good_completion) {
			fake_tfu_count++;
			fake_latch_hub(FV3D_HUB_TFUC);
		}
	}
	assert(offset == 0 && length == allocation->size);
	if (fx.enforce_sync) {
		if ((ops & (BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE)) != 0) {
			if (!allocation->device_owned)
				fx.sync_violation = true;
			allocation->device_owned = false;
		}
		if ((ops & (BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE)) != 0) {
			if (allocation->device_owned)
				fx.sync_violation = true;
			allocation->device_owned = true;
		}
	}
	if ((ops & BUS_DMASYNC_PREWRITE) != 0)
		memcpy(allocation->dev + offset, allocation->cpu + offset, length);
	if ((ops & BUS_DMASYNC_POSTREAD) != 0)
		memcpy(allocation->cpu + offset, allocation->dev + offset, length);
}

/* --- the two accepted job engines (fake GPU) ------------------------ */

static bool
fake_translate(bus_addr_t va, bus_addr_t *pa)
{
	struct fake_allocation *pt = allocation_containing(
	    (bus_addr_t)hub_pt_base << FV3D_PAGE_SHIFT);
	uint32_t pte;

	assert(pt != NULL);
	if ((va >> FV3D_PAGE_SHIFT) >= pt->size / 4)
		return false;
	memcpy(&pte, pt->dev + (va >> FV3D_PAGE_SHIFT) * 4, sizeof(pte));
	if ((pte & FV3D_PTE_VALID) == 0)
		return false;
	*pa = ((bus_addr_t)(pte & 0x00ffffff) << FV3D_PAGE_SHIFT) |
	    (va & (PAGE_SIZE - 1));
	return true;
}

static bool
fake_run_tfu(void)
{
	/* The accepted TFU payload through the alias translations; an
	 * invalid destination PTE must fault instead of writing. */
	struct fake_allocation *src, *dst;
	bus_addr_t src_pa, dst_pa;
	uint32_t *pt = (uint32_t *)(void *)allocation_containing(
	    (bus_addr_t)hub_pt_base << FV3D_PAGE_SHIFT)->dev;
	uint32_t pte;
	unsigned int id;

	fx.jobs++;
	if (fx.enforce_sync) {
		for (id = 0; id < FV3D_OBJ_COUNT; id++)
			if (!allocations[id].device_owned)
				fx.sync_violation = true;
	}
	if (fx.corrupt_object >= 0 && fx.jobs == (unsigned int)fx.corrupt_job)
		allocations[fx.corrupt_object].dev[fx.corrupt_offset] ^= 0x80;
	if (fx.job1_valid_dst) {
		/* Anomaly mode: the fault experiment's invalid entry is
		 * unexpectedly honoured; copy like the good job. */
		memcpy(allocation_containing(
		    default_bases[FV3D_OBJ_TFU_ADST])->dev,
		    allocation_containing(
		    default_bases[FV3D_OBJ_TFU_ASRC])->dev,
		    FV3D_OUTPUT_DATA);
		fake_tfu_count++;
		fake_latch_hub(FV3D_HUB_TFUC);
		return true;
	}
	memcpy(&pte, pt + (default_bases[FV3D_OBJ_TFU_LDST] >>
	    FV3D_PAGE_SHIFT), sizeof(pte));
	if ((pte & FV3D_PTE_VALID) == 0) {
		if (fx.fault_never)
			return true;	/* engine stalls: nothing latches */
		/* The pinned fault: latch MMU_CTL PT_INVALID and the HUB
		 * PTI interrupt; never complete the job. */
		if (!fx.missing_latch)
			hub_ctl_shadow |= __BIT(20);
		if (fx.sticky_latch)
			hub_ctl_shadow |= 0;	/* latch set; W1C ignored below */
		if (fx.fault_touch_dst)
			allocation_containing(
			    default_bases[FV3D_OBJ_TFU_ADST])->dev[0] = 1;
		if (fx.fault_with_count)
			fake_tfu_count++;
		fake_latch_hub(FV3D_HUB_MMU_PTI |
		    (fx.fault_with_tfuc ? FV3D_HUB_TFUC : 0));
		if (fx.duplicate_fault)
			fake_latch_hub(FV3D_HUB_MMU_PTI);
		return true;
	}
	if (fx.good_fault_only) {
		hub_ctl_shadow |= __BIT(20);
		fake_latch_hub(FV3D_HUB_MMU_PTI);
		return true;
	}
	if (fx.good_never)
		return true;	/* good engine stalls after recovery */
	if (mmuc_flush_writes < 2)
		return false;	/* unflushed MMU: the good job cannot run */
	if (!fake_translate(default_bases[FV3D_OBJ_TFU_LSRC], &src_pa) ||
	    !fake_translate(default_bases[FV3D_OBJ_TFU_LDST], &dst_pa))
		return false;
	src = allocation_containing(src_pa);
	dst = allocation_containing(dst_pa);
	if (src == NULL || dst == NULL ||
	    src->base != default_bases[FV3D_OBJ_TFU_ASRC] ||
	    dst->base != default_bases[FV3D_OBJ_TFU_ADST])
		return false;
	if (fx.good_image_bad) {
		dst->dev[dst_pa - dst->base] ^= 0xff;
	} else {
		memcpy(dst->dev + (dst_pa - dst->base), src->dev +
		    (src_pa - src->base), FV3D_OUTPUT_DATA);
	}
	if (fx.good_latch_only)
		hub_ctl_shadow |= __BIT(20);
	if (!fx.good_no_count)
		fake_tfu_count += fx.good_two_counts ? 2 : 1;
	fake_latch_hub(FV3D_HUB_TFUC |
	    (fx.good_with_fault ? FV3D_HUB_MMU_PTI : 0));
	if (fx.duplicate_good)
		fake_latch_hub(FV3D_HUB_TFUC);
	return true;
}

/* --- fixture state and cases --------------------------------------- */


static struct device dev = { 1 };
static struct fake_dma_tag tag;

static void
fake_aprint(device_t dv, const char *fmt, ...)
{
	va_list ap;

	(void)dv;
	va_start(ap, fmt);
	if (strstr(fmt, "fault post-mortem") != NULL)
		vsnprintf(fx.diagnostics, sizeof(fx.diagnostics), fmt, ap);
	else
		vsnprintf(fx.last_message, sizeof(fx.last_message), fmt, ap);
	va_end(ap);
}

static void
delay(unsigned int us)
{
	(void)us;
	fx.delays++;
}

static void
_wakeup(void *chan)
{
	(void)chan;
	wake_count++;
}

static int
_tsleep(void *chan, int pri, const char *wmesg, int timo)
{
	(void)chan; (void)pri; (void)wmesg; (void)timo;
	fx.sleeps++;
	assert(timo > 0);
	fx.sleep_ticks += timo;
	return 0;
}

bool
bcmv3d_takeover_complete(void);
bool
bcmv3d_takeover_complete(void)
{

	return fake_takeover_complete;
}

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

	if (fv3d.published)
		for (i = 0; i < FV3D_OBJ_COUNT; i++)
			if (allocations[i].live) {
				free(allocations[i].cpu);
				free(allocations[i].dev);
				if (allocations[i].map != NULL &&
				    !allocations[i].map->destroyed)
					free(allocations[i].map);
			}
	memset(&fv3d, 0, sizeof(fv3d));
	memset(allocations, 0, sizeof(allocations));
	memset(&hub_bank, 0, sizeof(hub_bank));
	memset(&fake_handlers, 0, sizeof(fake_handlers));
	memset(&fx, 0, sizeof(fx));
	for (i = 0; i < FV3D_OBJ_COUNT; i++)
		fx.fail_alloc[i] = false;
	hz = 100;
	fx.va_width_field = 6;
	/* CM5 V3D 7.1 fault capture; the 4.1 client map is different. */
	fx.vio_id = 0x7d;
	fx.fail_create = -1;
	fx.corrupt_object = -1;
	fx.fail_peek_offset = fx.fail_poke_offset = fx.fail_io_job = -1;
	fx_parse_ok = 1;
	fx_hub_gsi = 282;	/* _CRS index 0 */
	fx_core_gsi = 281;	/* _CRS index 1 */
	hub_bank.mask = 0x7f;
	fake_takeover_complete = true;
	hub_pokes = 0;
	hub_entries = 0;
	hub_latches = 0;
	disestablish_count = 0;
	wake_count = 0;
	establish_should_fail[0] = establish_should_fail[1] = false;
	unmasked_without_handler = false;
	hub_ctl_shadow = hub_pt_base = hub_illegal = 0;
	mmuc_flush_writes = 0;
	fake_tfu_count = 0;
	publication_seen = false;
	allocs = maps = creates = loads = 0;
	frees = unmaps = destroys = unloads = 0;
	inside_driver = false;
	(void)hub_pokes;
}

static int
run_case(const char *name, int expected)
{
	int error;

	fx.case_name = name;
	cases++;
	inside_driver = true;
	error = bcmv3d_fault_probe(&dev, &tag, (ACPI_HANDLE)1);
	inside_driver = false;
	CHECK(error == expected);
	return error;
}



int
main(void)
{
	int i;

	/* Without a completed takeover there is no fault experiment. */
	reset_fixture();
	fake_takeover_complete = false;
	run_case("no completed takeover", EPERM);

	/* The pinned happy path: fault, recovery, good job. */
	reset_fixture();
	run_case("fault then recovered good job", 0);
	CHECK(strstr(fx.last_message, "fault-recovery PASS") != NULL);
	/* delivered resets before the good job: TFUC of job 2 remains. */
	CHECK((fv3d.delivered & FV3D_HUB_TFUC) != 0);
	/* Exactly-once delivery per latched bit. */
	CHECK(hub_latches == 2 && hub_entries == 2);
	/* Fault diagnostics captured. */
	CHECK((fv3d.fault_bits & __BIT(20)) != 0);
	/* Clean exit: masks restored, handler gone. */
	CHECK(hub_bank.mask & (FV3D_HUB_TFUC | FV3D_HUB_MMU_FAULTS));
	CHECK(!fake_handlers[1].established);
	CHECK(disestablish_count == 1);
	CHECK(fx.controller_masks == 1 && fx.barriers == 1);
	CHECK(frees == 0 && unmaps == 0 && destroys == 0 && unloads == 0);

	/* One shot only. */
	run_case("second attempt", EBUSY);

	/* Resource validation. */
	reset_fixture();
	fx_parse_ok = 0;
	run_case("_CRS parse failure", ENXIO);

	reset_fixture();
	fx_hub_gsi = 283;
	run_case("wrong hub GSI", ENXIO);

	/* Stale latched status. */
	reset_fixture();
	hub_bank.int_sts = FV3D_HUB_TFUC;
	run_case("stale hub status", EBUSY);

	/* Anomaly: the invalid entry is honoured and the job completes. */
	reset_fixture();
	fx.job1_valid_dst = true;
	run_case("faulting job completed", EIO);
	CHECK(strstr(fx.last_message,
	    "faulting job completed unexpectedly") != NULL);

	/* The fault never arrives: bounded timeout. */
	reset_fixture();
	fx.fault_never = true;
	run_case("fault never arrived", ETIMEDOUT);
	CHECK(fx.sleeps == 5 && fx.sleep_ticks == 50);
	CHECK(strstr(fx.diagnostics, "MMU_CTL=") != NULL);
	CHECK(strstr(fx.diagnostics, "TFU_CS=") != NULL);
	CHECK(strstr(fx.diagnostics, "VIO_ID=") != NULL);
	CHECK(strstr(fx.diagnostics, "VIO_ADDR=") != NULL);

	reset_fixture();
	fx.fault_never = true;
	hz = 1;
	run_case("low Hz waits at least one tick", ETIMEDOUT);
	CHECK(fx.sleeps == 5 && fx.sleep_ticks == 5);

	/* A faulted job must not touch the destination. */
	reset_fixture();
	fx.fault_touch_dst = true;
	run_case("fault touched destination", EIO);
	CHECK(strstr(fx.last_message, "destination image or canary modified") != NULL);

	/* Recovery that leaves the latch set. */
	reset_fixture();
	fx.sticky_latch = true;
	run_case("recovery left latch", EIO);
	CHECK(strstr(fx.last_message, "unhealthy") != NULL);

	/* The good job stalls after a healthy recovery. */
	reset_fixture();
	fx.good_never = true;
	run_case("good job never completed", ETIMEDOUT);

	/* The good image mismatches. */
	reset_fixture();
	fx.good_image_bad = true;
	run_case("good image mismatch", EIO);

	/* Fault and completion must never be accepted together. */
	reset_fixture();
	fx.fault_with_tfuc = true;
	run_case("fault plus TFUC", EIO);
	CHECK(fx.jobs == 1);

	reset_fixture();
	fx.fault_with_count = true;
	run_case("fault plus completion counter", EIO);
	CHECK(fx.jobs == 1);

	reset_fixture();
	fx.duplicate_fault = true;
	run_case("duplicate fault delivery", EIO);
	CHECK(fx.jobs == 1);

	reset_fixture();
	fx.duplicate_good = true;
	run_case("duplicate completion delivery", EIO);

	reset_fixture();
	fx.good_no_count = true;
	run_case("TFUC without counter advance", EIO);

	reset_fixture();
	fx.good_two_counts = true;
	run_case("two conversions for one job", EIO);

	reset_fixture();
	fake_tfu_count = 255;
	run_case("completion counter wraps once", 0);

	reset_fixture();
	fx.initial_busy = true;
	run_case("busy before initial submission", EBUSY);
	CHECK(fx.jobs == 0);

	reset_fixture();
	fx.fault_busy = true;
	run_case("fault leaves TFU busy", EBUSY);
	CHECK(fx.jobs == 1);

	reset_fixture();
	fx.pre_good_busy = true;
	run_case("TFU busy before resubmission", EBUSY);
	CHECK(fx.jobs == 1);

	reset_fixture();
	fx.good_busy = true;
	run_case("completion while TFU busy", EBUSY);

	reset_fixture();
	fx.good_with_fault = true;
	run_case("good completion plus fault", EIO);

	reset_fixture();
	fx.missing_latch = true;
	run_case("fault without MMU latch", EIO);

	/* Every V3D 7.1 client and the ignored upper ID bits. */
	for (i = 0; i < 256; i++) {
		bool tfu = (i & 0x7f) == 0x7c || (i & 0x7f) == 0x7d;

		reset_fixture();
		fx.vio_id = (uint32_t)i;
		run_case("V3D 7.1 fault client identity", tfu ? 0 : EIO);
		CHECK(fx.jobs == (tfu ? 2u : 1u));
	}
	reset_fixture();
	fx.vio_id = 0xfffffffd;
	run_case("V3D 7.1 ignores upper AXI ID bits", 0);

	reset_fixture();
	fx.wrong_address = true;
	run_case("fault address is the source", EIO);
	CHECK(fx.jobs == 1);

	reset_fixture();
	fx.address_below = true;
	run_case("fault address below destination", EIO);

	reset_fixture();
	fx.address_at_end = true;
	run_case("fault address at exclusive end", EIO);

	/* Every image, its padding and the illegal-access scratch remain
	 * protected across both jobs; mutate GPU memory, not CPU memory. */
	for (int job = 1; job <= 2; job++) {
		for (i = FV3D_OBJ_SCRATCH; i < FV3D_OBJ_COUNT; i++) {
			reset_fixture();
			fx.corrupt_object = i;
			fx.corrupt_job = job;
			fx.corrupt_offset = i == FV3D_OBJ_TFU_ADST && job == 2 ?
			    FV3D_OUTPUT_DATA : 0;
			run_case("protected GPU memory corrupted", EIO);
		}
	}

	reset_fixture();
	fx.enforce_sync = true;
	run_case("two complete DMA ownership handoffs", 0);
	CHECK(!fx.sync_violation);

	/* Register access errors cannot leave stale values in the proof. */
	{
		static const int reads[] = { FV3D_TFU_CS, FV3D_MMU_VIO_ID,
		    FV3D_MMU_VIO_ADDR, FV3D_MMU_CTL, FV3D_INT_STS };
		static const int writes[] = { FV3D_TFU_IIA, FV3D_TFU_IIS,
		    FV3D_TFU_ICA, FV3D_TFU_IUA, FV3D_TFU_IOA, FV3D_TFU_IOC,
		    FV3D_TFU_IOS, FV3D_TFU_COEF(0), FV3D_TFU_COEF(1),
		    FV3D_TFU_COEF(2), FV3D_TFU_COEF(3), FV3D_TFU_ICFG,
		    FV3D_MSK_CLR, FV3D_INT_CLR };

		for (i = 0; i < (int)__arraycount(reads); i++) {
			reset_fixture();
			fx.fail_peek_offset = reads[i];
			fx.fail_io_job = 1;
			run_case("fault register read failure", EIO);
			CHECK(fx.jobs == 1);
		}
		for (i = 0; i < (int)__arraycount(writes); i++) {
			reset_fixture();
			fx.fail_poke_offset = writes[i];
			run_case("register write failure", EIO);
		}
	}

	reset_fixture();
	fx.sticky_irq = true;
	run_case("unacknowledgeable level fault", EIO);
	CHECK(hub_entries <= 3);

	reset_fixture();
	fx.late_good_fault = true;
	run_case("late MMU fault during final verification", EIO);

	reset_fixture();
	fx.late_good_completion = true;
	run_case("late extra conversion during final verification", EIO);

	reset_fixture();
	fx.fail_poke_offset = FV3D_MSK_SET;
	fx.fail_io_job = 2;
	run_case("cannot restore masks after completion", EIO);

	reset_fixture();
	fx.good_fault_only = true;
	run_case("good job faults again without completion", EIO);
	CHECK(fx.jobs == 2);

	reset_fixture();
	fx.good_latch_only = true;
	run_case("good completion retains MMU fault latch", EIO);

	reset_fixture();
	fx.va_width_field = 5;
	run_case("35-bit virtual fault address", 0);

	reset_fixture();
	fx.va_width_field = 7;
	run_case("37-bit virtual fault address", 0);

	reset_fixture();
	fx.fail_peek_offset = FV3D_TFU_CS;
	fx.fail_io_job = 0;
	run_case("cannot read TFU before initial job", EIO);
	CHECK(fx.jobs == 0);
	CHECK(strstr(fx.diagnostics, "read error 5") != NULL);

	reset_fixture();
	fx.fail_peek_offset = FV3D_TFU_CS;
	fx.fail_io_job = 2;
	run_case("cannot read TFU completion counter", EIO);

	reset_fixture();
	fx.fail_poke_offset = FV3D_MSK_CLR;
	fx.fail_io_job = 1;
	run_case("cannot rearm the good job interrupt", EIO);
	CHECK(fx.jobs == 1);

	/* Drain outstanding handler writes before accepting final evidence. */
	reset_fixture();
	fx.barrier_fault = true;
	run_case("in-flight fault finishes at teardown barrier", EIO);
	CHECK(fv3d.events == 3 && fv3d.fault_events == 2);
	CHECK(fx.barriers == 1 && !fake_handlers[1].established);

	reset_fixture();
	fx.barrier_completion = true;
	run_case("in-flight completion finishes at teardown barrier", EIO);
	CHECK(fv3d.events == 3 && fv3d.tfuc_events == 2);

	reset_fixture();
	fx.barrier_read_error = true;
	run_case("in-flight ISR fails at teardown barrier", EIO);
	CHECK(fv3d.irq_error == EIO);

	/* Allocation failures release cleanly before exposure. */




	for (i = 0; i < FV3D_OBJ_COUNT; i++) {
		reset_fixture();
		fx.fail_alloc[i] = true;
		run_case("allocation failure", ENOMEM);
		CHECK(!fv3d.published);
		CHECK(frees == allocs && unmaps == maps);
	}

	/* Handler self-protection under a level-line storm. */
	{
		int rc;

		reset_fixture();
		fake_handlers[1].established = true;
		fake_handlers[1].fn = fv3d_hub_intr;
		hub_bank.mask = 0;
		for (i = 0; i < FV3D_STORM_LIMIT + 3; i++)
			rc = fake_handlers[1].fn(NULL);
		cases++;
		checks++;
		if (!(rc == 0 &&
		    (hub_bank.mask & (FV3D_HUB_TFUC | FV3D_HUB_MMU_FAULTS)) !=
		    0)) {
			printf("FAIL case 'hub handler self-mask'\n");
			exit(1);
		}
	}

	printf("PASS: %u cases, %u checks\n", cases, checks);
	return 0;
}
