/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation of the Linux interval_tree_generic.h interface as used
 * by the MIT-licensed consumers in dist618; written without access to the
 * GPL original and to the earlier version of this file.  Sources read: the
 * two instantiations in drm_mm.c and drm_gpuvm.c of dist618 (and the code
 * around them that links nodes by hand and calls the generated augment
 * callbacks), drm618_rbtree.c, the host contract ember/tests/drm2-rbtree.c,
 * the shared layer's BSD interval_tree_generic.h.
 *
 * INTERVAL_TREE_DEFINE builds an interval tree over the closed intervals
 * [ITSTART(n), ITLAST(n)] of nodes of type ITSTRUCT, which embed the rb_node
 * member ITRB (a nested member path is fine) and a member ITSUBTREE of type
 * ITTYPE that holds the largest ITLAST of the node's subtree.  The tree is
 * ordered by start, nodes with the same start in order of insertion.
 * Generated, all with the storage class ITSTATIC and the name prefix
 * ITPREFIX:
 *   ITPREFIX_insert(node, root)           root: struct rb_root_cached *
 *   ITPREFIX_remove(node, root)
 *   ITPREFIX_iter_first(root, start, last)  first node (by start) that
 *                                         overlaps [start, last], or NULL
 *   ITPREFIX_iter_next(node, start, last)   the next one after node, or NULL
 * and, static, the callbacks ITPREFIX_augment for callers that link nodes
 * by hand and use rb_insert_augmented_cached().
 */
#ifndef _LINUX_INTERVAL_TREE_GENERIC_H_
#define _LINUX_INTERVAL_TREE_GENERIC_H_

#include <linux/rbtree.h>
#include <linux/rbtree_augmented.h>

#define	INTERVAL_TREE_DEFINE(ITSTRUCT, ITRB, ITTYPE, ITSUBTREE, ITSTART,     \
    ITLAST, ITSTATIC, ITPREFIX)						      \
									      \
RB_DECLARE_CALLBACKS_MAX(static, ITPREFIX##_augment, ITSTRUCT, ITRB,	      \
    ITTYPE, ITSUBTREE, ITLAST)						      \
									      \
ITSTATIC void								      \
ITPREFIX##_insert(ITSTRUCT *__node, struct rb_root_cached *__root)	      \
{									      \
	struct rb_node **__link = &__root->rb_root.rb_node;		      \
	struct rb_node *__parent = NULL;				      \
	const ITTYPE __start = ITSTART(__node), __last = ITLAST(__node);      \
	bool __leftmost = true;						      \
									      \
	__node->ITSUBTREE = __last;					      \
	while (*__link != NULL) {					      \
		ITSTRUCT *__p;						      \
									      \
		__parent = *__link;					      \
		__p = rb_entry(__parent, ITSTRUCT, ITRB);		      \
		if (__p->ITSUBTREE < __last)				      \
			__p->ITSUBTREE = __last;			      \
		if (__start < ITSTART(__p)) {				      \
			__link = &__parent->rb_left;			      \
		} else {						      \
			__link = &__parent->rb_right;			      \
			__leftmost = false;				      \
		}							      \
	}								      \
	rb_link_node(&__node->ITRB, __parent, __link);			      \
	rb_insert_augmented_cached(&__node->ITRB, __root, __leftmost,	      \
	    &ITPREFIX##_augment);					      \
}									      \
									      \
ITSTATIC void								      \
ITPREFIX##_remove(ITSTRUCT *__node, struct rb_root_cached *__root)	      \
{									      \
									      \
	rb_erase_augmented_cached(&__node->ITRB, __root,		      \
	    &ITPREFIX##_augment);					      \
}									      \
									      \
/*									      \
 * The first node in start order of the subtree of __node that overlaps	      \
 * [__start, __last].  Every node of a left subtree starts no later than    \
 * its parent, so when the left subtree reaches as far as __start, it has   \
 * a match if the parent starts in time and is the only place for one if    \
 * it does not: go left without coming back.  Otherwise the parent is next, \
 * then the right subtree, whose nodes start no earlier than the parent.    \
 */									      \
static inline ITSTRUCT *						      \
ITPREFIX##__subtree_search(ITSTRUCT *__node, ITTYPE __start, ITTYPE __last)   \
{									      \
									      \
	for (;;) {							      \
		if (__node->ITRB.rb_left != NULL) {			      \
			ITSTRUCT *__l = rb_entry(__node->ITRB.rb_left,	      \
			    ITSTRUCT, ITRB);				      \
									      \
			if (__start <= __l->ITSUBTREE) {		      \
				__node = __l;				      \
				continue;				      \
			}						      \
		}							      \
		if (ITSTART(__node) > __last)				      \
			return NULL;					      \
		if (__start <= ITLAST(__node))				      \
			return __node;					      \
		if (__node->ITRB.rb_right != NULL) {			      \
			ITSTRUCT *__r = rb_entry(__node->ITRB.rb_right,	      \
			    ITSTRUCT, ITRB);				      \
									      \
			if (__start <= __r->ITSUBTREE) {		      \
				__node = __r;				      \
				continue;				      \
			}						      \
		}							      \
		return NULL;						      \
	}								      \
}									      \
									      \
ITSTATIC ITSTRUCT *							      \
ITPREFIX##_iter_first(struct rb_root_cached *__root, ITTYPE __start,	      \
    ITTYPE __last)							      \
{									      \
	ITSTRUCT *__top;						      \
									      \
	if (__root->rb_root.rb_node == NULL)				      \
		return NULL;						      \
	__top = rb_entry(__root->rb_root.rb_node, ITSTRUCT, ITRB);	      \
	if (__top->ITSUBTREE < __start)					      \
		return NULL;						      \
	return ITPREFIX##__subtree_search(__top, __start, __last);	      \
}									      \
									      \
ITSTATIC ITSTRUCT *							      \
ITPREFIX##_iter_next(ITSTRUCT *__node, ITTYPE __start, ITTYPE __last)	      \
{									      \
	struct rb_node *__rb = __node->ITRB.rb_right, *__prev;		      \
									      \
	for (;;) {							      \
		if (__rb != NULL) {					      \
			ITSTRUCT *__r = rb_entry(__rb, ITSTRUCT, ITRB);	      \
									      \
			if (__start <= __r->ITSUBTREE) {		      \
				__r = ITPREFIX##__subtree_search(__r,	      \
				    __start, __last);			      \
				if (__r != NULL)			      \
					return __r;			      \
			}						      \
		}							      \
		/* up to the nearest ancestor that comes after __node */      \
		do {							      \
			__rb = rb_parent(&__node->ITRB);		      \
			if (__rb == NULL)				      \
				return NULL;				      \
			__prev = &__node->ITRB;				      \
			__node = rb_entry(__rb, ITSTRUCT, ITRB);	      \
			__rb = __node->ITRB.rb_right;			      \
		} while (__prev == __rb);				      \
		if (ITSTART(__node) > __last)				      \
			return NULL;					      \
		if (__start <= ITLAST(__node))				      \
			return __node;					      \
	}								      \
}

#endif	/* _LINUX_INTERVAL_TREE_GENERIC_H_ */
