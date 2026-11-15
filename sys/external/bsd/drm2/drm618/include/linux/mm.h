/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD.  Shadow of <linux/mm.h> for the 6.18 core: vm_area_struct, folios (drm618_mm.h). */
#ifndef _DRM618_LINUX_MM_H_
#define _DRM618_LINUX_MM_H_
#include_next <linux/mm.h>
#include <drm618_mm.h>
#include <drm618_page.h>

/* linux/mm.h: is the address the start of a page. */
#define	PAGE_ALIGNED(addr)	(((unsigned long)(addr) & (PAGE_SIZE - 1)) == 0)
#endif
