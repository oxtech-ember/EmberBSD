/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.  The fault path of a TTM
 * buffer object on uvm, written to the interface of include/drm/ttm/ttm_bo.h
 * (MIT) after the 5.6 replacement ttm/ttm_bo_vm.c of this tree (NetBSD
 * Foundation, BSD-2-Clause) that did the same for the old TTM.
 *
 * Linux's ttm_bo_vm.c answers a fault with vmf_insert_pfn_prot on a
 * vm_area_struct; the user mapping of a Linux file in this core
 * (drm618_vma.c) does not carry a TTM object yet, so the fault is a uvm
 * pager of its own:
 *
 *   ttm_bo_uvm_create(bo)  a uvm_object that maps the object; the reference of
 *                          the uvm_object holds one of the buffer object
 *   pgo_fault              reserve the object (try-lock: uvm holds the map
 *                          locks, a failure drops them and the fault is
 *                          retried), wait for a pipelined move, reserve the
 *                          io memory, populate the tt, then pmap_enter the
 *                          page: the page of the tt, or the physical address
 *                          of the aperture (bus.offset), with the cache
 *                          attribute of the object (ttm_io_prot).
 *
 * A mapping of the object (a GEM mmap) takes the uvm_object with
 * uvm_map and drops it by uvm_unmap.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/systm.h>

#include <uvm/uvm.h>
#include <uvm/uvm_extern.h>
#include <uvm/uvm_fault.h>

#include <linux/dma-resv.h>
#include <linux/mm.h>

#include <drm/ttm/ttm_bo.h>
#include <drm/ttm/ttm_device.h>
#include <drm/ttm/ttm_resource.h>
#include <drm/ttm/ttm_tt.h>

/*
 * pmap_enter is a weak alias of pmap_enter_default on x86, and the module
 * loader does not bind to weak kernel symbols: name the definition.
 */
#if defined(__x86_64__) || defined(__i386__)
int	pmap_enter_default(struct pmap *, vaddr_t, paddr_t, vm_prot_t, u_int);
#define	drm618_pmap_enter	pmap_enter_default
#else
#define	drm618_pmap_enter	pmap_enter
#endif

struct ttm_bo_uvm {
	struct uvm_object		uobj;
	struct ttm_buffer_object	*bo;
};

static const struct uvm_pagerops ttm_bo_uvm_pagerops;

struct uvm_object	*ttm_bo_uvm_create(struct ttm_buffer_object *);

static struct ttm_bo_uvm *
to_uvm(struct uvm_object *uobj)
{

	return container_of(uobj, struct ttm_bo_uvm, uobj);
}

/* A uvm object that maps bo; one reference, and one of bo is held. */
struct uvm_object *
ttm_bo_uvm_create(struct ttm_buffer_object *bo)
{
	struct ttm_bo_uvm *u = kmem_zalloc(sizeof(*u), KM_SLEEP);

	uvm_obj_init(&u->uobj, &ttm_bo_uvm_pagerops, true, 1);
	kref_get(&bo->kref);
	u->bo = bo;
	return &u->uobj;
}

static void
ttm_bo_uvm_reference(struct uvm_object *uobj)
{

	rw_enter(uobj->vmobjlock, RW_WRITER);
	uobj->uo_refs++;
	rw_exit(uobj->vmobjlock);
}

static void
ttm_bo_uvm_detach(struct uvm_object *uobj)
{
	struct ttm_bo_uvm *u = to_uvm(uobj);

	rw_enter(uobj->vmobjlock, RW_WRITER);
	if (--uobj->uo_refs > 0) {
		rw_exit(uobj->vmobjlock);
		return;
	}
	rw_exit(uobj->vmobjlock);
	ttm_bo_put(u->bo);
	uvm_obj_destroy(&u->uobj, true);
	kmem_free(u, sizeof(*u));
}

/*
 * Reserve for a fault: a try-lock (the fault holds the map locks), and on a
 * failure give them up and wait for the object to be released; the fault is
 * retried (ERESTART).
 */
static int
ttm_bo_uvm_reserve(struct ttm_buffer_object *bo, struct uvm_faultinfo *ufi)
{

	if (__predict_false(!dma_resv_trylock(bo->base.resv))) {
		kref_get(&bo->kref);
		uvmfault_unlockall(ufi, ufi->entry->aref.ar_amap, NULL);
		if (dma_resv_lock_interruptible(bo->base.resv, NULL) == 0)
			dma_resv_unlock(bo->base.resv);
		ttm_bo_put(bo);
		return ERESTART;
	}
	return 0;
}

static int
ttm_bo_uvm_fault_reserved(struct ttm_buffer_object *bo, struct uvm_faultinfo *ufi,
    vaddr_t vaddr, struct vm_page **pps, int npages, int centeridx, int flags)
{
	struct ttm_device *bdev = bo->bdev;
	struct ttm_resource *res = bo->resource;
	vm_prot_t vm_prot = ufi->entry->protection;
	pgprot_t prot;
	voff_t uoffset;
	unsigned startpage, i;
	int error, ret = 0;

	/* Pages that were imported are the exporter's: not mapped here. */
	if (bo->ttm != NULL && (bo->ttm->page_flags & TTM_TT_FLAG_EXTERNAL))
		return EINVAL;	/* SIGBUS */
	if (res == NULL)
		return EINVAL;

	/* A pipelined move is in flight: let it end without the map locks. */
	if (!dma_resv_test_signaled(bo->base.resv, DMA_RESV_USAGE_KERNEL)) {
		kref_get(&bo->kref);
		uvmfault_unlockall(ufi, ufi->entry->aref.ar_amap, NULL);
		(void)dma_resv_wait_timeout(bo->base.resv, DMA_RESV_USAGE_KERNEL,
		    true, MAX_SCHEDULE_TIMEOUT);
		dma_resv_unlock(bo->base.resv);
		ttm_bo_put(bo);
		return ERESTART;
	}

	error = ttm_mem_io_reserve(bdev, res);
	if (error)
		return EINVAL;	/* SIGBUS */
	prot = ttm_io_prot(bo, res, vm_prot);

	if (!res->bus.is_iomem) {
		struct ttm_operation_ctx ctx = { .interruptible = false };

		if (ttm_tt_populate(bdev, bo->ttm, &ctx))
			return ENOMEM;
	}

	uoffset = ufi->entry->offset + (vaddr - ufi->entry->start);
	startpage = uoffset >> PAGE_SHIFT;
	for (i = 0; i < (unsigned)npages; i++) {
		paddr_t paddr;

		if ((flags & PGO_ALLPAGES) == 0 && (int)i != centeridx)
			continue;
		if (pps[i] == PGO_DONTCARE)
			continue;
		if ((size_t)(startpage + i) >= (bo->base.size >> PAGE_SHIFT)) {
			ret = EINVAL;	/* SIGBUS: beyond the object */
			break;
		}
		if (!res->bus.is_iomem)
			paddr = VM_PAGE_TO_PHYS(&bo->ttm->pages[startpage + i]->p_vmp);
		else if (bdev->funcs->io_mem_pfn != NULL)
			paddr = (paddr_t)bdev->funcs->io_mem_pfn(bo, startpage + i)
			    << PAGE_SHIFT;
		else
			paddr = res->bus.offset + ((paddr_t)(startpage + i) << PAGE_SHIFT);
		ret = drm618_pmap_enter(ufi->orig_map->pmap, vaddr + (vaddr_t)i * PAGE_SIZE,
		    paddr, vm_prot, PMAP_CANFAIL | prot);
		if (ret)
			break;
	}
	pmap_update(ufi->orig_map->pmap);
	return ret;	/* 0: the access is retried */
}

static int
ttm_bo_uvm_fault(struct uvm_faultinfo *ufi, vaddr_t vaddr,
    struct vm_page **pps, int npages, int centeridx,
    vm_prot_t access_type __unused, int flags)
{
	struct uvm_object *uobj = ufi->entry->object.uvm_obj;
	struct ttm_buffer_object *bo = to_uvm(uobj)->bo;
	int ret;

	/* The object lock uvm took is not needed. */
	rw_exit(uobj->vmobjlock);

	/* Copy-on-write makes no sense for device memory. */
	if (UVM_ET_ISCOPYONWRITE(ufi->entry)) {
		ret = EINVAL;	/* SIGBUS */
		goto out;
	}
	ret = ttm_bo_uvm_reserve(bo, ufi);
	if (ret)
		return ret;	/* the locks are given up */
	ret = ttm_bo_uvm_fault_reserved(bo, ufi, vaddr, pps, npages, centeridx, flags);
	if (ret == ERESTART)
		return ret;	/* already unlocked */
	dma_resv_unlock(bo->base.resv);
out:
	uvmfault_unlockall(ufi, ufi->entry->aref.ar_amap, NULL);
	return ret;
}

static const struct uvm_pagerops ttm_bo_uvm_pagerops = {
	.pgo_reference = ttm_bo_uvm_reference,
	.pgo_detach = ttm_bo_uvm_detach,
	.pgo_fault = ttm_bo_uvm_fault,
};
