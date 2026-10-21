/* Origin: EmberBSD native BCM2712 CL/TFU/CSD submission, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Register facts and accepted payload: bcm2712_v3d_fault.c and its pins. */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/xcall.h>
#include <dev/acpi/acpi_intr.h>
#include <arm/broadcom/bcm2712_v3d_takeover.h>
#include <linux/err.h>
#include <linux/dma-fence.h>
#include <linux/slab.h>
#include <drm/drm_syncobj.h>
#include "bcmv3d_drv.h"
#include "bcmv3d_resv.h"
#include "bcmv3d_cl_reject.h"
#include "bcmv3d_tfu_reject.h"

#define V3D_INT_STS	0x50
#define V3D_INT_CLR	0x58
#define V3D_MSK_SET	0x60
#define V3D_MSK_CLR	0x64
#define V3D_IRQ_ALL	0x7f
#define V3D_TFUC		0x02
#define V3D_TFU_CS	0x700
#define V3D_ICFG		0x708
#define V3D_TFU_IOC	1U
/* Explicit Mesa 26.2.4 V3D71 fields, not full hardware register masks. */
#define V3D_TFU_MESA_ICFG	((7U << 23) | (31U << 16) | V3D_TFU_IOC)
#define V3D_TFU_MESA_IOC	(0xffff0000U | (7U << 12) | (15U << 4) | 1U)
#define V3D_TFU_MESA_MAX_DIM	16384U
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
/* torvalds/linux a5ebb76233b79db01e82e061173cb73c6d2b5c6b, v3d_regs.h. */
#define V3D_CLE_CT1CS	0x104
#define V3D_CLE_CT1EA	0x10c
#define V3D_CLE_CT1CA	0x114
#define V3D_CLE_PCS	0x130
#define V3D_CSD_BO_MAX	BCMV3D_RESV_MAX

struct bcmv3d_job {
	kmutex_t lock;
	kmutex_t irq_lock;
	spinlock_t fence_lock;
	uint64_t fence_context, fence_seqno;
	struct bcmv3d_bo *pt, *scratch;
	/* Retained on poison: never release possibly GPU-owned storage. */
	struct bcmv3d_bo *src, *dst;
	/* TFU role aliases do not own refs; this bounded vector retains them. */
	struct bcmv3d_bo *tfu_bos[4];
	unsigned int ntfu;
	/* Borrowed during a job; owned with all GEM refs after poison. */
	struct drm_gem_object **bos;
	unsigned int nbo;
	/* Per-device, per-boot admission receipts. Never reset or wrap. */
	volatile unsigned int cl_rejections, tfu_rejections;
	void *ih, *core_ih;
	const char *stage;
	bool published, poisoned, controller_masked, core_masked;
	volatile unsigned int events, irq_error, core_events, core_error;
	uint32_t csd_before, csd_after, csd_irq, core_expected;
};

struct bcmv3d_sync {
	struct drm_syncobj *output;
	struct dma_fence *input, *fence;
};

static const char *
bcmv3d_fence_name(struct dma_fence *fence)
{

	return "bcmv3d";
}

static const struct dma_fence_ops bcmv3d_fence_ops = {
	.use_64bit_seqno = true,
	.get_driver_name = bcmv3d_fence_name,
	.get_timeline_name = bcmv3d_fence_name,
};

static void
bcmv3d_sync_fini(struct bcmv3d_sync *sync)
{

	dma_fence_put(sync->input);
	dma_fence_put(sync->fence);
	if (sync->output != NULL)
		drm_syncobj_put(sync->output);
}

/* Retain all dependencies before waiting or replacing any output fence. */
static int
bcmv3d_sync_capture(struct drm_file *file, uint32_t in, uint32_t out,
    struct bcmv3d_sync *sync)
{
	struct drm_syncobj *input;

	if (in != 0) {
		input = drm_syncobj_find(file, in);
		if (input == NULL)
			return ENOENT;
		sync->input = drm_syncobj_fence_get(input);
		drm_syncobj_put(input);
		if (sync->input == NULL)
			return EINVAL;
	}
	if (out != 0) {
		sync->output = drm_syncobj_find(file, out);
		if (sync->output == NULL)
			return ENOENT;
	}
	return 0;
}

/* Never wait with the engine locked: a producer must be able to enter. */
static int
bcmv3d_sync_wait(struct bcmv3d_sync *sync)
{
	long waited;
	int status;

	if (sync->input == NULL)
		return 0;
	waited = dma_fence_wait_timeout(sync->input, true, MAX(1, mstohz(1000)));
	if (waited <= 0)
		return waited == 0 ? ETIME : -waited;
	status = dma_fence_get_status(sync->input);
	return status > 0 ? 0 : status == 0 ? EIO : -status;
}

static int
bcmv3d_sync_prepare(struct drm_file *file, uint32_t in, uint32_t out,
    struct bcmv3d_sync *sync)
{
	int error;

	error = bcmv3d_sync_capture(file, in, out, sync);
	return error != 0 ? error : bcmv3d_sync_wait(sync);
}

/* The job lock orders all fences on the shared CL/TFU/CSD timeline. */
static int
bcmv3d_sync_publish(struct bcmv3d_job *job, struct bcmv3d_sync *sync)
{

	if (job->fence_seqno == UINT64_MAX)
		return EOVERFLOW;
	sync->fence = kzalloc(sizeof(*sync->fence), GFP_KERNEL);
	if (sync->fence == NULL)
		return ENOMEM;
	dma_fence_init(sync->fence, &bcmv3d_fence_ops, &job->fence_lock,
	    job->fence_context, ++job->fence_seqno);
	if (sync->output != NULL)
		drm_syncobj_replace_fence(sync->output, sync->fence);
	return 0;
}

static void
bcmv3d_sync_complete(struct bcmv3d_sync *sync, int error)
{

	if (sync->fence == NULL)
		return;
	if (error != 0)
		dma_fence_set_error(sync->fence, -error);
	dma_fence_signal(sync->fence);
}

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
	if (error == 0 && (job->core_expected == 0 || status != job->core_expected))
		error = EIO;
	if (error == 0)
		error = bcmv3d_takeover_core_poke(V3D_INT_CLR, job->core_expected);
	mutex_enter(&job->irq_lock);
	if (error != 0)
		atomic_cas_uint(&job->core_error, 0, error);
	atomic_inc_uint(&job->core_events);
	wakeup(job);
	mutex_exit(&job->irq_lock);
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
	mutex_enter(&job->irq_lock);
	if (error != 0)
		atomic_cas_uint(&job->irq_error, 0, error);
	atomic_inc_uint(&job->events);
	wakeup(job);
	mutex_exit(&job->irq_lock);
	return 1;
}

/*
 * job->lock serializes submissions. The ISR takes only irq_lock. mtsleep
 * enqueues before dropping that spin mutex, so completion cannot lose a wakeup.
 * Release irq_lock before masking and draining the parent interrupt routes.
 */
static void
bcmv3d_wait_irq(struct bcmv3d_job *job, bool core, const char *wmesg)
{
	const unsigned int budget = MAX(1, mstohz(500));
	const unsigned int slice = MAX(1, mstohz(100));
	unsigned int start = (unsigned int)getticks();

	mutex_enter(&job->irq_lock);
	for (;;) {
		unsigned int elapsed;
		int ticks;

		if (atomic_load_acquire(&job->events) != 0 ||
		    (core && atomic_load_acquire(&job->core_events) != 0))
			break;
		/* Unsigned subtraction also covers a wrap of the tick counter. */
		elapsed = (unsigned int)getticks() - start;
		if (elapsed >= budget)
			break;
		ticks = MIN(slice, budget - elapsed);
		(void)mtsleep(job, PWAIT, wmesg, ticks, &job->irq_lock);
	}
	mutex_exit(&job->irq_lock);
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
	unsigned int count = job->nbo ? job->nbo : job->ntfu;
	unsigned int i;

	for (i = 0; i < count; i++) {
		struct bcmv3d_bo *bo = job->nbo ?
		    bcmv3d_to_bo(job->bos[i]) : job->tfu_bos[i];
		size_t remaining = bo->cma.dmasize;
		unsigned int page = 0;
		int seg;

		for (seg = 0; seg < bo->cma.dmamap->dm_nsegs && remaining; seg++) {
			const bus_dma_segment_t *dma = &bo->cma.dmamap->dm_segs[seg];
			uint32_t pfn = dma->ds_addr >> 12;
			size_t length = MIN(remaining, dma->ds_len);
			size_t offset;

			/* GPU MMU pages are 4 KiB, independent of kernel PAGE_SIZE. */
			for (offset = 0; offset < length; offset += 4096, page++)
				pt[(bo->gpu >> 12) + page] = map ?
				    (pfn + (offset >> 12)) | V3D_PTE_VALID |
				    (job->nbo || bo == job->dst ? V3D_PTE_WRITE : 0) : 0;
			remaining -= length;
		}
		KASSERT(remaining == 0);
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

/* Mesa v3d_tfu.h and v3dx_tfu.c, 26.2.4. This is a software subset.
 * Initial addresses are checked here. MMU containment bounds later accesses;
 * this does not validate the full tiled/mipmap image footprint. */
static int
bcmv3d_payload(const struct drm_v3d_submit_tfu *a,
    const struct bcmv3d_bo *src, const struct bcmv3d_bo *dst)
{
	unsigned int input = (a->icfg >> 23) & 7;
	unsigned int output = (a->v71.ioc >> 12) & 7;
	unsigned int type = (a->icfg >> 16) & 31;
	unsigned int levels = (a->v71.ioc >> 4) & 15;
	unsigned int stride = a->v71.ioc >> 16;
	unsigned int width = a->ios & 0xffff, height = a->ios >> 16;

	if (src == NULL || dst == NULL ||
	    (uint64_t)a->iia < src->gpu ||
	    (uint64_t)a->iia - src->gpu >= src->cma.dmasize ||
	    (uint64_t)a->ioa < dst->gpu ||
	    (uint64_t)a->ioa - dst->gpu >= dst->cma.dmasize ||
	    (a->icfg & ~V3D_TFU_MESA_ICFG) ||
	    (a->v71.ioc & ~V3D_TFU_MESA_IOC) ||
	    width == 0 || height == 0 || width > V3D_TFU_MESA_MAX_DIM ||
	    height > V3D_TFU_MESA_MAX_DIM ||
	    a->ica || a->iua || a->coef[0] || a->coef[1] ||
	    a->coef[2] || a->coef[3] || a->flags || a->extensions || a->v71.pad)
		return EINVAL;
	if ((input != 0 && input < 3) || (output != 0 && output < 3))
		return EINVAL;
	/* R4 is not emitted by the pinned format table. The float32 types
	 * and RGB9_E5 are copy-only in its TFU support helper. */
	if (type > 19 && type != 20 && (type < 29 || type > 31))
		return EINVAL;
	if (levels > 14 || ((a->v71.ioc & 1) != 0) != (levels != 0) ||
	    (levels != 0 && (output == 0 || type > 19)))
		return EINVAL;
	if ((input == 0 && a->iis < width) ||
	    (input >= 6 && a->iis == 0) ||
	    (input >= 3 && input <= 5 && a->iis != 0))
		return EINVAL;
	if ((output == 0 && stride < width) ||
	    (output >= 6 && stride == 0) ||
	    (output >= 3 && output <= 5 && stride != 0))
		return EINVAL;
	return 0;
}

/* Destination RW includes its preserved base level, guards and padding. */
static void
bcmv3d_tfu_sync(struct bcmv3d_job *job, bool before)
{
	unsigned int i;

	for (i = 0; i < job->ntfu; i++) {
		struct bcmv3d_bo *bo = job->tfu_bos[i];
		int ops = before ? BUS_DMASYNC_PREWRITE : BUS_DMASYNC_POSTWRITE;

		if (bo == job->dst)
			ops |= before ? BUS_DMASYNC_PREREAD : BUS_DMASYNC_POSTREAD;
		bcmv3d_sync(bo, ops);
	}
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
	bcmv3d_tfu_sync(job, true);
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
		error = bcmv3d_takeover_hub_poke(V3D_ICFG, a->icfg | V3D_TFU_IOC);
	if (error != 0)
		return error;
	job->stage = "completion interrupt";
	bcmv3d_wait_irq(job, false, "v3dtfu");
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
	bcmv3d_tfu_sync(job, false);
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

/* Metadata is copied while the existing GEM lookup reference is live. */
struct bcmv3d_tfu_reject_bo {
	uint64_t bytes;
	uint32_t gpu;
	bool known;
};

static void
bcmv3d_tfu_reject_snapshot(struct bcmv3d_tfu_reject_bo *info,
    const struct bcmv3d_bo *bo)
{

	info->bytes = bo->cma.dmasize;
	info->gpu = bo->gpu;
	info->known = true;
}

/* This budget is independent of CL receipts and never resets or wraps. */
static bool
bcmv3d_tfu_reject_take(struct bcmv3d_job *job, unsigned int *sample)
{
	unsigned int before;

	for (;;) {
		before = atomic_load_relaxed(&job->tfu_rejections);
		if (before >= BCMV3D_TFU_REJECT_MAX)
			return false;
		if (atomic_cas_uint(&job->tfu_rejections, before, before + 1) ==
		    before) {
			*sample = before + 1;
			return true;
		}
	}
}

/* No BO references, user pointers or register reads are needed to print. */
static void
bcmv3d_tfu_reject(struct bcmv3d_device *sc,
    const struct drm_v3d_submit_tfu *a, enum bcmv3d_tfu_reject_reason reason,
    int error, const struct bcmv3d_tfu_reject_bo *src,
    const struct bcmv3d_tfu_reject_bo *dst)
{
	static const char * const names[] = {
		"none", "dst-lookup", "src-lookup", "payload", "sync",
		"reservation", "poisoned", "fence-publish", "resv-publish"
	};
	unsigned int i, count = 0, sample;

	if (error == 0 || reason <= BCMV3D_TFU_REJECT_NONE ||
	    (unsigned int)reason >= __arraycount(names))
		return;
	if (!bcmv3d_tfu_reject_take(sc->job, &sample))
		return;
	for (i = 0; i < __arraycount(a->bo_handles); i++)
		count += a->bo_handles[i] != 0;
	aprint_error_dev(sc->dev, "TFU rejected: sample=%u reason=%s errno=%d "
	    "bos=%u flags=%#x pad=%#x extensions-present=%u "
	    "icfg=%#x iia=%#x ica=%#x iis=%#x iua=%#x "
	    "ioa=%#x ios=%#x ioc=%#x "
	    "coef0=%#x coef1=%#x coef2=%#x coef3=%#x in=%u out=%u "
	    "src-known=%u src-bytes=%llu src-gpu=%#x "
	    "dst-known=%u dst-bytes=%llu dst-gpu=%#x\n",
	    sample, names[reason], error, count, a->flags, a->v71.pad,
	    (unsigned int)(a->extensions != 0), a->icfg, a->iia, a->ica,
	    a->iis, a->iua, a->ioa, a->ios, a->v71.ioc, a->coef[0],
	    a->coef[1], a->coef[2], a->coef[3], a->in_sync, a->out_sync,
	    (unsigned int)src->known, (unsigned long long)src->bytes, src->gpu,
	    (unsigned int)dst->known, (unsigned long long)dst->bytes, dst->gpu);
}

/* One reference per unique native object. No user handle array is read. */
static int
bcmv3d_tfu_lookup(struct bcmv3d_device *sc, struct drm_file *file,
    const struct drm_v3d_submit_tfu *a, struct drm_gem_object **objects,
    unsigned int *count, struct bcmv3d_bo **destination,
    enum bcmv3d_tfu_reject_reason *reason)
{
	unsigned int i, j;
	bool tail = false;
	int error;

	*count = 0;
	*destination = NULL;
	*reason = BCMV3D_TFU_REJECT_DST_LOOKUP;
	if (a->bo_handles[0] == 0)
		return ENOENT;
	*reason = BCMV3D_TFU_REJECT_PAYLOAD;
	for (i = 0; i < __arraycount(a->bo_handles); i++) {
		if (a->bo_handles[i] == 0)
			tail = true;
		else if (tail)
			return EINVAL;
	}
	for (i = 0; i < __arraycount(a->bo_handles) && a->bo_handles[i]; i++) {
		struct drm_gem_object *obj;
		struct bcmv3d_bo *bo;

		*reason = i == 0 ? BCMV3D_TFU_REJECT_DST_LOOKUP :
		    BCMV3D_TFU_REJECT_SRC_LOOKUP;
		obj = drm_gem_object_lookup(file, a->bo_handles[i]);
		if (obj == NULL)
			return ENOENT;
		error = bcmv3d_bo_reference(sc, obj, &bo);
		/* The acquired native reference replaces the lookup reference. */
		drm_gem_object_put_unlocked(obj);
		if (error != 0)
			return error;
		/* Private translation/sink storage is never an admitted image. */
		if (bo == sc->job->pt || bo == sc->job->scratch) {
			drm_gem_object_put_unlocked(obj);
			return EINVAL;
		}
		if (i == 0)
			*destination = bo;
		for (j = 0; j < *count; j++)
			if (objects[j] == obj)
				break;
		if (j == *count)
			objects[(*count)++] = obj;
		else
			drm_gem_object_put_unlocked(obj);
	}
	return 0;
}

/* Initial addresses are inside admitted BOs; the MMU bounds the full access. */
static struct bcmv3d_bo *
bcmv3d_tfu_address(struct drm_gem_object **objects, unsigned int count,
    uint32_t address)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		struct bcmv3d_bo *bo = bcmv3d_to_bo(objects[i]);

		if ((uint64_t)address >= bo->gpu &&
		    (uint64_t)address - bo->gpu < bo->cma.dmasize)
			return bo;
	}
	return NULL;
}

int
bcmv3d_submit_tfu(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct bcmv3d_device *sc = dev->dev_private;
	struct bcmv3d_job *job = sc->job;
	struct drm_v3d_submit_tfu *args = data;
	struct drm_gem_object *objects[4] = {0};
	struct bcmv3d_bo *src, *dst;
	struct bcmv3d_resv reservations = {0};
	struct bcmv3d_sync sync = {0};
	struct bcmv3d_tfu_reject_bo source = {0}, destination = {0};
	enum bcmv3d_tfu_reject_reason reason;
	unsigned int count = 0, i;
	int error;

	error = bcmv3d_tfu_lookup(sc, file, args, objects, &count, &dst, &reason);
	if (dst != NULL)
		bcmv3d_tfu_reject_snapshot(&destination, dst);
	src = bcmv3d_tfu_address(objects, count, args->iia);
	if (src != NULL)
		bcmv3d_tfu_reject_snapshot(&source, src);
	if (error != 0)
		goto release;
	reason = BCMV3D_TFU_REJECT_PAYLOAD;
	if (src == NULL || bcmv3d_tfu_address(objects, count, args->ioa) != dst) {
		error = EINVAL;
		goto release;
	}
	error = bcmv3d_payload(args, src, dst);
	if (error != 0)
		goto release;
	reason = BCMV3D_TFU_REJECT_SYNC;
	error = bcmv3d_sync_prepare(file, args->in_sync, args->out_sync, &sync);
	if (error != 0)
		goto release;
	reason = BCMV3D_TFU_REJECT_RESERVATION;
	error = bcmv3d_resv_prepare(&reservations, objects, count,
	    MAX(1, mstohz(1000)));
	if (error != 0)
		goto release;
	mutex_enter(&job->lock);
	reason = BCMV3D_TFU_REJECT_POISONED;
	if (job->poisoned) {
		error = EIO;
		goto unlock;
	}
	KASSERT(job->ntfu == 0 && job->src == NULL && job->dst == NULL &&
	    job->nbo == 0 && job->bos == NULL);
	for (i = 0; i < __arraycount(job->tfu_bos); i++)
		KASSERT(job->tfu_bos[i] == NULL);
	reason = BCMV3D_TFU_REJECT_FENCE_PUBLISH;
	error = bcmv3d_sync_publish(job, &sync);
	if (error != 0)
		goto unlock;
	reason = BCMV3D_TFU_REJECT_RESV_PUBLISH;
	error = bcmv3d_resv_publish(&reservations, sync.fence);
	if (error != 0)
		goto unlock;
	reason = BCMV3D_TFU_REJECT_NONE;
	/* Completion cannot need a reservation held by this submission. */
	bcmv3d_resv_unlock(&reservations);
	job->src = src;
	job->dst = dst;
	job->ntfu = count;
	for (i = 0; i < count; i++) {
		job->tfu_bos[i] = bcmv3d_to_bo(objects[i]);
		atomic_store_release(&job->tfu_bos[i]->busy, 1);
	}
	error = bcmv3d_execute(job, args);
	if (error != 0) {
		job->poisoned = true;
		(void)bcmv3d_takeover_hub_poke(V3D_MSK_SET, V3D_IRQ_ALL);
		bcmv3d_mask(job);
		for (i = 0; i < count; i++)
			atomic_store_release(&job->tfu_bos[i]->error, EIO);
		bcmv3d_diagnose(sc);
		aprint_error_dev(sc->dev, "TFU engine stopped: %d; "
		    "%u buffers retained until reboot\n", error, count);
		/* The persistent TFU vector retains every unique reference. */
		bcmv3d_sync_complete(&sync, error);
		mutex_exit(&job->lock);
		bcmv3d_sync_fini(&sync);
		return -error;
	}
	for (i = 0; i < count; i++) {
		atomic_store_release(&job->tfu_bos[i]->busy, 0);
		job->tfu_bos[i] = NULL;
	}
	job->ntfu = 0;
	job->src = job->dst = NULL;
unlock:
	bcmv3d_resv_unlock(&reservations);
	bcmv3d_sync_complete(&sync, error);
	mutex_exit(&job->lock);
release:
	bcmv3d_resv_unlock(&reservations);
	bcmv3d_sync_fini(&sync);
	while (count != 0)
		drm_gem_object_put_unlocked(objects[--count]);
	bcmv3d_tfu_reject(sc, args, reason, error, &source, &destination);
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
		struct bcmv3d_bo *bo = bcmv3d_to_bo(job->bos[i]);

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
		bcmv3d_sync(bcmv3d_to_bo(job->bos[i]),
		    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
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
	job->core_expected = V3D_CSD_DONE;
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
	bcmv3d_wait_irq(job, true, "v3dcsd");
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
		bcmv3d_sync(bcmv3d_to_bo(job->bos[i]),
		    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
	return 0;
}

static int
bcmv3d_object_order(const void *left, const void *right)
{
	struct drm_gem_object * const *a = left, * const *b = right;
	uintptr_t x = (uintptr_t)*a, y = (uintptr_t)*b;

	return x < y ? -1 : x > y ? 1 : 0;
}

/* Allocate bounded metadata before lookup. Return one ref per vector element. */
static int
bcmv3d_job_lookup(struct drm_file *file, uint64_t user_handles,
    unsigned int capacity, struct drm_gem_object ***result, bool *duplicate)
{
	struct drm_gem_object **objects, *temporary;
	uint32_t *handles;
	unsigned int count = 0, i;
	size_t object_bytes, handle_bytes;
	int error;

	CTASSERT(BCMV3D_RESV_MAX <= INT_MAX);
	CTASSERT(BCMV3D_RESV_MAX <= SIZE_MAX / sizeof(*objects));
	CTASSERT(BCMV3D_RESV_MAX <= SIZE_MAX / sizeof(*handles));
	*result = NULL;
	*duplicate = false;
	if (capacity == 0 || capacity > BCMV3D_RESV_MAX)
		return EINVAL;
	object_bytes = capacity * sizeof(*objects);
	handle_bytes = capacity * sizeof(*handles);
	objects = kmem_zalloc(object_bytes, KM_NOSLEEP);
	if (objects == NULL)
		return ENOMEM;
	handles = kmem_zalloc(handle_bytes, KM_NOSLEEP);
	if (handles == NULL) {
		error = ENOMEM;
		goto release;
	}
	error = copyin((const void *)(uintptr_t)user_handles, handles, handle_bytes);
	if (error == 0) {
		for (i = 0; i < capacity; i++) {
			objects[count] = drm_gem_object_lookup(file, handles[i]);
			if (objects[count] == NULL) {
				error = ENOENT;
				break;
			}
			count++;
		}
	}
	kmem_free(handles, handle_bytes);
	if (error != 0 && error != ENOENT)
		goto release;
	/* Check the live prefix even after lookup failure, preserving EINVAL. */
	if (kheapsort(objects, count, sizeof(*objects), bcmv3d_object_order,
	    &temporary) != 0) {
		error = EIO;
		goto release;
	}
	for (i = 1; i < count; i++) {
		if (objects[i - 1] == objects[i]) {
			*duplicate = true;
			error = EINVAL;
			goto release;
		}
	}
	if (error == 0) {
		*result = objects;
		return 0;
	}
release:
	while (count != 0)
		drm_gem_object_put_unlocked(objects[--count]);
	kmem_free(objects, object_bytes);
	return error;
}

int
bcmv3d_submit_csd(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_v3d_submit_csd *a = data;
	struct bcmv3d_device *sc;
	struct bcmv3d_job *job;
	struct drm_gem_object **objects = NULL;
	struct bcmv3d_resv reservations = {0};
	struct bcmv3d_sync sync = {0};
	unsigned int i, capacity = a->bo_handle_count, count = 0;
	bool duplicate;
	int error;

	/* No extensions or performance objects are silently ignored. */
	if (a->flags || a->pad || a->extensions ||
	    a->perfmon_id || a->bo_handle_count == 0 ||
	    a->bo_handle_count > V3D_CSD_BO_MAX ||
	    a->coef[0] || a->coef[1] || a->coef[2] || a->coef[3] ||
	    (a->cfg[0] >> 16) == 0 || (a->cfg[1] >> 16) == 0 ||
	    (a->cfg[2] >> 16) == 0 || (a->cfg[3] & 0xfc000000) != 0 ||
	    a->cfg[4] == UINT32_MAX || (a->cfg[5] & 4) || (a->cfg[6] & 3))
		return -EINVAL;
	error = bcmv3d_job_lookup(file, a->bo_handles, capacity, &objects,
	    &duplicate);
	if (error != 0)
		goto release;
	count = capacity;
	sc = dev->dev_private;
	job = sc->job;
	error = bcmv3d_sync_prepare(file, a->in_sync, a->out_sync, &sync);
	if (error != 0)
		goto release;
	error = bcmv3d_resv_prepare(&reservations, objects, count,
	    MAX(1, mstohz(1000)));
	if (error != 0)
		goto release;
	mutex_enter(&job->lock);
	if (job->poisoned) {
		error = EIO;
		goto unlock;
	}
	job->nbo = count;
	job->bos = objects;
	if (!bcmv3d_csd_address(job, a->cfg[5] & ~7U, 8) ||
	    !bcmv3d_csd_address(job, a->cfg[6], 4)) {
		error = EINVAL;
		goto clear;
	}
	error = bcmv3d_sync_publish(job, &sync);
	if (error != 0)
		goto clear;
	error = bcmv3d_resv_publish(&reservations, sync.fence);
	if (error != 0)
		goto clear;
	/* Completion cannot need a reservation held by this submission. */
	bcmv3d_resv_unlock(&reservations);
	for (i = 0; i < count; i++)
		atomic_store_release(&bcmv3d_to_bo(job->bos[i])->busy, 1);
	error = bcmv3d_csd_execute(job, a);
	if (error != 0) {
		uint32_t state = 0;

		job->poisoned = true;
		(void)bcmv3d_takeover_hub_poke(V3D_MSK_SET, V3D_IRQ_ALL);
		(void)bcmv3d_takeover_core_poke(V3D_MSK_SET, V3D_CORE_IRQ_ALL);
		bcmv3d_mask(job);
		bcmv3d_core_mask(job);
		for (i = 0; i < count; i++)
			atomic_store_release(&bcmv3d_to_bo(job->bos[i])->error, EIO);
		(void)bcmv3d_takeover_core_peek(V3D_CSD_STATUS, &state);
		aprint_error_dev(sc->dev, "%s: error=%d status=%#x "
		    "before=%#x after=%#x irq-after=%#x "
		    "core-events=%u core-error=%u hub-events=%u; "
		    "%u buffers retained until reboot\n", job->stage, error,
		    state, job->csd_before, job->csd_after, job->csd_irq,
		    atomic_load_acquire(&job->core_events),
		    atomic_load_acquire(&job->core_error),
		    atomic_load_acquire(&job->events), count);
		/* Keep every BO reference, but complete the fence with an error. */
		objects = NULL; /* Transfer the vector with its retained refs. */
		count = 0;
		goto unlock;
	}
	for (i = 0; i < count; i++)
		atomic_store_release(&bcmv3d_to_bo(job->bos[i])->busy, 0);
clear:
	job->bos = NULL;
	job->nbo = 0;
unlock:
	bcmv3d_resv_unlock(&reservations);
	bcmv3d_sync_complete(&sync, error);
	mutex_exit(&job->lock);
release:
	bcmv3d_resv_unlock(&reservations);
	bcmv3d_sync_fini(&sync);
	while (count != 0)
		drm_gem_object_put_unlocked(objects[--count]);
	if (objects != NULL)
		kmem_free(objects, capacity * sizeof(*objects));
	return -error;
}

/* Each CL phase has one completion IRQ; errors never transfer BO ownership. */
static int
bcmv3d_cl_phase(struct bcmv3d_job *job, const struct drm_v3d_submit_cl *a,
    bool bin)
{
	uint32_t status;
	int error;

	bcmv3d_mask(job);
	bcmv3d_core_mask(job);
	atomic_store_relaxed(&job->core_events, 0);
	atomic_store_relaxed(&job->core_error, 0);
	job->core_expected = bin ? 2 : 1; /* FLDONE / FRDONE, V3D 7.1. */
	job->stage = bin ? "CL bin boundary" : "CL render boundary";
	error = bcmv3d_takeover_core_peek(V3D_INT_STS, &status);
	if (error != 0 || status != 0)
		return error != 0 ? error : EBUSY;
	if ((error = bcmv3d_clean()) != 0)
		return error;
	/* Invalidate before both phases, including render-only submissions. */
	error = bcmv3d_takeover_core_poke(0x34, 0);
	if (error == 0)
		error = bcmv3d_takeover_core_poke(0x38, UINT32_MAX);
	if (error == 0)
		error = bcmv3d_cache_wait(1, 1);
	if (error == 0)
		error = bcmv3d_takeover_core_poke(0x24, 0x0f0f0f0f);
	if (error != 0)
		return error;
	if (bin) {
		/* No stale overflow allocation may survive from a previous job. */
		error = bcmv3d_takeover_core_poke(0x30c, 0);
		if (error == 0)
			error = bcmv3d_takeover_core_poke(0x170, a->qma);
		if (error == 0)
			error = bcmv3d_takeover_core_poke(0x174, a->qms);
		if (error == 0)
			error = bcmv3d_takeover_core_poke(0x15c, a->qts | 2);
		if (error != 0)
			return error;
	}
	error = bcmv3d_takeover_core_poke(bin ? 0x160 : 0x164,
	    bin ? a->bcl_start : a->rcl_start);
	if (error != 0)
		return error;
	KASSERT(job->controller_masked && job->core_masked);
	acpi_intr_unmask(job->ih);
	job->controller_masked = false;
	acpi_intr_unmask(job->core_ih);
	job->core_masked = false;
	error = bcmv3d_takeover_hub_poke(V3D_MSK_CLR, V3D_IRQ_ALL);
	if (error == 0)
		error = bcmv3d_takeover_core_poke(V3D_MSK_CLR, V3D_CORE_IRQ_ALL);
	job->stage = bin ? "CL bin execution" : "CL render execution";
	if (error == 0)
		error = bcmv3d_takeover_core_poke(bin ? 0x168 : 0x16c,
		    bin ? a->bcl_end : a->rcl_end);
	if (error != 0)
		return error;
	bcmv3d_wait_irq(job, true, "v3dcl");
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
	error = bcmv3d_takeover_core_peek(V3D_INT_STS, &status);
	if (error != 0 || status != 0)
		return error != 0 ? error : EIO;
	return bcmv3d_clean();
}

static int
bcmv3d_cl_execute(struct bcmv3d_job *job, const struct drm_v3d_submit_cl *a)
{
	unsigned int i;
	int error;

	job->stage = "CL MMU publication";
	bcmv3d_mask(job);
	bcmv3d_core_mask(job);
	atomic_store_relaxed(&job->events, 0);
	atomic_store_relaxed(&job->irq_error, 0);
	for (i = 0; i < job->nbo; i++)
		bcmv3d_sync(bcmv3d_to_bo(job->bos[i]),
		    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	bcmv3d_sync(job->scratch, BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	bcmv3d_ptes(job, true);
	if ((error = bcmv3d_publish(job)) != 0)
		return error;
	if (a->bcl_start != a->bcl_end &&
	    (error = bcmv3d_cl_phase(job, a, true)) != 0)
		return error;
	if ((error = bcmv3d_cl_phase(job, a, false)) != 0)
		return error;
	/* Always clean TMU writes; FLUSH_CACHE requires at least this guarantee. */
	job->stage = "CL cache clean";
	error = bcmv3d_cache_wait(0x100, 0x100);
	if (error == 0)
		error = bcmv3d_cache_wait(5, 1);
	if (error != 0)
		return error;
	job->stage = "CL scratch integrity";
	bcmv3d_sync(job->scratch, BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	for (i = 0; i < 3 * PAGE_SIZE / sizeof(uint32_t); i++) {
		uint32_t expected = i < PAGE_SIZE / 4 || i >= PAGE_SIZE / 2 ?
		    0xa5c35a3c : 0;

		if (((uint32_t *)job->scratch->cma.vaddr)[i] != expected)
			return EIO;
	}
	job->stage = "CL PTE revocation";
	bcmv3d_ptes(job, false);
	if ((error = bcmv3d_flush()) != 0)
		return error;
	for (i = 0; i < job->nbo; i++)
		bcmv3d_sync(bcmv3d_to_bo(job->bos[i]),
		    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
	return 0;
}

/* The caller owns the powered engine and has masked and drained both IRQs. */
static void
bcmv3d_cl_diagnose(struct bcmv3d_device *sc, int error, unsigned int count)
{
	struct bcmv3d_job *job = sc->job;
	static const struct {
		uint32_t offset;
		bool core;
	} regs[] = {
		{ V3D_INT_STS, true }, { V3D_CLE_PCS, true },
		{ V3D_CLE_CT1CS, true }, { V3D_CLE_CT1CA, true },
		{ V3D_CLE_CT1EA, true }, { V3D_MMU, false }
	};
	uint32_t value[__arraycount(regs)] = {0}, failed = 0;
	unsigned int i;

	/* One finite read per register. No acknowledgement or recovery writes. */
	for (i = 0; i < __arraycount(regs); i++) {
		int read_error = regs[i].core ?
		    bcmv3d_takeover_core_peek(regs[i].offset, &value[i]) :
		    bcmv3d_takeover_hub_peek(regs[i].offset, &value[i]);
		if (read_error != 0) {
			value[i] = 0;
			failed |= 1U << i;
		}
	}
	aprint_error_dev(sc->dev, "%s: error=%d irq=%#x pcs=%#x "
	    "ct1cs=%#x ct1ca=%#x ct1ea=%#x mmu=%#x read-failed=%#x "
	    "core-events=%u core-error=%u hub-events=%u; "
	    "%u buffers retained until reboot\n", job->stage, error,
	    value[0], value[1], value[2], value[3], value[4], value[5], failed,
	    atomic_load_acquire(&job->core_events),
	    atomic_load_acquire(&job->core_error),
	    atomic_load_acquire(&job->events), count);
}

/* Claim at most eight receipts without a shared engine or reservation lock. */
static bool
bcmv3d_cl_reject_take(struct bcmv3d_job *job, unsigned int *sample)
{
	unsigned int before;

	for (;;) {
		before = atomic_load_relaxed(&job->cl_rejections);
		if (before >= BCMV3D_CL_REJECT_MAX)
			return false;
		if (atomic_cas_uint(&job->cl_rejections, before, before + 1) ==
		    before) {
			*sample = before + 1;
			return true;
		}
	}
}

/* The receipt contains GPU addresses and scalar UAPI fields, not user pointers. */
static void
bcmv3d_cl_reject(struct bcmv3d_device *sc,
    const struct drm_v3d_submit_cl *a, enum bcmv3d_cl_reject_reason reason,
    int error)
{
	static const char * const names[] = {
		"none", "header", "duplicate", "sync-bcl", "sync-rcl",
		"wait-bcl", "wait-rcl", "reservation", "range",
		"fence-publish", "resv-publish"
	};
	unsigned int sample;

	if (error != EINVAL || reason <= BCMV3D_CL_REJECT_NONE ||
	    (unsigned int)reason >= __arraycount(names))
		return;
	if (!bcmv3d_cl_reject_take(sc->job, &sample))
		return;
	aprint_error_dev(sc->dev, "CL rejected: sample=%u reason=%s errno=%d "
	    "bos=%u flags=%#x pad=%#x perfmon=%u extensions-present=%u "
	    "bcl-start=%#x bcl-end=%#x rcl-start=%#x rcl-end=%#x "
	    "qma=%#x qms=%#x qts=%#x in-bcl=%u in-rcl=%u out=%u\n",
	    sample, names[reason], error, a->bo_handle_count, a->flags, a->pad,
	    a->perfmon_id, (unsigned int)(a->extensions != 0), a->bcl_start,
	    a->bcl_end, a->rcl_start, a->rcl_end, a->qma, a->qms, a->qts,
	    a->in_sync_bcl, a->in_sync_rcl, a->out_sync);
}

int
bcmv3d_submit_cl(struct drm_device *dev, void *data, struct drm_file *file)
{
	const struct drm_v3d_submit_cl *a = data;
	struct bcmv3d_device *sc = dev->dev_private;
	struct bcmv3d_job *job = sc->job;
	struct drm_gem_object **objects = NULL;
	struct bcmv3d_resv reservations = {0};
	struct bcmv3d_sync sync = {0}, render_sync = {0};
	unsigned int i, capacity = a->bo_handle_count, count = 0;
	bool duplicate;
	bool bin = a->bcl_start != a->bcl_end;
	int error;
	enum bcmv3d_cl_reject_reason reason = BCMV3D_CL_REJECT_NONE;

	if ((a->flags & ~DRM_V3D_SUBMIT_CL_FLUSH_CACHE) || a->pad ||
	    a->perfmon_id || a->extensions || a->bo_handle_count == 0 ||
	    a->bo_handle_count > V3D_CSD_BO_MAX || a->rcl_end == 0 ||
	    a->rcl_start == a->rcl_end ||
	    (bin && (a->bcl_end == 0 || !a->qms ||
	    ((a->qma | a->qms | a->qts) & (PAGE_SIZE - 1))))) {
		bcmv3d_cl_reject(sc, a, BCMV3D_CL_REJECT_HEADER, EINVAL);
		return -EINVAL;
	}
	error = bcmv3d_job_lookup(file, a->bo_handles, capacity, &objects,
	    &duplicate);
	if (error != 0) {
		if (duplicate)
			reason = BCMV3D_CL_REJECT_DUPLICATE;
		goto release;
	}
	count = capacity;
	/* Snapshot both dependencies before replacing an aliased output fence. */
	reason = BCMV3D_CL_REJECT_SYNC_BCL;
	error = bcmv3d_sync_capture(file, a->in_sync_bcl, a->out_sync, &sync);
	if (error == 0) {
		reason = BCMV3D_CL_REJECT_SYNC_RCL;
		error = bcmv3d_sync_capture(file, a->in_sync_rcl, 0, &render_sync);
	}
	if (error == 0) {
		reason = BCMV3D_CL_REJECT_WAIT_BCL;
		error = bcmv3d_sync_wait(&sync);
	}
	if (error == 0) {
		reason = BCMV3D_CL_REJECT_WAIT_RCL;
		error = bcmv3d_sync_wait(&render_sync);
	}
	if (error != 0)
		goto release;
	reason = BCMV3D_CL_REJECT_RESERVATION;
	error = bcmv3d_resv_prepare(&reservations, objects, count,
	    MAX(1, mstohz(1000)));
	if (error != 0)
		goto release;
	mutex_enter(&job->lock);
	if (job->poisoned) {
		error = EIO;
		goto unlock;
	}
	job->nbo = count;
	job->bos = objects;
	/* Lists may branch between BOs; validate endpoints, not a linear span. */
	if (!bcmv3d_csd_address(job, a->rcl_start, 1) ||
	    !bcmv3d_csd_address(job, a->rcl_end - 1, 1) ||
	    (bin && (!bcmv3d_csd_address(job, a->bcl_start, 1) ||
	    !bcmv3d_csd_address(job, a->bcl_end - 1, 1) ||
	    !bcmv3d_csd_address(job, a->qma, a->qms) ||
	    !bcmv3d_csd_address(job, a->qts, 64)))) {
		reason = BCMV3D_CL_REJECT_RANGE;
		error = EINVAL;
		goto clear;
	}
	reason = BCMV3D_CL_REJECT_FENCE_PUBLISH;
	error = bcmv3d_sync_publish(job, &sync);
	if (error != 0)
		goto clear;
	reason = BCMV3D_CL_REJECT_RESV_PUBLISH;
	error = bcmv3d_resv_publish(&reservations, sync.fence);
	if (error != 0)
		goto clear;
	/* Execution failures have a separate diagnostic and lifetime contract. */
	reason = BCMV3D_CL_REJECT_NONE;
	/* Completion cannot need a reservation held by this submission. */
	bcmv3d_resv_unlock(&reservations);
	for (i = 0; i < count; i++)
		atomic_store_release(&bcmv3d_to_bo(job->bos[i])->busy, 1);
	error = bcmv3d_cl_execute(job, a);
	if (error != 0) {
		job->poisoned = true;
		(void)bcmv3d_takeover_hub_poke(V3D_MSK_SET, V3D_IRQ_ALL);
		(void)bcmv3d_takeover_core_poke(V3D_MSK_SET, V3D_CORE_IRQ_ALL);
		bcmv3d_mask(job);
		bcmv3d_core_mask(job);
		for (i = 0; i < count; i++)
			atomic_store_release(&bcmv3d_to_bo(job->bos[i])->error, EIO);
		bcmv3d_cl_diagnose(sc, error, count);
		objects = NULL; /* Transfer the vector with its retained refs. */
		count = 0;
		goto unlock;
	}
	for (i = 0; i < count; i++)
		atomic_store_release(&bcmv3d_to_bo(job->bos[i])->busy, 0);
clear:
	job->bos = NULL;
	job->nbo = 0;
unlock:
	bcmv3d_resv_unlock(&reservations);
	bcmv3d_sync_complete(&sync, error);
	mutex_exit(&job->lock);
release:
	bcmv3d_resv_unlock(&reservations);
	bcmv3d_sync_fini(&render_sync);
	bcmv3d_sync_fini(&sync);
	while (count != 0)
		drm_gem_object_put_unlocked(objects[--count]);
	if (objects != NULL)
		kmem_free(objects, capacity * sizeof(*objects));
	bcmv3d_cl_reject(sc, a, reason, error);
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
	unsigned int i;

	if (job == NULL)
		return;
	KASSERT(!job->published && job->src == NULL && job->dst == NULL &&
	    job->nbo == 0 && job->bos == NULL && job->ntfu == 0);
	for (i = 0; i < __arraycount(job->tfu_bos); i++)
		KASSERT(job->tfu_bos[i] == NULL);
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
	mutex_destroy(&job->irq_lock);
	mutex_destroy(&job->lock);
	spin_lock_destroy(&job->fence_lock);
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
	KASSERT(job->nbo == 0 && job->bos == NULL && job->ntfu == 0);
	for (i = 0; i < __arraycount(job->tfu_bos); i++)
		KASSERT(job->tfu_bos[i] == NULL);
	mutex_init(&job->lock, MUTEX_DEFAULT, IPL_NONE);
	mutex_init(&job->irq_lock, MUTEX_DEFAULT, IPL_VM);
	spin_lock_init(&job->fence_lock);
	job->fence_context = dma_fence_context_alloc(1);
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
