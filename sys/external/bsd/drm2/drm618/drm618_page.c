/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.  Pages and mappings of the
 * 6.18 core as TTM uses them (drm618_page.h): temporary mappings with a cache
 * attribute, ioremap with a record of the ranges, the pages of a shmem file.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <drm618_statics.h>
#include <linux/io-mapping.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

/*
 * The direct map.  x86 advertises it to the kernel (__HAVE_DIRECT_MAP, set by
 * _KERNEL_OPT) but not to modules, so the macro is not there: the base is the
 * kernel variable.
 */
#if defined(__x86_64__)
extern vaddr_t pmap_direct_base;
#define	DRM618_PA_TO_KVA(pa)	((vaddr_t)pmap_direct_base + (pa))
#elif defined(__aarch64__)
#define	DRM618_PA_TO_KVA(pa)	AARCH64_PA_TO_KVA(pa)
#else
#error "no direct map known for this architecture"
#endif

/*
 * Not trunc_page/round_page: the layer defines PAGE_MASK the way Linux does
 * (the mask of the page address), which turns the NetBSD macros inside out.
 */
#define	DRM618_TRUNC(x)	((x) & ~((__typeof__(x))PAGE_SIZE - 1))
#define	DRM618_ROUND(x)	(((x) + PAGE_SIZE - 1) & ~((__typeof__(x))PAGE_SIZE - 1))

const struct drm618_cpuinfo drm618_boot_cpu_data = { .x86 = 6 };

/* ---- the direct map ----------------------------------------------------------- */

void *
page_address(struct page *page)
{

	return (void *)DRM618_PA_TO_KVA(VM_PAGE_TO_PHYS(&page->p_vmp));
}

void
clear_highpage(struct page *page)
{

	clear_page(page_address(page));
}

void
copy_highpage(struct page *to, struct page *from)
{

	memcpy(page_address(to), page_address(from), PAGE_SIZE);
}

/* ---- the pages of a shmem file ------------------------------------------- */

void
put_page(struct page *page)
{
	struct vm_page *pg = &page->p_vmp;
	struct uvm_object *uobj = pg->uobject;

	if (uobj != NULL)
		uvm_obj_unwirepages(uobj, pg->offset, pg->offset + PAGE_SIZE);
}

struct page *
shmem_read_mapping_page_gfp(struct address_space *mapping, unsigned long index,
    gfp_t gfp)
{
	struct folio *folio = shmem_read_folio_gfp(mapping, index, gfp);

	if (IS_ERR(folio))
		return ERR_CAST(folio);
	return &folio->page;
}

/* ---- a temporary mapping with the cache attribute of prot ------------------ */

static bool
in_kernel_map(const void *addr)
{
	vaddr_t va = (vaddr_t)addr;

	return va >= vm_map_min(kernel_map) && va < vm_map_max(kernel_map);
}

void *
kmap_local_page_prot(struct page *page, pgprot_t prot)
{
	vaddr_t va;

	/* No attribute besides the protection: the direct map is the mapping. */
	if ((prot & ~(pgprot_t)UVM_PROT_ALL) == 0)
		return page_address(page);

	va = uvm_km_alloc(kernel_map, PAGE_SIZE, 0, UVM_KMF_VAONLY | UVM_KMF_WAITVA);
	pmap_kenter_pa(va, VM_PAGE_TO_PHYS(&page->p_vmp), prot & UVM_PROT_ALL, prot);
	pmap_update(pmap_kernel());
	return (void *)va;
}

void
kunmap_local(const void *addr)
{
	vaddr_t va = DRM618_TRUNC((vaddr_t)addr);

	if (!in_kernel_map(addr))
		return;		/* the direct map: nothing was entered */
	pmap_kremove(va, PAGE_SIZE);
	pmap_update(pmap_kernel());
	uvm_km_free(kernel_map, va, PAGE_SIZE, UVM_KMF_VAONLY);
}

/* ---- physical ranges ------------------------------------------------------- */

struct io_range {
	LIST_ENTRY(io_range)	link;
	vaddr_t			va;
	vsize_t			size;
};

DEFINE_MUTEX(drm618_io_lock);
static LIST_HEAD(, io_range) io_ranges = LIST_HEAD_INITIALIZER(io_ranges);

void *
drm618_ioremap(paddr_t pa, size_t size, int cache)
{
	paddr_t base = DRM618_TRUNC(pa);
	vsize_t off = pa - base;
	vsize_t len = DRM618_ROUND(off + size);
	struct io_range *r;
	vaddr_t va;
	vsize_t i;

	r = kmem_alloc(sizeof(*r), KM_SLEEP);
	va = uvm_km_alloc(kernel_map, len, 0, UVM_KMF_VAONLY | UVM_KMF_NOWAIT);
	if (va == 0) {
		kmem_free(r, sizeof(*r));
		return NULL;
	}
	for (i = 0; i < len; i += PAGE_SIZE)
		pmap_kenter_pa(va + i, base + i, VM_PROT_READ | VM_PROT_WRITE, cache);
	pmap_update(pmap_kernel());
	r->va = va;
	r->size = len;
	mutex_lock(&drm618_io_lock);
	LIST_INSERT_HEAD(&io_ranges, r, link);
	mutex_unlock(&drm618_io_lock);
	return (void *)(va + off);
}

void
drm618_iounmap(void *addr)
{
	vaddr_t va = DRM618_TRUNC((vaddr_t)addr);
	struct io_range *r;

	mutex_lock(&drm618_io_lock);
	LIST_FOREACH(r, &io_ranges, link) {
		if (r->va == va)
			break;
	}
	if (r != NULL)
		LIST_REMOVE(r, link);
	mutex_unlock(&drm618_io_lock);
	if (r == NULL) {
		printf("drm618: iounmap of %p, not an ioremap\n", addr);
		return;
	}
	pmap_kremove(r->va, r->size);
	pmap_update(pmap_kernel());
	uvm_km_free(kernel_map, r->va, r->size, UVM_KMF_VAONLY);
	kmem_free(r, sizeof(*r));
}

/* ---- one page of kernel memory ----------------------------------------------- */

struct page *
alloc_page(gfp_t gfp)
{
	struct pglist pgl;
	struct vm_page *pg;
	paddr_t high = (gfp & __GFP_DMA32) ? 0xffffffffUL : (paddr_t)-1;

	TAILQ_INIT(&pgl);
	if (uvm_pglistalloc(PAGE_SIZE, 0, high, PAGE_SIZE, 0, &pgl, 1,
	    (gfp & __GFP_NORETRY) ? 0 : 1) != 0)
		return NULL;
	pg = TAILQ_FIRST(&pgl);
	if (gfp & __GFP_ZERO)
		clear_page((void *)DRM618_PA_TO_KVA(VM_PAGE_TO_PHYS(pg)));
	return container_of(pg, struct page, p_vmp);
}

void
__free_page(struct page *page)
{
	struct pglist pgl;

	TAILQ_INIT(&pgl);
	TAILQ_INSERT_TAIL(&pgl, &page->p_vmp, pageq.queue);
	uvm_pglistfree(&pgl);
}

/* ---- vmap, and the pages of an io_mapping ---------------------------------------- */

void *
drm618_vmap(struct page **pages, unsigned int npages, int cache, int prot)
{
	struct io_range *r;
	vaddr_t va;
	unsigned int i;

	r = kmem_alloc(sizeof(*r), KM_SLEEP);
	va = uvm_km_alloc(kernel_map, (vsize_t)npages << PAGE_SHIFT, 0,
	    UVM_KMF_VAONLY | UVM_KMF_NOWAIT);
	if (va == 0) {
		kmem_free(r, sizeof(*r));
		return NULL;
	}
	for (i = 0; i < npages; i++)
		pmap_kenter_pa(va + (vaddr_t)i * PAGE_SIZE,
		    VM_PAGE_TO_PHYS(&pages[i]->p_vmp), prot, cache);
	pmap_update(pmap_kernel());
	r->va = va;
	r->size = (vsize_t)npages << PAGE_SHIFT;
	mutex_lock(&drm618_io_lock);
	LIST_INSERT_HEAD(&io_ranges, r, link);
	mutex_unlock(&drm618_io_lock);
	return (void *)va;
}

void *
io_mapping_map_local_wc(struct io_mapping *iomap, resource_size_t offset)
{

	return drm618_ioremap(iomap->base + offset, PAGE_SIZE, PMAP_WRITE_COMBINE);
}

void
io_mapping_unmap_local(void *addr)
{

	drm618_iounmap(addr);
}
