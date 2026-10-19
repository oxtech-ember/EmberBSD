/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted. Exercise the actual native fence array. */
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			abort();                                               \
		}                                                              \
		__atomic_add_fetch(&checks, 1, __ATOMIC_RELAXED);              \
	} while (0)
#define KASSERT(x) CHECK(x)
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define struct_size(p, m, n) (sizeof(*(p)) + sizeof((p)->m[0]) * (n))
#define membar_consumer() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define GFP_KERNEL 0
#define DMA_FENCE_FLAG_SIGNALED_BIT 1
#define test_bit(n, p) ((__atomic_load_n((p), __ATOMIC_ACQUIRE) >> (n)) & 1)
typedef int atomic_t;
#define atomic_set(p, v) __atomic_store_n((p), (v), __ATOMIC_RELAXED)
#define atomic_read(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define atomic_dec_return(p) __atomic_sub_fetch((p), 1, __ATOMIC_SEQ_CST)
static int
atomic_cmpxchg(atomic_t *p, int old, int value)
{
	__atomic_compare_exchange_n(p, &old, value, false, __ATOMIC_SEQ_CST,
				    __ATOMIC_SEQ_CST);
	return old;
}

typedef pthread_mutex_t spinlock_t;
static _Thread_local spinlock_t *locks[32];
static _Thread_local unsigned nlocks;
static void
spin_lock_init(spinlock_t *p)
{
	CHECK(pthread_mutex_init(p, NULL) == 0);
}
static bool
spin_is_locked(spinlock_t *p)
{
	for (unsigned i = 0; i < nlocks; i++)
		if (locks[i] == p)
			return true;
	return false;
}
static void
spin_lock(spinlock_t *p)
{
	CHECK(!spin_is_locked(p));
	CHECK(nlocks < 32);
	CHECK(pthread_mutex_lock(p) == 0);
	locks[nlocks++] = p;
}
static void
spin_unlock(spinlock_t *p)
{
	CHECK(nlocks && locks[nlocks - 1] == p);
	nlocks--;
	CHECK(pthread_mutex_unlock(p) == 0);
}
static void
spin_lock_destroy(spinlock_t *p)
{
	CHECK(pthread_mutex_destroy(p) == 0);
}
struct dma_fence;
struct dma_fence_cb {
	void (*fn)(struct dma_fence *, struct dma_fence_cb *);
	struct dma_fence_cb *next;
};
struct dma_fence_ops {
	const char *(*get_driver_name)(struct dma_fence *);
	const char *(*get_timeline_name)(struct dma_fence *);
	bool (*enable_signaling)(struct dma_fence *);
	bool (*signaled)(struct dma_fence *);
	void (*release)(struct dma_fence *);
};
struct dma_fence {
	const struct dma_fence_ops *ops;
	spinlock_t *lock;
	uint64_t context, seqno;
	unsigned long flags;
	int error, refs;
	bool enabled;
	struct dma_fence_cb *callbacks;
};
struct irq_work {
	void (*fn)(struct irq_work *);
	struct irq_work *next;
	bool queued;
};
#include "layout.h"

static pthread_mutex_t work_lock = PTHREAD_MUTEX_INITIALIZER;
static struct irq_work *work_head;
static unsigned allocations, releases;
static bool allocation_failure;
static void *
kzalloc(size_t n, int flag)
{
	(void)flag;
	if (allocation_failure)
		return NULL;
	void *p = calloc(1, n);
	CHECK(p);
	allocations++;
	return p;
}
#define kfree(p) free(p)
static struct dma_fence *
dma_fence_get(struct dma_fence *f)
{
	if (f)
		CHECK(__atomic_add_fetch(&f->refs, 1, __ATOMIC_SEQ_CST) > 1);
	return f;
}
static void
dma_fence_put(struct dma_fence *f)
{
	if (f && __atomic_sub_fetch(&f->refs, 1, __ATOMIC_SEQ_CST) == 0) {
		CHECK(!f->callbacks);
		f->ops->release(f);
	}
}
static void
dma_fence_free(struct dma_fence *f)
{
	releases++;
	free(f);
}
static void
dma_fence_init(struct dma_fence *f, const struct dma_fence_ops *ops,
	       spinlock_t *lock, uint64_t context, uint64_t seqno)
{
	memset(f, 0, sizeof(*f));
	f->ops = ops;
	f->lock = lock;
	f->context = context;
	f->seqno = seqno;
	f->refs = 1;
}
static int
dma_fence_signal_locked(struct dma_fence *f)
{
	CHECK(spin_is_locked(f->lock));
	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &f->flags))
		return -EINVAL;
	__atomic_fetch_or(&f->flags, 1UL << DMA_FENCE_FLAG_SIGNALED_BIT,
			  __ATOMIC_RELEASE);
	while (f->callbacks) {
		struct dma_fence_cb *cb = f->callbacks;
		f->callbacks = cb->next;
		cb->fn(f, cb);
	}
	return 0;
}
static bool
dma_fence_is_signaled_locked(struct dma_fence *f)
{
	CHECK(spin_is_locked(f->lock));
	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &f->flags))
		return true;
	if (f->ops->signaled && f->ops->signaled(f)) {
		dma_fence_signal_locked(f);
		return true;
	}
	return false;
}
static int
dma_fence_add_callback(struct dma_fence *f, struct dma_fence_cb *cb,
		       void (*fn)(struct dma_fence *, struct dma_fence_cb *))
{
	spin_lock(f->lock);
	if (!f->enabled) {
		f->enabled = true;
		if (!test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &f->flags) &&
		    f->ops->enable_signaling && !f->ops->enable_signaling(f))
			dma_fence_signal_locked(f);
	}
	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &f->flags)) {
		spin_unlock(f->lock);
		return -ENOENT;
	}
	cb->fn = fn;
	cb->next = f->callbacks;
	f->callbacks = cb;
	spin_unlock(f->lock);
	return 0;
}
static void
init_irq_work(struct irq_work *w, void (*fn)(struct irq_work *))
{
	w->fn = fn;
}
static void
irq_work_queue(struct irq_work *w)
{
	CHECK(pthread_mutex_lock(&work_lock) == 0);
	CHECK(!w->queued);
	w->queued = true;
	w->next = work_head;
	work_head = w;
	CHECK(pthread_mutex_unlock(&work_lock) == 0);
}
static void
drain(void)
{
	for (;;) {
		CHECK(pthread_mutex_lock(&work_lock) == 0);
		struct irq_work *w = work_head;
		if (w) {
			work_head = w->next;
			w->queued = false;
		}
		CHECK(pthread_mutex_unlock(&work_lock) == 0);
		if (!w)
			return;
		w->fn(w);
	}
}
struct dma_fence_array *to_dma_fence_array(struct dma_fence *);
bool dma_fence_is_array(struct dma_fence *);
#include "bodies.h"

struct child {
	struct dma_fence base;
	spinlock_t lock;
	int result;
};
static void
child_release(struct dma_fence *f)
{
	struct child *c = container_of(f, struct child, base);
	spin_lock_destroy(&c->lock);
	free(c);
}
static const struct dma_fence_ops child_ops = {.release = child_release};
static struct child *
child_new(int result)
{
	struct child *c = calloc(1, sizeof(*c));
	CHECK(c);
	spin_lock_init(&c->lock);
	dma_fence_init(&c->base, &child_ops, &c->lock, 99, 1);
	c->result = result;
	return c;
}
static void
child_signal(struct child *c)
{
	spin_lock(&c->lock);
	c->base.error = c->result;
	CHECK(dma_fence_signal_locked(&c->base) == 0);
	spin_unlock(&c->lock);
}
static void *
signal_thread(void *p)
{
	child_signal(p);
	return NULL;
}
static void
enable(struct dma_fence *f)
{
	spin_lock(f->lock);
	f->enabled = true;
	if (!f->ops->enable_signaling(f))
		dma_fence_signal_locked(f);
	spin_unlock(f->lock);
}
static int
status(struct dma_fence *f)
{
	spin_lock(f->lock);
	int v = dma_fence_is_signaled_locked(f) ? (f->error ? f->error : 1) : 0;
	spin_unlock(f->lock);
	return v;
}
static void
run(unsigned n, bool any, unsigned complete, int error, bool threaded)
{
	struct child *c[8];
	struct dma_fence **f = calloc(n, sizeof(*f));
	CHECK(f);
	for (unsigned i = 0; i < n; i++) {
		c[i] = child_new(i == 0 ? error : 0);
		f[i] = dma_fence_get(&c[i]->base);
		if (i < complete)
			child_signal(c[i]);
	}
	uint64_t context = UINT64_C(0x12345678abcdef01);
	struct dma_fence_array *a =
	    dma_fence_array_create((int)n, f, context, 7, any);
	CHECK(a);
	/* The constructor must retain the allocator's complete context. */
#ifdef ARRAY_MODE_FIELD
	CHECK(a->dfa_signal_on_any == any);
	CHECK(a->base.context == context);
#endif
	CHECK(a->base.refs == 1);
	enable(&a->base);
	if (!complete)
		CHECK(status(&a->base) == 0);
	if (!threaded) {
		for (unsigned i = complete; i < n; i++) {
			child_signal(c[i]);
			if (!any && i + 1 < n)
				CHECK(status(&a->base) == 0);
			if (any || i + 1 == n)
				CHECK(status(&a->base) == (error ? error : 1));
		}
	} else {
		pthread_t threads[8];
		for (unsigned i = complete; i < n; i++)
			CHECK(pthread_create(&threads[i], NULL, signal_thread,
					     c[i]) == 0);
		for (unsigned i = complete; i < n; i++)
			CHECK(pthread_join(threads[i], NULL) == 0);
	}
	drain();
	CHECK(status(&a->base) == (error ? error : 1));
	CHECK(a->base.refs == 1);
	dma_fence_put(&a->base);
	for (unsigned i = 0; i < n; i++) {
		CHECK(c[i]->base.refs == 1);
		dma_fence_put(&c[i]->base);
	}
	CHECK(allocations == releases);
}
int
main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "async") == 0)
		run(2, false, 0, -EIO, false);
	else {
		/* Baseline loses the error on already completed children. */
		run(2, false, 2, -EIO, false);
		for (unsigned n = 1; n <= 8; n++)
			for (unsigned complete = 0; complete <= n; complete++)
				for (unsigned any = 0; any < 2; any++) {
					run(n, any, complete, 0, false);
					run(n, any, complete, -EIO, false);
				}
		for (unsigned i = 0; i < 64; i++)
			run(8, false, 0, -EIO, true);
		/* OR result must not acquire a later child's error after
		 * signaling. */
		struct child *first = child_new(0), *late = child_new(-EIO);
		struct dma_fence **f = calloc(2, sizeof(*f));
		CHECK(f);
		f[0] = dma_fence_get(&first->base);
		f[1] = dma_fence_get(&late->base);
		struct dma_fence_array *a =
		    dma_fence_array_create(2, f, 10, 1, true);
		CHECK(a);
		enable(&a->base);
		child_signal(first);
		CHECK(status(&a->base) == 1);
		child_signal(late);
		drain();
		CHECK(status(&a->base) == 1);
		dma_fence_put(&a->base);
		dma_fence_put(&first->base);
		dma_fence_put(&late->base);
		allocation_failure = true;
		CHECK(dma_fence_array_create(1, NULL, 1, 1, false) == NULL);
		CHECK(allocations == releases);
	}
	printf("native fence array: %u checks PASS\n", checks);
	return 0;
}
