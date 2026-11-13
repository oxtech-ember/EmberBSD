/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.
 *
 * Shadow of the shared <linux/list_sort.h> for the 6.18 core: since Linux
 * 6.x the comparison function takes const list_head pointers.  The layer's
 * linux_list_sort has the 5.6 prototype; a function that takes const
 * pointers is called the same way, so the call is cast.
 */

#ifndef _DRM618_LINUX_LIST_SORT_H_
#define _DRM618_LINUX_LIST_SORT_H_

#include_next <linux/list_sort.h>

typedef int (*list_cmp_func_t)(void *, const struct list_head *,
    const struct list_head *);

static inline void
drm618_list_sort(void *priv, struct list_head *head, list_cmp_func_t cmp)
{

	linux_list_sort(priv, head,
	    (int (*)(void *, struct list_head *, struct list_head *))cmp);
}

#undef	list_sort
#define	list_sort	drm618_list_sort

#endif	/* _DRM618_LINUX_LIST_SORT_H_ */
