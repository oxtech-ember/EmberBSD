/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the Linux
 * interface, no Linux code.
 *
 * INTERVAL_TREE_DEFINE(ITSTRUCT, ITRB, ITTYPE, ITSUBTREE, ITSTART, ITLAST,
 *     ITSTATIC, ITPREFIX)
 *
 *	Defines an interval tree over closed intervals [ITSTART(n), ITLAST(n)]
 *	of nodes of type ITSTRUCT, linked through the rb_node field ITRB, with
 *	the maximum of ITLAST over the subtree kept in ITSUBTREE (type
 *	ITTYPE), as functions ITPREFIX_insert, ITPREFIX_remove,
 *	ITPREFIX_iter_first and ITPREFIX_iter_next.  The tree is a cached
 *	red-black tree ordered by ITSTART (equal starts keep the order of
 *	insertion); iter_first and iter_next visit, in that order, the nodes
 *	that overlap [start, last].  Shadows the shared layer's header (see
 *	rbtree.h).
 */
#ifndef _DRM618_LINUX_INTERVAL_TREE_GENERIC_H_
#define _DRM618_LINUX_INTERVAL_TREE_GENERIC_H_

#include <linux/rbtree_augmented.h>

#define	INTERVAL_TREE_DEFINE(ITSTRUCT, ITRB, ITTYPE, ITSUBTREE, ITSTART,\
    ITLAST, ITSTATIC, ITPREFIX)						\
									\
RB_DECLARE_CALLBACKS_MAX(static, ITPREFIX ## _augment, ITSTRUCT, ITRB,	\
    ITTYPE, ITSUBTREE, ITLAST)						\
									\
ITSTATIC void								\
ITPREFIX ## _insert(ITSTRUCT *node, struct rb_root_cached *root)	\
{									\
	struct rb_node **link = &root->rb_root.rb_node, *rb_parent = NULL;\
	ITTYPE start = ITSTART(node), last = ITLAST(node);		\
	ITSTRUCT *parent;						\
	bool leftmost = true;						\
									\
	while (*link) {							\
		rb_parent = *link;					\
		parent = rb_entry(rb_parent, ITSTRUCT, ITRB);		\
		if (parent->ITSUBTREE < last)				\
			parent->ITSUBTREE = last;			\
		if (start < ITSTART(parent))				\
			link = &parent->ITRB.rb_left;			\
		else {							\
			link = &parent->ITRB.rb_right;			\
			leftmost = false;				\
		}							\
	}								\
									\
	node->ITSUBTREE = last;						\
	rb_link_node(&node->ITRB, rb_parent, link);			\
	rb_insert_augmented_cached(&node->ITRB, root, leftmost,		\
	    &ITPREFIX ## _augment);					\
}									\
									\
ITSTATIC void								\
ITPREFIX ## _remove(ITSTRUCT *node, struct rb_root_cached *root)	\
{									\
	rb_erase_augmented_cached(&node->ITRB, root,			\
	    &ITPREFIX ## _augment);					\
}									\
									\
/* The first node of the subtree of node that overlaps [start, last]. */\
static ITSTRUCT *							\
ITPREFIX ## _subtree_search(ITSTRUCT *node, ITTYPE start, ITTYPE last)	\
{									\
	for (;;) {							\
		struct rb_node *rb;					\
									\
		rb = node->ITRB.rb_left;				\
		if (rb != NULL &&					\
		    rb_entry(rb, ITSTRUCT, ITRB)->ITSUBTREE >= start) {	\
			node = rb_entry(rb, ITSTRUCT, ITRB);		\
			continue;					\
		}							\
		if (ITSTART(node) > last)				\
			return NULL;					\
		if (start <= ITLAST(node))				\
			return node;					\
		rb = node->ITRB.rb_right;				\
		if (rb == NULL ||					\
		    rb_entry(rb, ITSTRUCT, ITRB)->ITSUBTREE < start)	\
			return NULL;					\
		node = rb_entry(rb, ITSTRUCT, ITRB);			\
	}								\
}									\
									\
ITSTATIC ITSTRUCT *							\
ITPREFIX ## _iter_first(struct rb_root_cached *root, ITTYPE start,	\
    ITTYPE last)							\
{									\
	ITSTRUCT *node;							\
									\
	if (root->rb_root.rb_node == NULL)				\
		return NULL;						\
	node = rb_entry(root->rb_root.rb_node, ITSTRUCT, ITRB);		\
	if (node->ITSUBTREE < start)					\
		return NULL;						\
	return ITPREFIX ## _subtree_search(node, start, last);		\
}									\
									\
ITSTATIC ITSTRUCT *							\
ITPREFIX ## _iter_next(ITSTRUCT *node, ITTYPE start, ITTYPE last)	\
{									\
	struct rb_node *rb, *prev;					\
	ITSTRUCT *found;						\
									\
	/* Right subtree first: it holds the next starts. */		\
	rb = node->ITRB.rb_right;					\
	if (rb != NULL &&						\
	    rb_entry(rb, ITSTRUCT, ITRB)->ITSUBTREE >= start) {		\
		found = ITPREFIX ## _subtree_search(			\
		    rb_entry(rb, ITSTRUCT, ITRB), start, last);		\
		if (found != NULL)					\
			return found;					\
	}								\
	/* Then the ancestors we are in the left subtree of, in order. */\
	prev = &node->ITRB;						\
	while ((rb = rb_parent(prev)) != NULL) {			\
		if (prev == rb->rb_left) {				\
			node = rb_entry(rb, ITSTRUCT, ITRB);		\
			if (ITSTART(node) > last)			\
				return NULL;				\
			if (start <= ITLAST(node))			\
				return node;				\
			if (rb->rb_right != NULL &&			\
			    rb_entry(rb->rb_right, ITSTRUCT,		\
				ITRB)->ITSUBTREE >= start) {		\
				found = ITPREFIX ## _subtree_search(	\
				    rb_entry(rb->rb_right, ITSTRUCT,	\
					ITRB), start, last);		\
				if (found != NULL)			\
					return found;			\
			}						\
		}							\
		prev = rb;						\
	}								\
	return NULL;							\
}

#endif
