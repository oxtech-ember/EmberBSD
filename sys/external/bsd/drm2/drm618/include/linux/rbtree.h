/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation of the Linux rbtree.h interface as used by the
 * MIT-licensed consumers in dist618; written without access to the GPL
 * original and to the earlier version of this file.  Sources read: the
 * consumers in dist618 (drm_mm, drm_gpuvm, drm_vma_manager, drm_prime,
 * drm_buddy, gpu/buddy, scheduler), drm618_rbtree.c, drm618_prefix.h,
 * the host contract ember/tests/drm2-rbtree.c, the shared layer's BSD
 * linux/rbtree.h, <sys/container_of.h>.
 *
 * A node is three words: the parent pointer with the colour in bit 0 (1 is
 * black, 0 is red; bit 1 is kept clear), and the two child pointers.  A node
 * that is not in a tree is marked empty by being its own parent
 * (RB_CLEAR_NODE).  The tree algorithms are in drm618_rbtree.c; this header
 * has the types, the inline helpers and the cached-leftmost wrappers.
 */
#ifndef _LINUX_RBTREE_H_
#define _LINUX_RBTREE_H_

#ifdef _KERNEL
#include <sys/types.h>
#include <sys/container_of.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifndef container_of
#define	container_of(PTR, TYPE, FIELD)					\
	((TYPE *)((char *)(PTR) - offsetof(TYPE, FIELD)))
#endif
#endif

struct rb_node {
	unsigned long	__rb_parent_color;
	struct rb_node	*rb_right;
	struct rb_node	*rb_left;
} __attribute__((aligned(sizeof(long))));

struct rb_root {
	struct rb_node	*rb_node;
};

/* A tree that also remembers its first (leftmost) node. */
struct rb_root_cached {
	struct rb_root	rb_root;
	struct rb_node	*rb_leftmost;
};

/* NetBSD's <sys/tree.h> has a RB_ROOT(head) of its own. */
#undef	RB_ROOT
#define	RB_ROOT		((struct rb_root){ NULL })
#define	RB_ROOT_CACHED	((struct rb_root_cached){ { NULL }, NULL })

#define	rb_parent(r)	((struct rb_node *)((r)->__rb_parent_color & ~3UL))

#define	rb_entry(PTR, TYPE, FIELD)	container_of(PTR, TYPE, FIELD)
#define	rb_entry_safe(PTR, TYPE, FIELD)					\
({									\
	__typeof__(PTR) __rb_p = (PTR);					\
	__rb_p != NULL ? rb_entry(__rb_p, TYPE, FIELD) : (TYPE *)NULL;	\
})

#define	RB_EMPTY_NODE(n)	((n)->__rb_parent_color == (unsigned long)(n))
#define	RB_CLEAR_NODE(n)	((void)((n)->__rb_parent_color = (unsigned long)(n)))

static inline bool
RB_EMPTY_ROOT(const struct rb_root *root)
{

	return root->rb_node == NULL;
}

void rb_insert_color(struct rb_node *, struct rb_root *);
void rb_erase(struct rb_node *, struct rb_root *);
struct rb_node *rb_first(const struct rb_root *);
struct rb_node *rb_last(const struct rb_root *);
struct rb_node *rb_next(const struct rb_node *);
struct rb_node *rb_prev(const struct rb_node *);
void rb_replace_node(struct rb_node *, struct rb_node *, struct rb_root *);
struct rb_node *rb_first_postorder(const struct rb_root *);
struct rb_node *rb_next_postorder(const struct rb_node *);

/*
 * Make node a leaf, a child of parent, at the place *link of parent (or at
 * the root); the caller found the place by walking down.  The node is red
 * and not yet balanced: rb_insert_color does that.
 */
static inline void
rb_link_node(struct rb_node *node, struct rb_node *parent,
    struct rb_node **link)
{

	node->__rb_parent_color = (unsigned long)parent;
	node->rb_left = node->rb_right = NULL;
	*link = node;
}

static inline struct rb_node *
rb_first_cached(const struct rb_root_cached *root)
{

	return root->rb_leftmost;
}

/* leftmost tells that the new node is the first of the tree. */
static inline void
rb_insert_color_cached(struct rb_node *node, struct rb_root_cached *root,
    bool leftmost)
{

	if (leftmost)
		root->rb_leftmost = node;
	rb_insert_color(node, &root->rb_root);
}

static inline void
rb_erase_cached(struct rb_node *node, struct rb_root_cached *root)
{

	if (root->rb_leftmost == node)
		root->rb_leftmost = rb_next(node);
	rb_erase(node, &root->rb_root);
}

static inline void
rb_replace_node_cached(struct rb_node *victim, struct rb_node *node,
    struct rb_root_cached *root)
{

	if (root->rb_leftmost == victim)
		root->rb_leftmost = node;
	rb_replace_node(victim, node, &root->rb_root);
}

/*
 * Insert node by the order less(a, b), "a goes before b": a node equal to
 * others goes after them.  Returns node when it became the first of the
 * tree, else NULL.
 */
static inline struct rb_node *
rb_add_cached(struct rb_node *node, struct rb_root_cached *root,
    bool (*less)(struct rb_node *, const struct rb_node *))
{
	struct rb_node **link = &root->rb_root.rb_node, *parent = NULL;
	bool leftmost = true;

	while (*link != NULL) {
		parent = *link;
		if (less(node, parent)) {
			link = &parent->rb_left;
		} else {
			link = &parent->rb_right;
			leftmost = false;
		}
	}
	rb_link_node(node, parent, link);
	rb_insert_color_cached(node, root, leftmost);
	return leftmost ? node : NULL;
}

static inline void
rb_add(struct rb_node *node, struct rb_root *root,
    bool (*less)(struct rb_node *, const struct rb_node *))
{
	struct rb_node **link = &root->rb_node, *parent = NULL;

	while (*link != NULL) {
		parent = *link;
		if (less(node, parent))
			link = &parent->rb_left;
		else
			link = &parent->rb_right;
	}
	rb_link_node(node, parent, link);
	rb_insert_color(node, root);
}

/*
 * Visit every entry of the tree children first.  The next entry is found
 * before the body runs, so the body may free the entry.
 */
#define	rbtree_postorder_for_each_entry_safe(pos, n, root, field)	\
	for ((pos) = rb_entry_safe(rb_first_postorder(root),		\
		    __typeof__(*(pos)), field);				\
	    (pos) != NULL &&						\
		((n) = rb_entry_safe(rb_next_postorder(&(pos)->field),	\
		    __typeof__(*(pos)), field), 1);			\
	    (pos) = (n))

#endif	/* _LINUX_RBTREE_H_ */
