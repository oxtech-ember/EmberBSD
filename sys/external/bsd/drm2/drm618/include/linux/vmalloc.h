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
#include_next <linux/vmalloc.h>
#undef	vmap
#undef	vunmap

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

#endif
