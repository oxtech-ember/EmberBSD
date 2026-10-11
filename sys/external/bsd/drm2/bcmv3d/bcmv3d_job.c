/* Origin: EmberBSD native BCM2712 TFU/CSD submission, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Register facts and accepted payload: bcm2712_v3d_fault.c and its pins. */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/xcall.h>
#include <dev/acpi/acpi_intr.h>
#include <arm/broadcom/bcm2712_v3d_takeover.h>
#include <linux/err.h>
#include "bcmv3d_drv.h"

#define V3D_INT_STS	0x50
#define V3D_INT_CLR	0x58
#define V3D_MSK_SET	0x60
#define V3D_MSK_CLR	0x64
#define V3D_IRQ_ALL	0x7f
#define V3D_TFUC		0x02
#define V3D_TFU_CS	0x700
#define V3D_ICFG		0x708
#define V3D_MMUC		0x1000
#define V3D_MMU		0x1200
#define V3D_PT_BASE	0x1204
#define V3D_ILLEGAL	0x1230
#define V3D_MMU_DEBUG	0x1238
#define V3D_MMU_VALUE	0x060d0c01
#define V3D_COPY_SIZE	(64 * 64 * 4)
#define V3D_PT_SIZE	(4 * 1024 * 1024)
#define V3D_PTE_VALID	(1U << 28)
#define V3D_PTE_WRITE	(1U << 29)
#define V3D_CSD_STATUS	0x900
#define V3D_CSD_CFG0	0x930
#define V3D_CSD_DONE	0x40
#define V3D_CORE_IRQ_ALL	0x0fff007f
#define V3D_CSD_BO_MAX	64

struct bcmv3d_job {
	kmutex_t lock;
	struct bcmv3d_bo *pt, *scratch;
	/* Retained on poison: never release possibly GPU-owned storage. */
	struct bcmv3d_bo *src, *dst;
	struct bcmv3d_bo *bos[V3D_CSD_BO_MAX];
	unsigned int nbo;
	void *ih, *core_ih;
	const char *stage;
	bool published, poisoned, controller_masked, core_masked;
	volatile unsigned int events, irq_error, core_events, core_error;
	uint32_t csd_before, csd_after, csd_irq;
};

static void
bcmv3d_core_mask(struct bcmv3d_job *job)
{

	if (!job->core_masked) {
		acpi_intr_mask(job->core_ih);
		job->core_masked = true;
	}
	xc_barrier(0);
}

static int
bcmv3d_core_intr(void *arg)
{
	struct bcmv3d_job *job = arg;
	uint32_t status;
	int error;

	error = bcmv3d_takeover_core_poke(V3D_MSK_SET, V3D_CORE_IRQ_ALL);
	if (error == 0)
		error = bcmv3d_takeover_core_peek(V3D_INT_STS, &status);
	if (error == 0 && status != V3D_CSD_DONE)
		error = EIO;
	if (error == 0)
		error = bcmv3d_takeover_core_poke(V3D_INT_CLR, V3D_CSD_DONE);
	if (error != 0)
		atomic_cas_uint(&job->core_error, 0, error);
	atomic_inc_uint(&job->core_events);
	wakeup(job);
	return 1;
}

static void
bcmv3d_mask(struct bcmv3d_job *job)
{

	/* ARM PIC intr_mask/unmask is reference-counted, not idempotent. */
	if (!job->controller_masked) {
		acpi_intr_mask(job->ih);
		job->controller_masked = true;
	}
	xc_barrier(0);
}

static void
bcmv3d_sync(struct bcmv3d_bo *bo, int ops)
{

	bus_dmamap_sync(bo->cma.dmat, bo->cma.dmamap, 0,
	    bo->cma.dmasize, ops);
}

static int
bcmv3d_hub_intr(void *arg)
{
	struct bcmv3d_job *job = arg;
	uint32_t status;
	int error;

	/* Self-mask even a spurious line: the submitter decides its outcome. */
	error = bcmv3d_takeover_hub_poke(V3D_MSK_SET, V3D_IRQ_ALL);
	if (error == 0)
		error = bcmv3d_takeover_hub_peek(V3D_INT_STS, &status);
	if (error == 0 && status != V3D_TFUC)
		error = EIO;
	if (error == 0)
		error = bcmv3d_takeover_hub_poke(V3D_INT_CLR, V3D_TFUC);
	if (error != 0)
		atomic_cas_uint(&job->irq_error, 0, error);
	atomic_inc_uint(&job->events);
	wakeup(job);
	return 1;
}

static int
bcmv3d_clean(void)
{
	uint32_t status, mmu;
	int error;

	error = bcmv3d_takeover_hub_peek(V3D_INT_STS, &status);
	if (error == 0)
		error = bcmv3d_takeover_hub_peek(V3D_MMU, &mmu);
	if (error != 0)
		return error;
	return status == 0 && mmu == V3D_MMU_VALUE ? 0 : EIO;
}

static int
bcmv3d_flush(void)
{
	const struct { uint32_t reg, value, busy; } flush[] = {
		{ V3D_MMUC, 3, 4 }, { V3D_MMU, V3D_MMU_VALUE | 4, 128 }
	};
	uint32_t value;
	unsigned int i, n;
	int error;

	for (i = 0; i < __arraycount(flush); i++) {
		error = bcmv3d_takeover_hub_poke(flush[i].reg, flush[i].value);
		if (error != 0)
			return error;
		for (n = 0; n < 1000; n++) {
			error = bcmv3d_takeover_hub_peek(flush[i].reg, &value);
			if (error != 0)
				return error;
			if ((value & flush[i].busy) == 0)
				break;
			delay(100);
		}
		if (n == 1000)
			return ETIMEDOUT;
	}
	return bcmv3d_clean();
}

static void
bcmv3d_ptes(struct bcmv3d_job *job, bool map)
{
	uint32_t *pt = job->pt->cma.vaddr;
	struct bcmv3d_bo *objects[] = { job->src, job->dst };
	unsigned int i, page;

	for (i = 0; i < (job->nbo ? job->nbo : __arraycount(objects)); i++) {
		struct bcmv3d_bo *bo = job->nbo ? job->bos[i] : objects[i];
		uint32_t pfn = bo->cma.dmamap->dm_segs[0].ds_addr >> 12;
		size_t length = job->nbo ? bo->cma.dmasize : V3D_COPY_SIZE;

		for (page = 0; page < length / PAGE_SIZE; page++)
			pt[(bo->gpu >> 12) + page] = map ? (pfn + page) |
			    V3D_PTE_VALID | (job->nbo || i == 1 ? V3D_PTE_WRITE : 0) : 0;
	}
	bcmv3d_sync(job->pt, BUS_DMASYNC_PREWRITE);
}

static int
bcmv3d_publish(struct bcmv3d_job *job)
{
	uint32_t pt = job->pt->cma.dmamap->dm_segs[0].ds_addr >> 12;
	uint32_t sink = (job->scratch->cma.dmamap->dm_segs[0].ds_addr +
	    PAGE_SIZE) >> 12;
	const struct { uint32_t reg, value; } setup[] = {
		{ V3D_PT_BASE, pt }, { V3D_MMU, V3D_MMU_VALUE },
		{ V3D_ILLEGAL, sink | (1U << 31) }, { V3D_MMUC, 1 }
	};
	unsigned int i;
	int error;

	if (!job->published) {
		/* A failed write may still have reached the device. */
		job->published = true;
		for (i = 0; i < __arraycount(setup); i++) {
			error = bcmv3d_takeover_hub_poke(setup[i].reg,
			    setup[i].value);
			if (error != 0)
				return error;
		}
	}
	return bcmv3d_flush();
}

static int
bcmv3d_payload(const struct drm_v3d_submit_tfu *a,
    const struct bcmv3d_bo *src, const struct bcmv3d_bo *dst)
{

	if (src == dst || src->cma.dmasize < V3D_COPY_SIZE ||
	    dst->cma.dmasize < V3D_COPY_SIZE ||
	    a->iia != src->gpu || a->ioa != dst->gpu ||
	    a->icfg != 0x001d0001 || a->iis != 64 ||
	    a->ios != 0x00400040 || a->v71.ioc != 0x00400000 ||
	    a->ica != 0 || a->iua != 0 || a->coef[0] != 0 ||
	    a->coef[1] != 0 || a->coef[2] != 0 || a->coef[3] != 0 ||
	    a->bo_handles[2] != 0 || a->bo_handles[3] != 0 ||
	    a->in_sync != 0 || a->out_sync != 0 || a->flags != 0 ||
	    a->extensions != 0 || a->v71.pad != 0)
		return EINVAL;
	return 0;
}

static int
bcmv3d_execute(struct bcmv3d_job *job, struct drm_v3d_submit_tfu *a)
{
	const struct { uint32_t reg, value; } payload[] = {
		{ 0x70c, a->iia }, { 0x714, a->iis }, { 0x710, a->ica },
		{ 0x718, a->iua }, { 0x720, a->ioa }, { 0x71c, a->v71.ioc },
		{ 0x724, a->ios }, { 0x728, 0 }, { 0x72c, 0 },
		{ 0x730, 0 }, { 0x734, 0 }
	};
	uint32_t before, after;
	unsigned int i;
	int error;

	job->stage = "idle boundary";
	bcmv3d_mask(job);
	atomic_store_relaxed(&job->events, 0);
	atomic_store_relaxed(&job->irq_error, 0);
	error = bcmv3d_takeover_hub_peek(V3D_TFU_CS, &before);
	if (error != 0 || (before & 1) != 0)
		return error != 0 ? error : EBUSY;
	/* Do not clear latched faults from a previous operation. */
	if (job->published && (error = bcmv3d_clean()) != 0)
		return error;
	bcmv3d_sync(job->src, BUS_DMASYNC_PREWRITE);
	bcmv3d_sync(job->dst, BUS_DMASYNC_PREREAD);
	bcmv3d_sync(job->scratch, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	bcmv3d_ptes(job, true);
	job->stage = "MMU publication";
	error = bcmv3d_publish(job);
	if (error != 0)
		return error;
	job->stage = "TFU register setup";
	for (i = 0; i < __arraycount(payload); i++) {
		error = bcmv3d_takeover_hub_poke(payload[i].reg, payload[i].value);
		if (error != 0)
			return error;
	}
	job->stage = "TFU start";
	KASSERT(job->controller_masked);
	acpi_intr_unmask(job->ih);
	job->controller_masked = false;
	error = bcmv3d_takeover_hub_poke(V3D_MSK_CLR, V3D_IRQ_ALL);
	if (error == 0)
		error = bcmv3d_takeover_hub_poke(V3D_ICFG, a->icfg);
	if (error != 0)
		return error;
	job->stage = "completion interrupt";
	for (i = 0; i < 5 && atomic_load_acquire(&job->events) == 0; i++)
		tsleep(job, PWAIT, "v3dtfu", MAX(1, mstohz(100)));
	bcmv3d_mask(job);
	error = bcmv3d_takeover_hub_poke(V3D_MSK_SET, V3D_IRQ_ALL);
	if (error != 0)
		return error;
	if (atomic_load_acquire(&job->events) != 1 ||
	    atomic_load_acquire(&job->irq_error) != 0)
		return EIO;
	job->stage = "completion boundary";
	error = bcmv3d_takeover_hub_peek(V3D_TFU_CS, &after);
	if (error != 0)
		return error;
	if ((after & 1) != 0 || (uint8_t)((after >> 16) -
	    (before >> 16)) != 1)
		return EIO;
	error = bcmv3d_clean();
	if (error != 0)
		return error;
	job->stage = "scratch integrity";
	bcmv3d_sync(job->scratch, BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	for (i = 0; i < 3 * PAGE_SIZE / sizeof(uint32_t); i++) {
		uint32_t expected = i < PAGE_SIZE / 4 || i >= PAGE_SIZE / 2 ?
		    0xa5c35a3c : 0;

		if (((uint32_t *)job->scratch->cma.vaddr)[i] != expected)
			return EIO;
	}
	/* Retirement alone does not revoke access through the MMU. */
	job->stage = "PTE revocation";
	bcmv3d_ptes(job, false);
	error = bcmv3d_flush();
	if (error != 0)
		return error;
	bcmv3d_sync(job->src, BUS_DMASYNC_POSTWRITE);
	bcmv3d_sync(job->dst, BUS_DMASYNC_POSTREAD);
	return 0;
}

static void
bcmv3d_diagnose(struct bcmv3d_device *sc)
{
	struct bcmv3d_job *job = sc->job;
	static const uint32_t regs[] = {
		V3D_INT_STS, 0x5c, V3D_TFU_CS, V3D_MMUC, V3D_MMU,
		V3D_PT_BASE, V3D_ILLEGAL, 0x122c, 0x1234
	};
	uint32_t value;
	unsigned int i;
	int error;

	aprint_normal_dev(sc->dev, "TFU stopped at %s: events=%u irq-error=%u "
	    "source=%#x destination=%#x\n", job->stage,
	    atomic_load_acquire(&job->events), atomic_load_acquire(&job->irq_error),
	    (uint32_t)job->src->gpu, (uint32_t)job->dst->gpu);
	for (i = 0; i < __arraycount(regs); i++) {
		value = 0;
		error = bcmv3d_takeover_hub_peek(regs[i], &value);
		aprint_normal_dev(sc->dev, "HUB[%#x]=%#x read-error=%d\n",
		    regs[i], value, error);
	}
}

int
bcmv3d_submit_tfu(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct bcmv3d_device *sc = dev->dev_private;
	struct bcmv3d_job *job = sc->job;
	struct drm_v3d_submit_tfu *args = data;
	struct drm_gem_object *src, *dst;
	int error;

	dst = drm_gem_object_lookup(file, args->bo_handles[0]);
	if (dst == NULL)
		return -ENOENT;
	src = drm_gem_object_lookup(file, args->bo_handles[1]);
	if (src == NULL) {
		drm_gem_object_put_unlocked(dst);
		return -ENOENT;
	}
	error = bcmv3d_payload(args, bcmv3d_to_bo(src), bcmv3d_to_bo(dst));
	if (error != 0)
		goto release;
	mutex_enter(&job->lock);
	if (job->poisoned) {
		error = EIO;
		goto unlock;
	}
	job->src = bcmv3d_to_bo(src);
	job->dst = bcmv3d_to_bo(dst);
	atomic_store_release(&job->src->busy, 1);
	atomic_store_release(&job->dst->busy, 1);
	error = bcmv3d_execute(job, args);
	if (error != 0) {
		job->poisoned = true;
		(void)bcmv3d_takeover_hub_poke(V3D_MSK_SET, V3D_IRQ_ALL);
		bcmv3d_mask(job);
		atomic_store_release(&job->src->error, EIO);
		atomic_store_release(&job->dst->error, EIO);
		bcmv3d_diagnose(sc);
		aprint_error_dev(sc->dev, "TFU engine stopped: %d; "
		    "buffers retained until reboot\n", error);
		/* Conservatively retain even failures before first publication. */
		mutex_exit(&job->lock);
		return -error;
	}
	atomic_store_release(&job->src->busy, 0);
	atomic_store_release(&job->dst->busy, 0);
	job->src = job->dst = NULL;
unlock:
	mutex_exit(&job->lock);
release:
	drm_gem_object_put_unlocked(src);
	drm_gem_object_put_unlocked(dst);
	return -error;
}

/* V3D 7.1 cache protocol, checked against Mesa and Linux V3D registers. */
static int
bcmv3d_cache_wait(uint32_t value, uint32_t busy)
{
	uint32_t state;
	unsigned int n;
	int error;

	error = bcmv3d_takeover_core_poke(0x30, value);
	if (error != 0)
		return error;
	for (n = 0; n < 1000; n++) {
		error = bcmv3d_takeover_core_peek(0x30, &state);
		if (error != 0)
			return error;
		if ((state & busy) == 0)
			return 0;
		delay(100);
	}
	return ETIMEDOUT;
}

static bool
bcmv3d_csd_address(struct bcmv3d_job *job, uint32_t address, size_t size)
{
	unsigned int i;

	for (i = 0; i < job->nbo; i++) {
		struct bcmv3d_bo *bo = job->bos[i];

		if (address >= bo->gpu &&
		    (uint64_t)address + size <= bo->gpu + bo->cma.dmasize)
			return true;
	}
	return false;
}

static int
bcmv3d_csd_execute(struct bcmv3d_job *job, struct drm_v3d_submit_csd *a)
{
	uint32_t irq_before, irq_after;
	unsigned int i;
	int error;

	job->stage = "CSD idle boundary";
	bcmv3d_mask(job);
	bcmv3d_core_mask(job);
	atomic_store_relaxed(&job->events, 0);
	atomic_store_relaxed(&job->irq_error, 0);
	atomic_store_relaxed(&job->core_events, 0);
	atomic_store_relaxed(&job->core_error, 0);
	job->csd_before = job->csd_after = job->csd_irq = 0;
	error = bcmv3d_takeover_core_peek(V3D_CSD_STATUS, &job->csd_before);
	if (error == 0)
		error = bcmv3d_takeover_core_peek(V3D_INT_STS, &irq_before);
	if (error != 0)
		return error;
	/* The reset takeover and this lock give us exclusive queue ownership. */
	if (irq_before != 0)
		return EBUSY;
	if (job->published && (error = bcmv3d_clean()) != 0)
		return error;
	for (i = 0; i < job->nbo; i++)
		bcmv3d_sync(job->bos[i], BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	bcmv3d_sync(job->scratch, BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	bcmv3d_ptes(job, true);
	job->stage = "CSD MMU publication";
	error = bcmv3d_publish(job);
	if (error != 0)
		return error;
	/* All addresses, then outer-to-inner invalidation before any fetch. */
	job->stage = "CSD cache invalidate";
	error = bcmv3d_takeover_core_poke(0x34, 0);
	if (error == 0)
		error = bcmv3d_takeover_core_poke(0x38, UINT32_MAX);
	if (error == 0)
		error = bcmv3d_cache_wait(1, 1);
	if (error == 0)
		error = bcmv3d_takeover_core_poke(0x24, 0x0f0f0f0f);
	if (error != 0)
		return error;
	job->stage = "CSD register setup";
	for (i = 1; i <= 7; i++) {
		error = bcmv3d_takeover_core_poke(V3D_CSD_CFG0 + 4 * i,
		    i == 7 ? 0 : a->cfg[i]);
		if (error != 0)
			return error;
	}
	job->stage = "CSD start";
	KASSERT(job->controller_masked && job->core_masked);
	acpi_intr_unmask(job->ih);
	job->controller_masked = false;
	acpi_intr_unmask(job->core_ih);
	job->core_masked = false;
	error = bcmv3d_takeover_hub_poke(V3D_MSK_CLR, V3D_IRQ_ALL);
	if (error == 0)
		error = bcmv3d_takeover_core_poke(V3D_MSK_CLR, V3D_CORE_IRQ_ALL);
	if (error == 0)
		error = bcmv3d_takeover_core_poke(V3D_CSD_CFG0, a->cfg[0]);
	if (error != 0)
		return error;
	job->stage = "CSD completion interrupt";
	for (i = 0; i < 5 && atomic_load_acquire(&job->core_events) == 0 &&
	    atomic_load_acquire(&job->events) == 0; i++)
		tsleep(job, PWAIT, "v3dcsd", MAX(1, mstohz(100)));
	bcmv3d_mask(job);
	bcmv3d_core_mask(job);
	error = bcmv3d_takeover_core_poke(V3D_MSK_SET, V3D_CORE_IRQ_ALL);
	if (error == 0)
		error = bcmv3d_takeover_hub_poke(V3D_MSK_SET, V3D_IRQ_ALL);
	if (error != 0)
		return error;
	if (atomic_load_acquire(&job->core_events) != 1 ||
	    atomic_load_acquire(&job->core_error) != 0 ||
	    atomic_load_acquire(&job->events) != 0)
		return EIO;
	job->stage = "CSD retirement";
	error = bcmv3d_takeover_core_peek(V3D_CSD_STATUS, &job->csd_after);
	if (error == 0)
		error = bcmv3d_takeover_core_peek(V3D_INT_STS, &irq_after);
	if (error != 0)
		return error;
	job->csd_irq = irq_after;
	/*
	 * Linux retires CSD by CSDDONE followed by cache cleaning. Its older
	 * CSD_STATUS counter/idle fields do not describe the observed V3D 7.1
	 * value (0x10002080 after one job). Keep raw status for diagnostics;
	 * do not invent a field layout or treat it as an ownership proof.
	 */
	if (irq_after != 0)
		return EIO;
	if ((error = bcmv3d_clean()) != 0)
		return error;
	/* CSDDONE does not mean the shader's writes have reached memory. */
	job->stage = "CSD cache clean";
	error = bcmv3d_cache_wait(0x100, 0x100);
	if (error == 0)
		error = bcmv3d_cache_wait(5, 1);
	if (error != 0)
		return error;
	job->stage = "CSD scratch integrity";
	bcmv3d_sync(job->scratch, BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	for (i = 0; i < 3 * PAGE_SIZE / sizeof(uint32_t); i++) {
		uint32_t expected = i < PAGE_SIZE / 4 || i >= PAGE_SIZE / 2 ?
		    0xa5c35a3c : 0;

		if (((uint32_t *)job->scratch->cma.vaddr)[i] != expected)
			return EIO;
	}
	job->stage = "CSD PTE revocation";
	bcmv3d_ptes(job, false);
	if ((error = bcmv3d_flush()) != 0)
		return error;
	for (i = 0; i < job->nbo; i++)
		bcmv3d_sync(job->bos[i], BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
	return 0;
}

int
bcmv3d_submit_csd(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_v3d_submit_csd *a = data;
	struct bcmv3d_device *sc;
	struct bcmv3d_job *job;
	struct drm_gem_object *objects[V3D_CSD_BO_MAX];
	uint32_t handles[V3D_CSD_BO_MAX];
	unsigned int i, j, count = 0;
	int error;

	/* No extensions, fences or performance objects are silently ignored. */
	if (a->flags || a->pad || a->extensions || a->in_sync || a->out_sync ||
	    a->perfmon_id || a->bo_handle_count == 0 ||
	    a->bo_handle_count > V3D_CSD_BO_MAX ||
	    a->coef[0] || a->coef[1] || a->coef[2] || a->coef[3] ||
	    (a->cfg[0] >> 16) == 0 || (a->cfg[1] >> 16) == 0 ||
	    (a->cfg[2] >> 16) == 0 || (a->cfg[3] & 0xfc000000) != 0 ||
	    a->cfg[4] == UINT32_MAX || (a->cfg[5] & 4) || (a->cfg[6] & 3))
		return -EINVAL;
	error = copyin((const void *)(uintptr_t)a->bo_handles, handles,
	    a->bo_handle_count * sizeof(handles[0]));
	if (error != 0)
		return -error;
	for (i = 0; i < a->bo_handle_count; i++) {
		objects[count] = drm_gem_object_lookup(file, handles[i]);
		if (objects[count] == NULL) {
			error = ENOENT;
			goto release;
		}
		count++;
		for (j = 0; j < i; j++) {
			if (objects[j] == objects[i]) {
				error = EINVAL;
				goto release;
			}
		}
	}
	sc = dev->dev_private;
	job = sc->job;
	mutex_enter(&job->lock);
	if (job->poisoned) {
		error = EIO;
		goto unlock;
	}
	job->nbo = count;
	for (i = 0; i < count; i++)
		job->bos[i] = bcmv3d_to_bo(objects[i]);
	if (!bcmv3d_csd_address(job, a->cfg[5] & ~7U, 8) ||
	    !bcmv3d_csd_address(job, a->cfg[6], 4)) {
		error = EINVAL;
		goto clear;
	}
	for (i = 0; i < count; i++)
		atomic_store_release(&job->bos[i]->busy, 1);
	error = bcmv3d_csd_execute(job, a);
	if (error != 0) {
		uint32_t state = 0;

		job->poisoned = true;
		(void)bcmv3d_takeover_hub_poke(V3D_MSK_SET, V3D_IRQ_ALL);
		(void)bcmv3d_takeover_core_poke(V3D_MSK_SET, V3D_CORE_IRQ_ALL);
		bcmv3d_mask(job);
		bcmv3d_core_mask(job);
		for (i = 0; i < count; i++)
			atomic_store_release(&job->bos[i]->error, EIO);
		(void)bcmv3d_takeover_core_peek(V3D_CSD_STATUS, &state);
		aprint_error_dev(sc->dev, "%s: error=%d status=%#x "
		    "before=%#x after=%#x irq-after=%#x "
		    "core-events=%u core-error=%u hub-events=%u; "
		    "%u buffers retained until reboot\n", job->stage, error,
		    state, job->csd_before, job->csd_after, job->csd_irq,
		    atomic_load_acquire(&job->core_events),
		    atomic_load_acquire(&job->core_error),
		    atomic_load_acquire(&job->events), count);
		mutex_exit(&job->lock);
		return -error;
	}
	for (i = 0; i < count; i++)
		atomic_store_release(&job->bos[i]->busy, 0);
clear:
	memset(job->bos, 0, sizeof(job->bos));
	job->nbo = 0;
unlock:
	mutex_exit(&job->lock);
release:
	while (count != 0)
		drm_gem_object_put_unlocked(objects[--count]);
	return -error;
}

int
bcmv3d_wait_bo(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_v3d_wait_bo *args = data;
	struct drm_gem_object *obj;
	struct bcmv3d_bo *bo;
	struct timespec start, now, elapsed;
	uint64_t ns;
	int error = 0;

	if (args->pad != 0)
		return -EINVAL;
	obj = drm_gem_object_lookup(file, args->handle);
	if (obj == NULL)
		return -ENOENT;
	bo = bcmv3d_to_bo(obj);
	getnanouptime(&start);
	for (;;) {
		if (atomic_load_acquire(&bo->error) != 0) {
			error = EIO;
			break;
		}
		if (atomic_load_acquire(&bo->busy) == 0)
			break;
		getnanouptime(&now);
		timespecsub(&now, &start, &elapsed);
		ns = (uint64_t)elapsed.tv_sec * 1000000000 + elapsed.tv_nsec;
		if (ns >= args->timeout_ns) {
			error = ETIME;
			break;
		}
		error = tsleep(bo, PWAIT | PCATCH, "v3dwait", 1);
		if (error != 0 && error != EWOULDBLOCK)
			break;
		error = 0;
	}
	drm_gem_object_put_unlocked(obj);
	return -error;
}

/* Called only before userspace registration; no page table was published. */
void
bcmv3d_job_fini(struct bcmv3d_device *sc)
{
	struct bcmv3d_job *job = sc->job;

	if (job == NULL)
		return;
	KASSERT(!job->published && job->src == NULL && job->dst == NULL &&
	    job->nbo == 0);
	if (job->ih != NULL) {
		bcmv3d_mask(job);
		acpi_intr_disestablish(job->ih);
	}
	if (job->core_ih != NULL) {
		bcmv3d_core_mask(job);
		acpi_intr_disestablish(job->core_ih);
	}
	if (job->scratch != NULL)
		drm_gem_object_put_unlocked(&job->scratch->cma.base);
	if (job->pt != NULL)
		drm_gem_object_put_unlocked(&job->pt->cma.base);
	mutex_destroy(&job->lock);
	kmem_free(job, sizeof(*job));
	sc->job = NULL;
}

int
bcmv3d_job_init(struct bcmv3d_device *sc)
{
	struct acpi_resources res;
	struct acpi_irq *irq;
	struct bcmv3d_job *job;
	struct bcmv3d_bo *bo;
	uint32_t debug, status;
	unsigned int i;
	int error;

	error = bcmv3d_takeover_hub_peek(V3D_MMU_DEBUG, &debug);
	if (error == 0)
		error = bcmv3d_takeover_hub_peek(V3D_INT_STS, &status);
	if (error != 0)
		return error;
	if (((debug >> 8) & 15) < 5 || ((debug >> 4) & 15) < 5 || status != 0)
		return EIO;
	job = sc->job = kmem_zalloc(sizeof(*job), KM_SLEEP);
	mutex_init(&job->lock, MUTEX_DEFAULT, IPL_NONE);
	bo = bcmv3d_bo_alloc(sc, V3D_PT_SIZE);
	if (IS_ERR(bo)) {
		error = -PTR_ERR(bo);
		goto fail;
	}
	job->pt = bo;
	bo = bcmv3d_bo_alloc(sc, 3 * PAGE_SIZE);
	if (IS_ERR(bo)) {
		error = -PTR_ERR(bo);
		goto fail;
	}
	job->scratch = bo;
	for (i = 0; i < 3 * PAGE_SIZE / sizeof(uint32_t); i++)
		((uint32_t *)bo->cma.vaddr)[i] =
		    i < PAGE_SIZE / 4 || i >= PAGE_SIZE / 2 ? 0xa5c35a3c : 0;
	if (ACPI_FAILURE(acpi_resource_parse(sc->dev, sc->handle, "_CRS", &res,
	    &acpi_resource_parse_ops_quiet))) {
		error = ENXIO;
		goto fail;
	}
	irq = acpi_res_irq(&res, 0);
	if (irq != NULL && irq->ar_irq == 282 &&
	    irq->ar_type == ACPI_LEVEL_SENSITIVE)
		job->ih = acpi_intr_establish_irq(sc->dev, irq, IPL_VM, true,
		    bcmv3d_hub_intr, job, "v3d render");
	irq = acpi_res_irq(&res, 1);
	if (irq != NULL && irq->ar_irq == 281 &&
	    irq->ar_type == ACPI_LEVEL_SENSITIVE)
		job->core_ih = acpi_intr_establish_irq(sc->dev, irq, IPL_VM, true,
		    bcmv3d_core_intr, job, "v3d compute");
	acpi_resource_cleanup(&res);
	if (job->ih == NULL || job->core_ih == NULL) {
		error = ENXIO;
		goto fail;
	}
	bcmv3d_mask(job);
	bcmv3d_core_mask(job);
	return 0;
fail:
	bcmv3d_job_fini(sc);
	return error;
}
