/* Origin: EmberBSD native BCM2712 TFU submission, 2026-10-11. */
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

struct bcmv3d_job {
	kmutex_t lock;
	struct bcmv3d_bo *pt, *scratch;
	/* Retained on poison: never release possibly GPU-owned storage. */
	struct bcmv3d_bo *src, *dst;
	void *ih;
	const char *stage;
	bool published, poisoned;
	volatile unsigned int events, irq_error;
};

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

	for (i = 0; i < __arraycount(objects); i++) {
		struct bcmv3d_bo *bo = objects[i];
		uint32_t pfn = bo->cma.dmamap->dm_segs[0].ds_addr >> 12;

		for (page = 0; page < V3D_COPY_SIZE / PAGE_SIZE; page++)
			pt[(bo->gpu >> 12) + page] = map ? (pfn + page) |
			    V3D_PTE_VALID | (i == 1 ? V3D_PTE_WRITE : 0) : 0;
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
	acpi_intr_mask(job->ih);
	xc_barrier(0);
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
	acpi_intr_unmask(job->ih);
	error = bcmv3d_takeover_hub_poke(V3D_MSK_CLR, V3D_IRQ_ALL);
	if (error == 0)
		error = bcmv3d_takeover_hub_poke(V3D_ICFG, a->icfg);
	if (error != 0)
		return error;
	job->stage = "completion interrupt";
	for (i = 0; i < 5 && atomic_load_acquire(&job->events) == 0; i++)
		tsleep(job, PWAIT, "v3dtfu", MAX(1, mstohz(100)));
	acpi_intr_mask(job->ih);
	xc_barrier(0);
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
		acpi_intr_mask(job->ih);
		xc_barrier(0);
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
	KASSERT(!job->published && job->src == NULL && job->dst == NULL);
	if (job->ih != NULL) {
		acpi_intr_mask(job->ih);
		xc_barrier(0);
		acpi_intr_disestablish(job->ih);
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
	acpi_resource_cleanup(&res);
	if (job->ih == NULL) {
		error = ENXIO;
		goto fail;
	}
	acpi_intr_mask(job->ih);
	xc_barrier(0);
	return 0;
fail:
	bcmv3d_job_fini(sc);
	return error;
}
