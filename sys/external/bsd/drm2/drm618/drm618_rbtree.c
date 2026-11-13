/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the Linux
 * interface, no Linux code.
 *
 * Red-black tree with the Linux interface (include/linux/rbtree.h of
 * drm618) for the Linux 6.18 DRM core.  Insertion and deletion are the
 * textbook algorithms on nodes that keep their identity: a node with two
 * children is swapped for its in-order successor structurally, so the
 * pointers callers hold to their objects stay valid.  The callbacks of
 * rbtree_augmented.h are called after each structural change.
 */

#ifdef _KERNEL
#include <sys/cdefs.h>
#include <sys/param.h>
#else
#include <stdint.h>
#endif
#include <linux/rbtree_augmented.h>

#define	RB_RED		0UL
#define	RB_BLACK	1UL

static inline bool
is_black(const struct rb_node *n)
{

	return n == NULL || (n->__rb_parent_color & 1) != 0;
}

static inline bool
is_red(const struct rb_node *n)
{

	return !is_black(n);
}

static inline void
set_parent(struct rb_node *n, struct rb_node *p)
{

	n->__rb_parent_color = (n->__rb_parent_color & 3UL) | (unsigned long)p;
}

static inline void
set_color(struct rb_node *n, unsigned long c)
{

	n->__rb_parent_color = (n->__rb_parent_color & ~1UL) | c;
}

static void
nop_propagate(struct rb_node *node, struct rb_node *stop)
{
}

static void
nop_copy(struct rb_node *old, struct rb_node *new)
{
}

static void
nop_rotate(struct rb_node *old, struct rb_node *new)
{
}

static const struct rb_augment_callbacks no_augment = {
	.propagate = nop_propagate,
	.copy = nop_copy,
	.rotate = nop_rotate,
};

/* In the place of old, as a child of parent (or the root), put new. */
static inline void
replace_child(struct rb_root *root, struct rb_node *parent,
    struct rb_node *old, struct rb_node *new)
{

	if (parent == NULL)
		root->rb_node = new;
	else if (parent->rb_left == old)
		parent->rb_left = new;
	else
		parent->rb_right = new;
}

/* x goes down on the left, its right child y takes its place. */
static void
rotate_left(struct rb_node *x, struct rb_root *root,
    const struct rb_augment_callbacks *aug)
{
	struct rb_node *y = x->rb_right, *parent = rb_parent(x);

	x->rb_right = y->rb_left;
	if (y->rb_left != NULL)
		set_parent(y->rb_left, x);
	y->__rb_parent_color = (y->__rb_parent_color & 3UL) |
	    (unsigned long)parent;
	replace_child(root, parent, x, y);
	y->rb_left = x;
	set_parent(x, y);
	aug->rotate(x, y);
}

/* x goes down on the right, its left child y takes its place. */
static void
rotate_right(struct rb_node *x, struct rb_root *root,
    const struct rb_augment_callbacks *aug)
{
	struct rb_node *y = x->rb_left, *parent = rb_parent(x);

	x->rb_left = y->rb_right;
	if (y->rb_right != NULL)
		set_parent(y->rb_right, x);
	y->__rb_parent_color = (y->__rb_parent_color & 3UL) |
	    (unsigned long)parent;
	replace_child(root, parent, x, y);
	y->rb_right = x;
	set_parent(x, y);
	aug->rotate(x, y);
}

static void
insert_fixup(struct rb_node *node, struct rb_root *root,
    const struct rb_augment_callbacks *aug)
{
	struct rb_node *parent, *gparent, *uncle, *t;

	set_color(node, RB_RED);
	while ((parent = rb_parent(node)) != NULL && is_red(parent)) {
		gparent = rb_parent(parent);	/* a red node is not the root */
		if (parent == gparent->rb_left) {
			uncle = gparent->rb_right;
			if (is_red(uncle)) {
				set_color(parent, RB_BLACK);
				set_color(uncle, RB_BLACK);
				set_color(gparent, RB_RED);
				node = gparent;
				continue;
			}
			if (node == parent->rb_right) {
				rotate_left(parent, root, aug);
				t = parent;
				parent = node;
				node = t;
			}
			set_color(parent, RB_BLACK);
			set_color(gparent, RB_RED);
			rotate_right(gparent, root, aug);
		} else {
			uncle = gparent->rb_left;
			if (is_red(uncle)) {
				set_color(parent, RB_BLACK);
				set_color(uncle, RB_BLACK);
				set_color(gparent, RB_RED);
				node = gparent;
				continue;
			}
			if (node == parent->rb_left) {
				rotate_right(parent, root, aug);
				t = parent;
				parent = node;
				node = t;
			}
			set_color(parent, RB_BLACK);
			set_color(gparent, RB_RED);
			rotate_left(gparent, root, aug);
		}
	}
	set_color(root->rb_node, RB_BLACK);
}

void
rb_insert_augmented(struct rb_node *node, struct rb_root *root,
    const struct rb_augment_callbacks *augment)
{

	insert_fixup(node, root, augment);
}

void
rb_insert_color(struct rb_node *node, struct rb_root *root)
{

	insert_fixup(node, root, &no_augment);
}

/*
 * The subtree of parent on the side of node is one black node short
 * (node is NULL or black, parent its parent, NULL when node is the root).
 */
static void
erase_fixup(struct rb_node *node, struct rb_node *parent,
    struct rb_root *root, const struct rb_augment_callbacks *aug)
{
	struct rb_node *sib;

	while (parent != NULL && is_black(node)) {
		if (node == parent->rb_left) {
			sib = parent->rb_right;
			if (is_red(sib)) {
				set_color(sib, RB_BLACK);
				set_color(parent, RB_RED);
				rotate_left(parent, root, aug);
				sib = parent->rb_right;
			}
			if (is_black(sib->rb_left) && is_black(sib->rb_right)) {
				set_color(sib, RB_RED);
				node = parent;
				parent = rb_parent(node);
				continue;
			}
			if (is_black(sib->rb_right)) {
				set_color(sib->rb_left, RB_BLACK);
				set_color(sib, RB_RED);
				rotate_right(sib, root, aug);
				sib = parent->rb_right;
			}
			set_color(sib, parent->__rb_parent_color & 1UL);
			set_color(parent, RB_BLACK);
			set_color(sib->rb_right, RB_BLACK);
			rotate_left(parent, root, aug);
			node = root->rb_node;
			break;
		} else {
			sib = parent->rb_left;
			if (is_red(sib)) {
				set_color(sib, RB_BLACK);
				set_color(parent, RB_RED);
				rotate_right(parent, root, aug);
				sib = parent->rb_left;
			}
			if (is_black(sib->rb_left) && is_black(sib->rb_right)) {
				set_color(sib, RB_RED);
				node = parent;
				parent = rb_parent(node);
				continue;
			}
			if (is_black(sib->rb_left)) {
				set_color(sib->rb_right, RB_BLACK);
				set_color(sib, RB_RED);
				rotate_left(sib, root, aug);
				sib = parent->rb_left;
			}
			set_color(sib, parent->__rb_parent_color & 1UL);
			set_color(parent, RB_BLACK);
			set_color(sib->rb_left, RB_BLACK);
			rotate_right(parent, root, aug);
			node = root->rb_node;
			break;
		}
	}
	if (node != NULL)
		set_color(node, RB_BLACK);
}

/*
 * Put succ, the in-order successor of node (the leftmost node of its right
 * subtree), in the place of node, and node in the place of succ: links,
 * parents and colours are exchanged, no payload is touched.  Afterwards
 * node has no left child.
 */
static void
swap_with_successor(struct rb_node *node, struct rb_node *succ,
    struct rb_root *root)
{
	struct rb_node *nparent = rb_parent(node), *sparent = rb_parent(succ);
	struct rb_node *nleft = node->rb_left, *nright = node->rb_right;
	struct rb_node *sright = succ->rb_right;
	unsigned long ncolor = node->__rb_parent_color & 1UL;
	unsigned long scolor = succ->__rb_parent_color & 1UL;

	/* succ takes the place of node */
	replace_child(root, nparent, node, succ);
	succ->__rb_parent_color = (unsigned long)nparent | ncolor;
	succ->rb_left = nleft;
	set_parent(nleft, succ);

	if (sparent == node) {
		/* succ was the right child of node */
		succ->rb_right = node;
		node->__rb_parent_color = (unsigned long)succ | scolor;
	} else {
		succ->rb_right = nright;
		set_parent(nright, succ);
		sparent->rb_left = node;
		node->__rb_parent_color = (unsigned long)sparent | scolor;
	}
	node->rb_left = NULL;
	node->rb_right = sright;
	if (sright != NULL)
		set_parent(sright, node);
}

static void
erase_common(struct rb_node *node, struct rb_root *root,
    const struct rb_augment_callbacks *aug)
{
	struct rb_node *child, *parent, *succ = NULL, *start;
	bool black;

	if (node->rb_left != NULL && node->rb_right != NULL) {
		succ = node->rb_right;
		while (succ->rb_left != NULL)
			succ = succ->rb_left;
		/*
		 * The ancestors of the place of node hold values that
		 * include node's; succ takes that place and the value.
		 */
		aug->copy(node, succ);
		swap_with_successor(node, succ, root);
	}

	/* node has at most one child, on the right or the left. */
	child = node->rb_left != NULL ? node->rb_left : node->rb_right;
	parent = rb_parent(node);
	black = (node->__rb_parent_color & 1UL) != 0;
	replace_child(root, parent, node, child);
	if (child != NULL)
		set_parent(child, parent);

	/*
	 * Values on the path from the removed place up are stale; rotations
	 * of the rebalancing below take them as they are, so first bring
	 * them right.
	 */
	start = parent;
	if (succ != NULL) {
		/*
		 * Up to succ, whose value is that of node's place; its
		 * own recomputation may differ, so go on from it, not
		 * just from where the first walk could end early.
		 */
		aug->propagate(start, succ);
		aug->propagate(succ, NULL);
	} else if (start != NULL) {
		aug->propagate(start, NULL);
	}

	if (black) {
		if (is_red(child))
			set_color(child, RB_BLACK);
		else
			erase_fixup(child, parent, root, aug);
	}
}

void
rb_erase_augmented(struct rb_node *node, struct rb_root *root,
    const struct rb_augment_callbacks *augment)
{

	erase_common(node, root, augment);
}

void
rb_erase(struct rb_node *node, struct rb_root *root)
{

	erase_common(node, root, &no_augment);
}

struct rb_node *
rb_first(const struct rb_root *root)
{
	struct rb_node *n = root->rb_node;

	if (n == NULL)
		return NULL;
	while (n->rb_left != NULL)
		n = n->rb_left;
	return n;
}

struct rb_node *
rb_last(const struct rb_root *root)
{
	struct rb_node *n = root->rb_node;

	if (n == NULL)
		return NULL;
	while (n->rb_right != NULL)
		n = n->rb_right;
	return n;
}

struct rb_node *
rb_next(const struct rb_node *node)
{
	struct rb_node *parent;

	if (RB_EMPTY_NODE(node))
		return NULL;
	if (node->rb_right != NULL) {
		node = node->rb_right;
		while (node->rb_left != NULL)
			node = node->rb_left;
		return (struct rb_node *)(uintptr_t)node;
	}
	while ((parent = rb_parent(node)) != NULL && node == parent->rb_right)
		node = parent;
	return parent;
}

struct rb_node *
rb_prev(const struct rb_node *node)
{
	struct rb_node *parent;

	if (RB_EMPTY_NODE(node))
		return NULL;
	if (node->rb_left != NULL) {
		node = node->rb_left;
		while (node->rb_right != NULL)
			node = node->rb_right;
		return (struct rb_node *)(uintptr_t)node;
	}
	while ((parent = rb_parent(node)) != NULL && node == parent->rb_left)
		node = parent;
	return parent;
}

void
rb_replace_node(struct rb_node *victim, struct rb_node *new,
    struct rb_root *root)
{
	struct rb_node *parent = rb_parent(victim);

	*new = *victim;
	replace_child(root, parent, victim, new);
	if (victim->rb_left != NULL)
		set_parent(victim->rb_left, new);
	if (victim->rb_right != NULL)
		set_parent(victim->rb_right, new);
}

/* The deepest node of the subtree of n that comes first in post-order. */
static struct rb_node *
postorder_deepest(struct rb_node *n)
{

	for (;;) {
		if (n->rb_left != NULL)
			n = n->rb_left;
		else if (n->rb_right != NULL)
			n = n->rb_right;
		else
			return n;
	}
}

struct rb_node *
rb_first_postorder(const struct rb_root *root)
{

	if (root->rb_node == NULL)
		return NULL;
	return postorder_deepest(root->rb_node);
}

struct rb_node *
rb_next_postorder(const struct rb_node *node)
{
	struct rb_node *parent;

	if (node == NULL)
		return NULL;
	parent = rb_parent(node);
	if (parent == NULL)
		return NULL;
	if (node == parent->rb_left && parent->rb_right != NULL)
		return postorder_deepest(parent->rb_right);
	return parent;
}
