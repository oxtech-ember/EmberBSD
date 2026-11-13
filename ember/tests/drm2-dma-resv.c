/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native kernel tests for the dma_fence and dma_resv
 * layer at the Linux 6.18 API level (sys/external/bsd/drm2/linux).  Run
 * by modload of sys/modules/drmkms_linux_test next to drm2-linux-compat.c;
 * a failed check makes the module refuse to load (EIO) and logs "FAIL".
 * Kernel only: the code under test needs the kernel's RCU, ww_mutex and
 * condvars.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/systm.h>

#include <linux/dma-fence.h>
#include <linux/dma-fence-array.h>
#include <linux/dma-fence-chain.h>
#include <linux/dma-resv.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

/* Test fences: kzalloc'ed, counted so leaks and double puts show up. */

static spinlock_t tf_lock;
static int tf_live, tf_deadlines;
static ktime_t tf_last_deadline;

static const char *
tf_name(struct dma_fence *f __unused)
{

	return "drm2test";
}

static void
tf_release(struct dma_fence *f)
{

	tf_live--;
	dma_fence_free(f);
}

static void
tf_set_deadline(struct dma_fence *f __unused, ktime_t deadline)
{

	tf_deadlines++;
	tf_last_deadline = deadline;
}

static const struct dma_fence_ops tf_ops = {
	.get_driver_name = tf_name,
	.get_timeline_name = tf_name,
	.release = tf_release,
	.set_deadline = tf_set_deadline,
};

static const struct dma_fence_ops tf_ops64 = {
	.use_64bit_seqno = true,
	.get_driver_name = tf_name,
	.get_timeline_name = tf_name,
	.release = tf_release,
};

static struct dma_fence *
tf_new(uint64_t context, uint64_t seqno)
{
	struct dma_fence *f = kzalloc(sizeof(*f), GFP_KERNEL);

	if (f == NULL)
		return NULL;
	dma_fence_init(f, &tf_ops, &tf_lock, context, seqno);
	tf_live++;
	return f;
}

static struct dma_fence *
tf_new64(uint64_t context, uint64_t seqno)
{
	struct dma_fence *f = kzalloc(sizeof(*f), GFP_KERNEL);

	if (f == NULL)
		return NULL;
	dma_fence_init(f, &tf_ops64, &tf_lock, context, seqno);
	tf_live++;
	return f;
}

/* ---- dma_fence level: 6.18 additions ---- */

static void
test_fence_order(void)
{
	struct dma_fence *a, *b, *c, *w1, *w2;
	uint64_t ctx = dma_fence_context_alloc(1);

	/* 32-bit sequence numbers: wrap-around aware, equal is not later. */
	a = tf_new(ctx, 5);
	b = tf_new(ctx, 5);
	c = tf_new(ctx, 6);
	CHECK(!dma_fence_is_later(a, b));
	CHECK(dma_fence_is_later(c, a));
	CHECK(!dma_fence_is_later(a, c));
	CHECK(dma_fence_is_later_or_same(a, a));
	/* Same position but not the same fence: not "later or same". */
	CHECK(!dma_fence_is_later_or_same(a, b));
	CHECK(dma_fence_is_later_or_same(c, a));
	CHECK(!dma_fence_is_later_or_same(a, c));
	CHECK(dma_fence_later(c, a) == c);
	CHECK(dma_fence_later(a, c) == c);
	/* A signalled winner has nothing to wait for: NULL. */
	dma_fence_signal(c);
	CHECK(dma_fence_later(c, a) == NULL);
	CHECK(dma_fence_later(a, c) == NULL);
	dma_fence_put(a);
	dma_fence_put(b);
	dma_fence_put(c);

	w1 = tf_new(ctx, 0xfffffffeULL);
	w2 = tf_new(ctx, 0x100000001ULL);	/* low 32 bits: 1 */
	CHECK(dma_fence_is_later(w2, w1));
	CHECK(!dma_fence_is_later(w1, w2));
	dma_fence_put(w1);
	dma_fence_put(w2);

	/* 64-bit sequence numbers compare as plain integers. */
	w1 = tf_new64(ctx, 0xfffffffeULL);
	w2 = tf_new64(ctx, 0x100000001ULL);
	CHECK(dma_fence_is_later(w2, w1));
	CHECK(!dma_fence_is_later(w1, w2));
	CHECK(!dma_fence_is_later(w2, w2));
	dma_fence_put(w1);
	dma_fence_put(w2);

	/* dma_fence_init64 selects 64-bit comparison for any ops. */
	w1 = kzalloc(sizeof(*w1), GFP_KERNEL);
	w2 = kzalloc(sizeof(*w2), GFP_KERNEL);
	dma_fence_init64(w1, &tf_ops, &tf_lock, ctx, 0x100000002ULL);
	dma_fence_init64(w2, &tf_ops, &tf_lock, ctx, 0x1ULL);
	tf_live += 2;
	CHECK(dma_fence_is_later(w1, w2));
	CHECK(!dma_fence_is_later(w2, w1));
	dma_fence_put(w1);
	dma_fence_put(w2);
	CHECK(tf_live == 0);
}

static void
test_fence_status_timestamp(void)
{
	struct dma_fence *f;
	ktime_t t0, t1, ts;
	bool cookie;
	int s;

	/* Signalling annotations are accepted and have no effect here. */
	cookie = dma_fence_begin_signalling();
	dma_fence_end_signalling(cookie);
	__dma_fence_might_wait();

	f = tf_new(dma_fence_context_alloc(1), 1);
	spin_lock(f->lock);
	s = dma_fence_get_status_locked(f);
	spin_unlock(f->lock);
	CHECK(s == 0);
	t0 = ktime_get();
	dma_fence_signal(f);
	t1 = ktime_get();
	spin_lock(f->lock);
	s = dma_fence_get_status_locked(f);
	spin_unlock(f->lock);
	CHECK(s == 1);
	ts = dma_fence_timestamp(f);
	CHECK(ktime_compare(ts, t0) >= 0);
	CHECK(ktime_compare(ts, t1) <= 0);
	dma_fence_put(f);

	/* An error recorded before signalling is the status. */
	f = tf_new(dma_fence_context_alloc(1), 1);
	dma_fence_set_error(f, -EIO);
	dma_fence_signal(f);
	spin_lock(f->lock);
	s = dma_fence_get_status_locked(f);
	spin_unlock(f->lock);
	CHECK(s == -EIO);
	CHECK(dma_fence_get_status(f) == -EIO);
	dma_fence_put(f);

	/* Signal with a caller supplied timestamp. */
	f = tf_new(dma_fence_context_alloc(1), 1);
	CHECK(dma_fence_signal_timestamp(f, 12345) == 0);
	CHECK(dma_fence_timestamp(f) == 12345);
	CHECK(dma_fence_signal_timestamp(f, 999) == -EINVAL);
	CHECK(dma_fence_timestamp(f) == 12345);
	dma_fence_put(f);

	f = tf_new(dma_fence_context_alloc(1), 1);
	spin_lock(f->lock);
	CHECK(dma_fence_signal_timestamp_locked(f, 777) == 0);
	spin_unlock(f->lock);
	CHECK(dma_fence_timestamp(f) == 777);
	dma_fence_put(f);

	/* Private stub: already signalled with the given timestamp. */
	f = dma_fence_allocate_private_stub(4242);
	CHECK(f != NULL);
	if (f != NULL) {
		CHECK(dma_fence_is_signaled(f));
		CHECK(dma_fence_timestamp(f) == 4242);
		CHECK(f != dma_fence_get_stub());
		dma_fence_put(f);
	}
	dma_fence_put(dma_fence_get_stub());
	CHECK(tf_live == 0);
}

static void
test_fence_deadline(void)
{
	struct dma_fence *f, *g;

	tf_deadlines = 0;
	f = tf_new(dma_fence_context_alloc(1), 1);
	dma_fence_set_deadline(f, 5000);
	CHECK(tf_deadlines == 1);
	CHECK(tf_last_deadline == 5000);
	dma_fence_set_deadline(f, 4000);
	CHECK(tf_deadlines == 2);
	CHECK(tf_last_deadline == 4000);
	/* Not for fences that are done. */
	dma_fence_signal(f);
	dma_fence_set_deadline(f, 3000);
	CHECK(tf_deadlines == 2);
	dma_fence_put(f);

	/* Ops without the hook are fine. */
	g = tf_new64(dma_fence_context_alloc(1), 1);
	dma_fence_set_deadline(g, 1);
	CHECK(tf_deadlines == 2);
	dma_fence_put(g);
	CHECK(tf_live == 0);
}

static void
test_fence_container(void)
{
	struct dma_fence *plain, *f1, *f2, **fa;
	struct dma_fence_array *arr;
	struct dma_fence_chain *chain;

	plain = tf_new(dma_fence_context_alloc(1), 1);
	CHECK(!dma_fence_is_array(plain));
	CHECK(!dma_fence_is_chain(plain));
	CHECK(!dma_fence_is_container(plain));

	f1 = tf_new(dma_fence_context_alloc(1), 1);
	f2 = tf_new(dma_fence_context_alloc(1), 1);
	fa = kmalloc_array(2, sizeof(*fa), GFP_KERNEL);
	fa[0] = f1;
	fa[1] = f2;
	arr = dma_fence_array_create(2, fa, dma_fence_context_alloc(1), 1,
	    false);
	CHECK(arr != NULL);
	if (arr != NULL) {
		CHECK(dma_fence_is_array(&arr->base));
		CHECK(!dma_fence_is_chain(&arr->base));
		CHECK(dma_fence_is_container(&arr->base));
		dma_fence_signal(f1);
		dma_fence_signal(f2);
		dma_fence_put(&arr->base);
	}

	chain = kzalloc(sizeof(*chain), GFP_KERNEL);
	f1 = tf_new(dma_fence_context_alloc(1), 1);
	dma_fence_chain_init(chain, NULL, f1, 1);
	CHECK(dma_fence_is_chain(&chain->base));
	CHECK(!dma_fence_is_array(&chain->base));
	CHECK(dma_fence_is_container(&chain->base));
	dma_fence_signal(f1);
	dma_fence_put(&chain->base);

	dma_fence_put(plain);
	CHECK(tf_live == 0);
}

int	drm2_dma_resv_run(void);

int
drm2_dma_resv_run(void)
{

	t_run = t_fail = 0;
	spin_lock_init(&tf_lock);
	test_fence_order();
	test_fence_status_timestamp();
	test_fence_deadline();
	test_fence_container();
	printf("drm2-dma-resv: %d checks, %d failed\n", t_run, t_fail);
	return t_fail;
}
