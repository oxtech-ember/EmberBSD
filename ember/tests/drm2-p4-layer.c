/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the layer additions that
 * the GPU scheduler and TTM of Linux 6.18 need: release/acquire accessors,
 * check_sub_overflow, rcu_dereference_check, wait_event_killable, the
 * *_enabled tracepoint test, delayed_work.timer.expires and the task view
 * (group_leader, flags, exit_code).
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/systm.h>

#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/device.h>
#include <linux/overflow.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/tracepoint.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

TRACE_EVENT(p4_test_event,
	TP_PROTO(int a),
	TP_ARGS(a),
	TP_STRUCT__entry(__field(int, a)),
	TP_fast_assign(__entry->a = a;),
	TP_printk("a=%d", __entry->a)
);

struct p4_obj {
	int		a;
	int		b;
};

static void
p4_noop(struct work_struct *w __unused)
{
}

int	drm2_p4_layer_run(void);

int
drm2_p4_layer_run(void)
{
	/* smp_store_release stores the value it was given, smp_load_acquire reads it */
	{
		unsigned long x = 1;
		int y = 7;
		struct p4_obj o = { 0, 0 };
		struct p4_obj *volatile p = NULL;

		smp_store_release(&x, 0x1234UL);
		CHECK(x == 0x1234UL);
		smp_store_release(&y, -5);
		CHECK(y == -5);
		CHECK(smp_load_acquire(&x) == 0x1234UL);
		smp_store_release(&p, &o);
		CHECK(smp_load_acquire(&p) == &o);
	}

	/* check_sub_overflow */
	{
		unsigned int u;
		int s;
		unsigned long ul;

		CHECK(!check_sub_overflow(10u, 3u, &u) && u == 7);
		CHECK(check_sub_overflow(3u, 10u, &u));
		CHECK(!check_sub_overflow(-5, 10, &s) && s == -15);
		CHECK(check_sub_overflow(INT_MIN, 1, &s));
		CHECK(!check_sub_overflow(5ul, 5ul, &ul) && ul == 0);
	}

	/* rcu_dereference_check names the protected pointer like Linux */
	{
		struct p4_obj o = { 1, 2 };
		struct p4_obj *p;

		rcu_assign_pointer(p, &o);
		CHECK(rcu_dereference_check(p, true) == &o);
		CHECK(rcu_dereference_check(p, lockdep_is_held(&o)) == &o);
	}

	/* wait_event_killable: a true condition returns 0 at once, a wake-up ends the wait */
	{
		wait_queue_head_t wq;
		int cond = 1;

		init_waitqueue_head(&wq);
		CHECK(wait_event_killable(wq, cond) == 0);
		destroy_waitqueue_head(&wq);
	}

	/* a TRACE_EVENT tracepoint is never enabled, and can be called */
	{
		trace_p4_test_event(3);
		CHECK(!trace_p4_test_event_enabled());
	}

	/* delayed_work.timer.expires: the jiffies at which the work runs */
	{
		struct workqueue_struct *wq = alloc_ordered_workqueue("p4dw", 0);
		struct delayed_work dw;
		long left;

		CHECK(wq != NULL);
		if (wq != NULL) {
			INIT_DELAYED_WORK(&dw, p4_noop);
			CHECK(queue_delayed_work(wq, &dw, 1000));
			left = (long)(dw.timer.expires - (unsigned long)getticks());
			CHECK(left > 900 && left <= 1000);
			/* mod_delayed_work moves it; the suspend value does not fire */
			CHECK(mod_delayed_work(wq, &dw, 2000));
			left = (long)(dw.timer.expires - (unsigned long)getticks());
			CHECK(left > 1900 && left <= 2000);
			CHECK(mod_delayed_work(wq, &dw, MAX_SCHEDULE_TIMEOUT));
			kpause("p4dw", false, hz / 10, NULL);
			CHECK(delayed_work_pending(&dw));
			CHECK(cancel_delayed_work_sync(&dw));
			CHECK(!delayed_work_pending(&dw));
			destroy_workqueue(wq);
		}
	}

	/* the task view: the group leader is stable, a live process does not exit */
	{
		struct task_struct *t = current;

		CHECK(t->group_leader != NULL);
		CHECK(t->group_leader == current->group_leader);
		CHECK((t->flags & PF_EXITING) == 0);
		CHECK(t->exit_code == 0);
	}

	/* atomic_long arithmetic (ttm_tt.c counts pages with it) */
	{
		atomic_long_t a;

		atomic_long_set(&a, 5);
		atomic_long_add(10, &a);
		CHECK(atomic_long_read(&a) == 15);
		atomic_long_sub(20, &a);
		CHECK(atomic_long_read(&a) == -5);
		CHECK(atomic_long_add_return(8, &a) == 3);
		CHECK(atomic_long_sub_return(1, &a) == 2);
		atomic_long_inc(&a);
		atomic_long_dec(&a);
		atomic_long_dec(&a);
		CHECK(atomic_long_read(&a) == 1);
	}

	/* dev_info_once names the message once (the second call must not print again) */
	{
		int i;

		for (i = 0; i < 2; i++)
			dev_info_once(NULL, "drm2-p4-layer: dev_info_once, printed once\n");
		CHECK(i == 2);
	}

	printf("drm2-p4-layer: %d checks, %d failed\n", t_run, t_fail);
	return t_fail;
}
