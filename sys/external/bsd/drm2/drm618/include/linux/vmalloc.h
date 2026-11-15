/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.  Shadow of the shared
 * <linux/vmalloc.h> for the 6.18 core: vunmap takes the address only (the
 * layer's takes the number of pages too), and vmap enters the pages with the
 * cache attribute of the protection value (drm618_mm.h).
 */
#ifndef _DRM618_LINUX_VMALLOC_H_
#define _DRM618_LINUX_VMALLOC_H_

#define	vmap	layer_vmap
#define	vunmap	layer_vunmap
#define	is_vmalloc_addr	layer_is_vmalloc_addr
#include_next <linux/vmalloc.h>
#undef	vmap
#undef	vunmap
#undef	is_vmalloc_addr

/*
 * No address is a vmalloc one here: vmalloc and kvmalloc are the heap (the
 * shared layer says yes for every address so that kvfree works on both).  The
 * 6.18 sources use the answer to tell memory that is physically contiguous and
 * page aligned from memory that must be walked by pages (virtgpu_vq.c): the
 * heap is neither, and is handled as a kernel address range by the scatter
 * list walk (drm618_sg_walk).
 */
static inline bool
is_vmalloc_addr(const void *addr __unused)
{

	return false;
}

#include <drm618_page.h>

static inline void *
vmap(struct page **pages, unsigned int npages, unsigned long flags __unused,
    pgprot_t prot)
{

	return drm618_vmap(pages, npages, prot & ~(pgprot_t)UVM_PROT_ALL,
	    (prot & UVM_PROT_ALL) != 0 ? (prot & UVM_PROT_ALL) : UVM_PROT_RW);
}

static inline void
vunmap(const void *addr)
{

	drm618_iounmap(__UNCONST(addr));
}

/* The page behind an address of the kernel (mapped memory: the heap or a vmap range). */
struct page	*drm618_kva_to_page(const void *);
#define	vmalloc_to_page(addr)	drm618_kva_to_page(addr)

#endif
