/* Origin: EmberBSD actual-source V3D interrupt contract, 2026-10-10. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

/*
 * Host contract for the bounded BCM2712 V3D interrupt experiment.
 * It compiles the production probe body and substitutes host services:
 * a fake ACPI (resource parse and interrupt establish), the sealed
 * takeover window, bus_dma with CPU/GPU memory views, the two accepted
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
#define MUTEX_DEFAULT 0
#define MAX(a, b) ((a) > (b) ? (a) : (b))
static unsigned int fake_hz = 100;
#define mstohz(ms) ((ms) * fake_hz / 1000)
typedef struct { bool initialized, held; } kmutex_t;
static void
mutex_init(kmutex_t *m, int type, int ipl)
{
	(void)type; (void)ipl;
	assert(!m->initialized);
	m->initialized = true;
}
static void
mutex_enter(kmutex_t *m)
{
	assert(m->initialized && !m->held);
	m->held = true;
}
static void
mutex_exit(kmutex_t *m)
{
	assert(m->initialized && m->held);
	m->held = false;
}
static void
mutex_destroy(kmutex_t *m)
{
	assert(m->initialized && !m->held);
	m->initialized = false;
}

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
};

/* --- production body ---------------------------------------------- */

static void fake_aprint(device_t, const char *, ...);
static void delay(unsigned int);
static void _wakeup(void *);
static unsigned int wake_count;
#define wakeup(chan) _wakeup((void *)(uintptr_t)(chan))
static int _mtsleep(void *, int, const char *, int, kmutex_t *);
#define mtsleep(chan, pri, msg, timo, lock) \
    _mtsleep((void *)(uintptr_t)(chan), pri, msg, timo, lock)
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
#define acpi_resource_parse fake_acpi_resource_parse
#define acpi_res_irq fake_acpi_res_irq
#define acpi_resource_cleanup fake_acpi_resource_cleanup
static int fake_hub_peek(bus_size_t, uint32_t *);
static int fake_hub_poke(bus_size_t, uint32_t);
static int fake_core_peek(bus_size_t, uint32_t *);
static int fake_core_poke(bus_size_t, uint32_t);
bool bcmv3d_takeover_complete(void);

#include "irq-body.h"

/* --- fake ACPI ------------------------------------------------------ */

struct fake_irq_handler {
	int (*fn)(void *);
	void *arg;
	bool established, masked;
};
static struct fake_irq_handler fake_handlers[2]; /* 0 = core, 1 = hub */
static unsigned int disestablish_count, controller_masks, barriers;
static bool barrier_completed;
static bool establish_should_fail[2];
static bool unmasked_without_handler;

static ACPI_STATUS
fake_acpi_resource_parse(device_t dev, ACPI_HANDLE h, const char *path,
    struct acpi_resources *res, const void *ops)
{
	struct fixture;

	(void)dev;
	(void)h;
	(void)path;
	(void)ops;
	res->dummy = 0;
	return fx_parse_ok ? ACPI_OK : 4;
}

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

static void
fake_acpi_resource_cleanup(struct acpi_resources *res)
{

	(void)res;
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

	assert(ih != NULL && barrier_completed && !iv3d.lock.held);
	assert(((struct fake_irq_handler *)ih)->masked);
	((struct fake_irq_handler *)ih)->established = false;
	((struct fake_irq_handler *)ih)->fn = NULL;
	disestablish_count++;
}

static struct fixture {
	bool fail_alloc[IV3D_OBJ_COUNT];
	int fail_create, fail_hub_write, fail_core_write;
	int delayed_irq;
	bool fail_restore_mask;
	bool tfu_partial, clear_bad, stall_tfu;
	bool tfu_busy_after, duplicate_tfu, duplicate_fl, duplicate_fr;
	bool late_tfu, late_fr;
	bool foreign_core, extra_final_conversion, final_tfu_busy;
	uint32_t late_hub_status, late_core_status;
	bool binner_storm, early_frdone, tfu_initial_busy;
	unsigned int tfu_count_step, tfu_submissions, render_submissions;
	int corrupt_object;
	size_t corrupt_offset;
	unsigned int delays, sleeps, slept_ticks;
	bool complete_tfu_on_sleep;
	char last_message[512];
	const char *case_name;
} fx;

/* --- register banks and level-triggered interrupt model ------------ */

enum {
	R_INT_STS = 0x50, R_INT_CLR = 0x58, R_MSK_SET = 0x60, R_MSK_CLR = 0x64
};
static uint32_t hub_ctl_shadow, hub_pt_base, hub_illegal, fake_tfu_count;
static bool publication_seen;
static struct {
	uint32_t int_sts, mask;
} hub_bank, core_bank;
static unsigned int hub_pokes, core_pokes;
static unsigned int hub_entries, core_entries; /* handler invocations */
static unsigned int hub_latches, core_latches; /* bits latched by jobs */

static bool fake_run_tfu(void);
static bool fake_run_bcl(void);
static bool fake_run_rcl(void);
static void fake_corrupt_buffer(void);

static void
fake_acpi_intr_mask(void *ih)
{
	struct fake_irq_handler *h = ih;

	assert(h != NULL && h->established && !iv3d.lock.held);
	h->masked = true;
	controller_masks++;
	barrier_completed = false;
}

static void
fake_xc_barrier(unsigned int flags)
{

	assert(flags == 0 && !iv3d.lock.held);
	for (unsigned int i = 0; i < __arraycount(fake_handlers); i++)
		assert(!fake_handlers[i].established || fake_handlers[i].masked);
	/* An ISR already dispatched before the controller mask still runs. */
	if (fx.delayed_irq == 1 && fake_handlers[1].established) {
		hub_bank.int_sts |= IV3D_HUB_TFUC;
		fake_handlers[1].fn(fake_handlers[1].arg);
	} else if (fx.delayed_irq == 2 && fake_handlers[0].established) {
		for (unsigned int i = 0; i < 5; i++)
			fake_handlers[0].fn(fake_handlers[0].arg);
	}
	barriers++;
	barrier_completed = true;
}

/* Level model: re-enter only while the bit stays unmasked, latched
 * and unacknowledged; the guard bounds a storm for diagnostics. */
static void
fake_fire(struct fake_irq_handler *h, uint32_t *sts, uint32_t *mask,
    uint32_t bit, unsigned int *entries)
{
	int guard = 8;

	while (guard-- > 0 && (*sts & bit) != 0 && (*mask & bit) == 0 &&
	    h->established && !h->masked && h->fn != NULL) {
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

static void
fake_latch_core(uint32_t bit)
{

	core_bank.int_sts |= bit;
	core_latches++;
	if ((core_bank.mask & bit) == 0 && fake_handlers[0].established)
		fake_fire(&fake_handlers[0], &core_bank.int_sts,
		    &core_bank.mask, bit, &core_entries);
}

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

static void
fake_mask_changed_core(void)
{
	uint32_t pending = core_bank.int_sts & ~core_bank.mask;

	if (pending == 0)
		return;
	if (!fake_handlers[0].established) {
		unmasked_without_handler = true;
		return;
	}
	while (pending != 0) {
		uint32_t bit = pending & -pending;

		fake_fire(&fake_handlers[0], &core_bank.int_sts,
		    &core_bank.mask, bit, &core_entries);
		pending &= pending - 1;
	}
}

static int
fake_hub_peek(bus_size_t offset, uint32_t *value)
{

	switch (offset) {
	case R_INT_STS:
		*value = hub_bank.int_sts;
		break;
	case 0x5c: *value = 0x7f; break;	/* MASK_STS: all masked */
	case 0x1238: *value = 0x20804664; break;	/* MMU_DEBUG */
	case 0x1000: *value = 0; break;		/* MMUC_CTL */
	case 0x1200: *value = hub_ctl_shadow; break;
	case 0x1204: *value = hub_pt_base; break;
	case 0x1230: *value = hub_illegal; break;
	case IV3D_TFU_CS:
		*value = 0x100 | (fake_tfu_count << IV3D_TFU_CVTCT_SHIFT);
		if (fx.tfu_initial_busy ||
		    (fx.tfu_submissions != 0 && fx.tfu_busy_after))
			*value |= IV3D_TFU_BUSY;
		break;
	default: *value = 0; break;
	}
	return 0;
}

static int
fake_hub_poke(bus_size_t offset, uint32_t value)
{

	hub_pokes++;
	if ((int)offset == fx.fail_hub_write ||
	    (fx.fail_restore_mask && offset == R_MSK_SET &&
	    value == IV3D_HUB_IRQS_ALL))
		return EIO;
	switch (offset) {
	case R_MSK_SET:
		hub_bank.mask |= value;
		if (value == IV3D_HUB_IRQS_ALL && fx.render_submissions != 0) {
			hub_bank.int_sts |= fx.late_hub_status;
			if (fx.extra_final_conversion)
				fake_tfu_count = (fake_tfu_count + 1) & 0xff;
			if (fx.final_tfu_busy)
				fx.tfu_busy_after = true;
		}

		if (fx.late_tfu && fx.tfu_submissions != 0)
			hub_bank.int_sts |= IV3D_HUB_TFUC;
		break;
	case R_MSK_CLR:
		hub_bank.mask &= ~value;
		/* Unmasking before the handler exists is a violation. */
		if (!fake_handlers[1].established)
			unmasked_without_handler = true;
		fake_mask_changed_hub();
		break;
	case R_INT_CLR:
		hub_bank.int_sts &= ~value;
		break;
	case 0x1200:
		hub_ctl_shadow = value & ~IV3D_MMU_CTL_TLB_CLEAR;
		break;
	case 0x1204:
		hub_pt_base = value;
		publication_seen = true;
		break;
	case 0x1230:
		hub_illegal = value;
		break;
	case IV3D_TFU_ICFG:
		fx.tfu_submissions++;
		/* A stalled engine never latches anything. */
		if (fx.stall_tfu)
			break;
		/* The accepted TFU payload: verify the registers, store. */
		if (!fake_run_tfu())
			return 0;	/* engine stalls, no interrupt */
		fake_latch_hub(IV3D_HUB_TFUC);
		if (fx.duplicate_tfu)
			fake_latch_hub(IV3D_HUB_TFUC);
		break;
	default:
		break;
	}
	return 0;
}

static int
fake_core_peek(bus_size_t offset, uint32_t *value)
{

	switch (offset) {
	case R_INT_STS:
		*value = core_bank.int_sts;
		break;
	default:
		*value = 0;
		break;
	}
	return 0;
}

static int
fake_core_poke(bus_size_t offset, uint32_t value)
{

	core_pokes++;
	if ((int)offset == fx.fail_core_write)
		return EIO;
	switch (offset) {
	case R_MSK_SET:
		core_bank.mask |= value;
		if (iv3d.stage != NULL &&
		    strcmp(iv3d.stage, "verification") == 0)
			core_bank.int_sts |= fx.late_core_status;
		if (fx.late_fr && fx.render_submissions != 0)
			core_bank.int_sts |= IV3D_CORE_FRDONE;
		break;
	case R_MSK_CLR:
		core_bank.mask &= ~value;
		if (!fake_handlers[0].established)
			unmasked_without_handler = true;
		if (fx.binner_storm) {
			for (unsigned int i = 0; i < 5; i++)
				fake_handlers[0].fn(NULL);
		} else
			fake_mask_changed_core();
		if (fx.duplicate_fl)
			fake_latch_core(IV3D_CORE_FLDONE);
		break;
	case R_INT_CLR:
		core_bank.int_sts &= ~value;
		break;
	case IV3D_CLE_CT0QEA:
		/* The accepted BCL: byte-compare against the expectation. */
		if (!fake_run_bcl())
			return 0;
		if (fx.binner_storm)
			break;
		fake_latch_core(IV3D_CORE_FLDONE);
		if (fx.early_frdone)
			fake_latch_core(IV3D_CORE_FRDONE);
		break;
	case IV3D_CLE_CT1QEA:
		fx.render_submissions++;
		/* The TFU line must already be remasked before rendering. */
		assert((hub_bank.mask & IV3D_HUB_TFUC) != 0);
		/* The accepted RCL: byte-compare, then clear and store. */
		if (!fake_run_rcl())
			return 0;
		/* A foreign masked bit latches alongside ours: a handler
		 * may only acknowledge the bits it handles. */
		if (fx.foreign_core)
			core_bank.int_sts |= __BIT(4);	/* TRFB, stays masked */
		if (fx.corrupt_object >= 0)
			fake_corrupt_buffer();
		fake_latch_core(IV3D_CORE_FRDONE);
		if (fx.duplicate_fr)
			fake_latch_core(IV3D_CORE_FRDONE);
		break;
	default:
		break;
	}
	return 0;
}

/* --- bus_dma (CPU/GPU views) --------------------------------------- */

static struct fake_allocation allocations[IV3D_OBJ_COUNT];
static bus_addr_t default_bases[IV3D_OBJ_COUNT] = {
	0x10000000, 0x10400000, 0x10401000, 0x10406000, 0x1040b000,
	0x10410000, 0x10415000, 0x10418000, 0x10419000, 0x1041a000,
	0x1041c000
};

static struct fake_allocation *
allocation_by_seg(const bus_dma_segment_t *seg)
{
	int i;

	for (i = 0; i < IV3D_OBJ_COUNT; i++)
		if (seg == (bus_dma_segment_t *)&iv3d.obj[i].seg)
			return &allocations[i];
	return NULL;
}

static struct fake_allocation *
allocation_containing(bus_addr_t addr)
{
	int i;

	for (i = 0; i < IV3D_OBJ_COUNT; i++)
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
	assert(!iv3d.published);
	for (i = 0; i < IV3D_OBJ_COUNT; i++)
		if (allocations[i].live && allocations[i].cpu == kva)
			allocations[i].map = NULL;
	unmaps++;
}

static void
fake_bus_dmamem_free(bus_dma_tag_t t, bus_dma_segment_t *segs, int nsegs)
{
	struct fake_allocation *allocation = allocation_by_seg(segs);

	(void)t; (void)nsegs;
	assert(!iv3d.published);
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
	assert(!iv3d.published);
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
	for (i = 0; i < IV3D_OBJ_COUNT; i++)
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
	assert(!iv3d.published);
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

/* --- the two accepted job engines (fake GPU) ------------------------ */

static bool
fake_translate(bus_addr_t va, bus_addr_t *pa)
{
	struct fake_allocation *pt = allocation_containing(
	    (bus_addr_t)hub_pt_base << IV3D_PAGE_SHIFT);
	uint32_t pte;

	assert(pt != NULL);
	if ((va >> IV3D_PAGE_SHIFT) >= pt->size / 4)
		return false;
	memcpy(&pte, pt->dev + (va >> IV3D_PAGE_SHIFT) * 4, sizeof(pte));
	if ((pte & IV3D_PTE_VALID) == 0)
		return false;
	*pa = ((bus_addr_t)(pte & 0x00ffffff) << IV3D_PAGE_SHIFT) |
	    (va & (PAGE_SIZE - 1));
	return true;
}

static bool
fake_run_tfu(void)
{
	/* Copy the actual-source pattern through the alias translation. */
	struct fake_allocation *src, *dst;
	bus_addr_t src_pa, dst_pa;
	size_t limit = fx.tfu_partial ? IV3D_OUTPUT_DATA / 8 :
	    IV3D_OUTPUT_DATA;

	if (!fake_translate(default_bases[IV3D_OBJ_TFU_LSRC], &src_pa) ||
	    !fake_translate(default_bases[IV3D_OBJ_TFU_LDST], &dst_pa))
		return false;
	src = allocation_containing(src_pa);
	dst = allocation_containing(dst_pa);
	if (src == NULL || dst == NULL ||
	    src->base != default_bases[IV3D_OBJ_TFU_ASRC] ||
	    dst->base != default_bases[IV3D_OBJ_TFU_ADST])
		return false;
	memcpy(dst->dev, src->dev, limit);
	fake_tfu_count = (fake_tfu_count + fx.tfu_count_step) & 0xff;
	return true;
}

/* The fixture's own BCL encoder (identical to the queue contract). */
static size_t
fixture_expected_bcl(uint8_t *out)
{
	size_t at = 0;
	unsigned int o;

	memset(out, 0, IV3D_BCL_SIZE);
#define P(op, payload) do { \
	out[at] = (op); o = (at + 1) * 8; \
	memset(out + at + 1, 0, (payload)); at += 1 + (payload); \
} while (0)
#define F(start, size, value) do { \
	unsigned int _b = o + (start), _v = (unsigned)(value); \
	for (unsigned int _i = 0; _i < (size); _i++) \
		if ((_v >> _i) & 1) \
			out[(_b + _i) / 8] |= 1 << ((_b + _i) % 8); \
} while (0)
	P(120, 8);
	F(32, 16, 63); F(48, 16, 63); F(8, 3, 3); F(11, 3, 3);
	F(4, 2, 1); F(2, 2, 1);
	P(19, 0);
	P(92, 4);
	F(0, 32, 0);
	P(6, 0);
	P(4, 0);
#undef P
#undef F
	return at;
}

static bool
fake_run_bcl(void)
{
	static uint8_t expected[IV3D_BCL_SIZE];
	struct fake_allocation *bcl = allocation_containing(
	    default_bases[IV3D_OBJ_BCL]);

	return bcl != NULL &&
	    memcmp(bcl->dev, expected, fixture_expected_bcl(expected)) == 0;
}

static size_t
fixture_expected_rcl(uint8_t *out, uint32_t rcl_va);

static bool
fake_run_rcl(void)
{
	static uint8_t expected[IV3D_RCL_SIZE];
	size_t length;
	struct fake_allocation *rcl, *out_img;
	bus_addr_t pa;

	length = fixture_expected_rcl(expected, default_bases[IV3D_OBJ_RCL]);
	rcl = allocation_containing(default_bases[IV3D_OBJ_RCL]);
	if (rcl == NULL || length != 123 ||
	    memcmp(rcl->dev, expected, length) != 0)
		return false;
	if (!fake_translate(default_bases[IV3D_OBJ_OUTPUT], &pa))
		return false;
	out_img = allocation_containing(pa & ~(bus_addr_t)(PAGE_SIZE - 1));
	if (out_img == NULL ||
	    out_img->base != default_bases[IV3D_OBJ_OUTPUT])
		return false;
	{
		size_t words = fx.clear_bad ? 4 : IV3D_OUTPUT_DATA / 4;

		for (size_t i = 0; i < words; i++)
			((uint32_t *)(out_img->dev + (pa - out_img->base)))[i] =
			    0x305e7b4c;
	}
	return true;
}

/* The fixture's own RCL encoder (identical to the queue contract). */
static size_t
fixture_expected_rcl(uint8_t *out, uint32_t rcl_va)
{
	size_t at = 0;
	unsigned int o, generic_start, generic_end;

	memset(out, 0, IV3D_RCL_SIZE);
#define P(op, payload) do { \
	out[at] = (op); o = (at + 1) * 8; \
	memset(out + at + 1, 0, (payload)); at += 1 + (payload); \
} while (0)
#define F(start, size, value) do { \
	unsigned int _b = o + (start), _v = (unsigned)(value); \
	for (unsigned int _i = 0; _i < (size); _i++) \
		if ((_v >> _i) & 1) \
			out[(_b + _i) / 8] |= 1 << ((_b + _i) % 8); \
} while (0)
	P(121, 8);
	F(0, 3, 0); F(4, 4, 0); F(8, 16, 64); F(24, 16, 64);
	F(44, 1, 1); F(46, 1, 1); F(52, 3, 3); F(55, 3, 3);
	P(121, 8);
	F(0, 3, 2); F(3, 3, 0); F(7, 11, 0); F(18, 7, 31);
	F(25, 2, 0); F(27, 5, 8); F(32, 32, 0x305e7b4c);
	P(121, 8);
	F(0, 4, 1);
	P(126, 1);
	F(0, 2, 1); F(2, 1, 1);
	P(123, 4);
	F(0, 4, 0); F(6, 26, default_bases[IV3D_OBJ_TILE_ALLOC] >> 6);
	P(122, 8);
	F(0, 8, 0); F(8, 8, 0); F(16, 8, 1); F(24, 8, 1);
	F(32, 12, 1); F(44, 12, 1); F(61, 3, 0);
	for (unsigned int pass = 0; pass < 2; pass++) {
		P(124, 3);
		F(0, 12, 0); F(12, 12, 0);
		P(26, 0);
		P(29, 12);
		F(0, 4, 8);
		P(25, 0);
		P(27, 0);
	}
	P(19, 0);
	generic_start = at;
	P(125, 0);
	P(26, 0);
	P(56, 1);
	F(0, 6, 2);
	P(54, 4);
	F(0, 32, 0);
	P(21, 1);
	F(0, 8, 0);
	P(29, 12);
	F(0, 4, 0); F(4, 3, 0); F(12, 6, 27);
	F(28, 20, 256); F(48, 16, 64);
	F(64, 32, default_bases[IV3D_OBJ_OUTPUT]);
	P(27, 0);
	P(18, 0);
	generic_end = at;
	P(20, 8);
	F(0, 32, rcl_va + generic_start);
	F(32, 32, rcl_va + generic_end);
	P(23, 2);
	F(0, 8, 0); F(8, 8, 0);
	P(13, 0);
#undef P
#undef F
	return at;
}

/* GPU-side corruption must be visible only through POSTREAD sync. */
static void
fake_corrupt_buffer(void)
{
	struct fake_allocation *a = &allocations[fx.corrupt_object];

	assert(fx.corrupt_offset < a->size);
	a->dev[fx.corrupt_offset] ^= 1;
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
	assert(iv3d.lock.held);
	wake_count++;
}

static int
_mtsleep(void *chan, int pri, const char *wmesg, int timo, kmutex_t *lock)
{
	(void)chan; (void)pri; (void)wmesg;
	assert(timo > 0 && lock == &iv3d.lock && lock->held);
	fx.sleeps++;
	fx.slept_ticks += timo;
	mutex_exit(lock);
	if (fx.complete_tfu_on_sleep) {
		fx.complete_tfu_on_sleep = false;
		assert(fake_run_tfu());
		fake_latch_hub(IV3D_HUB_TFUC);
	}
	mutex_enter(lock);
	return EWOULDBLOCK;
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

	if (iv3d.published)
		for (i = 0; i < IV3D_OBJ_COUNT; i++)
			if (allocations[i].live) {
				free(allocations[i].cpu);
				free(allocations[i].dev);
				if (allocations[i].map != NULL &&
				    !allocations[i].map->destroyed)
					free(allocations[i].map);
			}
	memset(&iv3d, 0, sizeof(iv3d));
	memset(allocations, 0, sizeof(allocations));
	memset(&hub_bank, 0, sizeof(hub_bank));
	memset(&core_bank, 0, sizeof(core_bank));
	memset(&fake_handlers, 0, sizeof(fake_handlers));
	memset(&fx, 0, sizeof(fx));
	for (i = 0; i < IV3D_OBJ_COUNT; i++)
		fx.fail_alloc[i] = false;
	fx.fail_create = -1;
	fx.fail_hub_write = fx.fail_core_write = -1;
	fx.corrupt_object = -1;
	fx.tfu_count_step = 1;
	fake_hz = 100;
	fx_parse_ok = 1;
	fx_hub_gsi = 282;	/* _CRS index 0 */
	fx_core_gsi = 281;	/* _CRS index 1 */
	hub_bank.mask = 0x7f;
	core_bank.mask = 0xffffffff;
	fake_takeover_complete = true;
	hub_pokes = core_pokes = 0;
	hub_entries = core_entries = 0;
	hub_latches = core_latches = 0;
	disestablish_count = controller_masks = barriers = 0;
	barrier_completed = false;
	wake_count = 0;
	establish_should_fail[0] = establish_should_fail[1] = false;
	unmasked_without_handler = false;
	hub_ctl_shadow = hub_pt_base = hub_illegal = 0;
	fake_tfu_count = 0;
	publication_seen = false;
	allocs = maps = creates = loads = 0;
	frees = unmaps = destroys = unloads = 0;
	inside_driver = false;
	(void)hub_pokes; (void)core_pokes;
}

static int
run_case(const char *name, int expected)
{
	int error;

	fx.case_name = name;
	cases++;
	inside_driver = true;
	error = bcmv3d_irq_probe(&dev, &tag, (ACPI_HANDLE)1);
	inside_driver = false;
	CHECK(error == expected);
	return error;
}

static void
assert_clean_exit(void)
{

	/* Masks restored, no handler left registered, nothing unmasked. */
	CHECK(hub_bank.mask & IV3D_HUB_TFUC);
	CHECK(core_bank.mask & IV3D_CORE_DONE);
	CHECK(!fake_handlers[0].established && !fake_handlers[1].established);
	CHECK(!unmasked_without_handler);
}

int
main(void)
{
	int i;

	/* Without a completed takeover there is no IRQ experiment. */
	reset_fixture();
	fake_takeover_complete = false;
	run_case("no completed takeover", EPERM);

	/* The pinned happy path: both jobs complete by interrupt. */
	reset_fixture();
	run_case("two jobs, two lines", 0);
	CHECK(strstr(fx.last_message, "IRQ PASS") != NULL);
	CHECK(iv3d.hub_delivered == IV3D_HUB_TFUC);
	CHECK(iv3d.core_delivered == IV3D_CORE_DONE);
	/* The handlers must wake their waiters (ack-then-wake). */
	CHECK(wake_count >= 3);
	/* Exactly-once delivery per latched bit, ack included. */
	CHECK(hub_latches == 1 && hub_entries == 1);
	CHECK(core_latches == 2 && core_entries == 2);
	/* Masks restored and handlers disestablished on success too. */
	assert_clean_exit();
	CHECK(disestablish_count == 2);
	CHECK(controller_masks == 2 && barriers == 1 && barrier_completed);
	/* Retention after publication. */
	CHECK(frees == 0 && unmaps == 0 && destroys == 0 && unloads == 0);

	/* One shot only. */
	run_case("second attempt", EBUSY);

	/* Resource validation. */
	reset_fixture();
	fx_parse_ok = 0;
	run_case("_CRS parse failure", ENXIO);
	assert_clean_exit();

	reset_fixture();
	fx_core_gsi = 283;
	run_case("wrong core GSI", ENXIO);

	reset_fixture();
	fx_hub_gsi = 280;
	run_case("wrong hub GSI", ENXIO);

	/* Establish failure. */
	reset_fixture();
	establish_should_fail[1] = true;
	run_case("hub establish failure", ENXIO);
	assert_clean_exit();

	/* Stale latched status stops before submission. */
	reset_fixture();
	core_bank.int_sts = IV3D_CORE_FRDONE;
	run_case("stale core status", EBUSY);
	assert_clean_exit();

	/* Allocation failures release cleanly before exposure. */
	for (i = 0; i < IV3D_OBJ_COUNT; i++) {
		reset_fixture();
		fx.fail_alloc[i] = true;
		run_case("allocation failure", ENOMEM);
		CHECK(!iv3d.published);
		CHECK(frees == allocs && unmaps == maps);
	}
	reset_fixture();
	fx.fail_create = IV3D_OBJ_BCL;
	run_case("map create failure", ENOMEM);

	/* A stalled engine must time out within the bounded wait. */
	reset_fixture();
	fx.stall_tfu = true;
	run_case("TFU interrupt timeout", ETIMEDOUT);
	CHECK(strstr(fx.last_message, "hub TFUC never arrived") != NULL);
	/* Bounded wait: the fixture's own constant, not the probe's. */
	CHECK(fx.sleeps == 5 && fx.slept_ticks == 50);
	assert_clean_exit();

	reset_fixture();
	fake_hz = 1000;
	fx.stall_tfu = true;
	run_case("timeout at 1000 Hz", ETIMEDOUT);
	CHECK(fx.sleeps == 5 && fx.slept_ticks == 500);

	reset_fixture();
	fake_hz = 1;
	fx.stall_tfu = true;
	run_case("timeout clamps sub-tick duration", ETIMEDOUT);
	CHECK(fx.sleeps == 5 && fx.slept_ticks == 5);

	reset_fixture();
	fx.stall_tfu = true;
	fx.complete_tfu_on_sleep = true;
	run_case("completion while waiter releases ISR mutex", 0);
	CHECK(fx.sleeps == 1);

	/* The TFU image verification. */
	reset_fixture();
	fx.tfu_partial = true;
	run_case("TFU image mismatch", EIO);
	CHECK(strstr(fx.last_message, "TFU image mismatch") != NULL);

	/* The cleared image verification. */
	reset_fixture();
	fx.clear_bad = true;
	run_case("cleared image mismatch", EIO);
	CHECK(strstr(fx.last_message, "cleared image mismatch") != NULL);

	/* Teardown must wait for already-dispatched handlers before free. */
	reset_fixture();
	fx.delayed_irq = 1;
	run_case("in-flight extra TFUC drained at teardown", EIO);
	CHECK(iv3d.tfuc_count == 2);
	CHECK(controller_masks == 2 && barriers == 1 && barrier_completed);
	assert_clean_exit();

	reset_fixture();
	fx.delayed_irq = 2;
	run_case("in-flight handler error drained at teardown", EIO);
	CHECK(iv3d.irq_error == EIO);
	CHECK(controller_masks == 2 && barriers == 1 && barrier_completed);
	assert_clean_exit();

	/* No failed setup/unmask write may launch the following stage. */
	{
		static const int hub_writes[] = {
			IV3D_TFU_IIA, IV3D_TFU_IIS, IV3D_TFU_ICA, IV3D_TFU_IUA,
			IV3D_TFU_IOA, IV3D_TFU_IOC, IV3D_TFU_IOS,
			IV3D_TFU_COEF(0), IV3D_TFU_COEF(1), IV3D_TFU_COEF(2),
			IV3D_TFU_COEF(3), IV3D_MSK_CLR
		};
		static const int core_writes[] = {
			IV3D_PTB_BPOS, IV3D_CTL_L2TCACTL, IV3D_CTL_SLCACTL,
			IV3D_CLE_CT0QMA, IV3D_CLE_CT0QMS, IV3D_CLE_CT0QTS,
			IV3D_CLE_CT0QBA, IV3D_MSK_CLR, IV3D_CLE_CT1QBA
		};

		for (size_t n = 0; n < __arraycount(hub_writes); n++) {
			reset_fixture();
			fx.fail_hub_write = hub_writes[n];
			run_case("hub setup write failure", EIO);
			CHECK(fx.tfu_submissions == 0);
			assert_clean_exit();
		}
		for (size_t n = 0; n < __arraycount(core_writes); n++) {
			reset_fixture();
			fx.fail_core_write = core_writes[n];
			run_case("core setup write failure", EIO);
			CHECK(fx.render_submissions == 0);
			assert_clean_exit();
		}
	}
	reset_fixture();
	fx.fail_hub_write = IV3D_MSK_SET;
	run_case("hub remask write failure", EIO);
	CHECK(fx.render_submissions == 0);
	CHECK(!fake_handlers[0].established && !fake_handlers[1].established);

	reset_fixture();
	fx.fail_core_write = IV3D_MSK_SET;
	run_case("core remask write failure", EIO);
	CHECK(!fake_handlers[0].established && !fake_handlers[1].established);

	reset_fixture();
	fx.fail_restore_mask = true;
	run_case("final mask restore write failure", EIO);
	CHECK(strstr(fx.last_message, "restore interrupt masks") != NULL);

	/* Fail closed: no render may follow an unsuccessful binner wait. */
	reset_fixture();
	fx.binner_storm = true;
	run_case("binner handler storm", EIO);
	CHECK(fx.render_submissions == 0);
	assert_clean_exit();

	reset_fixture();
	fx.early_frdone = true;
	run_case("FRDONE before render submission", EIO);
	CHECK(fx.render_submissions == 0);

	reset_fixture();
	fx.duplicate_tfu = true;
	run_case("duplicate TFUC", EIO);
	CHECK(fx.render_submissions == 0);
	assert_clean_exit();

	reset_fixture();
	fx.duplicate_fl = true;
	run_case("duplicate FLDONE", EIO);
	CHECK(fx.render_submissions == 0);

	reset_fixture();
	fx.duplicate_fr = true;
	run_case("duplicate FRDONE", EIO);
	assert_clean_exit();

	reset_fixture();
	fx.late_tfu = true;
	run_case("duplicate TFUC latched after masking", EIO);
	assert_clean_exit();

	reset_fixture();
	fx.late_fr = true;
	run_case("duplicate FRDONE latched after masking", EIO);
	assert_clean_exit();

	/* Masked errors also invalidate the result, without a done duplicate. */
	reset_fixture();
	fx.foreign_core = true;
	run_case("unhandled core bit alongside completion", EIO);
	CHECK((core_bank.int_sts & __BIT(4)) != 0);
	assert_clean_exit();

	reset_fixture();
	fx.late_hub_status = __BIT(5);
	run_case("masked hub MMU fault after verification", EIO);
	CHECK((hub_bank.int_sts & __BIT(5)) != 0);

	reset_fixture();
	fx.late_core_status = __BIT(2);
	run_case("masked core OUTOMEM after verification", EIO);
	CHECK((core_bank.int_sts & __BIT(2)) != 0);

	reset_fixture();
	fx.late_core_status = __BIT(5);
	run_case("masked core GMP violation after verification", EIO);
	CHECK((core_bank.int_sts & __BIT(5)) != 0);

	reset_fixture();
	fx.extra_final_conversion = true;
	run_case("extra TFU conversion after verification", EIO);
	CHECK(strstr(fx.last_message, "TFU state changed") != NULL);

	reset_fixture();
	fx.final_tfu_busy = true;
	run_case("TFU busy after verification", EIO);
	CHECK(strstr(fx.last_message, "TFU state changed") != NULL);

	reset_fixture();
	fx.tfu_initial_busy = true;


	run_case("TFU initially busy", EBUSY);
	CHECK(fx.tfu_submissions == 0);

	reset_fixture();
	fx.tfu_busy_after = true;
	run_case("TFUC while TFU remains busy", EIO);
	CHECK(fx.render_submissions == 0);

	reset_fixture();
	fx.tfu_count_step = 0;
	run_case("TFUC without CVTCT increment", EIO);
	CHECK(fx.render_submissions == 0);

	reset_fixture();
	fx.tfu_count_step = 2;
	run_case("TFUC with two CVTCT increments", EIO);
	CHECK(fx.render_submissions == 0);

	reset_fixture();
	fake_tfu_count = 255;
	run_case("CVTCT wraps modulo 256", 0);
	CHECK(fake_tfu_count == 0);

	/* Every protected object has its own negative case. */
	{
		static const struct {
			int object;
			size_t offset;
			const char *name;
		} corruptions[] = {
			{ IV3D_OBJ_TFU_ASRC, 0, "actual source modified" },
			{ IV3D_OBJ_TFU_LSRC, 0, "alias source modified" },
			{ IV3D_OBJ_TFU_LDST, 0, "alias destination modified" },
			{ IV3D_OBJ_TFU_ADST, IV3D_OUTPUT_DATA,
			    "TFU destination padding modified" },
			{ IV3D_OBJ_SCRATCH, 0, "scratch modified" },
			{ IV3D_OBJ_BCL, 0, "BCL modified" },
			{ IV3D_OBJ_RCL, 0, "RCL modified" },
			{ IV3D_OBJ_BCL, IV3D_BCL_SIZE - 1, "BCL padding modified" },
			{ IV3D_OBJ_RCL, IV3D_RCL_SIZE - 1, "RCL padding modified" },
			{ IV3D_OBJ_OUTPUT, IV3D_OUTPUT_DATA,
			    "render destination padding modified" }
		};

		for (size_t n = 0; n < __arraycount(corruptions); n++) {
			reset_fixture();
			fx.corrupt_object = corruptions[n].object;
			fx.corrupt_offset = corruptions[n].offset;
			run_case(corruptions[n].name, EIO);
			assert_clean_exit();
		}
	}

	/* Handler self-protection: a level line asserting without our bit
	 * must be masked by the handler itself, not by the waiting thread. */
	{
		int rc;

		reset_fixture();
		fake_handlers[1].established = true;
		fake_handlers[1].fn = iv3d_hub_intr;
		mutex_init(&iv3d.lock, MUTEX_DEFAULT, IPL_VM);
		hub_bank.mask = 0;	/* line unmasked, nothing latched */
		for (i = 0; i < IV3D_STORM_LIMIT + 3; i++)
			rc = fake_handlers[1].fn(NULL);
		cases++;
		checks++;
		if (!(rc == 0 && (hub_bank.mask & IV3D_HUB_TFUC) != 0)) {
			printf("FAIL case 'hub handler self-mask'\n");
			exit(1);
		}
	}

	printf("PASS: %u cases, %u checks\n", cases, checks);
	return 0;
}
