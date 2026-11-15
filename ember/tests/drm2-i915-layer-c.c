/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the parts of the Linux layer that the second batch of
 * i915 files of Linux 6.18 needs and that have behaviour of their own: the
 * sleeping protocol with wait queue entries (prepare_to_wait, finish_wait,
 * schedule, wait on a variable), 64 bit compare and swap, castable_to_type
 * with the layer pgoff_t.  Run inside drmkms618_test after drm2-i915-layer.c.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/systm.h>

#include <linux/overflow.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include <i915drm618_compat.h>

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

/* the wait on a variable of the tests uses a queue of its own */
static wait_queue_head_t t_vq;
#undef	__var_waitqueue
#define	__var_waitqueue(var)	(&t_vq)

static wait_queue_head_t t_q;
static volatile int t_up, t_flag, t_done;
static volatile long t_left;
static int t_var;
static volatile long t_vret;

/* a kernel thread that waits for t_flag the way Linux does: prepare_to_wait, test, schedule */
static void
t_waiter(void *cookie __unused)
{
	DEFINE_WAIT(w);

	t_up = 1;
	for (;;) {
		prepare_to_wait(&t_q, &w, TASK_UNINTERRUPTIBLE);
		if (t_flag)
			break;
		t_left = schedule_timeout(10 * hz);
	}
	finish_wait(&t_q, &w);
	t_done = 1;
	kthread_exit(0);
}

/* the same with ___wait_var_event on t_var */
static void
t_var_waiter(void *cookie __unused)
{

	t_up = 1;
	t_vret = ___wait_var_event(&t_var, t_var != 0, TASK_UNINTERRUPTIBLE, 0, 0,
	    t_left = schedule_timeout(10 * hz));
	t_done = 1;
	kthread_exit(0);
}

static int
t_start(void (*fn)(void *), const char *name, struct lwp **lp)
{

	t_up = t_flag = t_done = 0;
	t_left = 0;
	if (kthread_create(PRI_NONE, KTHREAD_MPSAFE | KTHREAD_MUSTJOIN, NULL,
	    fn, NULL, lp, "%s", name) != 0)
		return -1;
	while (!t_up)
		kpause("t618w", false, 1, NULL);
	kpause("t618w", false, hz / 20 + 1, NULL);	/* let it get to the sleep */
	return 0;
}

int	drm2_i915_layer_c_run(void);

int
drm2_i915_layer_c_run(void)
{
	wait_queue_head_t q1;
	struct lwp *l;
	unsigned start;
	long r;
	unsigned long long ull = 5;
	int i = 5;
	long lg = 5;
	uint64_t v;
	uint64_t o;

	t_run = t_fail = 0;
	init_waitqueue_head(&t_q);
	init_waitqueue_head(&t_vq);
	init_waitqueue_head(&q1);

	/* schedule and io_schedule_timeout: a wake before the sleep means no sleep */
	set_current_state(TASK_INTERRUPTIBLE);
	wake_up_process(current);
	start = getticks();
	schedule();
	CHECK(getticks() - start < (unsigned)hz);
	set_current_state(TASK_INTERRUPTIBLE);
	wake_up_process(current);
	r = io_schedule_timeout(5 * hz);
	CHECK(r > 4 * hz && getticks() - start < (unsigned)hz);
	/* state running: no sleep either */
	set_current_state(TASK_RUNNING);
	schedule();
	r = io_schedule_timeout(3 * hz);
	CHECK(r > 2 * hz);

	/* signal_pending_state: nothing pending, in any state */
	CHECK(!signal_pending_state(TASK_UNINTERRUPTIBLE, current));
	CHECK(!signal_pending_state(TASK_INTERRUPTIBLE, current));

	/* prepare_to_wait puts the entry on the queue once and sets the state */
	{
		DEFINE_WAIT(w);
		int n = 0;
		struct wait_queue_entry *e;

		CHECK(w.private == (void *)current && list_empty(&w.entry));
		prepare_to_wait(&q1, &w, TASK_UNINTERRUPTIBLE);
		prepare_to_wait(&q1, &w, TASK_UNINTERRUPTIBLE);
		list_for_each_entry(e, &q1.head, entry)
			n++;
		CHECK(n == 1);
		/* a wake-up wakes the task (the state goes back to running) and autoremove takes the entry off */
		wake_up(&q1);
		CHECK(list_empty(&q1.head));
		start = getticks();
		r = schedule_timeout(5 * hz);
		CHECK(r > 4 * hz && getticks() - start < (unsigned)hz);
		finish_wait(&q1, &w);
		CHECK(list_empty(&w.entry));
		/* finish_wait removes an entry that is still queued */
		prepare_to_wait(&q1, &w, TASK_INTERRUPTIBLE);
		CHECK(!list_empty(&q1.head));
		finish_wait(&q1, &w);
		CHECK(list_empty(&q1.head) && list_empty(&w.entry));
		/* an entry that is not on a queue: finish_wait does nothing more */
		finish_wait(&q1, &w);
		CHECK(list_empty(&q1.head));
		/* init_wait names the current task and autoremoves */
		init_wait(&w);
		CHECK(w.private == (void *)current && w.func == autoremove_wake_function);
	}

	/* another thread in prepare_to_wait + schedule_timeout is woken by wake_up on the queue */
	CHECK(t_start(t_waiter, "t618a", &l) == 0);
	t_flag = 1;
	start = getticks();
	wake_up(&t_q);
	kthread_join(l);
	CHECK(t_done == 1 && t_left > 8 * hz && getticks() - start < (unsigned)hz);
	CHECK(list_empty(&t_q.head));

	/* ___wait_var_event: condition already true: no sleep, the given result */
	{
		long rr;

		t_var = 1;
		start = getticks();
		rr = ___wait_var_event(&t_var, t_var != 0, TASK_UNINTERRUPTIBLE, 0, 0,
		    schedule_timeout(hz));
		CHECK(rr == 0 && getticks() - start < (unsigned)(hz / 2));
		rr = ___wait_var_event(&t_var, t_var != 0, TASK_UNINTERRUPTIBLE, 0, 7,
		    schedule_timeout(hz));
		CHECK(rr == 7);
	}
	/* and woken by wake_up_all on the queue the variable maps to */
	t_var = 0;
	CHECK(t_start(t_var_waiter, "t618v", &l) == 0);
	t_var = 1;
	start = getticks();
	wake_up_all(&t_vq);
	kthread_join(l);
	CHECK(t_done == 1 && t_vret == 0 && t_left > 8 * hz && getticks() - start < (unsigned)hz);
	CHECK(list_empty(&t_vq.head));

	/* try_cmpxchg64: stores when equal, else reports what it found */
	v = 10;
	o = 10;
	CHECK(try_cmpxchg64(&v, &o, 11) && v == 11 && o == 10);
	o = 10;
	CHECK(!try_cmpxchg64(&v, &o, 12) && v == 11 && o == 11);
	v = 0x100000000ull;
	o = 0x100000000ull;
	CHECK(try_cmpxchg64(&v, &o, 0xffffffff00000001ull) && v == 0xffffffff00000001ull);
	CHECK(cmpxchg64(&v, 0xffffffff00000001ull, 3) == 0xffffffff00000001ull && v == 3);

	/* castable_to_type with pgoff_t: unsigned long wide, as Linux's pgoff_t */
	CHECK(castable_to_type(5, pgoff_t) && !castable_to_type(-1, pgoff_t));
	CHECK(castable_to_type((unsigned long)i, pgoff_t));
	CHECK(castable_to_type(ull, pgoff_t));
	CHECK(!castable_to_type(i, pgoff_t) && !castable_to_type(lg, pgoff_t));
	CHECK(castable_to_type(i, int) && !castable_to_type(lg, int));

	destroy_waitqueue_head(&q1);
	destroy_waitqueue_head(&t_vq);
	destroy_waitqueue_head(&t_q);
	if (t_fail != 0)
		printf("drm2-i915-layer-c: %d of %d checks FAILED\n", t_fail, t_run);
	else
		printf("drm2-i915-layer-c: %d checks, 0 failed\n", t_run);
	return t_fail;
}
