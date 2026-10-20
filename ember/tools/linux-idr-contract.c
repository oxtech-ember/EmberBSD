/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Actual native Linux idr source. Model rbtree, locks, kmem and LWP data.
 * idr_init_base() must bound every allocation from below, as on Linux:
 * DRM GEM, syncobj and i915 VM handle tables reserve id 0 this way.
 */
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(e) do { checks++; if (!(e)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); exit(1); \
} } while (0)
#define KASSERT(e) CHECK(e)
#define ASSERT_SLEEPABLE() do { } while (0)
#define ISSET(t, f) ((t) & (f))
#define __predict_false(e) (e)
#define __diagused
#define __unused __attribute__((__unused__))
#define __read_mostly
#define __KERNEL_RCSID(n, s)
#define SDT_PROBE_DEFINE0(...)
#define SDT_PROBE_DEFINE1(...)
#define SDT_PROBE_DEFINE3(...)
#define SDT_PROBE_DEFINE4(...)
#define SDT_PROBE0(...) do { } while (0)
#define SDT_PROBE1(...) do { } while (0)
#define SDT_PROBE3(...) do { } while (0)
#define SDT_PROBE4(...) do { } while (0)
#define ERR_PTR(e) ((void *)(intptr_t)(e))

typedef unsigned gfp_t;
#define __GFP_WAIT 1u
#define GFP_KERNEL __GFP_WAIT
#define GFP_ATOMIC 0u
typedef int km_flag_t;
#define KM_SLEEP 1
#define KM_NOSLEEP 2

/* Locks: record that every tree access happens under the idr lock. */
typedef struct { bool held; } kmutex_t;
#define MUTEX_DEFAULT 0
#define IPL_VM 0
static void mutex_init(kmutex_t *m, int t, int i) { (void)t; (void)i; m->held = false; }
static void mutex_destroy(kmutex_t *m) { CHECK(!m->held); }
static void mutex_spin_enter(kmutex_t *m) { CHECK(!m->held); m->held = true; }
static void mutex_spin_exit(kmutex_t *m) { CHECK(m->held); m->held = false; }

/* kmem: count live allocations so failed allocations cannot leak nodes. */
static long live_allocs;
static void *kmem_alloc(size_t n, km_flag_t f) { (void)f; live_allocs++; return malloc(n); }
static void *kmem_zalloc(size_t n, km_flag_t f) { (void)f; live_allocs++; return calloc(1, n); }
static void kmem_free(void *p, size_t n) { (void)n; live_allocs--; free(p); }

/* One LWP: lwp-specific storage is a single slot. */
typedef int specificdata_key_t;
struct lwp { const char *l_name; };
static struct lwp lwp0 = { "contract" };
#define curlwp (&lwp0)
static void *lwp_slot;
static void (*lwp_dtor)(void *);
static int lwp_specific_key_create(specificdata_key_t *k, void (*d)(void *))
{ *k = 1; lwp_dtor = d; return 0; }
static void lwp_specific_key_delete(specificdata_key_t k)
{ (void)k; if (lwp_slot != NULL) lwp_dtor(lwp_slot); lwp_slot = NULL; }
static void *lwp_getspecific(specificdata_key_t k) { (void)k; return lwp_slot; }
static void lwp_setspecific(specificdata_key_t k, void *v) { (void)k; lwp_slot = v; }

/* Sorted-array model of the NetBSD rbtree(3) interface used by idr. */
typedef struct { int unused; } rb_node_t;
typedef struct {
	signed int (*rbto_compare_nodes)(void *, const void *, const void *);
	signed int (*rbto_compare_key)(void *, const void *, const void *);
	size_t rbto_node_offset;
	void *rbto_context;
} rb_tree_ops_t;
typedef struct {
	const rb_tree_ops_t *ops;
	void *nodes[64];
	size_t count;
} rb_tree_t;
#define RB_DIR_LEFT 0
#define RB_DIR_RIGHT 1
#define RB_TREE_MIN(t) ((t)->count ? (t)->nodes[0] : NULL)
#define RB_TREE_FOREACH(n, t) \
	for (size_t rb_i_ = 0; \
	    rb_i_ < (t)->count && ((n) = (t)->nodes[rb_i_], 1); rb_i_++)
static void rb_tree_init(rb_tree_t *t, const rb_tree_ops_t *o)
{ t->ops = o; t->count = 0; }
static size_t rb_index(rb_tree_t *t, const void *node)
{
	size_t i;

	for (i = 0; i < t->count; i++)
		if (t->nodes[i] == node)
			return i;
	CHECK(!"node not in tree");
	return 0;
}
static void *rb_tree_find_node(rb_tree_t *t, const void *key)
{
	size_t i;

	for (i = 0; i < t->count; i++)
		if (t->ops->rbto_compare_key(NULL, t->nodes[i], key) == 0)
			return t->nodes[i];
	return NULL;
}
static void *rb_tree_find_node_geq(rb_tree_t *t, const void *key)
{
	size_t i;

	for (i = 0; i < t->count; i++)
		if (t->ops->rbto_compare_key(NULL, t->nodes[i], key) >= 0)
			return t->nodes[i];
	return NULL;
}
static void *rb_tree_iterate(rb_tree_t *t, void *node, int dir)
{
	size_t i = rb_index(t, node);

	CHECK(dir == RB_DIR_RIGHT);
	return i + 1 < t->count ? t->nodes[i + 1] : NULL;
}
static void *rb_tree_insert_node(rb_tree_t *t, void *node)
{
	size_t i;

	CHECK(t->count < sizeof(t->nodes) / sizeof(t->nodes[0]));
	for (i = 0; i < t->count; i++) {
		int c = t->ops->rbto_compare_nodes(NULL, t->nodes[i], node);
		if (c == 0)
			return t->nodes[i];
		if (c > 0)
			break;
	}
	memmove(&t->nodes[i + 1], &t->nodes[i],
	    (t->count - i) * sizeof(t->nodes[0]));
	t->nodes[i] = node;
	t->count++;
	return node;
}
static void rb_tree_remove_node(rb_tree_t *t, void *node)
{
	size_t i = rb_index(t, node);

	memmove(&t->nodes[i], &t->nodes[i + 1],
	    (t->count - i - 1) * sizeof(t->nodes[0]));
	t->count--;
}

struct idr {
	kmutex_t idr_lock;
	rb_tree_t idr_tree;
	int idr_base;
};
#define idr_alloc linux_idr_alloc
#define idr_destroy linux_idr_destroy
#define idr_find linux_idr_find
#define idr_for_each linux_idr_for_each
#define idr_get_next linux_idr_get_next
#define idr_init linux_idr_init
#define idr_init_base linux_idr_init_base
#define idr_is_empty linux_idr_is_empty
#define idr_preload linux_idr_preload
#define idr_preload_end linux_idr_preload_end
#define idr_remove linux_idr_remove
#define idr_replace linux_idr_replace
int linux_idr_module_init(void);
void linux_idr_module_fini(void);
void idr_init(struct idr *);
void idr_init_base(struct idr *, int);
void idr_destroy(struct idr *);
bool idr_is_empty(struct idr *);
void *idr_find(struct idr *, int);
void *idr_get_next(struct idr *, int *);
void *idr_replace(struct idr *, void *, int);
void *idr_remove(struct idr *, int);
void idr_preload(gfp_t);
int idr_alloc(struct idr *, void *, int, int, gfp_t);
void idr_preload_end(void);
int idr_for_each(struct idr *, int (*)(int, void *, void *), void *);

#include "linux-idr-body.h"

static int
alloc1(struct idr *idr, void *data, int start, int end)
{
	int id;

	idr_preload(GFP_KERNEL);
	id = idr_alloc(idr, data, start, end, GFP_KERNEL);
	idr_preload_end();
	return id;
}

static void
drain(struct idr *idr)
{
	int id = 0;

	while (idr_get_next(idr, &id) != NULL) {
		CHECK(idr_remove(idr, id) != NULL);
		id = 0;
	}
	CHECK(idr_is_empty(idr));
}

int
main(void)
{
	struct idr base1, base0, base5;
	int a, b, c, d;

	CHECK(linux_idr_module_init() == 0);

	/* i915 VM handles: idr_init_base(..., 1) with start 0, no end. */
	idr_init_base(&base1, 1);
	CHECK(alloc1(&base1, &a, 0, 0) == 1);
	CHECK(alloc1(&base1, &b, 0, 0) == 2);
	CHECK(idr_find(&base1, 0) == NULL);
	CHECK(idr_find(&base1, 1) == &a);
	/* An explicit start at or above the base keeps its meaning. */
	CHECK(alloc1(&base1, &c, 1, 0) == 3);
	CHECK(alloc1(&base1, &d, 10, 0) == 10);
	/* Freed low ids are reused, but never below the base. */
	CHECK(idr_remove(&base1, 1) == &a);
	CHECK(alloc1(&base1, &a, 0, 0) == 1);
	/* An end at the base leaves no valid id. */
	CHECK(alloc1(&base1, &a, 0, 1) == -ENOSPC);
	CHECK(alloc1(&base1, &a, -1, 0) == -EINVAL);
	drain(&base1);
	idr_destroy(&base1);

	/* idr_init keeps base 0. */
	idr_init(&base0);
	CHECK(alloc1(&base0, &a, 0, 0) == 0);
	CHECK(alloc1(&base0, &b, 0, 0) == 1);
	drain(&base0);
	idr_destroy(&base0);

	/* A larger base, with a bounded end above it. */
	idr_init_base(&base5, 5);
	CHECK(alloc1(&base5, &a, 0, 7) == 5);
	CHECK(alloc1(&base5, &b, 2, 7) == 6);
	CHECK(alloc1(&base5, &c, 0, 7) == -ENOSPC);
	drain(&base5);
	idr_destroy(&base5);

	linux_idr_module_fini();
	CHECK(live_allocs == 0);
	printf("linux idr base contract: %u checks passed\n", checks);
	return 0;
}
