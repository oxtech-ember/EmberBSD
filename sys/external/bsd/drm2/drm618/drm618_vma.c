/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the interface in
 * include/drm618_mm.h and to the way the mmap handlers of the 6.18 core and
 * its drivers use struct vm_area_struct, vm_operations_struct and the
 * vmf_insert_* calls (drm_gem.c, drm_prime.c of dist618, MIT).  The uvm
 * pager follows the pager of a TTM buffer object in this directory
 * (ttm_bo_vm_bsd.c).  No Linux code.
 *
 * The user mapping of a Linux file.  mmap(2) on a file of the layer
 * (fo_mmap of drm618_file.c) runs the mmap handler of the file once on a
 * struct vm_area_struct whose range is [0, len) and whose vm_pgoff is the
 * offset the user gave, in pages (for a DRM device file the fake offset of a
 * GEM object).  What the handler leaves in the vma (vm_ops, vm_private_data,
 * flags, the range of remap_pfn_range) is the mapping.  The mapping is a uvm
 * object, one per mmap(2) call, that the user's map holds:
 *
 *   - Every reference of the uvm object (the first one is the mmap handler's
 *     own, further ones come from fork and from splitting an entry) is a vma
 *     of the Linux model: pgo_reference runs vm_ops->open, pgo_detach runs
 *     vm_ops->close, and the last detach releases the file the mapping held.
 *   - A fault (pgo_fault) runs vm_ops->fault on a copy of the vma whose
 *     vm_start is the address of the first page of the mapping, so that
 *     "address - vm_start" is the page offset in the mapping as in Linux, and
 *     whose bsd members name the faulting map.  vmf_insert_page, vmf_insert_pfn
 *     and the fault of a range of remap_pfn_range enter one page there
 *     (pmap_enter), and the page is remembered so that unmap_mapping_range
 *     can take it out again.
 *
 * Private (copy on write) mappings are refused: EINVAL.  A mapping longer
 * than a GEM object, or at an offset that is no object, is the business of the
 * mmap handler and fails before anything is mapped.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/kmem.h>
#include <sys/mman.h>
#include <sys/mutex.h>
#include <sys/once.h>
#include <sys/queue.h>
#include <sys/rwlock.h>
#include <sys/systm.h>

#include <uvm/uvm.h>
#include <uvm/uvm_extern.h>
#include <uvm/uvm_fault.h>

#include <drm618_mm.h>

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

#define	VMA_PAGE_SIZE	((size_t)PAGE_SIZE)
/* The pages of a mapping that are remembered; a longer mapping is not tracked. */
#define	VMA_TRACK_MAX	((size_t)1 << 18)

struct vma_uobj {
	struct uvm_object	vu_uobj;
	struct vm_area_struct	vu_vma;		/* the vma the mmap handler set up */
	struct address_space	*vu_mapping;	/* the page cache of the file: what unmap_mapping_range names */
	size_t			vu_npages;
	kmutex_t		vu_lock;	/* vu_entered */
	paddr_t			*vu_entered;	/* per page: the address entered, or 0; NULL if not tracked */
	LIST_ENTRY(vma_uobj)	vu_entry;
};

static LIST_HEAD(, vma_uobj) vma_list = LIST_HEAD_INITIALIZER(vma_list);
static kmutex_t vma_list_lock;
static ONCE_DECL(vma_once);
static volatile unsigned vma_said;

static int
vma_init(void)
{

	mutex_init(&vma_list_lock, MUTEX_DEFAULT, IPL_NONE);
	return 0;
}

static void
vma_say_once(const char *msg)
{

	if (atomic_swap_uint(&vma_said, 1) == 0)
		printf("drm618: %s\n", msg);
}

static const struct uvm_pagerops vma_pagerops;

static struct vma_uobj *
to_vu(struct uvm_object *uobj)
{

	return container_of(uobj, struct vma_uobj, vu_uobj);
}

/* ---- the references ---------------------------------------------------- */

/* One more vma shares the mapping: fork, or the split of a map entry. */
static void
vma_reference(struct uvm_object *uobj)
{
	struct vma_uobj *vu = to_vu(uobj);

	if (vu->vu_vma.vm_ops != NULL && vu->vu_vma.vm_ops->open != NULL)
		(*vu->vu_vma.vm_ops->open)(&vu->vu_vma);
	rw_enter(uobj->vmobjlock, RW_WRITER);
	uobj->uo_refs++;
	rw_exit(uobj->vmobjlock);
}

static void
vma_destroy(struct vma_uobj *vu)
{
	struct DRM618_FILE_TAG *filp = vu->vu_vma.vm_file;

	mutex_enter(&vma_list_lock);
	LIST_REMOVE(vu, vu_entry);
	mutex_exit(&vma_list_lock);
	if (vu->vu_entered != NULL)
		kmem_free(vu->vu_entered, vu->vu_npages * sizeof(*vu->vu_entered));
	mutex_destroy(&vu->vu_lock);
	uvm_obj_destroy(&vu->vu_uobj, true);
	kmem_free(vu, sizeof(*vu));
	fput(filp);
}

/*
 * One vma is gone.  vm_ops->close runs while this reference still holds the
 * structure: another thread may drop the last reference meanwhile.
 */
static void
vma_detach(struct uvm_object *uobj)
{
	struct vma_uobj *vu = to_vu(uobj);
	bool last;

	if (vu->vu_vma.vm_ops != NULL && vu->vu_vma.vm_ops->close != NULL)
		(*vu->vu_vma.vm_ops->close)(&vu->vu_vma);
	rw_enter(uobj->vmobjlock, RW_WRITER);
	KASSERT(uobj->uo_refs > 0);
	last = --uobj->uo_refs == 0;
	rw_exit(uobj->vmobjlock);
	if (last)
		vma_destroy(vu);
}

/* ---- entering pages ---------------------------------------------------- */

static vm_fault_t
vma_enter(struct vm_area_struct *vma, unsigned long addr, paddr_t pa,
    pgprot_t prot)
{
	struct uvm_faultinfo *ufi = vma->vm_bsd_ufi;
	struct vma_uobj *vu = vma->vm_bsd_uobj;
	vm_prot_t vp;
	size_t idx;
	int error;

	if (ufi == NULL || vu == NULL) {
		vma_say_once("a page was entered outside a fault of a user mapping");
		return VM_FAULT_SIGBUS;
	}
	if (addr < vma->vm_start || addr >= vma->vm_end)
		return VM_FAULT_SIGBUS;
	addr &= ~((unsigned long)PAGE_SIZE - 1);
	vp = ufi->entry->protection;
	error = drm618_pmap_enter(ufi->orig_map->pmap, addr, pa, vp,
	    PMAP_CANFAIL | (vma->vm_bsd_access & VM_PROT_ALL) |
	    ((unsigned)prot & ~(unsigned)VM_PROT_ALL));
	if (error)
		return error == ENOMEM ? VM_FAULT_OOM : VM_FAULT_SIGBUS;
	if (vu->vu_entered != NULL) {
		idx = (addr - vma->vm_start) >> PAGE_SHIFT;
		mutex_enter(&vu->vu_lock);
		vu->vu_entered[idx] = pa;
		mutex_exit(&vu->vu_lock);
	}
	return VM_FAULT_NOPAGE;
}

vm_fault_t
vmf_insert_page(struct vm_area_struct *vma, unsigned long addr, struct page *page)
{

	return vma_enter(vma, addr, VM_PAGE_TO_PHYS(&page->p_vmp),
	    vma->vm_page_prot);
}

vm_fault_t
vmf_insert_pfn_prot(struct vm_area_struct *vma, unsigned long addr,
    unsigned long pfn, pgprot_t prot)
{

	return vma_enter(vma, addr, (paddr_t)pfn << PAGE_SHIFT, prot);
}

vm_fault_t
vmf_insert_pfn(struct vm_area_struct *vma, unsigned long addr, unsigned long pfn)
{

	return vma_enter(vma, addr, (paddr_t)pfn << PAGE_SHIFT, vma->vm_page_prot);
}

/*
 * Linux enters the whole range at once.  The range is only recorded here (the
 * mmap handler runs before the address of the mapping is known); the faults
 * enter its pages one by one.  One range per mapping, starting at the start
 * of the vma: anything else says so and fails.
 */
int
remap_pfn_range(struct vm_area_struct *vma, unsigned long addr,
    unsigned long pfn, unsigned long size, pgprot_t prot)
{
	unsigned long npages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;

	if (addr != vma->vm_start || npages == 0 ||
	    npages > ((vma->vm_end - vma->vm_start) >> PAGE_SHIFT))
		return -EOPNOTSUPP;
	if (vma->vm_bsd_remap)
		return -EOPNOTSUPP;
	vma->vm_bsd_remap = true;
	vma->vm_bsd_pfn = pfn;
	vma->vm_bsd_npages = npages;
	vma->vm_bsd_remap_prot = prot;
	vm_flags_set(vma, VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP);
	return 0;
}

static vm_fault_t
vma_remap_fault(struct vm_area_struct *vma, const struct vm_fault *vmf)
{
	unsigned long idx = (vmf->address - vma->vm_start) >> PAGE_SHIFT;

	if (idx >= vma->vm_bsd_npages)
		return VM_FAULT_SIGBUS;
	return vma_enter(vma, vmf->address,
	    (paddr_t)(vma->vm_bsd_pfn + idx) << PAGE_SHIFT, vma->vm_bsd_remap_prot);
}

/* ---- the fault ----------------------------------------------------------- */

/*
 * uvm calls with the lock of the object held and the map read-locked, and
 * wants every lock gone when it returns.  A page that is entered makes the
 * access retry (0); a refusal is SIGBUS (EINVAL, the convention of the TTM
 * pager); no memory waits and tries again.
 */
static int
vma_fault(struct uvm_faultinfo *ufi, vaddr_t vaddr, struct vm_page **pps __unused,
    int npages __unused, int centeridx, vm_prot_t access_type,
    int flags __unused)
{
	struct uvm_object *uobj = ufi->entry->object.uvm_obj;
	struct vma_uobj *vu = to_vu(uobj);
	struct vm_area_struct vma = vu->vu_vma;
	struct vm_fault vmf;
	vaddr_t va = vaddr + (vaddr_t)centeridx * PAGE_SIZE;
	vm_fault_t res;
	int error;

	/* The lock of the object is not needed. */
	rw_exit(uobj->vmobjlock);

	if (UVM_ET_ISCOPYONWRITE(ufi->entry)) {
		error = EINVAL;
		goto out;
	}
	vma.vm_start = ufi->entry->start - (vaddr_t)ufi->entry->offset;
	vma.vm_end = vma.vm_start + vu->vu_npages * PAGE_SIZE;
	if (va < vma.vm_start || va >= vma.vm_end) {
		error = EINVAL;
		goto out;
	}
	vma.vm_bsd_ufi = ufi;
	vma.vm_bsd_access = access_type;
	memset(&vmf, 0, sizeof(vmf));
	vmf.vma = &vma;
	vmf.address = va;
	vmf.pgoff = vma.vm_pgoff + ((va - vma.vm_start) >> PAGE_SHIFT);
	vmf.flags = (access_type & VM_PROT_WRITE) ? FAULT_FLAG_WRITE : 0;

	if (vma.vm_ops != NULL && vma.vm_ops->fault != NULL)
		res = (*vma.vm_ops->fault)(&vmf);
	else if (vma.vm_bsd_remap)
		res = vma_remap_fault(&vma, &vmf);
	else
		res = VM_FAULT_SIGBUS;
	pmap_update(ufi->orig_map->pmap);

	if (res & VM_FAULT_NOPAGE)
		error = 0;
	else if (res & VM_FAULT_RETRY)
		error = ERESTART;
	else if (res & VM_FAULT_OOM) {
		uvmfault_unlockall(ufi, ufi->entry->aref.ar_amap, NULL);
		uvm_wait("drm618vma");
		return ERESTART;
	} else
		error = EINVAL;
out:
	uvmfault_unlockall(ufi, ufi->entry->aref.ar_amap, NULL);
	return error;
}

static const struct uvm_pagerops vma_pagerops = {
	.pgo_reference = vma_reference,
	.pgo_detach = vma_detach,
	.pgo_fault = vma_fault,
};

/* ---- mmap(2) ---------------------------------------------------------------- */

int
drm618_vma_mmap(struct DRM618_FILE_TAG *lf, off_t *offp, size_t len, int prot,
    int *flagsp, int *advicep, struct uvm_object **uobjp, int *maxprotp)
{
	struct vm_area_struct vma;
	struct vma_uobj *vu;
	vm_prot_t maxprot;
	int error;

	(void)RUN_ONCE(&vma_once, vma_init);
	if (lf->f_op == NULL || lf->f_op->mmap == NULL)
		return ENODEV;
	/* Device memory and GEM objects have no copy on write. */
	if ((*flagsp & MAP_SHARED) == 0)
		return EINVAL;
	if (len == 0 || *offp < 0 || (len & (PAGE_SIZE - 1)) != 0 ||
	    (*offp & (PAGE_SIZE - 1)) != 0)
		return EINVAL;
	maxprot = ((lf->f_mode & FMODE_READ) ? VM_PROT_READ : 0) |
	    ((lf->f_mode & FMODE_WRITE) ? VM_PROT_WRITE : 0);
	if ((prot & ~maxprot) != 0)
		return EACCES;

	memset(&vma, 0, sizeof(vma));
	vma.vm_start = 0;
	vma.vm_end = len;
	vma.vm_pgoff = (unsigned long)*offp >> PAGE_SHIFT;
	vma.vm_flags = VM_SHARED | ((prot & VM_PROT_READ) ? VM_READ : 0) |
	    ((prot & VM_PROT_WRITE) ? VM_WRITE : 0);
	vma.vm_page_prot = vm_get_page_prot(vma.vm_flags);
	vma.vm_file = lf;
	error = (*lf->f_op->mmap)(lf, &vma);
	if (error)
		return error == -ERESTARTSYS ? ERESTART : -error;

	vu = kmem_zalloc(sizeof(*vu), KM_SLEEP);
	vu->vu_vma = vma;
	vu->vu_vma.vm_file = get_file(lf);
	vu->vu_vma.vm_bsd_uobj = vu;
	vu->vu_npages = len >> PAGE_SHIFT;
	vu->vu_mapping = lf->f_mapping;
	if (vu->vu_npages <= VMA_TRACK_MAX)
		vu->vu_entered = kmem_zalloc(vu->vu_npages *
		    sizeof(*vu->vu_entered), KM_SLEEP);
	mutex_init(&vu->vu_lock, MUTEX_DEFAULT, IPL_NONE);
	uvm_obj_init(&vu->vu_uobj, &vma_pagerops, true, 1);
	mutex_enter(&vma_list_lock);
	LIST_INSERT_HEAD(&vma_list, vu, vu_entry);
	mutex_exit(&vma_list_lock);

	*uobjp = &vu->vu_uobj;
	*offp = 0;
	*advicep = UVM_ADV_RANDOM;
	*maxprotp = maxprot;
	return 0;
}

/* ---- revoking ---------------------------------------------------------- */

/*
 * Take the pages of a range of the file out of every user mapping of it
 * (holelen 0: to the end).  The pages that were entered through the layer are
 * known; a page that is not memory of the page cache (device memory, which
 * has no pv entries) cannot be taken out, and that is said.
 */
void
unmap_mapping_range(struct address_space *mapping, loff_t holebegin,
    loff_t holelen, int even_cows __unused)
{
	struct vma_uobj *vu;
	unsigned long first, last, lo, hi, i;
	unsigned kept = 0;

	(void)RUN_ONCE(&vma_once, vma_init);
	first = (unsigned long)holebegin >> PAGE_SHIFT;
	last = holelen <= 0 ? ~0ul :
	    ((unsigned long)(holebegin + holelen) + PAGE_SIZE - 1) >> PAGE_SHIFT;
	mutex_enter(&vma_list_lock);
	LIST_FOREACH(vu, &vma_list, vu_entry) {
		if (vu->vu_mapping != mapping)
			continue;
		lo = MAX(first, vu->vu_vma.vm_pgoff);
		hi = MIN(last, vu->vu_vma.vm_pgoff + vu->vu_npages);
		if (lo >= hi)
			continue;
		if (vu->vu_entered == NULL) {
			kept++;
			continue;
		}
		mutex_enter(&vu->vu_lock);
		for (i = lo; i < hi; i++) {
			paddr_t pa = vu->vu_entered[i - vu->vu_vma.vm_pgoff];
			struct vm_page *pg;

			if (pa == 0)
				continue;
			pg = PHYS_TO_VM_PAGE(pa);
			if (pg == NULL) {
				kept++;
				continue;
			}
			pmap_page_protect(pg, VM_PROT_NONE);
			vu->vu_entered[i - vu->vu_vma.vm_pgoff] = 0;
		}
		mutex_exit(&vu->vu_lock);
	}
	mutex_exit(&vma_list_lock);
	if (kept != 0)
		vma_say_once("unmap_mapping_range: device memory or an untracked"
		    " mapping stays mapped");
}

/*
 * Before the module goes: EBUSY while a user mapping exists (it points into
 * the module); drm618_vma_fini then removes the lock.
 */
int
drm618_vma_busy(void)
{
	int busy;

	if (!vma_once.o_status)
		return 0;
	mutex_enter(&vma_list_lock);
	busy = !LIST_EMPTY(&vma_list);
	mutex_exit(&vma_list_lock);
	return busy ? EBUSY : 0;
}

void
drm618_vma_fini(void)
{

	if (vma_once.o_status)
		mutex_destroy(&vma_list_lock);
}
