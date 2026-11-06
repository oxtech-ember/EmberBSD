/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Actual syncobj flattening and native chain source over modelled fences. */
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, live, released, arms, irqs;
#define CHECK(e) do { checks++; if (!(e)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); abort(); \
} } while (0)
#define KASSERT(e) CHECK(e)
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#ifndef __KERNEL_RCSID
#define __KERNEL_RCSID(n, s)
#endif

typedef struct { bool held; } spinlock_t;
struct kref { unsigned value; };
struct dma_fence;
struct dma_fence_cb {
	struct dma_fence_cb *next;
	void (*fn)(struct dma_fence *, struct dma_fence_cb *);
	bool queued;
};
struct dma_fence_ops {
	bool use_64bit_seqno;
	const char *(*get_driver_name)(struct dma_fence *);
	const char *(*get_timeline_name)(struct dma_fence *);
	bool (*enable_signaling)(struct dma_fence *);
	bool (*signaled)(struct dma_fence *);
	void (*release)(struct dma_fence *);
};
struct dma_fence {
	struct kref refcount;
	const struct dma_fence_ops *ops;
	spinlock_t *lock;
	uint64_t context, seqno;
	struct dma_fence_cb *callbacks;
	int error;
	bool signaled, enabled;
};
struct irq_work {
	void (*fn)(struct irq_work *);
	struct irq_work *next;
	bool pending;
};
struct dma_fence_chain {
	struct dma_fence base;
	uint64_t prev_seqno;
	spinlock_t dfc_lock;
	struct dma_fence *volatile dfc_prev;
	struct dma_fence *dfc_fence;
	struct dma_fence_cb dfc_callback;
	struct irq_work dfc_irq_work;
};
struct leaf { struct dma_fence base; spinlock_t lock; };
static struct irq_work *irq_head, **irq_tail = &irq_head;
static struct dma_fence *signal_before_arm;
static unsigned rcu_depth;
static uint64_t next_context;

static void spin_lock_init(spinlock_t *l) { l->held = false; }
static void spin_lock(spinlock_t *l) { CHECK(!l->held); l->held = true; }
static void spin_unlock(spinlock_t *l) { CHECK(l->held); l->held = false; }
static void spin_lock_destroy(spinlock_t *l) { CHECK(!l->held); }
static unsigned kref_read(const struct kref *r) { return r->value; }
static struct dma_fence *dma_fence_get(struct dma_fence *f)
{ if (f != NULL) { CHECK(f->refcount.value != 0); f->refcount.value++; } return f; }
static void dma_fence_put(struct dma_fence *f)
{
	if (f == NULL) return;
	CHECK(kref_read(&f->refcount) != 0);
	if (--f->refcount.value != 0) return;
	CHECK(f->callbacks == NULL && !f->lock->held);
	if (f->ops->release != NULL) f->ops->release(f);
	else { CHECK(live != 0); live--; released++; free(f); }
}
static void dma_fence_free(struct dma_fence *f)
{ CHECK(live != 0); live--; released++; free(f); }
static uint64_t dma_fence_context_alloc(unsigned n)
{ uint64_t c = next_context; next_context += n; return c; }
static bool __dma_fence_is_later(uint64_t a, uint64_t b,
    const struct dma_fence_ops *ops) { CHECK(ops != NULL); return a > b; }
static void dma_fence_init(struct dma_fence *f, const struct dma_fence_ops *ops,
    spinlock_t *l, uint64_t context, uint64_t seqno)
{
	f->ops = ops; f->lock = l; f->context = context; f->seqno = seqno;
	f->refcount.value = 1;
}
static void dma_fence_signal_locked(struct dma_fence *f)
{
	CHECK(f->lock->held && f->refcount.value != 0);
	if (f->signaled) return;
	f->signaled = true;
	while (f->callbacks != NULL) {
		struct dma_fence_cb *cb = f->callbacks;
		f->callbacks = cb->next; cb->queued = false; cb->next = NULL;
		cb->fn(f, cb);
	}
}
static void dma_fence_signal(struct dma_fence *f)
{ spin_lock(f->lock); dma_fence_signal_locked(f); spin_unlock(f->lock); }
static bool dma_fence_is_signaled(struct dma_fence *f)
{
	CHECK(f != NULL && f->refcount.value != 0);
	if (!f->signaled && f->ops->signaled != NULL && f->ops->signaled(f))
		dma_fence_signal(f);
	return f->signaled;
}
static int dma_fence_add_callback(struct dma_fence *f, struct dma_fence_cb *cb,
    void (*fn)(struct dma_fence *, struct dma_fence_cb *))
{
	CHECK(f != NULL && f->refcount.value != 0 && !cb->queued);
	/* A real child can signal after traversal and before callback insertion. */
	if (f == signal_before_arm) { signal_before_arm = NULL; dma_fence_signal(f); }
	if (f->signaled) return -ENOENT;
	spin_lock(f->lock);
	if (!f->enabled) {
		f->enabled = true;
		if (f->ops->enable_signaling != NULL && !f->ops->enable_signaling(f))
			dma_fence_signal_locked(f);
	}
	if (f->signaled) { spin_unlock(f->lock); return -ENOENT; }
	cb->fn = fn; cb->next = f->callbacks; cb->queued = true;
	f->callbacks = cb; arms++;
	spin_unlock(f->lock);
	return 0;
}
static void init_irq_work(struct irq_work *w, void (*fn)(struct irq_work *))
{ w->fn = fn; w->pending = false; w->next = NULL; }
static bool irq_work_queue(struct irq_work *w)
{
	if (w->pending) return false;
	w->pending = true; *irq_tail = w; irq_tail = &w->next; return true;
}
static void drain_irqs(void)
{
	unsigned count = 0;
	while (irq_head != NULL) {
		struct irq_work *w = irq_head;
		CHECK(++count < 100000 && w->pending);
		irq_head = w->next;
		if (irq_head == NULL) irq_tail = &irq_head;
		w->next = NULL; w->pending = false; irqs++; w->fn(w);
	}
}
static void rcu_read_lock(void) { rcu_depth++; }
static void rcu_read_unlock(void) { CHECK(rcu_depth != 0); rcu_depth--; }
static struct dma_fence *dma_fence_get_rcu_safe(struct dma_fence *volatile *p)
{ CHECK(rcu_depth != 0); return dma_fence_get(*p); }
static void membar_release(void) { }
static struct dma_fence *atomic_cas_ptr(struct dma_fence *volatile *p,
    struct dma_fence *old, struct dma_fence *value)
{ struct dma_fence *found = *p; if (found == old) *p = value; return found; }

struct dma_fence_chain *to_dma_fence_chain(struct dma_fence *);
struct dma_fence *dma_fence_chain_walk(struct dma_fence *);
#define dma_fence_chain_for_each(v, f) \
	for ((v) = dma_fence_get(f); (v) != NULL; (v) = dma_fence_chain_walk(v))
#include "dma-fence-chain-body.h"


#define GFP_KERNEL 0
static size_t largest_vector;
static void *kmalloc_array(size_t n, size_t size, int flags)
{ (void)flags; CHECK(n != 0 && size != 0); if (n > largest_vector) largest_vector = n; return calloc(n, size); }
static void kfree(void *p) { free(p); }

/* An immortal signaled stub, as dma_fence_get_stub provides. */
static spinlock_t stub_lock;
static const struct dma_fence_ops stub_ops = { .use_64bit_seqno = true };
static struct dma_fence stub = { .refcount = { 1 }, .ops = &stub_ops, .lock = &stub_lock, .signaled = true };
static struct dma_fence *dma_fence_get_stub(void) { return dma_fence_get(&stub); }

struct dma_fence_array {
	struct dma_fence base;
	spinlock_t lock;
	struct dma_fence **fences;
	unsigned num_fences;
	bool dfa_signal_on_any;
};
static void array_release(struct dma_fence *f);
static bool array_signaled(struct dma_fence *f);
/* Like the native array: complete once enabled and every member completed. */
static const struct dma_fence_ops array_ops = { .signaled = array_signaled, .release = array_release };
static unsigned arrays_enabled;
static bool dma_fence_is_array(struct dma_fence *f) { return f->ops == &array_ops; }
static struct dma_fence_array *to_dma_fence_array(struct dma_fence *f)
{ CHECK(dma_fence_is_array(f)); return container_of(f, struct dma_fence_array, base); }
static bool array_signaled(struct dma_fence *f)
{
	struct dma_fence_array *a = to_dma_fence_array(f);
	unsigned done = 0;
	if (!f->enabled) return false;
	for (unsigned i = 0; i < a->num_fences; i++)
		if (dma_fence_is_signaled(a->fences[i])) done++;
	return a->dfa_signal_on_any ? done != 0 : done == a->num_fences;
}
static void array_release(struct dma_fence *f)
{
	struct dma_fence_array *a = to_dma_fence_array(f);
	for (unsigned i = 0; i < a->num_fences; i++) dma_fence_put(a->fences[i]);
	free(a->fences); CHECK(live != 0); live--; released++; free(a);
}
static struct dma_fence_array *dma_fence_array_create(int n, struct dma_fence **fences,
    uint64_t context, unsigned seqno, bool any)
{
	struct dma_fence_array *a = calloc(1, sizeof(*a));
	CHECK(a != NULL && n > 0); live++; spin_lock_init(&a->lock);
	dma_fence_init(&a->base, &array_ops, &a->lock, context, seqno);
	a->fences = fences; a->num_fences = (unsigned)n; a->dfa_signal_on_any = any;
	return a;
}
static void dma_fence_enable_sw_signaling(struct dma_fence *f)
{ CHECK(dma_fence_is_array(f) && !f->enabled); f->enabled = true; arrays_enabled++; }
#include "flatten-body.h"

static const struct dma_fence_ops leaf_ops = { .use_64bit_seqno = true };
static struct dma_fence *new_leaf(bool signaled)
{
	struct leaf *l = calloc(1, sizeof(*l));
	CHECK(l != NULL); live++; spin_lock_init(&l->lock);
	dma_fence_init(&l->base, &leaf_ops, &l->lock, dma_fence_context_alloc(1), 1);
	l->base.signaled = signaled; return &l->base;
}
/* Consumes prev and contained, like drm_syncobj_add_point. */
static struct dma_fence *point(struct dma_fence *prev, struct dma_fence *contained, uint64_t seqno)
{
	struct dma_fence_chain *c = calloc(1, sizeof(*c));
	CHECK(c != NULL); live++;
	dma_fence_chain_init(c, prev, contained, seqno);
	return &c->base;
}
static struct dma_fence *array_of(bool any, unsigned n, struct dma_fence **members)
{
	struct dma_fence **v = calloc(n, sizeof(*v));
	CHECK(v != NULL); memcpy(v, members, n * sizeof(*v));
	return &dma_fence_array_create((int)n, v, dma_fence_context_alloc(1), 1, any)->base;
}
/* Nesting depth of a contained fence: a leaf is 0. */
static unsigned depth(struct dma_fence *f)
{
	unsigned d = 0;
	if (to_dma_fence_chain(f) != NULL) {
		struct dma_fence_chain *c = to_dma_fence_chain(f);
		d = 1 + (c->dfc_fence ? depth(c->dfc_fence) : 0);
		if (c->dfc_prev) { unsigned p = depth(c->dfc_prev); if (p > d) d = p; }
	} else if (dma_fence_is_array(f)) {
		struct dma_fence_array *a = to_dma_fence_array(f);
		for (unsigned i = 0; i < a->num_fences; i++) { unsigned m = 1 + depth(a->fences[i]); if (m > d) d = m; }
	}
	return d;
}
static unsigned leaves(struct dma_fence *f)
{ return dma_fence_is_array(f) ? to_dma_fence_array(f)->num_fences : 1; }
static bool holds(struct dma_fence *flat, struct dma_fence *leaf)
{
	if (flat == leaf) return true;
	if (!dma_fence_is_array(flat)) return false;
	for (unsigned i = 0; i < to_dma_fence_array(flat)->num_fences; i++)
		if (to_dma_fence_array(flat)->fences[i] == leaf) return true;
	return false;
}
static void signal_leaf(struct dma_fence *f) { dma_fence_signal(f); drain_irqs(); }

int
main(void)
{
	struct dma_fence *f, *a, *b, *c, *d, *head, *ring[3] = { NULL, NULL, NULL };
	unsigned before;

	spin_lock_init(&stub_lock);
	/* A collected timeline point arrives as NULL and becomes the stub. */
	f = NULL; CHECK(drm_syncobj_flatten(&f) == 0 && f == &stub); dma_fence_put(f);
	/* A plain fence is already flat and keeps the caller's reference. */
	a = new_leaf(false); f = a; CHECK(drm_syncobj_flatten(&f) == 0 && f == a); dma_fence_put(f);
	/* Finished leaves drop, failed and pending leaves stay; arrays are enabled. */
	a = new_leaf(true); b = new_leaf(false); c = new_leaf(true); c->error = -EIO;
	head = point(point(point(NULL, a, 1), b, 2), c, 3);
	before = arrays_enabled; f = head;
	CHECK(drm_syncobj_flatten(&f) == 0 && dma_fence_is_array(f) && leaves(f) == 2);
	CHECK(holds(f, b) && holds(f, c) && !holds(f, a) && arrays_enabled == before + 1);
	CHECK(depth(f) == 1); dma_fence_put(f);
	/* Only finished leaves: the stub. */
	f = point(point(NULL, new_leaf(true), 1), new_leaf(true), 2);
	CHECK(drm_syncobj_flatten(&f) == 0 && f == &stub); dma_fence_put(f);
	/* One pending leaf is returned itself, not wrapped. */
	b = new_leaf(false); f = point(point(NULL, new_leaf(true), 1), dma_fence_get(b), 2);
	CHECK(drm_syncobj_flatten(&f) == 0 && f == b); dma_fence_put(f); dma_fence_put(b);
	/* A leaf reached through two points appears once. */
	b = new_leaf(false);
	f = point(point(NULL, dma_fence_get(b), 1), dma_fence_get(b), 2);
	CHECK(drm_syncobj_flatten(&f) == 0 && f == b); dma_fence_put(f); dma_fence_put(b);
	/* A point containing a chain unwraps completely. */
	a = new_leaf(false); b = new_leaf(false);
	f = point(NULL, point(point(NULL, a, 1), b, 2), 7);
	CHECK(drm_syncobj_flatten(&f) == 0 && leaves(f) == 2 && holds(f, a) && holds(f, b) && depth(f) == 1);
	dma_fence_put(f);
	/* Repeated binary->timeline transfers of the head no longer nest. */
	head = NULL;
	for (unsigned n = 1; n <= 1000; n++) {
		struct dma_fence *job = new_leaf(false);
		if (ring[n % 3] != NULL) { signal_leaf(ring[n % 3]); dma_fence_put(ring[n % 3]); }
		ring[n % 3] = dma_fence_get(job);
		f = head != NULL ? dma_fence_get(head) : NULL;
		/* Job publication, then a TRANSFER of the binary payload into the next point. */
		head = point(head, job, 2 * n - 1);
		/* drm_syncobj_add_point also collects completed previous points. */
		{ struct dma_fence *walk;
		  dma_fence_chain_for_each(walk, head); }
		if (f == NULL) f = dma_fence_get(head); else { dma_fence_put(f); f = dma_fence_get(head); }
		CHECK(drm_syncobj_flatten(&f) == 0);
		CHECK(depth(f) <= 1 && leaves(f) <= 3);
		head = point(head, f, 2 * n);
		CHECK(depth(to_dma_fence_chain(head)->dfc_fence) <= 1);
	}
	for (unsigned i = 0; i < 3; i++) if (ring[i]) { signal_leaf(ring[i]); dma_fence_put(ring[i]); }
	dma_fence_put(head); drain_irqs();
	/* An all-of array holding a chain unwraps; an any-of array of leaves stays. */
	a = new_leaf(false); b = new_leaf(false); c = new_leaf(false); d = new_leaf(false);
	{ struct dma_fence *m[2] = { point(NULL, a, 1), b }; f = array_of(false, 2, m); }
	CHECK(drm_syncobj_flatten(&f) == 0 && leaves(f) == 2 && holds(f, a) && holds(f, b)); dma_fence_put(f);
	{ struct dma_fence *m[2] = { c, d }; f = point(NULL, array_of(true, 2, m), 1); }
	CHECK(drm_syncobj_flatten(&f) == 0 && dma_fence_is_array(f) && to_dma_fence_array(f)->dfa_signal_on_any);
	CHECK(depth(f) == 1); dma_fence_put(f);
	/* Preserve OR semantics even when one child is a timeline chain. */
	a = new_leaf(false);
	{ struct dma_fence *m[2] = { point(NULL, a, 1), new_leaf(false) }; head = array_of(true, 2, m); }
	f = point(NULL, head, 1);
	CHECK(drm_syncobj_flatten(&f) == 0 && f == head);
	dma_fence_enable_sw_signaling(f);
	CHECK(!dma_fence_is_signaled(f));
	signal_leaf(to_dma_fence_array(f)->fences[1]);
	CHECK(dma_fence_is_signaled(f)); dma_fence_put(f);
	/* The work list is bounded; the caller keeps its reference on refusal. */
	head = NULL;
	for (unsigned n = 1; n <= DRM_SYNCOBJ_FLATTEN_VISITS; n++) head = point(head, new_leaf(false), n);
	f = head; CHECK(drm_syncobj_flatten(&f) == -E2BIG && f == head); dma_fence_put(f);
	CHECK(largest_vector <= DRM_SYNCOBJ_FLATTEN_VISITS);
	/* Reject an oversized array before allocating storage for its children. */
	{ unsigned n = DRM_SYNCOBJ_FLATTEN_VISITS * 2;
	  struct dma_fence **v = calloc(n, sizeof(*v)); CHECK(v != NULL);
	  for (unsigned i = 0; i < n; i++) v[i] = new_leaf(false);
	  f = array_of(false, n, v); free(v); head = f; largest_vector = 0;
	  CHECK(drm_syncobj_flatten(&f) == -E2BIG && f == head);
	  CHECK(largest_vector == 8); dma_fence_put(f); }
	drain_irqs();
	CHECK(live == 0 && stub.refcount.value == 1 && rcu_depth == 0);
	printf("PASS: syncobj flatten %u checks, %u releases, %u arms, %u irqs\n",
	    checks, released, arms, irqs);
	return 0;
}
