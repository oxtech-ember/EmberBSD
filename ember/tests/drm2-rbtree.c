/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Host contract for the Linux style red-black tree of the 6.18 DRM core
 * (sys/external/bsd/drm2/drm618/include/linux/rbtree*.h and
 * drm618_rbtree.c): after every insertion and erasure of long random
 * sequences the tree has the red-black properties (root black, no red node
 * with a red child, equal black height on all paths), parent links agree
 * with child links, the in-order walk is sorted and matches rb_first/
 * rb_next/rb_last/rb_prev and the cached leftmost; the per-node subtree
 * maximum of an augmented tree is right; interval queries agree with a
 * brute-force scan, also with two interval tree instantiations in one file,
 * a nested node member, and hand-linked augmented trees.
 * usage: drm2-rbtree.sh   (compiles and runs this file with the host cc)
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/interval_tree_generic.h>
#include <linux/rbtree.h>
#include <linux/rbtree_augmented.h>

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

struct item {
	struct rb_node	rb;
	unsigned long	key;
	unsigned long	last;		/* interval end, for the augmented tests */
	unsigned long	subtree_last;
	unsigned	seq;
	int		in;
};

static bool
item_less(struct rb_node *a, const struct rb_node *b)
{

	return rb_entry(a, struct item, rb)->key <
	    rb_entry((struct rb_node *)(uintptr_t)b, struct item, rb)->key;
}

static unsigned long long rng_state = 88172645463325252ULL;

static unsigned long
rnd(unsigned long n)
{

	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 7;
	rng_state ^= rng_state << 17;
	return (unsigned long)(rng_state % n);
}

/* black height of the subtree, or -1 on a violation; counts nodes in *n */
static int
check_sub(const struct rb_node *node, const struct rb_node *parent, unsigned *n,
    int *bad)
{
	int lh, rh;

	if (node == NULL)
		return 1;
	(*n)++;
	if (rb_parent(node) != parent)
		*bad = 1;
	if ((node->__rb_parent_color & 1) == 0) {	/* red */
		if ((node->rb_left && !(node->rb_left->__rb_parent_color & 1)) ||
		    (node->rb_right && !(node->rb_right->__rb_parent_color & 1)))
			*bad = 1;
	}
	lh = check_sub(node->rb_left, node, n, bad);
	rh = check_sub(node->rb_right, node, n, bad);
	if (lh != rh)
		*bad = 1;
	return lh + (node->__rb_parent_color & 1);
}

static int
valid_tree(const struct rb_root *root, unsigned expect)
{
	unsigned n = 0;
	int bad = 0;

	if (root->rb_node != NULL && (root->rb_node->__rb_parent_color & 1) == 0)
		bad = 1;
	if (root->rb_node != NULL && rb_parent(root->rb_node) != NULL)
		bad = 1;
	check_sub(root->rb_node, NULL, &n, &bad);
	return !bad && n == expect;
}

static int
walk_ok(const struct rb_root *root, unsigned expect, struct rb_node *leftmost)
{
	struct rb_node *n, *p;
	unsigned count = 0;
	unsigned long prevkey = 0;

	for (n = rb_first(root); n != NULL; n = rb_next(n)) {
		struct item *it = rb_entry(n, struct item, rb);

		if (count > 0 && it->key < prevkey)
			return 0;
		prevkey = it->key;
		count++;
		p = rb_prev(n);
		if (count > 1 && rb_next(p) != n)
			return 0;
	}
	if (count != expect)
		return 0;
	/* backwards */
	count = 0;
	for (n = rb_last(root); n != NULL; n = rb_prev(n))
		count++;
	if (count != expect)
		return 0;
	if (leftmost != NULL && leftmost != rb_first(root))
		return 0;
	return 1;
}

static void
test_random_plain(void)
{
	enum { N = 600 };
	static struct item items[N];
	struct rb_root root = RB_ROOT;
	unsigned i, count = 0, op, bad_valid = 0, bad_walk = 0;
	int round;

	for (i = 0; i < N; i++) {
		items[i].key = rnd(200);	/* many equal keys */
		items[i].in = 0;
		RB_CLEAR_NODE(&items[i].rb);
	}
	CHECK(RB_EMPTY_ROOT(&root));
	CHECK(rb_first(&root) == NULL && rb_last(&root) == NULL);
	CHECK(RB_EMPTY_NODE(&items[0].rb));

	for (round = 0; round < 20000; round++) {
		i = rnd(N);
		op = rnd(100);
		if (!items[i].in && (op < 60 || count < 20)) {
			rb_add(&items[i].rb, &root, item_less);
			items[i].in = 1;
			count++;
		} else if (items[i].in) {
			rb_erase(&items[i].rb, &root);
			RB_CLEAR_NODE(&items[i].rb);
			items[i].in = 0;
			count--;
		}
		if (!valid_tree(&root, count))
			bad_valid++;
		if (!walk_ok(&root, count, NULL))
			bad_walk++;
	}
	CHECK(bad_valid == 0);
	CHECK(bad_walk == 0);

	/* Drain in post-order, as drm does when it frees a tree. */
	{
		struct item *pos, *tmp;
		unsigned freed = 0;

		rbtree_postorder_for_each_entry_safe(pos, tmp, &root, rb)
			freed++;
		CHECK(freed == count);
	}
	/* Erase everything in random order. */
	while (count > 0) {
		i = rnd(N);
		if (!items[i].in)
			continue;
		rb_erase(&items[i].rb, &root);
		items[i].in = 0;
		count--;
		if (!valid_tree(&root, count))
			bad_valid++;
	}
	CHECK(bad_valid == 0);
	CHECK(RB_EMPTY_ROOT(&root));
}

/* Equal keys keep the order of insertion. */
static void
test_stable_equal(void)
{
	struct item it[50];
	struct rb_root root = RB_ROOT;
	struct rb_node *n;
	unsigned i, seen = 0, bad = 0;

	for (i = 0; i < 50; i++) {
		it[i].key = i % 3;
		it[i].seq = i;
		rb_add(&it[i].rb, &root, item_less);
	}
	for (n = rb_first(&root); n != NULL; n = rb_next(n))
		seen++;
	CHECK(seen == 50);
	{
		struct rb_node *p;
		for (n = rb_first(&root); n != NULL; n = rb_next(n)) {
			p = rb_prev(n);
			if (p == NULL)
				continue;
			if (rb_entry(p, struct item, rb)->key ==
			    rb_entry(n, struct item, rb)->key &&
			    rb_entry(p, struct item, rb)->seq >
			    rb_entry(n, struct item, rb)->seq)
				bad++;
		}
	}
	CHECK(bad == 0);
}

static void
test_cached_and_replace(void)
{
	enum { N = 300 };
	static struct item items[N];
	struct item spare;
	struct rb_root_cached root = RB_ROOT_CACHED;
	unsigned i, count = 0, op, bad = 0;
	int round;

	CHECK(rb_first_cached(&root) == NULL);
	for (i = 0; i < N; i++) {
		items[i].key = rnd(100);
		items[i].in = 0;
	}
	for (round = 0; round < 8000; round++) {
		i = rnd(N);
		op = rnd(100);
		if (!items[i].in && (op < 55 || count < 10)) {
			rb_add_cached(&items[i].rb, &root, item_less);
			items[i].in = 1;
			count++;
		} else if (items[i].in && op < 90) {
			rb_erase_cached(&items[i].rb, &root);
			items[i].in = 0;
			count--;
		} else if (items[i].in) {
			/* replace by a spare node with the same key */
			spare = items[i];
			rb_replace_node_cached(&items[i].rb, &spare.rb,
			    &root);
			items[i].in = 0;
			rb_erase_cached(&spare.rb, &root);
			count--;
		}
		if (!valid_tree(&root.rb_root, count) ||
		    !walk_ok(&root.rb_root, count, rb_first_cached(&root)))
			bad++;
	}
	CHECK(bad == 0);
}

/* Interval tree over [key, last] with the subtree maximum of last. */

#define	ISTART(n)	((n)->key)
#define	ILAST(n)	((n)->last)

INTERVAL_TREE_DEFINE(struct item, rb, unsigned long, subtree_last,
    ISTART, ILAST, static, itest)

static int
sub_max_ok(const struct rb_node *node, unsigned long *max)
{
	const struct item *it;
	unsigned long m, c;
	int ok = 1;

	if (node == NULL) {
		*max = 0;
		return 1;
	}
	it = rb_entry((struct rb_node *)(uintptr_t)node, struct item, rb);
	m = it->last;
	if (node->rb_left != NULL) {
		ok &= sub_max_ok(node->rb_left, &c);
		if (c > m)
			m = c;
	}
	if (node->rb_right != NULL) {
		ok &= sub_max_ok(node->rb_right, &c);
		if (c > m)
			m = c;
	}
	*max = m;
	return ok && it->subtree_last == m;
}

static void
test_interval(void)
{
	enum { N = 300 };
	static struct item items[N];
	struct rb_root_cached root = RB_ROOT_CACHED;
	unsigned i, count = 0, op, bad_aug = 0, bad_valid = 0, bad_q = 0, q;
	int round;

	for (i = 0; i < N; i++)
		items[i].in = 0;
	for (round = 0; round < 6000; round++) {
		i = rnd(N);
		op = rnd(100);
		if (!items[i].in && (op < 55 || count < 10)) {
			items[i].key = rnd(1000);
			items[i].last = items[i].key + rnd(60);
			itest_insert(&items[i], &root);
			items[i].in = 1;
			count++;
		} else if (items[i].in) {
			itest_remove(&items[i], &root);
			items[i].in = 0;
			count--;
		}
		{
			unsigned long m;

			if (!sub_max_ok(root.rb_root.rb_node, &m))
				bad_aug++;
			if (!valid_tree(&root.rb_root, count))
				bad_valid++;
			if (rb_first_cached(&root) != rb_first(&root.rb_root))
				bad_valid++;
		}
		if (round % 25 == 0) {
			/* queries against a brute-force scan, in start order */
			for (q = 0; q < 6; q++) {
				unsigned long s = rnd(1100), l = s + rnd(80);
				struct item *f;
				struct rb_node *n;
				unsigned expect = 0, got = 0;

				for (n = rb_first(&root.rb_root); n != NULL;
				    n = rb_next(n)) {
					struct item *e = rb_entry(n,
					    struct item, rb);
					if (e->key <= l && s <= e->last)
						expect++;
				}
				n = rb_first(&root.rb_root);
				for (f = itest_iter_first(&root, s, l);
				    f != NULL; f = itest_iter_next(f, s, l)) {
					got++;
					if (!(f->key <= l && s <= f->last))
						bad_q++;
					if (got > N)
						break;
				}
				if (got != expect)
					bad_q++;
			}
		}
	}
	CHECK(bad_aug == 0);
	CHECK(bad_valid == 0);
	CHECK(bad_q == 0);
}


/*
 * A second instantiation in the same file, on a node with the tree node
 * and the subtree maximum nested in a member, as drm_gpuvm does; the
 * interval tree of the first instantiation must not be disturbed.
 */
struct gva {
	struct {
		struct rb_node		node;
		unsigned long long	__subtree_last;
	} rb;
	unsigned long long	addr, range;
	int			in;
};

#define	GSTART(n)	((n)->addr)
#define	GLAST(n)	((n)->addr + (n)->range - 1)

INTERVAL_TREE_DEFINE(struct gva, rb.node, unsigned long long,
    rb.__subtree_last, GSTART, GLAST, static inline __attribute__((unused)),
    gvt)

static int
gva_max_ok(const struct rb_node *node, unsigned long long *max)
{
	const struct gva *g;
	unsigned long long m, c;
	int ok = 1;

	if (node == NULL) {
		*max = 0;
		return 1;
	}
	g = rb_entry(node, struct gva, rb.node);
	m = GLAST(g);
	if (node->rb_left != NULL) {
		ok &= gva_max_ok(node->rb_left, &c);
		if (c > m)
			m = c;
	}
	if (node->rb_right != NULL) {
		ok &= gva_max_ok(node->rb_right, &c);
		if (c > m)
			m = c;
	}
	*max = m;
	return ok && g->rb.__subtree_last == m;
}

static void
test_two_instances(void)
{
	enum { N = 200 };
	static struct gva gv[N];
	static struct item it[N];
	struct rb_root_cached groot = RB_ROOT_CACHED, iroot = RB_ROOT_CACHED;
	unsigned i, gcount = 0, icount = 0, bad_aug = 0, bad_valid = 0, bad_q = 0;
	int round;

	for (round = 0; round < 5000; round++) {
		i = rnd(N);
		if (!gv[i].in) {
			gv[i].addr = rnd(500);
			gv[i].range = 1 + rnd(40);	/* many equal starts */
			gvt_insert(&gv[i], &groot);
			gv[i].in = 1;
			gcount++;
		} else if (rnd(100) < 60) {
			gvt_remove(&gv[i], &groot);
			gv[i].in = 0;
			gcount--;
		}
		i = rnd(N);
		if (!it[i].in) {
			it[i].key = rnd(300);
			it[i].last = it[i].key + rnd(30);
			itest_insert(&it[i], &iroot);
			it[i].in = 1;
			icount++;
		} else if (rnd(100) < 60) {
			itest_remove(&it[i], &iroot);
			it[i].in = 0;
			icount--;
		}
		{
			unsigned long long gm;
			unsigned long im;

			if (!gva_max_ok(groot.rb_root.rb_node, &gm) ||
			    !sub_max_ok(iroot.rb_root.rb_node, &im))
				bad_aug++;
			if (!valid_tree(&groot.rb_root, gcount) ||
			    !valid_tree(&iroot.rb_root, icount) ||
			    rb_first_cached(&groot) != rb_first(&groot.rb_root) ||
			    rb_first_cached(&iroot) != rb_first(&iroot.rb_root))
				bad_valid++;
		}
		if (round % 20 == 0) {
			unsigned long long s = rnd(560), l = s + rnd(50);
			struct gva *f;
			unsigned expect = 0, got = 0;
			unsigned long long prev = 0;

			for (i = 0; i < N; i++)
				if (gv[i].in && gv[i].addr <= l &&
				    s <= GLAST(&gv[i]))
					expect++;
			for (f = gvt_iter_first(&groot, s, l); f != NULL;
			    f = gvt_iter_next(f, s, l)) {
				got++;
				if (!(f->addr <= l && s <= GLAST(f)) ||
				    f->addr < prev)
					bad_q++;
				prev = f->addr;
				if (got > N)
					break;
			}
			if (got != expect)
				bad_q++;
		}
	}
	CHECK(bad_aug == 0);
	CHECK(bad_valid == 0);
	CHECK(bad_q == 0);
	/* an empty tree has no match */
	{
		struct rb_root_cached empty = RB_ROOT_CACHED;

		CHECK(gvt_iter_first(&empty, 0, ~0ULL) == NULL);
	}
}

/*
 * Callbacks of a maximum, built by hand as drm_mm does for the holes: the
 * caller links nodes itself and raises the maxima on the way down, and
 * removal goes through rb_erase_augmented.
 */
struct hole {
	struct rb_node		rb;
	unsigned long		addr;
	unsigned long		size;
	unsigned long		max;
	int			in;
};

#define	HOLE_SIZE(h)	((h)->size)

RB_DECLARE_CALLBACKS_MAX(static, hole_cb, struct hole, rb, unsigned long,
    max, HOLE_SIZE)

static int
hole_max_ok(const struct rb_node *node, unsigned long *max)
{
	const struct hole *h;
	unsigned long m, c;
	int ok = 1;

	if (node == NULL) {
		*max = 0;
		return 1;
	}
	h = rb_entry(node, struct hole, rb);
	m = h->size;
	if (node->rb_left != NULL) {
		ok &= hole_max_ok(node->rb_left, &c);
		if (c > m)
			m = c;
	}
	if (node->rb_right != NULL) {
		ok &= hole_max_ok(node->rb_right, &c);
		if (c > m)
			m = c;
	}
	*max = m;
	return ok && h->max == m;
}

static void
hole_insert(struct hole *h, struct rb_root *root)
{
	struct rb_node **link = &root->rb_node, *parent = NULL;

	h->max = h->size;
	while (*link != NULL) {
		struct hole *p;

		parent = *link;
		p = rb_entry(parent, struct hole, rb);
		if (p->max < h->max)
			p->max = h->max;
		link = h->addr < p->addr ? &parent->rb_left : &parent->rb_right;
	}
	rb_link_node(&h->rb, parent, link);
	rb_insert_augmented(&h->rb, root, &hole_cb);
}

static void
test_hand_linked_augmented(void)
{
	enum { N = 300 };
	static struct hole hs[N];
	struct rb_root root = RB_ROOT;
	unsigned i, count = 0, bad_aug = 0, bad_valid = 0;
	int round;

	for (round = 0; round < 8000; round++) {
		i = rnd(N);
		if (!hs[i].in) {
			hs[i].addr = rnd(1000);
			hs[i].size = 1 + rnd(5000);
			hole_insert(&hs[i], &root);
			hs[i].in = 1;
			count++;
		} else if (rnd(100) < 55) {
			rb_erase_augmented(&hs[i].rb, &root, &hole_cb);
			hs[i].in = 0;
			count--;
		}
		{
			unsigned long m;

			if (!hole_max_ok(root.rb_node, &m))
				bad_aug++;
			if (!valid_tree(&root, count))
				bad_valid++;
		}
	}
	CHECK(bad_aug == 0);
	CHECK(bad_valid == 0);
}

int
main(void)
{

	test_random_plain();
	test_stable_equal();
	test_cached_and_replace();
	test_interval();
	test_two_instances();
	test_hand_linked_augmented();
#ifdef DRM2_RBTREE_SELFTEST_FAIL
	CHECK(0 && "negative control");
#endif
	printf("drm2-rbtree: %d checks, %d failed\n", t_run, t_fail);
	return t_fail != 0;
}
