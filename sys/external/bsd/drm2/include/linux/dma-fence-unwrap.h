/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native (AI-assisted).  Interface of Linux 6.18
 * include/linux/dma-fence-unwrap.h (GPL-2.0-only; no source copied).
 *
 * Iteration over the leaf fences of fence containers (dma_fence_chain
 * nodes and dma_fence_array members) and merging of fences into one.
 */

#ifndef _LINUX_DMA_FENCE_UNWRAP_H_
#define _LINUX_DMA_FENCE_UNWRAP_H_

#include <linux/array_size.h>
#include <linux/dma-fence.h>

#define	__dma_fence_unwrap_merge	linux___dma_fence_unwrap_merge
#define	dma_fence_dedup_array		linux_dma_fence_dedup_array
#define	dma_fence_unwrap_first		linux_dma_fence_unwrap_first
#define	dma_fence_unwrap_next		linux_dma_fence_unwrap_next

struct dma_fence_unwrap {
	struct dma_fence	*chain;	/* current chain node (referenced) */
	struct dma_fence	*array;	/* its payload, maybe an array */
	unsigned int		index;	/* last returned index in array */
};

/*
 * The leaf fences of head, chain nodes from the newest to the oldest
 * unsignalled one, the members of arrays in order.  The returned fence is
 * not referenced; it stays valid while the cursor is.  A loop left early
 * must dma_fence_put(cursor.chain).
 */
struct dma_fence *
	dma_fence_unwrap_first(struct dma_fence *, struct dma_fence_unwrap *);
struct dma_fence *
	dma_fence_unwrap_next(struct dma_fence_unwrap *);

#define	dma_fence_unwrap_for_each(fence, cursor, head)			\
	for (fence = dma_fence_unwrap_first(head, cursor); fence != NULL; \
	     fence = dma_fence_unwrap_next(cursor))

/*
 * One referenced fence for all the unsignalled leaf fences of the given
 * ones, latest per context: a stub carrying the latest signal time if none
 * is left, the fence itself if one, otherwise a fence array in ascending
 * context order.  NULL on allocation failure.  fences and cursors are
 * scratch space of num_fences elements.
 */
struct dma_fence *
	__dma_fence_unwrap_merge(unsigned int, struct dma_fence **,
	    struct dma_fence_unwrap *);

/*
 * Sort array[0..num_fences) by context, latest first within a context,
 * drop all but the latest of each context (their references are put) and
 * return the new count.
 */
int	dma_fence_dedup_array(struct dma_fence **, int);

#define	dma_fence_unwrap_merge(...)					\
	({								\
		struct dma_fence *__f[] = { __VA_ARGS__ };		\
		struct dma_fence_unwrap __c[ARRAY_SIZE(__f)];		\
									\
		__dma_fence_unwrap_merge(ARRAY_SIZE(__f), __f, __c);	\
	})

#endif	/* _LINUX_DMA_FENCE_UNWRAP_H_ */
