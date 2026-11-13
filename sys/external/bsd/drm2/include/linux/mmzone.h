/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native.  Interface of Linux 6.18 include/linux/mmzone.h
 * (GPL-2.0; no source copied).
 *
 * Only the page allocator order limits TTM (ttm_pool) sizes its tables by.
 * Values are those of Linux on amd64 with 4 KiB pages.
 */

#ifndef _LINUX_MMZONE_H_
#define _LINUX_MMZONE_H_

#define	PAGE_ALLOC_COSTLY_ORDER	3
#define	MAX_PAGE_ORDER		10
#define	NR_PAGE_ORDERS		(MAX_PAGE_ORDER + 1)

#endif	/* _LINUX_MMZONE_H_ */
