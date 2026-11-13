/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the Linux
 * interface, no Linux code.
 *
 * Red-black trees that keep a value per node computed from its subtree
 * (interval trees, drm_mm holes).  The caller supplies callbacks:
 *
 *	propagate(node, stop)	recompute the value of node and of its
 *				ancestors up to, not including, stop (NULL:
 *				the root); may end early at a node whose value
 *				did not change
 *	copy(old, new)		new takes the value of old (new is about to
 *				replace old in the tree)
 *	rotate(old, new)	a rotation made new the root of the subtree
 *				that old headed: new takes old's value, then
 *				the value of old is recomputed
 */
#ifndef _DRM618_LINUX_RBTREE_AUGMENTED_H_
#define _DRM618_LINUX_RBTREE_AUGMENTED_H_

#include <linux/rbtree.h>

struct rb_augment_callbacks {
	void (*propagate)(struct rb_node *node, struct rb_node *stop);
	void (*copy)(struct rb_node *old, struct rb_node *new);
	void (*rotate)(struct rb_node *old, struct rb_node *new);
};

void	rb_insert_augmented(struct rb_node *node, struct rb_root *root,
	    const struct rb_augment_callbacks *augment);
void	rb_erase_augmented(struct rb_node *node, struct rb_root *root,
	    const struct rb_augment_callbacks *augment);

static inline void
rb_insert_augmented_cached(struct rb_node *node, struct rb_root_cached *root,
    bool newleft, const struct rb_augment_callbacks *augment)
{

	if (newleft)
		root->rb_leftmost = node;
	rb_insert_augmented(node, &root->rb_root, augment);
}

static inline struct rb_node *
rb_erase_augmented_cached(struct rb_node *node, struct rb_root_cached *root,
    const struct rb_augment_callbacks *augment)
{
	struct rb_node *leftmost = NULL;

	if (root->rb_leftmost == node)
		leftmost = root->rb_leftmost = rb_next(node);
	rb_erase_augmented(node, &root->rb_root, augment);
	return leftmost;
}

/*
 * RB_DECLARE_CALLBACKS_MAX(rbstatic, rbname, rbstruct, rbfield,
 *     rbtype, rbaugmented, rbcompute)
 *
 *	Callbacks for the common case: the value rbaugmented of a node is
 *	the maximum of rbcompute(node) and the values of its children.
 */
#define	RB_DECLARE_CALLBACKS_MAX(rbstatic, rbname, rbstruct, rbfield,	\
    rbtype, rbaugmented, rbcompute)					\
static inline bool							\
rbname ## _compute_max(rbstruct *node)					\
{									\
	rbtype max = rbcompute(node), child;				\
									\
	if (node->rbfield.rb_left) {					\
		child = rb_entry(node->rbfield.rb_left, rbstruct,	\
		    rbfield)->rbaugmented;				\
		if (child > max)					\
			max = child;					\
	}								\
	if (node->rbfield.rb_right) {					\
		child = rb_entry(node->rbfield.rb_right, rbstruct,	\
		    rbfield)->rbaugmented;				\
		if (child > max)					\
			max = child;					\
	}								\
	if (node->rbaugmented == max)					\
		return false;						\
	node->rbaugmented = max;					\
	return true;							\
}									\
static void								\
rbname ## _propagate(struct rb_node *rb, struct rb_node *stop)		\
{									\
	while (rb != stop) {						\
		rbstruct *node = rb_entry(rb, rbstruct, rbfield);	\
		if (!rbname ## _compute_max(node))			\
			break;						\
		rb = rb_parent(&node->rbfield);				\
	}								\
}									\
static void								\
rbname ## _copy(struct rb_node *rb_old, struct rb_node *rb_new)	\
{									\
	rbstruct *old = rb_entry(rb_old, rbstruct, rbfield);		\
	rbstruct *new = rb_entry(rb_new, rbstruct, rbfield);		\
	new->rbaugmented = old->rbaugmented;				\
}									\
static void								\
rbname ## _rotate(struct rb_node *rb_old, struct rb_node *rb_new)	\
{									\
	rbstruct *old = rb_entry(rb_old, rbstruct, rbfield);		\
	rbstruct *new = rb_entry(rb_new, rbstruct, rbfield);		\
	new->rbaugmented = old->rbaugmented;				\
	old->rbaugmented = rbcompute(old);				\
	(void)rbname ## _compute_max(old);				\
}									\
rbstatic const struct rb_augment_callbacks rbname = {			\
	.propagate = rbname ## _propagate,				\
	.copy = rbname ## _copy,					\
	.rotate = rbname ## _rotate					\
};

#endif
