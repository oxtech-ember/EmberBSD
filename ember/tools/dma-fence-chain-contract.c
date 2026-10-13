/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Actual native chain source. Model generic fences, locks, RCU and IRQ work. */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, live, released, notifications, arms, irqs;
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
	CHECK(f->refcount.value != 0);
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
		CHECK(++count < 1024 && w->pending);
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

static const struct dma_fence_ops leaf_ops = { .use_64bit_seqno = true };
static struct dma_fence *new_leaf(bool signaled)
{
	struct leaf *l = calloc(1, sizeof(*l));
	CHECK(l != NULL); live++; spin_lock_init(&l->lock);
	dma_fence_init(&l->base, &leaf_ops, &l->lock,
	    dma_fence_context_alloc(1), 1);
	l->base.signaled = signaled; return &l->base;
}
static struct dma_fence_chain *new_chain(struct dma_fence *prev,
    struct dma_fence *leaf, uint64_t seqno)
{
	struct dma_fence_chain *c = calloc(1, sizeof(*c));
	CHECK(c != NULL); live++;
	dma_fence_chain_init(c, dma_fence_get(prev), dma_fence_get(leaf), seqno);
	return c;
}
static void notified(struct dma_fence *f, struct dma_fence_cb *cb)
{ CHECK(f->signaled && !cb->queued); notifications++; }
static void clean(void)
{ CHECK(live == 0 && irq_head == NULL && rcu_depth == 0 && signal_before_arm == NULL); }

static void single_pending(void)
{
	struct dma_fence *l = new_leaf(false);
	struct dma_fence_chain *c = new_chain(NULL, l, 1);
	struct dma_fence_cb cb = {0};
	unsigned before = notifications;

	CHECK(strcmp(c->base.ops->get_driver_name(&c->base), "dma_fence_chain") == 0);
	CHECK(strcmp(c->base.ops->get_timeline_name(&c->base), "unbound") == 0);
	CHECK(!dma_fence_is_signaled(&c->base));
	CHECK(dma_fence_add_callback(&c->base, &cb, notified) == 0);
	CHECK(l->callbacks == &c->dfc_callback && c->base.refcount.value == 2);
	CHECK(l->refcount.value == 3 && !c->base.signaled);
	dma_fence_put(&c->base); /* Close the only external chain reference. */
	dma_fence_signal(l);
	CHECK(notifications == before && irq_head != NULL && live == 2);
	dma_fence_put(l); /* Both external references are closed before IRQ work. */
	drain_irqs();
	CHECK(notifications == before + 1); clean();
}

static void pending_predecessors(bool signal_old_first, bool raw_prev)
{
	struct dma_fence *a = new_leaf(false), *b = new_leaf(false);
	struct dma_fence_chain *ca = raw_prev ? NULL : new_chain(NULL, a, 10);
	struct dma_fence_chain *cb = new_chain(raw_prev ? a : &ca->base, b, 20);
	struct dma_fence_cb done = {0};
	unsigned before = notifications;

	CHECK(dma_fence_add_callback(&cb->base, &done, notified) == 0);
	CHECK(b->callbacks == &cb->dfc_callback && a->callbacks == NULL);
	if (ca != NULL) dma_fence_put(&ca->base);
	dma_fence_put(&cb->base);
	if (signal_old_first) dma_fence_signal(a);
	dma_fence_signal(b);
	CHECK(notifications == before); drain_irqs();
	if (!signal_old_first) {
		CHECK(notifications == before && a->callbacks != NULL);
		dma_fence_signal(a); CHECK(notifications == before); drain_irqs();
	}
	CHECK(notifications == before + 1);
	dma_fence_put(a); dma_fence_put(b); clean();
}

static void already_signaled_and_race(bool race)
{
	struct dma_fence *a = new_leaf(false), *b = new_leaf(!race);
	struct dma_fence_chain *ca = new_chain(NULL, a, 3);
	struct dma_fence_chain *cb = new_chain(&ca->base, b, 4);
	struct dma_fence_cb done = {0};
	unsigned before = notifications;

	if (race) signal_before_arm = b;
	CHECK(dma_fence_add_callback(&cb->base, &done, notified) == 0);
	CHECK(b->callbacks == NULL && a->callbacks == &cb->dfc_callback);
	CHECK(notifications == before && signal_before_arm == NULL);
	dma_fence_signal(a); drain_irqs();
	CHECK(notifications == before + 1 && cb->base.signaled);
	dma_fence_put(&cb->base); dma_fence_put(&ca->base);
	dma_fence_put(a); dma_fence_put(b); clean();
}

static void all_signaled(void)
{
	struct dma_fence *a = new_leaf(true), *b = new_leaf(true);
	struct dma_fence_chain *ca = new_chain(NULL, a, 1);
	struct dma_fence_chain *cb = new_chain(&ca->base, b, 2);
	struct dma_fence_cb done = {0};
	unsigned before = arms;

	CHECK(dma_fence_add_callback(&cb->base, &done, notified) == -ENOENT);
	CHECK(cb->base.signaled && !done.queued && arms == before && irq_head == NULL);
	CHECK(cb->base.refcount.value == 1);
	dma_fence_put(&cb->base); dma_fence_put(&ca->base);
	dma_fence_put(a); dma_fence_put(b); clean();
}

static void traversal_and_release(void)
{
	struct dma_fence *leaf = new_leaf(false), *f;
	struct dma_fence_chain *a = new_chain(NULL, leaf, 10);
	struct dma_fence_chain *b = new_chain(&a->base, leaf, 20);
	struct dma_fence_chain *c = new_chain(&b->base, leaf, 30);

	CHECK(b->base.context == a->base.context && b->prev_seqno == 10);
	f = dma_fence_get(&c->base);
	CHECK(dma_fence_chain_find_seqno(&f, 15) == 0 && f == &b->base);
	dma_fence_put(f);
	f = dma_fence_get(&c->base);
	CHECK(dma_fence_chain_find_seqno(&f, 31) == -EINVAL && f == &c->base);
	CHECK(dma_fence_chain_find_seqno(&f, 0) == 0 && f == &c->base);
	dma_fence_put(f);
	f = dma_fence_get(leaf);
	CHECK(dma_fence_chain_find_seqno(&f, 1) == -EINVAL && f == leaf);
	dma_fence_put(f);
	dma_fence_put(&b->base); dma_fence_put(&c->base);
	CHECK(a->base.refcount.value == 1); /* Shared predecessor survives. */
	dma_fence_put(&a->base); dma_fence_put(leaf); clean();

	leaf = new_leaf(true); f = NULL;
	for (unsigned n = 1; n <= 64; n++) {
		c = new_chain(f, leaf, n); dma_fence_put(f); f = &c->base;
	}
	CHECK(dma_fence_is_signaled(f));
	dma_fence_put(f); dma_fence_put(leaf); clean();

	leaf = new_leaf(false); a = new_chain(NULL, leaf, 30);
	b = new_chain(&a->base, leaf, 20);
	CHECK(b->base.context != a->base.context && b->prev_seqno == 0 && b->base.seqno == 30);
	dma_fence_put(&a->base); dma_fence_put(&b->base); dma_fence_put(leaf); clean();
}

int main(void)
{
	single_pending();
	pending_predecessors(false, false); pending_predecessors(true, false);
	pending_predecessors(false, true); pending_predecessors(true, true);
	already_signaled_and_race(false); already_signaled_and_race(true);
	all_signaled(); traversal_and_release();
	CHECK(irqs != 0 && released != 0);
	printf("native fence chain: %u checks, %u releases, %u deferred IRQs\n",
	    checks, released, irqs);
	return 0;
}
