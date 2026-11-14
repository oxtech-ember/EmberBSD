/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation of the Linux rbtree_augmented.h interface as used by
 * the MIT-licensed consumers in dist618; written without access to the GPL
 * original and to the earlier version of this file.  Sources read: drm_mm.c
 * and drm_gpuvm.c of dist618 (the users of augmented trees), drm618_rbtree.c
 * (the algorithms and the points where they call the callbacks), the host
 * contract ember/tests/drm2-rbtree.c.
 *
 * An augmented tree keeps a value in each node that is a function of the
 * node's whole subtree (the largest interval end, the largest hole).  The
 * tree code calls three callbacks to keep it right:
 *   propagate(node, stop)  recompute node and its ancestors up to, not
 *                          including, stop (NULL: up to the root), and stop
 *                          early once a value comes out unchanged;
 *   copy(old, new)         new takes the subtree value of old;
 *   rotate(old, new)       after a rotation: new is where old was and has
 *                          the same set of nodes below it, so it takes old's
 *                          value, and old is recomputed from its new children.
 * On insertion the caller has already put the node's own value in and raised
 * the values of the ancestors; on erasure the callbacks do all the work.
 */
#ifndef _LINUX_RBTREE_AUGMENTED_H_
#define _LINUX_RBTREE_AUGMENTED_H_

#include <linux/rbtree.h>

struct rb_augment_callbacks {
	void (*propagate)(struct rb_node *, struct rb_node *);
	void (*copy)(struct rb_node *, struct rb_node *);
	void (*rotate)(struct rb_node *, struct rb_node *);
};

void rb_insert_augmented(struct rb_node *, struct rb_root *,
    const struct rb_augment_callbacks *);
void rb_erase_augmented(struct rb_node *, struct rb_root *,
    const struct rb_augment_callbacks *);

static inline void
rb_insert_augmented_cached(struct rb_node *node, struct rb_root_cached *root,
    bool leftmost, const struct rb_augment_callbacks *augment)
{

	if (leftmost)
		root->rb_leftmost = node;
	rb_insert_augmented(node, &root->rb_root, augment);
}

static inline void
rb_erase_augmented_cached(struct rb_node *node, struct rb_root_cached *root,
    const struct rb_augment_callbacks *augment)
{

	if (root->rb_leftmost == node)
		root->rb_leftmost = rb_next(node);
	rb_erase_augmented(node, &root->rb_root, augment);
}

/*
 * Callbacks for a value that is the maximum of a per-node quantity over the
 * subtree: node->RBAUGMENTED = max(RBCOMPUTE(node), the value of the left
 * child, the value of the right child).  RBSTRUCT is the type that embeds
 * the rb_node member RBFIELD (which may be a nested member path), RBTYPE the
 * type of the value and of RBCOMPUTE's result.  The callbacks are the object
 * RBNAME, declared with the storage class RBSTATIC.
 */
#define	RB_DECLARE_CALLBACKS_MAX(RBSTATIC, RBNAME, RBSTRUCT, RBFIELD,	\
    RBTYPE, RBAUGMENTED, RBCOMPUTE)					\
									\
/* Recompute the value of node; true when it changed. */		\
static inline bool							\
RBNAME##_recompute(RBSTRUCT *__node)					\
{									\
	RBTYPE __max = RBCOMPUTE(__node), __sub;			\
									\
	if (__node->RBFIELD.rb_left != NULL) {				\
		__sub = rb_entry(__node->RBFIELD.rb_left, RBSTRUCT,	\
		    RBFIELD)->RBAUGMENTED;				\
		if (__sub > __max)					\
			__max = __sub;					\
	}								\
	if (__node->RBFIELD.rb_right != NULL) {				\
		__sub = rb_entry(__node->RBFIELD.rb_right, RBSTRUCT,	\
		    RBFIELD)->RBAUGMENTED;				\
		if (__sub > __max)					\
			__max = __sub;					\
	}								\
	if (__node->RBAUGMENTED == __max)				\
		return false;						\
	__node->RBAUGMENTED = __max;					\
	return true;							\
}									\
									\
static inline void							\
RBNAME##_propagate(struct rb_node *__rb, struct rb_node *__stop)	\
{									\
									\
	while (__rb != __stop) {					\
		RBSTRUCT *__node = rb_entry(__rb, RBSTRUCT, RBFIELD);	\
									\
		if (!RBNAME##_recompute(__node))			\
			break;						\
		__rb = rb_parent(&__node->RBFIELD);			\
	}								\
}									\
									\
static inline void							\
RBNAME##_copy(struct rb_node *__old, struct rb_node *__new)		\
{									\
									\
	rb_entry(__new, RBSTRUCT, RBFIELD)->RBAUGMENTED =		\
	    rb_entry(__old, RBSTRUCT, RBFIELD)->RBAUGMENTED;		\
}									\
									\
static inline void							\
RBNAME##_rotate(struct rb_node *__old, struct rb_node *__new)		\
{									\
									\
	RBNAME##_copy(__old, __new);					\
	(void)RBNAME##_recompute(rb_entry(__old, RBSTRUCT, RBFIELD));	\
}									\
									\
RBSTATIC const struct rb_augment_callbacks RBNAME = {			\
	.propagate = RBNAME##_propagate,				\
	.copy = RBNAME##_copy,						\
	.rotate = RBNAME##_rotate,					\
};

#endif	/* _LINUX_RBTREE_AUGMENTED_H_ */
