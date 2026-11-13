/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native (AI-assisted).  Behaviour of the dma_fence_unwrap
 * interface of Linux 6.18 (drivers/dma-buf/dma-fence-unwrap.c is
 * GPL-2.0-only; no source copied, written from the interface and its
 * documented behaviour).
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/systm.h>

#include <linux/dma-fence.h>
#include <linux/dma-fence-array.h>
#include <linux/dma-fence-chain.h>
#include <linux/dma-fence-unwrap.h>
#include <linux/errno.h>
#include <linux/slab.h>

/*
 * unwrap_array(cursor)
 *
 *	The next leaf fence of the current chain node, moving on to the
 *	next unsignalled node when the node's payload is used up.
 */
static struct dma_fence *
unwrap_array(struct dma_fence_unwrap *cursor)
{
	struct dma_fence *fence;

	for (;;) {
		if (cursor->index == 0)
			fence = dma_fence_array_first(cursor->array);
		else
			fence = dma_fence_array_next(cursor->array,
			    cursor->index);
		if (fence != NULL)
			return fence;

		/* Consumes the reference to the node we leave.  */
		cursor->chain = dma_fence_chain_walk(cursor->chain);
		if (cursor->chain == NULL) {
			cursor->array = NULL;
			return NULL;
		}
		cursor->array = dma_fence_chain_contained(cursor->chain);
		cursor->index = 0;
	}
}

struct dma_fence *
dma_fence_unwrap_first(struct dma_fence *head, struct dma_fence_unwrap *cursor)
{

	cursor->chain = dma_fence_get(head);
	cursor->array = dma_fence_chain_contained(cursor->chain);
	cursor->index = 0;
	return unwrap_array(cursor);
}

struct dma_fence *
dma_fence_unwrap_next(struct dma_fence_unwrap *cursor)
{

	cursor->index++;
	return unwrap_array(cursor);
}

/* Context ascending; within a context the later fence first.  */
static int
fence_order(struct dma_fence *a, struct dma_fence *b)
{

	if (a->context != b->context)
		return a->context < b->context ? -1 : 1;
	if (a->seqno == b->seqno)
		return 0;
	return dma_fence_is_later(a, b) ? -1 : 1;
}

int
dma_fence_dedup_array(struct dma_fence **array, int num_fences)
{
	struct dma_fence *f;
	int i, j, n;

	/* Insertion sort: the arrays are short.  */
	for (i = 1; i < num_fences; i++) {
		f = array[i];
		for (j = i; j > 0 && fence_order(array[j - 1], f) > 0; j--)
			array[j] = array[j - 1];
		array[j] = f;
	}

	/* Keep the first (latest) fence of every context.  */
	n = 0;
	for (i = 0; i < num_fences; i++) {
		if (n > 0 && array[n - 1]->context == array[i]->context)
			dma_fence_put(array[i]);
		else
			array[n++] = array[i];
	}
	return n;
}

struct dma_fence *
__dma_fence_unwrap_merge(unsigned int num_fences, struct dma_fence **fences,
    struct dma_fence_unwrap *cursors)
{
	struct dma_fence_array *result;
	struct dma_fence *tmp, *last = NULL, **array;
	ktime_t timestamp = 0, t;
	unsigned int i, count = 0;
	int n;

	/* Count the unsignalled leaves, note the latest signal time.  */
	for (i = 0; i < num_fences; i++) {
		dma_fence_unwrap_for_each(tmp, &cursors[i], fences[i]) {
			if (!dma_fence_is_signaled(tmp)) {
				dma_fence_put(last);
				last = dma_fence_get(tmp);
				count++;
			} else {
				t = dma_fence_timestamp(tmp);
				if (ktime_after(t, timestamp))
					timestamp = t;
			}
		}
	}
	if (count == 0)
		return dma_fence_allocate_private_stub(timestamp);
	if (count == 1)
		return last;
	dma_fence_put(last);

	array = kmalloc_array(count, sizeof(*array), GFP_KERNEL);
	if (array == NULL)
		return NULL;

	/*
	 * Collect them.  A fence can have signalled since the count, so
	 * the number found can be smaller; it cannot be larger because a
	 * signalled fence never becomes unsignalled.
	 */
	n = 0;
	for (i = 0; i < num_fences; i++) {
		dma_fence_unwrap_for_each(tmp, &cursors[i], fences[i]) {
			if (!dma_fence_is_signaled(tmp) && (unsigned)n < count)
				array[n++] = dma_fence_get(tmp);
		}
	}
	if (n == 0) {
		kfree(array);
		return dma_fence_allocate_private_stub(timestamp);
	}

	n = dma_fence_dedup_array(array, n);
	if (n == 1) {
		tmp = array[0];
		kfree(array);
		return tmp;
	}

	result = dma_fence_array_create(n, array, dma_fence_context_alloc(1),
	    1, false);
	if (result == NULL) {
		while (n--)
			dma_fence_put(array[n]);
		kfree(array);
		return NULL;
	}
	return &result->base;
}
