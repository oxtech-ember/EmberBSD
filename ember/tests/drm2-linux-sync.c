/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the wait queue, raw spinlock and completion
 * parts of the drm2 Linux layer that the Linux 6.18 DRM core needs
 * (run by sys/modules/drmkms_linux_test).  The wait queue is the Linux
 * API (wait_queue_head_t, wait_event*, wake_up*) on top of a spin lock
 * and a condition variable.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/kthread.h>
#include <sys/lwp.h>
#include <sys/systm.h>

#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

static void
test_raw_spinlock(void)
{
	raw_spinlock_t l;
	unsigned long flags = 1;
	int n = 0;

	raw_spin_lock_init(&l);
	raw_spin_lock(&l);
	n++;
	raw_spin_unlock(&l);
	raw_spin_lock_irqsave(&l, flags);
	n++;
	raw_spin_unlock_irqrestore(&l, flags);
	CHECK(raw_spin_trylock_irqsave(&l, flags));
	raw_spin_unlock_irqrestore(&l, flags);
	CHECK(raw_spin_trylock(&l));
	raw_spin_unlock(&l);
	CHECK(n == 2);
	raw_spin_lock_destroy(&l);
}

/* Single-threaded semantics of the wait_event family. */

static void
test_wait_event_basic(void)
{
	wait_queue_head_t wq;
	volatile int cond = 0;
	long r;
	unsigned t0, dt;

	init_waitqueue_head(&wq);
	CHECK(!waitqueue_active(&wq));

	/* True condition: immediate. */
	cond = 1;
	wait_event(wq, cond);
	CHECK(wait_event_interruptible(wq, cond) == 0);
	r = wait_event_timeout(wq, cond, 5);
	CHECK(r >= 1 && r <= 5);
	r = wait_event_interruptible_timeout(wq, cond, 5);
	CHECK(r >= 1 && r <= 5);

	/* A timeout that is already due, condition true: 1, like Linux. */
	r = wait_event_timeout(wq, cond, 0);
	CHECK(r == 1);

	/* False condition: times out after at least that many ticks. */
	cond = 0;
	t0 = getticks();
	r = wait_event_timeout(wq, cond, 3);
	dt = getticks() - t0;
	CHECK(r == 0);
	CHECK(dt >= 3);
	r = wait_event_interruptible_timeout(wq, cond, 2);
	CHECK(r == 0);
	r = wait_event_timeout(wq, cond, 0);
	CHECK(r == 0);

	/* wake_up with nobody waiting is harmless. */
	wake_up(&wq);
	wake_up_all(&wq);
	wake_up_interruptible(&wq);
	spin_lock(&wq.lock);
	wake_up_all_locked(&wq);
	spin_unlock(&wq.lock);
}

struct wk {
	wait_queue_head_t	wq;
	volatile int		cond;
	volatile unsigned	woken;
	volatile unsigned	ready;
	spinlock_t		lock;
};

static void
wk_waiter(void *arg)
{
	struct wk *w = arg;
	long r;

	atomic_inc_uint(&w->ready);
	r = wait_event_timeout(w->wq, w->cond != 0, hz * 5);
	if (r > 0)
		atomic_inc_uint(&w->woken);
	kthread_exit(0);
}

static void
wk_waiter_lock_irq(void *arg)
{
	struct wk *w = arg;

	spin_lock(&w->lock);
	atomic_inc_uint(&w->ready);
	wait_event_lock_irq(w->wq, w->cond != 0, w->lock);
	/* the lock is held again here */
	KASSERT(spin_is_locked(&w->lock));
	spin_unlock(&w->lock);
	atomic_inc_uint(&w->woken);
	kthread_exit(0);
}

static void
wk_waiter_locked(void *arg)
{
	struct wk *w = arg;
	int error;

	spin_lock(&w->wq.lock);
	atomic_inc_uint(&w->ready);
	error = wait_event_interruptible_locked(w->wq, w->cond != 0);
	KASSERT(spin_is_locked(&w->wq.lock));
	spin_unlock(&w->wq.lock);
	if (error == 0)
		atomic_inc_uint(&w->woken);
	kthread_exit(0);
}

static void
wk_start(struct wk *w)
{

	init_waitqueue_head(&w->wq);
	spin_lock_init(&w->lock);
	w->cond = 0;
	w->woken = 0;
	w->ready = 0;
}

static void
wk_wait_ready(struct wk *w, unsigned n)
{
	unsigned i;

	for (i = 0; i < 500 && w->ready < n; i++)
		kpause("drm2wk", false, 1, NULL);
	/* let the waiters reach the sleep */
	kpause("drm2wk", false, hz / 10 + 1, NULL);
}

static void
test_wait_event_wake(void)
{
	struct wk w;
	lwp_t *l[3];
	unsigned i, t0;
	int error;

	/* wake_up_all wakes all three waiters, cond set before the wake. */
	wk_start(&w);
	for (i = 0; i < 3; i++) {
		error = kthread_create(PRI_NONE,
		    KTHREAD_MPSAFE | KTHREAD_MUSTJOIN, NULL, wk_waiter, &w,
		    &l[i], "drm2wkw");
		CHECK(error == 0);
	}
	wk_wait_ready(&w, 3);
	CHECK(waitqueue_active(&w.wq));
	CHECK(w.woken == 0);
	t0 = getticks();
	w.cond = 1;
	wake_up_all(&w.wq);
	for (i = 0; i < 3; i++)
		kthread_join(l[i]);
	CHECK(w.woken == 3);
	CHECK(getticks() - t0 < hz);	/* woken, not timed out */
	CHECK(!waitqueue_active(&w.wq));

	/* wait_event_lock_irq: the caller's lock is dropped while asleep. */
	wk_start(&w);
	error = kthread_create(PRI_NONE, KTHREAD_MPSAFE | KTHREAD_MUSTJOIN,
	    NULL, wk_waiter_lock_irq, &w, &l[0], "drm2wkl");
	CHECK(error == 0);
	wk_wait_ready(&w, 1);
	spin_lock(&w.lock);		/* succeeds only if the waiter let go */
	w.cond = 1;
	wake_up_all(&w.wq);
	spin_unlock(&w.lock);
	kthread_join(l[0]);
	CHECK(w.woken == 1);

	/* wait_event_interruptible_locked: wq.lock held by caller. */
	wk_start(&w);
	error = kthread_create(PRI_NONE, KTHREAD_MPSAFE | KTHREAD_MUSTJOIN,
	    NULL, wk_waiter_locked, &w, &l[0], "drm2wkq");
	CHECK(error == 0);
	wk_wait_ready(&w, 1);
	spin_lock(&w.wq.lock);
	w.cond = 1;
	wake_up_all_locked(&w.wq);
	spin_unlock(&w.wq.lock);
	kthread_join(l[0]);
	CHECK(w.woken == 1);

	/* A wake-up with the condition still false does not end the wait. */
	wk_start(&w);
	error = kthread_create(PRI_NONE, KTHREAD_MPSAFE | KTHREAD_MUSTJOIN,
	    NULL, wk_waiter, &w, &l[0], "drm2wkw");
	CHECK(error == 0);
	wk_wait_ready(&w, 1);
	wake_up(&w.wq);
	kpause("drm2wk", false, hz / 10 + 1, NULL);
	CHECK(w.woken == 0);
	w.cond = 1;
	wake_up(&w.wq);
	kthread_join(l[0]);
	CHECK(w.woken == 1);
}

static void
test_completion_618(void)
{
	struct completion c;
	unsigned long r;

	init_completion(&c);
	CHECK(!try_wait_for_completion(&c));
	r = wait_for_completion_timeout(&c, 2);
	CHECK(r == 0);
	complete_all(&c);
	CHECK(try_wait_for_completion(&c));
	r = wait_for_completion_timeout(&c, 2);
	CHECK(r >= 1);
	CHECK(wait_for_completion_interruptible(&c) == 0);
	r = wait_for_completion_interruptible_timeout(&c, 2);
	CHECK(r >= 1);
	reinit_completion(&c);
	CHECK(!try_wait_for_completion(&c));
	destroy_completion(&c);
}

/*
 * kthread_worker: one thread that runs queued works in order.
 */

struct tw {
	struct kthread_work	work;
	struct tw_log		*log;
	int			id;
	int			sleep_ticks;	/* sleep inside the work */
	volatile int		started;
	volatile int		runs;
	int			requeue;	/* queue itself again this often */
	struct kthread_worker	*worker;
};

struct tw_log {
	volatile unsigned	n;
	int			order[16];
};

static void
tw_func(struct kthread_work *work)
{
	struct tw *w = container_of(work, struct tw, work);

	w->started = 1;
	if (w->sleep_ticks != 0)
		kpause("drm2twk", false, w->sleep_ticks, NULL);
	if (w->log != NULL && w->log->n < 16)
		w->log->order[w->log->n++] = w->id;
	w->runs++;
	if (w->requeue > 0) {
		w->requeue--;
		(void)kthread_queue_work(w->worker, &w->work);
	}
}

static void
tw_init(struct tw *w, struct tw_log *log, int id, struct kthread_worker *kw)
{

	memset(w, 0, sizeof(*w));
	kthread_init_work(&w->work, tw_func);
	w->log = log;
	w->id = id;
	w->worker = kw;
}

static void
test_kthread_worker(void)
{
	struct kthread_worker *kw;
	struct tw a, b, c;
	struct tw_log log;

	memset(&log, 0, sizeof(log));
	kw = kthread_run_worker(0, "drm2tw%d", 7);
	CHECK(!IS_ERR(kw) && kw != NULL);
	if (IS_ERR(kw) || kw == NULL)
		return;
	/* sched_set_fifo: the worker's thread gets a kernel real time priority. */
	sched_set_fifo(NULL);
	CHECK(kw->task != NULL);
	{
		struct lwp *l = (struct lwp *)kw->task;
		pri_t before, after;

		lwp_lock(l);
		before = l->l_priority;
		lwp_unlock(l);
		sched_set_fifo(kw->task);
		lwp_lock(l);
		after = l->l_priority;
		lwp_unlock(l);
		CHECK(before < PRI_KERNEL_RT);
		CHECK(after == PRI_KERNEL_RT);
	}

	tw_init(&a, &log, 1, kw);
	tw_init(&b, &log, 2, kw);
	tw_init(&c, &log, 3, kw);

	/* Works run in the order queued, once each. */
	CHECK(kthread_queue_work(kw, &a.work));
	CHECK(kthread_queue_work(kw, &b.work));
	CHECK(kthread_queue_work(kw, &c.work));
	kthread_flush_worker(kw);
	CHECK(log.n == 3 && log.order[0] == 1 && log.order[1] == 2 &&
	    log.order[2] == 3);
	CHECK(a.runs == 1 && b.runs == 1 && c.runs == 1);

	/* Queueing a work that is queued already does nothing (false). */
	a.sleep_ticks = hz / 5 + 1;
	CHECK(kthread_queue_work(kw, &a.work));	/* runs and sleeps */
	while (!a.started)
		kpause("drm2tws", false, 1, NULL);
	CHECK(kthread_queue_work(kw, &b.work));
	CHECK(!kthread_queue_work(kw, &b.work));	/* b waits behind a */
	kthread_flush_work(&b.work);
	CHECK(a.runs == 2 && b.runs == 2);	/* a ran before b */

	/* A work running can be queued again (it runs twice, not at once). */
	a.sleep_ticks = 0;
	a.requeue = 2;
	a.runs = 0;
	CHECK(kthread_queue_work(kw, &a.work));
	kthread_flush_worker(kw);
	CHECK(a.runs == 3);

	/* flush_work waits for a running work and returns when it is done. */
	c.sleep_ticks = hz / 5 + 1;
	c.started = 0;
	c.runs = 0;
	CHECK(kthread_queue_work(kw, &c.work));
	while (!c.started)
		kpause("drm2twf", false, 1, NULL);
	kthread_flush_work(&c.work);
	CHECK(c.runs == 1);
	kthread_flush_work(&c.work);	/* idle: returns at once */

	/* cancel_work_sync: pending work is removed (true), idle one is not (false). */
	b.sleep_ticks = 0;
	b.runs = 0;
	c.sleep_ticks = hz / 5 + 1;
	c.started = 0;
	c.runs = 0;
	CHECK(kthread_queue_work(kw, &c.work));
	while (!c.started)
		kpause("drm2twc", false, 1, NULL);
	CHECK(kthread_queue_work(kw, &b.work));	/* waits behind c */
	CHECK(kthread_cancel_work_sync(&b.work));	/* was pending */
	CHECK(!kthread_cancel_work_sync(&b.work));	/* not any more */
	kthread_flush_worker(kw);
	CHECK(b.runs == 0 && c.runs == 1);

	/* cancel_work_sync of a running work waits for it, returns false. */
	c.started = 0;
	c.runs = 0;
	CHECK(kthread_queue_work(kw, &c.work));
	while (!c.started)
		kpause("drm2twr", false, 1, NULL);
	CHECK(!kthread_cancel_work_sync(&c.work));
	CHECK(c.runs == 1);			/* done by the time it returns */

	/* The worker is still usable, and goes away with its works done. */
	b.runs = 0;
	CHECK(kthread_queue_work(kw, &b.work));
	kthread_flush_worker(kw);
	CHECK(b.runs == 1);
	kthread_destroy_worker(kw);
}

int	drm2_linux_sync_run(void);

int
drm2_linux_sync_run(void)
{

	t_run = t_fail = 0;
	test_raw_spinlock();
	test_wait_event_basic();
	test_wait_event_wake();
	test_completion_618();
	test_kthread_worker();
	printf("drm2-linux-sync: %d checks, %d failed\n", t_run, t_fail);
	return t_fail;
}
