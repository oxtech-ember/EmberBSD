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
#include <sys/atomic.h>
#include <sys/callout.h>
#include <sys/kthread.h>
#include <sys/lwp.h>
#include <sys/kmem.h>
#include <sys/systm.h>

#include <linux/dma-fence.h>
#include <linux/dma-fence-array.h>
#include <linux/dma-fence-chain.h>
#include <linux/dma-fence-unwrap.h>
#include <linux/dma-resv.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/ktime.h>
#include <linux/rcupdate.h>
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
static volatile unsigned tf_live;
static int tf_deadlines;
static ktime_t tf_last_deadline;

static const char *
tf_name(struct dma_fence *f __unused)
{

	return "drm2test";
}

static void
tf_release(struct dma_fence *f)
{

	atomic_dec_uint(&tf_live);
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
	atomic_inc_uint(&tf_live);
	return f;
}

static struct dma_fence *
tf_new64(uint64_t context, uint64_t seqno)
{
	struct dma_fence *f = kzalloc(sizeof(*f), GFP_KERNEL);

	if (f == NULL)
		return NULL;
	dma_fence_init(f, &tf_ops64, &tf_lock, context, seqno);
	atomic_inc_uint(&tf_live);
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
	atomic_add_int((volatile unsigned *)&tf_live, 2);
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
test_fence_names(void)
{
	struct dma_fence *f = tf_new(dma_fence_context_alloc(1), 1);

	CHECK(strcmp(dma_fence_driver_name(f), "drm2test") == 0);
	CHECK(strcmp(dma_fence_timeline_name(f), "drm2test") == 0);
	dma_fence_signal(f);
	CHECK(strcmp(dma_fence_driver_name(f), "detached-driver") == 0);
	CHECK(strcmp(dma_fence_timeline_name(f), "signaled-timeline") == 0);
	dma_fence_put(f);
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

/* ---- dma_resv: Linux 6.18 API ---- */

#define	NF	16

static unsigned
tr_collect(struct dma_resv *r, enum dma_resv_usage usage,
    struct dma_fence **out, enum dma_resv_usage *uout)
{
	struct dma_resv_iter c;
	struct dma_fence *f;
	unsigned n = 0;

	dma_resv_for_each_fence(&c, r, usage, f) {
		if (n < NF) {
			out[n] = f;
			uout[n] = dma_resv_iter_usage(&c);
		}
		n++;
	}
	dma_resv_iter_end(&c);
	return n;
}

static int
tr_find(struct dma_fence **v, unsigned n, struct dma_fence *f)
{
	unsigned i;

	for (i = 0; i < n && i < NF; i++)
		if (v[i] == f)
			return (int)i;
	return -1;
}

static struct dma_fence *
tr_ctx_new(uint64_t seqno)
{

	return tf_new(dma_fence_context_alloc(1), seqno);
}

static void
tr_free(struct dma_resv *r, struct dma_fence **fs, unsigned n)
{
	unsigned i;

	dma_resv_fini(r);
	for (i = 0; i < n; i++)
		dma_fence_put(fs[i]);
}

static void
test_resv_usage_filter(void)
{
	struct dma_resv r;
	struct dma_fence *fk, *fw, *fr, *fb, *all[4];
	struct dma_fence *out[NF];
	enum dma_resv_usage u[NF];
	unsigned n;
	int i;

	dma_resv_init(&r);
	fk = tr_ctx_new(1);
	fw = tr_ctx_new(1);
	fr = tr_ctx_new(1);
	fb = tr_ctx_new(1);
	all[0] = fk; all[1] = fw; all[2] = fr; all[3] = fb;
	dma_resv_lock(&r, NULL);
	CHECK(dma_resv_reserve_fences(&r, 4) == 0);
	dma_resv_add_fence(&r, fk, DMA_RESV_USAGE_KERNEL);
	dma_resv_add_fence(&r, fw, DMA_RESV_USAGE_WRITE);
	dma_resv_add_fence(&r, fr, DMA_RESV_USAGE_READ);
	dma_resv_add_fence(&r, fb, DMA_RESV_USAGE_BOOKKEEP);

	n = tr_collect(&r, DMA_RESV_USAGE_KERNEL, out, u);
	CHECK(n == 1 && tr_find(out, n, fk) >= 0);
	n = tr_collect(&r, DMA_RESV_USAGE_WRITE, out, u);
	CHECK(n == 2 && tr_find(out, n, fk) >= 0 && tr_find(out, n, fw) >= 0);
	n = tr_collect(&r, DMA_RESV_USAGE_READ, out, u);
	CHECK(n == 3 && tr_find(out, n, fb) < 0);
	n = tr_collect(&r, DMA_RESV_USAGE_BOOKKEEP, out, u);
	CHECK(n == 4);
	i = tr_find(out, n, fk);
	CHECK(i >= 0 && u[i] == DMA_RESV_USAGE_KERNEL);
	i = tr_find(out, n, fw);
	CHECK(i >= 0 && u[i] == DMA_RESV_USAGE_WRITE);
	i = tr_find(out, n, fr);
	CHECK(i >= 0 && u[i] == DMA_RESV_USAGE_READ);
	i = tr_find(out, n, fb);
	CHECK(i >= 0 && u[i] == DMA_RESV_USAGE_BOOKKEEP);

	/* The locked iterator does not hide signalled fences. */
	dma_fence_signal(fr);
	n = tr_collect(&r, DMA_RESV_USAGE_READ, out, u);
	CHECK(n == 3 && tr_find(out, n, fr) >= 0);
	dma_resv_unlock(&r);

	CHECK(dma_resv_usage_rw(true) == DMA_RESV_USAGE_READ);
	CHECK(dma_resv_usage_rw(false) == DMA_RESV_USAGE_WRITE);
	tr_free(&r, all, 4);
	CHECK(tf_live == 0);
}

static void
test_resv_reserve(void)
{
	struct dma_resv r;
	struct dma_fence *fs[10];
	unsigned i;

	dma_resv_init(&r);
	dma_resv_lock(&r, NULL);
	CHECK(dma_resv_reserve_fences(&r, 0) == -EINVAL);
	/* One reservation covers that many adds, whatever the usage. */
	CHECK(dma_resv_reserve_fences(&r, 6) == 0);
	for (i = 0; i < 6; i++) {
		fs[i] = tr_ctx_new(1);
		dma_resv_add_fence(&r, fs[i], (enum dma_resv_usage)(i % 4));
	}
	/* Again, past the first capacity. */
	CHECK(dma_resv_reserve_fences(&r, 4) == 0);
	for (i = 6; i < 10; i++) {
		fs[i] = tr_ctx_new(1);
		dma_resv_add_fence(&r, fs[i], DMA_RESV_USAGE_BOOKKEEP);
	}
	{
		struct dma_fence *out[NF];
		enum dma_resv_usage u[NF];

		CHECK(tr_collect(&r, DMA_RESV_USAGE_BOOKKEEP, out, u) == 10);
		/* usages 0,1,2,3,0,1 up to READ: five */
		CHECK(tr_collect(&r, DMA_RESV_USAGE_READ, out, u) == 5);
	}
	dma_resv_unlock(&r);
	tr_free(&r, fs, 10);
	CHECK(tf_live == 0);
}

static void
test_resv_replace_rules(void)
{
	struct dma_resv r;
	struct dma_fence *out[NF], *all[8];
	enum dma_resv_usage u[NF];
	struct dma_fence *a1, *a2, *a0, *b1, *s1, *s2, *w1, *w2;
	uint64_t ca = dma_fence_context_alloc(1);
	uint64_t cb = dma_fence_context_alloc(1);
	unsigned n;
	int i;

	dma_resv_init(&r);
	dma_resv_lock(&r, NULL);
	CHECK(dma_resv_reserve_fences(&r, 8) == 0);

	/* Same context, later seqno, same usage: replaces. */
	a1 = tf_new(ca, 1);
	a2 = tf_new(ca, 2);
	dma_resv_add_fence(&r, a1, DMA_RESV_USAGE_READ);
	dma_resv_add_fence(&r, a2, DMA_RESV_USAGE_READ);
	n = tr_collect(&r, DMA_RESV_USAGE_BOOKKEEP, out, u);
	CHECK(n == 1 && out[0] == a2);

	/* Same context, earlier seqno: both kept. */
	a0 = tf_new(ca, 0);
	dma_resv_add_fence(&r, a0, DMA_RESV_USAGE_READ);
	n = tr_collect(&r, DMA_RESV_USAGE_BOOKKEEP, out, u);
	CHECK(n == 2 && tr_find(out, n, a2) >= 0 && tr_find(out, n, a0) >= 0);

	/* Promotion: a later fence of the context as WRITE takes over. */
	b1 = tf_new(ca, 3);
	dma_resv_add_fence(&r, b1, DMA_RESV_USAGE_WRITE);
	n = tr_collect(&r, DMA_RESV_USAGE_BOOKKEEP, out, u);
	i = tr_find(out, n, b1);
	CHECK(i >= 0 && u[i] == DMA_RESV_USAGE_WRITE);
	CHECK(tr_find(out, n, a2) < 0 || tr_find(out, n, a0) < 0);
	CHECK(n == 2);
	n = tr_collect(&r, DMA_RESV_USAGE_WRITE, out, u);
	CHECK(n == 1 && out[0] == b1);

	/*
	 * No degrading: a READ of the same context does not replace the
	 * WRITE, it replaces the older READ (only one entry is replaced).
	 */
	s1 = tf_new(ca, 4);
	dma_resv_add_fence(&r, s1, DMA_RESV_USAGE_READ);
	n = tr_collect(&r, DMA_RESV_USAGE_BOOKKEEP, out, u);
	CHECK(n == 2 && tr_find(out, n, b1) >= 0 && tr_find(out, n, s1) >= 0);
	CHECK(tr_find(out, n, a0) < 0 && tr_find(out, n, a2) < 0);

	/* A second writer of another context: both writers stay visible. */
	w1 = tf_new(cb, 1);
	dma_resv_add_fence(&r, w1, DMA_RESV_USAGE_WRITE);
	n = tr_collect(&r, DMA_RESV_USAGE_WRITE, out, u);
	CHECK(n == 2 && tr_find(out, n, w1) >= 0 && tr_find(out, n, b1) >= 0);

	/* A signalled fence frees its slot for any new one. */
	dma_fence_signal(w1);
	s2 = tr_ctx_new(1);
	dma_resv_add_fence(&r, s2, DMA_RESV_USAGE_BOOKKEEP);
	n = tr_collect(&r, DMA_RESV_USAGE_BOOKKEEP, out, u);
	CHECK(tr_find(out, n, w1) < 0);
	CHECK(tr_find(out, n, s2) >= 0);

	/* Adding the same fence again is a no-op for the contents. */
	i = (int)n;
	dma_resv_add_fence(&r, s2, DMA_RESV_USAGE_BOOKKEEP);
	n = tr_collect(&r, DMA_RESV_USAGE_BOOKKEEP, out, u);
	CHECK((int)n == i);
	/* ... and promotes it. */
	dma_resv_add_fence(&r, s2, DMA_RESV_USAGE_READ);
	n = tr_collect(&r, DMA_RESV_USAGE_BOOKKEEP, out, u);
	i = tr_find(out, n, s2);
	CHECK(i >= 0 && u[i] == DMA_RESV_USAGE_READ);
	dma_resv_unlock(&r);

	w2 = NULL;
	all[0] = a1; all[1] = a2; all[2] = a0; all[3] = b1;
	all[4] = s1; all[5] = s2; all[6] = w1; all[7] = w2;
	tr_free(&r, all, 7);
	CHECK(tf_live == 0);
}

/*
 * Readers of the 5.6 API (dma_resv_do_poll, get_fences_rcu, the *_rcu
 * waits) walk list->shared[i] over a snapshot of the count without
 * revalidating each entry.  So removing a shared fence must replace the
 * list (copy on write): the old list, still held by a reader, keeps its
 * entries and its count.  Found by the vgem stress (a poller crashed on a
 * NULL entry written by an in-place removal).
 */
static void
test_resv_shared_cow(void)
{
	struct dma_resv r;
	struct dma_resv_list *snap, *cur;
	struct dma_fence *a1, *b1, *c1, *a2, *x, *all[5];
	uint64_t ca = dma_fence_context_alloc(1);
	uint64_t cb = dma_fence_context_alloc(1);
	uint64_t cc = dma_fence_context_alloc(1);

	dma_resv_init(&r);
	dma_resv_lock(&r, NULL);
	CHECK(dma_resv_reserve_fences(&r, 4) == 0);
	a1 = tf_new(ca, 1);
	b1 = tf_new(cb, 1);
	c1 = tf_new(cc, 1);
	dma_resv_add_fence(&r, a1, DMA_RESV_USAGE_READ);
	dma_resv_add_fence(&r, b1, DMA_RESV_USAGE_READ);
	dma_resv_add_fence(&r, c1, DMA_RESV_USAGE_READ);

	/* A later fence of context a replaces a1. */
	rcu_read_lock();
	snap = dma_resv_get_list(&r);
	CHECK(snap != NULL && snap->shared_count == 3);
	a2 = tf_new(ca, 2);
	dma_resv_add_fence(&r, a2, DMA_RESV_USAGE_READ);
	CHECK(snap->shared_count == 3);
	CHECK(snap->shared[0] == a1 && snap->shared[1] == b1 &&
	    snap->shared[2] == c1);
	cur = dma_resv_get_list(&r);
	CHECK(cur != snap && cur != NULL && cur->shared_count == 3);
	CHECK(cur->shared[0] != a1 && cur->shared[1] != a1 &&
	    cur->shared[2] != a1);
	rcu_read_unlock();

	/* dma_resv_replace_fences (context b) goes the same way. */
	rcu_read_lock();
	snap = dma_resv_get_list(&r);
	CHECK(snap != NULL && snap->shared_count == 3);
	x = tf_new(cb, 2);
	dma_resv_replace_fences(&r, cb, x, DMA_RESV_USAGE_READ);
	CHECK(snap->shared_count == 3);
	CHECK(snap->shared[0] != NULL && snap->shared[1] != NULL &&
	    snap->shared[2] != NULL);
	cur = dma_resv_get_list(&r);
	CHECK(cur != snap && cur != NULL && cur->shared_count == 3);
	rcu_read_unlock();
	dma_resv_unlock(&r);

	all[0] = a1; all[1] = b1; all[2] = c1; all[3] = a2; all[4] = x;
	tr_free(&r, all, 5);
	CHECK(tf_live == 0);
}

/*
 * dma_resv_add_excl_fence on a list that a 5.6 reader holds: the reader
 * walks list->shared[i] over a count it read earlier, so the entries of
 * the list it holds must stay valid; the object gets a fresh empty list
 * of the same capacity instead (copy on write).
 */
static void
test_resv_excl_cow(void)
{
	struct dma_resv r;
	struct dma_resv_list *snap, *cur;
	struct dma_fence *a, *b, *c, *x, *y, *all[5];

	dma_resv_init(&r);
	dma_resv_lock(&r, NULL);
	CHECK(dma_resv_reserve_shared(&r, 3) == 0);
	a = tr_ctx_new(1);
	b = tr_ctx_new(1);
	c = tr_ctx_new(1);
	dma_resv_add_shared_fence(&r, a);
	dma_resv_add_shared_fence(&r, b);
	dma_resv_add_shared_fence(&r, c);

	rcu_read_lock();
	snap = dma_resv_get_list(&r);
	CHECK(snap != NULL && snap->shared_count == 3);
	x = tr_ctx_new(1);
	dma_resv_add_excl_fence(&r, x);
	CHECK(snap->shared_count == 3);
	CHECK(snap->shared[0] == a && snap->shared[1] == b &&
	    snap->shared[2] == c);
	cur = dma_resv_get_list(&r);
	CHECK(cur != NULL && cur != snap && cur->shared_count == 0);
	CHECK(cur != NULL && cur->shared_max == snap->shared_max);
	rcu_read_unlock();

	/* The reserved capacity survives: a shared add needs no reserve. */
	y = tr_ctx_new(1);
	dma_resv_add_shared_fence(&r, y);
	cur = dma_resv_get_list(&r);
	CHECK(cur != NULL && cur->shared_count == 1 && cur->shared[0] == y);
	CHECK(dma_resv_get_excl(&r) == x);

	/* Clearing the exclusive fence with an empty list changes nothing. */
	dma_resv_add_excl_fence(&r, NULL);
	CHECK(dma_resv_get_excl(&r) == NULL);
	cur = dma_resv_get_list(&r);
	CHECK(cur != NULL && cur->shared_count == 0);
	dma_resv_unlock(&r);

	all[0] = a; all[1] = b; all[2] = c; all[3] = x; all[4] = y;
	tr_free(&r, all, 5);
	CHECK(tf_live == 0);
}

static void
test_resv_unlocked_iter(void)
{
	struct dma_resv r;
	struct dma_fence *a, *b, *c, *g, *f, *all[4];
	struct dma_resv_iter cur;
	unsigned n, restarts, seen;
	bool added = false;

	dma_resv_init(&r);
	a = tr_ctx_new(1);
	b = tr_ctx_new(1);
	c = tr_ctx_new(1);
	g = tr_ctx_new(1);
	all[0] = a; all[1] = b; all[2] = c; all[3] = g;
	dma_resv_lock(&r, NULL);
	dma_resv_reserve_fences(&r, 4);
	dma_resv_add_fence(&r, a, DMA_RESV_USAGE_READ);
	dma_resv_add_fence(&r, b, DMA_RESV_USAGE_READ);
	dma_resv_add_fence(&r, c, DMA_RESV_USAGE_WRITE);
	dma_resv_unlock(&r);

	/* All three, restarted flag only on the first. */
	n = 0;
	dma_resv_iter_begin(&cur, &r, DMA_RESV_USAGE_READ);
	dma_resv_for_each_fence_unlocked(&cur, f) {
		CHECK(dma_resv_iter_is_restarted(&cur) == (n == 0));
		n++;
	}
	dma_resv_iter_end(&cur);
	CHECK(n == 3);

	/* Signalled fences are skipped. */
	dma_fence_signal(b);
	n = 0;
	dma_resv_iter_begin(&cur, &r, DMA_RESV_USAGE_READ);
	dma_resv_for_each_fence_unlocked(&cur, f) {
		CHECK(f != b);
		n++;
	}
	dma_resv_iter_end(&cur);
	CHECK(n == 2);

	/* Usage filter. */
	n = 0;
	dma_resv_iter_begin(&cur, &r, DMA_RESV_USAGE_WRITE);
	dma_resv_for_each_fence_unlocked(&cur, f) {
		CHECK(f == c && dma_resv_iter_usage(&cur) ==
		    DMA_RESV_USAGE_WRITE);
		n++;
	}
	dma_resv_iter_end(&cur);
	CHECK(n == 1);

	/* A change during the walk restarts it; the second pass sees g. */
	restarts = seen = 0;
	dma_resv_iter_begin(&cur, &r, DMA_RESV_USAGE_BOOKKEEP);
	dma_resv_for_each_fence_unlocked(&cur, f) {
		if (dma_resv_iter_is_restarted(&cur)) {
			restarts++;
			seen = 0;
		}
		seen++;
		if (!added) {
			added = true;
			dma_resv_lock(&r, NULL);
			dma_resv_add_fence(&r, g, DMA_RESV_USAGE_READ);
			dma_resv_unlock(&r);
		}
	}
	dma_resv_iter_end(&cur);
	CHECK(restarts == 2);
	CHECK(seen == 3);

	tr_free(&r, all, 4);
	CHECK(tf_live == 0);
}

static void
test_resv_get_fences(void)
{
	struct dma_resv r;
	struct dma_fence *w, *rd, *bk, *all[3], **arr;
	unsigned n, i;

	dma_resv_init(&r);
	CHECK(dma_resv_get_fences(&r, DMA_RESV_USAGE_BOOKKEEP, &n, &arr) == 0);
	CHECK(n == 0 && arr == NULL);

	w = tr_ctx_new(1);
	rd = tr_ctx_new(1);
	bk = tr_ctx_new(1);
	all[0] = w; all[1] = rd; all[2] = bk;
	dma_resv_lock(&r, NULL);
	dma_resv_reserve_fences(&r, 3);
	dma_resv_add_fence(&r, w, DMA_RESV_USAGE_WRITE);
	dma_resv_add_fence(&r, rd, DMA_RESV_USAGE_READ);
	dma_resv_add_fence(&r, bk, DMA_RESV_USAGE_BOOKKEEP);
	dma_resv_unlock(&r);

	CHECK(dma_resv_get_fences(&r, DMA_RESV_USAGE_WRITE, &n, &arr) == 0);
	CHECK(n == 1 && arr != NULL && arr[0] == w);
	for (i = 0; i < n; i++)
		dma_fence_put(arr[i]);
	kfree(arr);

	CHECK(dma_resv_get_fences(&r, DMA_RESV_USAGE_READ, &n, &arr) == 0);
	CHECK(n == 2 && arr != NULL);
	for (i = 0; i < n; i++)
		dma_fence_put(arr[i]);
	kfree(arr);

	CHECK(dma_resv_get_fences(&r, DMA_RESV_USAGE_BOOKKEEP, &n, &arr) == 0);
	CHECK(n == 3);
	for (i = 0; i < n; i++)
		dma_fence_put(arr[i]);
	kfree(arr);

	/* Signalled fences are not returned. */
	dma_fence_signal(w);
	dma_fence_signal(rd);
	CHECK(dma_resv_get_fences(&r, DMA_RESV_USAGE_BOOKKEEP, &n, &arr) == 0);
	CHECK(n == 1 && arr[0] == bk);
	dma_fence_put(arr[0]);
	kfree(arr);

	tr_free(&r, all, 3);
	CHECK(tf_live == 0);
}

static void
test_resv_singleton(void)
{
	struct dma_resv r;
	struct dma_fence *f1, *f2, *s, *all[2];

	dma_resv_init(&r);
	s = (struct dma_fence *)1;
	CHECK(dma_resv_get_singleton(&r, DMA_RESV_USAGE_BOOKKEEP, &s) == 0);
	CHECK(s == NULL);

	f1 = tr_ctx_new(1);
	f2 = tr_ctx_new(1);
	all[0] = f1; all[1] = f2;
	dma_resv_lock(&r, NULL);
	dma_resv_reserve_fences(&r, 2);
	dma_resv_add_fence(&r, f1, DMA_RESV_USAGE_READ);
	dma_resv_unlock(&r);

	/* One fence: that very fence, referenced. */
	CHECK(dma_resv_get_singleton(&r, DMA_RESV_USAGE_READ, &s) == 0);
	CHECK(s == f1);
	dma_fence_put(s);

	/* Two: an array that signals when both have. */
	dma_resv_lock(&r, NULL);
	dma_resv_add_fence(&r, f2, DMA_RESV_USAGE_BOOKKEEP);
	dma_resv_unlock(&r);
	CHECK(dma_resv_get_singleton(&r, DMA_RESV_USAGE_READ, &s) == 0);
	CHECK(s == f1);
	dma_fence_put(s);
	CHECK(dma_resv_get_singleton(&r, DMA_RESV_USAGE_BOOKKEEP, &s) == 0);
	CHECK(s != NULL && s != f1 && s != f2 && dma_fence_is_array(s));
	if (s != NULL && dma_fence_is_array(s)) {
		CHECK(dma_fence_wait_timeout(s, false, 0) == 0);
		dma_fence_signal(f1);
		CHECK(!dma_fence_is_signaled(s));
		dma_fence_signal(f2);
		CHECK(dma_fence_wait_timeout(s, false, hz) > 0);
		CHECK(dma_fence_is_signaled(s));
		dma_fence_put(s);
	}
	tr_free(&r, all, 2);
	CHECK(tf_live == 0);
}

static struct callout tr_co;

static void
tr_signal_cb(void *arg)
{

	dma_fence_signal(arg);
}

static void
test_resv_wait_test(void)
{
	struct dma_resv r;
	struct dma_fence *w, *rd, *bk, *all[3];
	ktime_t t0, dt;
	long ret;

	dma_resv_init(&r);
	/* Empty object: signalled; a wait returns its timeout (1 if 0). */
	CHECK(dma_resv_test_signaled(&r, DMA_RESV_USAGE_BOOKKEEP));
	CHECK(dma_resv_wait_timeout(&r, DMA_RESV_USAGE_BOOKKEEP, false, 5) == 5);
	CHECK(dma_resv_wait_timeout(&r, DMA_RESV_USAGE_BOOKKEEP, false, 0) == 1);

	w = tr_ctx_new(1);
	rd = tr_ctx_new(1);
	bk = tr_ctx_new(1);
	all[0] = w; all[1] = rd; all[2] = bk;
	dma_resv_lock(&r, NULL);
	dma_resv_reserve_fences(&r, 3);
	dma_resv_add_fence(&r, w, DMA_RESV_USAGE_WRITE);
	dma_resv_add_fence(&r, rd, DMA_RESV_USAGE_READ);
	dma_resv_add_fence(&r, bk, DMA_RESV_USAGE_BOOKKEEP);
	dma_resv_unlock(&r);

	CHECK(!dma_resv_test_signaled(&r, DMA_RESV_USAGE_WRITE));
	CHECK(dma_resv_wait_timeout(&r, DMA_RESV_USAGE_WRITE, false, 0) == 0);
	CHECK(dma_resv_wait_timeout(&r, DMA_RESV_USAGE_WRITE, false,
	    mstohz(20)) == 0);

	/* Only fences up to the usage matter. */
	dma_fence_signal(w);
	CHECK(dma_resv_test_signaled(&r, DMA_RESV_USAGE_WRITE));
	CHECK(!dma_resv_test_signaled(&r, DMA_RESV_USAGE_READ));
	dma_fence_signal(rd);
	CHECK(dma_resv_test_signaled(&r, DMA_RESV_USAGE_READ));
	CHECK(!dma_resv_test_signaled(&r, DMA_RESV_USAGE_BOOKKEEP));
	CHECK(dma_resv_wait_timeout(&r, DMA_RESV_USAGE_READ, false,
	    mstohz(20)) == mstohz(20));
	CHECK(dma_resv_wait_timeout(&r, DMA_RESV_USAGE_BOOKKEEP, false,
	    mstohz(20)) == 0);

	/* A wait that really blocks until another context signals. */
	callout_init(&tr_co, CALLOUT_MPSAFE);
	callout_reset(&tr_co, mstohz(30), tr_signal_cb, bk);
	t0 = ktime_get();
	ret = dma_resv_wait_timeout(&r, DMA_RESV_USAGE_BOOKKEEP, false, hz * 5);
	dt = ktime_sub(ktime_get(), t0);
	CHECK(ret > 0 && ret <= hz * 5);
	CHECK(dma_fence_is_signaled(bk));
	CHECK(ktime_to_ms(dt) >= 10 && ktime_to_ms(dt) < 2000);
	callout_halt(&tr_co, NULL);
	callout_destroy(&tr_co);
	CHECK(dma_resv_test_signaled(&r, DMA_RESV_USAGE_BOOKKEEP));

	tr_free(&r, all, 3);
	CHECK(tf_live == 0);
}

static void
test_resv_deadline(void)
{
	struct dma_resv r;
	struct dma_fence *w, *rd, *bk, *all[3];

	dma_resv_init(&r);
	w = tr_ctx_new(1);
	rd = tr_ctx_new(1);
	bk = tr_ctx_new(1);
	all[0] = w; all[1] = rd; all[2] = bk;
	dma_resv_lock(&r, NULL);
	dma_resv_reserve_fences(&r, 3);
	dma_resv_add_fence(&r, w, DMA_RESV_USAGE_WRITE);
	dma_resv_add_fence(&r, rd, DMA_RESV_USAGE_READ);
	dma_resv_add_fence(&r, bk, DMA_RESV_USAGE_BOOKKEEP);
	dma_resv_unlock(&r);

	tf_deadlines = 0;
	dma_resv_set_deadline(&r, DMA_RESV_USAGE_READ, 9000);
	CHECK(tf_deadlines == 2 && tf_last_deadline == 9000);
	dma_resv_set_deadline(&r, DMA_RESV_USAGE_BOOKKEEP, 8000);
	CHECK(tf_deadlines == 5 && tf_last_deadline == 8000);
	dma_fence_signal(w);
	dma_resv_set_deadline(&r, DMA_RESV_USAGE_READ, 7000);
	CHECK(tf_deadlines == 6);
	tr_free(&r, all, 3);
	CHECK(tf_live == 0);
}

static void
test_resv_replace_fences(void)
{
	struct dma_resv r;
	struct dma_fence *f1, *f2, *g, *all[3], *out[NF];
	enum dma_resv_usage u[NF];
	uint64_t ca = dma_fence_context_alloc(1);
	unsigned n;
	int i;

	dma_resv_init(&r);
	f1 = tf_new(ca, 1);
	f2 = tr_ctx_new(1);
	g = tf_new(ca, 5);
	all[0] = f1; all[1] = f2; all[2] = g;
	dma_resv_lock(&r, NULL);
	dma_resv_reserve_fences(&r, 4);
	dma_resv_add_fence(&r, f1, DMA_RESV_USAGE_READ);
	dma_resv_add_fence(&r, f2, DMA_RESV_USAGE_WRITE);
	dma_resv_replace_fences(&r, ca, g, DMA_RESV_USAGE_KERNEL);
	n = tr_collect(&r, DMA_RESV_USAGE_BOOKKEEP, out, u);
	CHECK(n == 2 && tr_find(out, n, f1) < 0);
	i = tr_find(out, n, g);
	CHECK(i >= 0 && u[i] == DMA_RESV_USAGE_KERNEL);
	/* No fence of that context: nothing changes. */
	dma_resv_replace_fences(&r, dma_fence_context_alloc(1), g,
	    DMA_RESV_USAGE_READ);
	CHECK(tr_collect(&r, DMA_RESV_USAGE_BOOKKEEP, out, u) == 2);
	dma_resv_unlock(&r);
	tr_free(&r, all, 3);
	CHECK(tf_live == 0);
}

static void
test_resv_legacy(void)
{
	struct dma_resv r, d;
	struct dma_fence *s1, *s2, *e, *k, *x, *rd, *bk, *wr, *all[7];
	struct dma_fence *out[NF];
	enum dma_resv_usage u[NF];
	struct dma_resv_list *list;
	unsigned n;
	int i;

	/* 5.6 calls: shared fences, then an exclusive one drops them. */
	dma_resv_init(&r);
	s1 = tr_ctx_new(1);
	s2 = tr_ctx_new(1);
	e = tr_ctx_new(1);
	all[0] = s1; all[1] = s2; all[2] = e;
	dma_resv_lock(&r, NULL);
	CHECK(dma_resv_reserve_shared(&r, 2) == 0);
	dma_resv_add_shared_fence(&r, s1);
	dma_resv_add_shared_fence(&r, s2);
	list = dma_resv_get_list(&r);
	CHECK(list != NULL && list->shared_count == 2);
	CHECK(dma_resv_get_excl(&r) == NULL);
	n = tr_collect(&r, DMA_RESV_USAGE_BOOKKEEP, out, u);
	CHECK(n == 2 && u[0] == DMA_RESV_USAGE_READ);
	dma_resv_add_excl_fence(&r, e);
	CHECK(dma_resv_get_excl(&r) == e);
	list = dma_resv_get_list(&r);
	CHECK(list == NULL || list->shared_count == 0);
	n = tr_collect(&r, DMA_RESV_USAGE_BOOKKEEP, out, u);
	CHECK(n == 1 && out[0] == e && u[0] == DMA_RESV_USAGE_WRITE);
	/* Clearing the exclusive fence. */
	dma_resv_add_excl_fence(&r, NULL);
	CHECK(dma_resv_get_excl(&r) == NULL);
	CHECK(tr_collect(&r, DMA_RESV_USAGE_BOOKKEEP, out, u) == 0);
	dma_resv_unlock(&r);

	/* 5.6 waits: writers only, or all (READ) with shared set. */
	dma_resv_lock(&r, NULL);
	dma_resv_reserve_shared(&r, 1);
	dma_resv_add_excl_fence(&r, e);
	dma_resv_add_shared_fence(&r, s1);
	dma_resv_unlock(&r);
	CHECK(!dma_resv_test_signaled_rcu(&r, false));
	CHECK(dma_resv_wait_timeout_rcu(&r, false, false, 0) == 0);
	dma_fence_signal(e);
	CHECK(dma_resv_test_signaled_rcu(&r, false));
	CHECK(!dma_resv_test_signaled_rcu(&r, true));
	CHECK(dma_resv_wait_timeout_rcu(&r, true, false, mstohz(20)) == 0);
	dma_fence_signal(s1);
	CHECK(dma_resv_test_signaled_rcu(&r, true));
	CHECK(dma_resv_wait_timeout_rcu(&r, true, false, mstohz(20)) ==
	    mstohz(20));
	dma_resv_fini(&r);

	/* A 6.x KERNEL fence is a writer to the 5.6 wait. */
	dma_resv_init(&r);
	k = tr_ctx_new(1);
	dma_resv_lock(&r, NULL);
	dma_resv_reserve_fences(&r, 1);
	dma_resv_add_fence(&r, k, DMA_RESV_USAGE_KERNEL);
	dma_resv_unlock(&r);
	CHECK(!dma_resv_test_signaled_rcu(&r, false));
	dma_fence_signal(k);
	CHECK(dma_resv_test_signaled_rcu(&r, false));
	dma_resv_fini(&r);
	dma_fence_put(k);

	/* Copy: everything with its usage; old contents of dst go away. */
	dma_resv_init(&r);
	dma_resv_init(&d);
	rd = tr_ctx_new(1);
	bk = tr_ctx_new(1);
	wr = tr_ctx_new(1);
	x = tr_ctx_new(1);
	all[3] = rd; all[4] = bk; all[5] = wr; all[6] = x;
	dma_resv_lock(&r, NULL);
	dma_resv_reserve_fences(&r, 3);
	dma_resv_add_fence(&r, wr, DMA_RESV_USAGE_WRITE);
	dma_resv_add_fence(&r, rd, DMA_RESV_USAGE_READ);
	dma_resv_add_fence(&r, bk, DMA_RESV_USAGE_BOOKKEEP);
	dma_resv_unlock(&r);
	dma_resv_lock(&d, NULL);
	dma_resv_reserve_fences(&d, 1);
	dma_resv_add_fence(&d, x, DMA_RESV_USAGE_READ);
	CHECK(dma_resv_copy_fences(&d, &r) == 0);
	n = tr_collect(&d, DMA_RESV_USAGE_BOOKKEEP, out, u);
	CHECK(n == 3 && tr_find(out, n, x) < 0);
	i = tr_find(out, n, wr);
	CHECK(i >= 0 && u[i] == DMA_RESV_USAGE_WRITE);
	i = tr_find(out, n, rd);
	CHECK(i >= 0 && u[i] == DMA_RESV_USAGE_READ);
	i = tr_find(out, n, bk);
	CHECK(i >= 0 && u[i] == DMA_RESV_USAGE_BOOKKEEP);
	/* The copy is usable: more fences can be added afterwards. */
	CHECK(dma_resv_reserve_fences(&d, 1) == 0);
	dma_resv_add_fence(&d, x, DMA_RESV_USAGE_KERNEL);
	CHECK(tr_collect(&d, DMA_RESV_USAGE_BOOKKEEP, out, u) == 4);
	dma_resv_unlock(&d);
	dma_resv_fini(&r);
	dma_resv_fini(&d);

	for (i = 0; i < 7; i++)
		dma_fence_put(all[i]);
	CHECK(tf_live == 0);
}

/* ---- dma_fence_chain, dma_fence_array, dma_fence_unwrap (6.18) ---- */

static void
tc_settle(void)
{

	/* Chain and array releases run from irq_work. */
	kpause("drm2set", false, hz / 10, NULL);
}

static struct dma_fence_chain *
tc_node(struct dma_fence *prev, struct dma_fence *payload, uint64_t seqno)
{
	struct dma_fence_chain *c = dma_fence_chain_alloc();

	dma_fence_chain_init(c, prev, payload, seqno);
	return c;
}

static void
test_chain(void)
{
	struct dma_fence *p1, *p2, *p3, *plain, *f, *it;
	struct dma_fence_chain *c1, *c2, *c3;
	uint64_t seq[4];
	unsigned n;
	int r;

	p1 = tr_ctx_new(1);
	p2 = tr_ctx_new(1);
	p3 = tr_ctx_new(1);
	plain = tr_ctx_new(1);
	/* Each init consumes a reference to the payload and to prev. */
	c1 = tc_node(NULL, dma_fence_get(p1), 1);
	c2 = tc_node(dma_fence_get(&c1->base), dma_fence_get(p2), 2);
	c3 = tc_node(dma_fence_get(&c2->base), dma_fence_get(p3), 3);
	CHECK(c1->base.context == c2->base.context &&
	    c2->base.context == c3->base.context);
	CHECK(c1->base.seqno == 1 && c2->base.seqno == 2 && c3->base.seqno == 3);
	CHECK(c1->prev_seqno == 0 && c2->prev_seqno == 1 && c3->prev_seqno == 2);

	/* Predicates and the contained fence. */
	CHECK(dma_fence_is_chain(&c3->base) && !dma_fence_is_chain(p1));
	CHECK(to_dma_fence_chain(&c3->base) == c3);
	CHECK(to_dma_fence_chain(p1) == NULL);
	CHECK(to_dma_fence_chain(NULL) == NULL);
	CHECK(dma_fence_chain_contained(&c3->base) == p3);
	CHECK(c3->fence == p3 && c3->dfc_fence == p3);
	CHECK(dma_fence_chain_contained(plain) == plain);

	/* Walk from the head: newest to oldest. */
	n = 0;
	dma_fence_chain_for_each(it, &c3->base) {
		if (n < 4)
			seq[n] = it->seqno;
		n++;
	}
	CHECK(n == 3 && seq[0] == 3 && seq[1] == 2 && seq[2] == 1);

	/* find_seqno: the node that will signal the requested point. */
	f = dma_fence_get(&c3->base);
	CHECK(dma_fence_chain_find_seqno(&f, 0) == 0 && f == &c3->base);
	CHECK(dma_fence_chain_find_seqno(&f, 3) == 0 && f == &c3->base);
	CHECK(dma_fence_chain_find_seqno(&f, 2) == 0 && f == &c2->base);
	dma_fence_put(f);
	f = dma_fence_get(&c3->base);
	CHECK(dma_fence_chain_find_seqno(&f, 1) == 0 && f == &c1->base);
	dma_fence_put(f);
	/* Not far enough yet: error, pointer untouched. */
	f = dma_fence_get(&c3->base);
	r = dma_fence_chain_find_seqno(&f, 4);
	CHECK(r == -EINVAL && f == &c3->base);
	dma_fence_put(f);
	/* Not a chain: only seqno 0 is a no-op. */
	f = dma_fence_get(plain);
	CHECK(dma_fence_chain_find_seqno(&f, 0) == 0 && f == plain);
	CHECK(dma_fence_chain_find_seqno(&f, 1) == -EINVAL && f == plain);
	dma_fence_put(f);

	/* The walk skips nodes whose payload has signalled. */
	dma_fence_signal(p2);
	n = 0;
	dma_fence_chain_for_each(it, &c3->base) {
		if (n < 4)
			seq[n] = it->seqno;
		n++;
	}
	CHECK(n == 2 && seq[0] == 3 && seq[1] == 1);

	/* A new head on a chain continues its context; unrelated starts one. */
	{
		struct dma_fence_chain *c4, *x;
		struct dma_fence *p4 = tr_ctx_new(1), *px = tr_ctx_new(1);

		c4 = tc_node(dma_fence_get(&c3->base), dma_fence_get(p4), 4);
		CHECK(c4->base.context == c3->base.context);
		x = tc_node(dma_fence_get(&c3->base), dma_fence_get(px), 3);
		/* seqno not later than prev: a new context. */
		CHECK(x->base.context != c3->base.context);
		dma_fence_signal(p4);
		dma_fence_signal(px);
		dma_fence_put(&c4->base);
		dma_fence_put(&x->base);
		dma_fence_put(p4);
		dma_fence_put(px);
	}

	dma_fence_signal(p1);
	dma_fence_signal(p3);
	dma_fence_signal(plain);
	dma_fence_put(&c1->base);
	dma_fence_put(&c2->base);
	dma_fence_put(&c3->base);
	dma_fence_put(p1);
	dma_fence_put(p2);
	dma_fence_put(p3);
	dma_fence_put(plain);
	tc_settle();
	CHECK(tf_live == 0);

	/* alloc/free pair. */
	c1 = dma_fence_chain_alloc();
	CHECK(c1 != NULL);
	dma_fence_chain_free(c1);
}

static struct dma_fence_array *
tc_array(struct dma_fence *a, struct dma_fence *b)
{
	struct dma_fence **v = kmalloc_array(2, sizeof(*v), GFP_KERNEL);

	v[0] = a;
	v[1] = b;
	return dma_fence_array_create(2, v, dma_fence_context_alloc(1), 1,
	    false);
}

static void
test_array_unwrap(void)
{
	struct dma_fence *a, *b, *c, *f, *h;
	struct dma_fence_array *arr, *arr2;
	struct dma_fence_chain *c1, *c2;
	struct dma_fence_unwrap cur;
	unsigned idx, n;
	struct dma_fence *got[8];

	a = tr_ctx_new(1);
	b = tr_ctx_new(1);
	c = tr_ctx_new(1);

	/* 6.x array accessors; to_dma_fence_array is NULL for non-arrays. */
	arr = tc_array(dma_fence_get(a), dma_fence_get(b));
	CHECK(to_dma_fence_array(&arr->base) == arr);
	CHECK(to_dma_fence_array(a) == NULL);
	CHECK(to_dma_fence_array(NULL) == NULL);
	n = 0;
	dma_fence_array_for_each(f, idx, &arr->base) {
		CHECK(idx == n);
		got[n++] = f;
	}
	CHECK(n == 2 && got[0] == a && got[1] == b);
	n = 0;
	dma_fence_array_for_each(f, idx, a)
		got[n++] = f;
	CHECK(n == 1 && got[0] == a);
	CHECK(dma_fence_array_first(NULL) == NULL);
	CHECK(dma_fence_array_next(&arr->base, 2) == NULL);

	/* Unwrap: plain fence, array. */
	n = 0;
	dma_fence_unwrap_for_each(f, &cur, c)
		got[n++] = f;
	CHECK(n == 1 && got[0] == c);
	n = 0;
	dma_fence_unwrap_for_each(f, &cur, &arr->base)
		got[n++] = f;
	CHECK(n == 2 && got[0] == a && got[1] == b);

	/* A chain of arrays and plain fences, head first. */
	arr2 = tc_array(dma_fence_get(b), dma_fence_get(c));
	c1 = tc_node(NULL, dma_fence_get(&arr->base), 1);
	c2 = tc_node(dma_fence_get(&c1->base), dma_fence_get(&arr2->base), 2);
	n = 0;
	dma_fence_unwrap_for_each(f, &cur, &c2->base) {
		if (n < 8)
			got[n] = f;
		n++;
	}
	CHECK(n == 4);
	CHECK(got[0] == b && got[1] == c && got[2] == a && got[3] == b);

	/* Merge: a single unsignalled fence comes back as itself. */
	h = dma_fence_unwrap_merge(a);
	CHECK(h == a);
	dma_fence_put(h);

	/* Signalled ones drop out; with nothing left, a stub. */
	{
		struct dma_fence *s1 = tr_ctx_new(1), *s2 = tr_ctx_new(1);

		dma_fence_signal_timestamp(s1, 1000);
		dma_fence_signal_timestamp(s2, 5000);
		h = dma_fence_unwrap_merge(s1, s2);
		CHECK(h != NULL && dma_fence_is_signaled(h));
		if (h != NULL) {
			CHECK(dma_fence_timestamp(h) == 5000);
			dma_fence_put(h);
		}
		h = dma_fence_unwrap_merge(s1, a);
		CHECK(h == a);
		dma_fence_put(h);
		dma_fence_put(s1);
		dma_fence_put(s2);
	}

	/* One context: the latest wins, so one fence comes back. */
	{
		uint64_t ctx = dma_fence_context_alloc(1);
		struct dma_fence *x1 = tf_new(ctx, 1), *x2 = tf_new(ctx, 2);

		h = dma_fence_unwrap_merge(x1, x2);
		CHECK(h == x2);
		dma_fence_put(h);
		h = dma_fence_unwrap_merge(x2, x1);
		CHECK(h == x2);
		dma_fence_put(h);

		/* Several contexts: an array of the latest per context. */
		h = dma_fence_unwrap_merge(x1, b, x2);
		CHECK(h != NULL && dma_fence_is_array(h));
		if (h != NULL && dma_fence_is_array(h)) {
			struct dma_fence_array *m = to_dma_fence_array(h);

			CHECK(m->num_fences == 2);
			CHECK((m->fences[0] == x2 && m->fences[1] == b) ||
			    (m->fences[0] == b && m->fences[1] == x2));
			/* ascending context order */
			CHECK(m->fences[0]->context < m->fences[1]->context);
			CHECK(!dma_fence_is_signaled(h));
			dma_fence_signal(x2);
			dma_fence_signal(b);
			CHECK(dma_fence_wait_timeout(h, false, hz) > 0);
		}
		dma_fence_put(h);
		dma_fence_signal(x1);
		dma_fence_put(x1);
		dma_fence_put(x2);
	}

	/* Containers are flattened into their leaf fences. */
	h = dma_fence_unwrap_merge(&c2->base, c);
	CHECK(h != NULL && dma_fence_is_array(h));
	if (h != NULL && dma_fence_is_array(h)) {
		/*
		 * Leaves: b, c (arr2), a, b (arr), c.  b has signalled by
		 * now and drops out; a and c are in one context each.
		 */
		CHECK(to_dma_fence_array(h)->num_fences == 2);
	}
	dma_fence_put(h);

	dma_fence_signal(a);
	dma_fence_signal(b);
	dma_fence_signal(c);
	dma_fence_put(&c2->base);
	dma_fence_put(&c1->base);
	dma_fence_put(&arr2->base);
	dma_fence_put(&arr->base);
	dma_fence_put(a);
	dma_fence_put(b);
	dma_fence_put(c);
	tc_settle();
	CHECK(tf_live == 0);
}

/*
 * Stress: one writer adds fences of every usage (and signals some) while
 * readers walk the object without the lock, with the 6.x iterators, the
 * 6.x snapshot and the 5.6 snapshot.  Checks that nothing panics (the
 * kernel has DIAGNOSTIC) and that every reference is returned.
 *
 * kthread_create hands the new thread itself as the argument when the
 * argument is NULL, so the readers get id + 1.
 */

static struct dma_resv st_r;
static volatile unsigned st_stop, st_done;
static volatile unsigned st_iters[3], st_added, st_restarts;
static uint64_t st_ctx[8];

static void
st_writer(void *arg __unused)
{
	struct dma_fence *f;
	unsigned i = 0;

	while (!st_stop) {
		f = tf_new(st_ctx[i % 8], i / 8 + 1);
		dma_resv_lock(&st_r, NULL);
		if (dma_resv_reserve_fences(&st_r, 1) == 0)
			dma_resv_add_fence(&st_r, f,
			    (enum dma_resv_usage)(i % 4));
		dma_resv_unlock(&st_r);
		if (i % 3 == 0)
			dma_fence_signal(f);
		dma_fence_put(f);
		st_added++;
		i++;
		if (i % 7 == 0)
			kpause("drm2stw", false, 1, NULL);
	}
	atomic_inc_uint(&st_done);
	kthread_exit(0);
}

static void
st_reader(void *arg)
{
	unsigned id = (unsigned)(uintptr_t)arg - 1, round = 0, n, i, nsh;
	struct dma_resv_iter c;
	struct dma_fence *f, *excl, **v, **sh;

	KASSERT(id < 3);
	while (!st_stop) {
		switch (id) {
		case 0:
			n = 0;
			dma_resv_iter_begin(&c, &st_r, DMA_RESV_USAGE_BOOKKEEP);
			dma_resv_for_each_fence_unlocked(&c, f) {
				if (dma_resv_iter_is_restarted(&c)) {
					n = 0;
					st_restarts++;
				}
				KASSERT(f->context != 0 || f->seqno != 0);
				n++;
			}
			dma_resv_iter_end(&c);
			break;
		case 1:
			if (dma_resv_get_fences(&st_r, DMA_RESV_USAGE_READ, &n,
				&v) == 0) {
				for (i = 0; i < n; i++)
					dma_fence_put(v[i]);
				kfree(v);
			}
			(void)dma_resv_test_signaled(&st_r,
			    DMA_RESV_USAGE_WRITE);
			break;
		default:
			if (dma_resv_get_fences_rcu(&st_r, &excl, &nsh,
				&sh) == 0) {
				for (i = 0; i < nsh; i++)
					dma_fence_put(sh[i]);
				kfree(sh);
				dma_fence_put(excl);
			}
			break;
		}
		st_iters[id]++;
		if (++round % 64 == 0)
			kpause("drm2str", false, 1, NULL);
	}
	atomic_inc_uint(&st_done);
	kthread_exit(0);
}

static void
test_resv_stress(void)
{
	lwp_t *l[4];
	unsigned i;
	int error;

	for (i = 0; i < 8; i++)
		st_ctx[i] = dma_fence_context_alloc(1);
	st_stop = st_done = st_added = st_restarts = 0;
	for (i = 0; i < 3; i++)
		st_iters[i] = 0;
	dma_resv_init(&st_r);
	error = kthread_create(PRI_NONE, KTHREAD_MPSAFE | KTHREAD_MUSTJOIN,
	    NULL, st_writer, NULL, &l[0], "drm2stw");
	CHECK(error == 0);
	for (i = 0; i < 3; i++) {
		error = kthread_create(PRI_NONE,
		    KTHREAD_MPSAFE | KTHREAD_MUSTJOIN, NULL, st_reader,
		    (void *)(uintptr_t)(i + 1), &l[i + 1], "drm2str");
		CHECK(error == 0);
	}
	kpause("drm2stress", false, hz * 3, NULL);
	st_stop = 1;
	for (i = 0; i < 4; i++)
		kthread_join(l[i]);
	CHECK(st_done == 4);
	CHECK(st_added > 100);
	CHECK(st_iters[0] > 50 && st_iters[1] > 50 && st_iters[2] > 50);
	printf("drm2-dma-resv stress: %u fences added, reader rounds %u/%u/%u, "
	    "%u restarts\n", st_added, st_iters[0], st_iters[1], st_iters[2],
	    st_restarts);
	dma_resv_fini(&st_r);
	CHECK(tf_live == 0);
}

/*
 * Stress for the 5.6 write path: dma_resv_add_excl_fence empties a shared
 * list while readers of the 5.6 API walk it.  Same checks as above.
 */
static volatile unsigned xs_iters[3], xs_added;

static void
xs_writer(void *arg __unused)
{
	struct dma_fence *f1, *f2, *e;
	unsigned i = 0;

	while (!st_stop) {
		f1 = tf_new(st_ctx[0], i + 1);
		f2 = tf_new(st_ctx[1], i + 1);
		e = tf_new(st_ctx[2], i + 1);
		dma_resv_lock(&st_r, NULL);
		if (dma_resv_reserve_shared(&st_r, 2) == 0) {
			dma_resv_add_shared_fence(&st_r, f1);
			dma_resv_add_shared_fence(&st_r, f2);
		}
		if (i % 5 == 4)
			dma_resv_add_excl_fence(&st_r, e);
		else
			dma_resv_add_excl_fence(&st_r, NULL);
		dma_resv_unlock(&st_r);
		dma_fence_signal(f1);
		dma_fence_signal(f2);
		dma_fence_put(f1);
		dma_fence_put(f2);
		dma_fence_put(e);
		xs_added++;
		i++;
		if (i % 16 == 0)
			kpause("drm2xsw", false, 1, NULL);
	}
	atomic_inc_uint(&st_done);
	kthread_exit(0);
}

static void
xs_reader(void *arg)
{
	unsigned id = (unsigned)(uintptr_t)arg - 1, round = 0, i, nsh;
	struct dma_fence *excl, **sh;

	KASSERT(id < 3);
	while (!st_stop) {
		if (id == 1) {
			(void)dma_resv_test_signaled_rcu(&st_r, true);
		} else if (dma_resv_get_fences_rcu(&st_r, &excl, &nsh, &sh)
		    == 0) {
			for (i = 0; i < nsh; i++)
				dma_fence_put(sh[i]);
			kfree(sh);
			dma_fence_put(excl);
		}
		xs_iters[id]++;
		if (++round % 256 == 0)
			kpause("drm2xsr", false, 1, NULL);
	}
	atomic_inc_uint(&st_done);
	kthread_exit(0);
}

static void
test_resv_excl_stress(void)
{
	lwp_t *l[4];
	unsigned i;
	int error;

	for (i = 0; i < 8; i++)
		st_ctx[i] = dma_fence_context_alloc(1);
	st_stop = st_done = 0;
	xs_added = 0;
	for (i = 0; i < 3; i++)
		xs_iters[i] = 0;
	dma_resv_init(&st_r);
	error = kthread_create(PRI_NONE, KTHREAD_MPSAFE | KTHREAD_MUSTJOIN,
	    NULL, xs_writer, NULL, &l[0], "drm2xsw");
	CHECK(error == 0);
	for (i = 0; i < 3; i++) {
		error = kthread_create(PRI_NONE,
		    KTHREAD_MPSAFE | KTHREAD_MUSTJOIN, NULL, xs_reader,
		    (void *)(uintptr_t)(i + 1), &l[i + 1], "drm2xsr");
		CHECK(error == 0);
	}
	kpause("drm2xstress", false, hz * 3, NULL);
	st_stop = 1;
	for (i = 0; i < 4; i++)
		kthread_join(l[i]);
	CHECK(st_done == 4);
	CHECK(xs_added > 100);
	CHECK(xs_iters[0] > 50 && xs_iters[1] > 50 && xs_iters[2] > 50);
	printf("drm2-dma-resv excl stress: %u rounds, reader rounds %u/%u/%u\n",
	    xs_added, xs_iters[0], xs_iters[1], xs_iters[2]);
	dma_resv_fini(&st_r);
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
	test_fence_names();
	test_fence_deadline();
	test_fence_container();
	test_resv_usage_filter();
	test_resv_reserve();
	test_resv_replace_rules();
	test_resv_shared_cow();
	test_resv_excl_cow();
	test_resv_unlocked_iter();
	test_resv_get_fences();
	test_resv_singleton();
	test_resv_wait_test();
	test_resv_deadline();
	test_resv_replace_fences();
	test_resv_legacy();
	test_chain();
	test_array_unwrap();
	test_resv_stress();
	test_resv_excl_stress();
	printf("drm2-dma-resv: %d checks, %d failed\n", t_run, t_fail);
	return t_fail;
}
