/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the Linux
 * interface (struct and macro names, the encoding of rb_parent and
 * RB_EMPTY_NODE that callers use), no Linux code.
 *
 * Linux style red-black tree for the Linux 6.18 DRM core: the caller
 * walks the tree itself, links the new node with rb_link_node and lets
 * rb_insert_color rebalance.  Shadows <linux/rbtree.h> of the shared
 * layer, which is the NetBSD rb_tree(3) behind Linux names and has a
 * different struct rb_root (the 5.6 core uses that one).  The code is
 * drm618/drm618_rbtree.c.
 *
 * rb_parent_color of a node holds the parent in the upper bits and the
 * colour in bit 0 (0 red, 1 black); a node that is not in a tree has
 * itself as the parent (RB_CLEAR_NODE).
 */
#ifndef _DRM618_LINUX_RBTREE_H_
#define _DRM618_LINUX_RBTREE_H_

#ifdef _KERNEL
#include <sys/param.h>
#include <sys/types.h>
#else
#include <stddef.h>
#include <stdbool.h>
#endif

#ifndef container_of
#define	container_of(ptr, type, member)					\
	((type *)((char *)(ptr) - offsetof(type, member)))
#endif

struct rb_node {
	unsigned long	__rb_parent_color;
	struct rb_node	*rb_right;
	struct rb_node	*rb_left;
} __attribute__((aligned(sizeof(long))));

struct rb_root {
	struct rb_node	*rb_node;
};

struct rb_root_cached {
	struct rb_root	rb_root;
	struct rb_node	*rb_leftmost;
};

#define	RB_ROOT		(struct rb_root) { NULL, }
#define	RB_ROOT_CACHED	(struct rb_root_cached) { {NULL, }, NULL }

#define	rb_parent(r)	((struct rb_node *)((r)->__rb_parent_color & ~3UL))

#define	rb_entry(ptr, type, member)	container_of(ptr, type, member)
#define	rb_entry_safe(ptr, type, member)				\
	({ __typeof__(ptr) ____ptr = (ptr);				\
	   ____ptr ? rb_entry(____ptr, type, member) : NULL;		\
	})

#define	RB_EMPTY_ROOT(root)	((root)->rb_node == NULL)
#define	RB_EMPTY_NODE(node)	((node)->__rb_parent_color == (unsigned long)(node))
#define	RB_CLEAR_NODE(node)	((node)->__rb_parent_color = (unsigned long)(node))

static inline void
rb_link_node(struct rb_node *node, struct rb_node *parent,
    struct rb_node **rb_link)
{

	node->__rb_parent_color = (unsigned long)parent;
	node->rb_left = node->rb_right = NULL;
	*rb_link = node;
}

void	rb_insert_color(struct rb_node *, struct rb_root *);
void	rb_erase(struct rb_node *, struct rb_root *);

struct rb_node *rb_next(const struct rb_node *);
struct rb_node *rb_prev(const struct rb_node *);
struct rb_node *rb_first(const struct rb_root *);
struct rb_node *rb_last(const struct rb_root *);

/* Replace victim by new in the tree; new takes the links and the colour. */
void	rb_replace_node(struct rb_node *victim, struct rb_node *new,
	    struct rb_root *root);

/* Post-order walk, for freeing a whole tree. */
struct rb_node *rb_first_postorder(const struct rb_root *);
struct rb_node *rb_next_postorder(const struct rb_node *);

#define	rbtree_postorder_for_each_entry_safe(pos, n, root, field)	\
	for (pos = rb_entry_safe(rb_first_postorder(root),		\
		    __typeof__(*pos), field);				\
	    pos && ({ n = rb_entry_safe(rb_next_postorder(&pos->field),	\
		    __typeof__(*pos), field); 1; });			\
	    pos = n)

/* The leftmost node of a cached tree is kept in rb_leftmost. */
static inline struct rb_node *
rb_first_cached(const struct rb_root_cached *root)
{

	return root->rb_leftmost;
}

static inline void
rb_insert_color_cached(struct rb_node *node, struct rb_root_cached *root,
    bool leftmost)
{

	if (leftmost)
		root->rb_leftmost = node;
	rb_insert_color(node, &root->rb_root);
}

static inline struct rb_node *
rb_erase_cached(struct rb_node *node, struct rb_root_cached *root)
{
	struct rb_node *leftmost = NULL;

	if (root->rb_leftmost == node)
		leftmost = root->rb_leftmost = rb_next(node);
	rb_erase(node, &root->rb_root);
	return leftmost;
}

static inline void
rb_replace_node_cached(struct rb_node *victim, struct rb_node *new,
    struct rb_root_cached *root)
{

	if (root->rb_leftmost == victim)
		root->rb_leftmost = new;
	rb_replace_node(victim, new, &root->rb_root);
}

/*
 * rb_add(node, tree, less)
 *
 *	Insert node; less(a, b) says whether a sorts before b.  A node that
 *	compares equal goes after the existing ones.
 */
static inline void
rb_add(struct rb_node *node, struct rb_root *tree,
    bool (*less)(struct rb_node *, const struct rb_node *))
{
	struct rb_node **link = &tree->rb_node;
	struct rb_node *parent = NULL;

	while (*link != NULL) {
		parent = *link;
		if (less(node, parent))
			link = &parent->rb_left;
		else
			link = &parent->rb_right;
	}
	rb_link_node(node, parent, link);
	rb_insert_color(node, tree);
}

static inline struct rb_node *
rb_add_cached(struct rb_node *node, struct rb_root_cached *tree,
    bool (*less)(struct rb_node *, const struct rb_node *))
{
	struct rb_node **link = &tree->rb_root.rb_node;
	struct rb_node *parent = NULL;
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
	rb_insert_color_cached(node, tree, leftmost);
	return leftmost ? node : NULL;
}

#endif
