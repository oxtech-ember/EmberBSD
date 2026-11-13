/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the interface the
 * 6.18 DRM core uses (drm_syncobj.c), no Linux code.
 *
 * The sleeping protocol of a Linux task:
 *
 *	set_current_state(TASK_INTERRUPTIBLE);
 *	if (condition) break;
 *	timeout = schedule_timeout(timeout);
 *
 * and, from another context, wake_up_process(task).  A wake that comes
 * between the state change and the sleep is not lost: it puts the task back
 * to TASK_RUNNING and schedule_timeout then returns at once.
 *
 * A task here is a process (see linux/sched.h): the process pointer in
 * a struct task_struct * names every lwp of the process, and
 * wake_up_process wakes all those that are in the sleeping state.  The
 * state is kept per lwp.  The kernel threads share proc0: waking proc0
 * wakes all sleeping kernel threads of the core (they all check their
 * condition and sleep again).
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/condvar.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/lwp.h>
#include <sys/mutex.h>
#include <sys/once.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/specificdata.h>
#include <sys/systm.h>

#include <linux/sched.h>

struct sleeper {
	LIST_ENTRY(sleeper)	s_entry;
	struct proc		*s_proc;
	int			s_state;	/* TASK_RUNNING or a sleeping state */
};

static kmutex_t sleep_lock;
static kcondvar_t sleep_cv;
static LIST_HEAD(, sleeper) sleepers;
static specificdata_key_t sleep_key;
static ONCE_DECL(sleep_once);

static void
sleeper_dtor(void *p)
{
	struct sleeper *s = p;

	mutex_enter(&sleep_lock);
	LIST_REMOVE(s, s_entry);
	mutex_exit(&sleep_lock);
	kmem_free(s, sizeof(*s));
}

static int
sleep_init(void)
{
	int error;

	mutex_init(&sleep_lock, MUTEX_DEFAULT, IPL_NONE);
	cv_init(&sleep_cv, "lnxslp");
	error = lwp_specific_key_create(&sleep_key, sleeper_dtor);
	if (error) {
		cv_destroy(&sleep_cv);
		mutex_destroy(&sleep_lock);
	}
	return error;
}

static struct sleeper *
sleeper_get(void)
{
	struct sleeper *s;

	if (RUN_ONCE(&sleep_once, sleep_init) != 0)
		return NULL;
	s = lwp_getspecific(sleep_key);
	if (s != NULL)
		return s;
	s = kmem_zalloc(sizeof(*s), KM_SLEEP);
	s->s_proc = curproc;
	s->s_state = TASK_RUNNING;
	mutex_enter(&sleep_lock);
	LIST_INSERT_HEAD(&sleepers, s, s_entry);
	mutex_exit(&sleep_lock);
	lwp_setspecific(sleep_key, s);
	return s;
}

void
drm618_set_current_state(int state)
{
	struct sleeper *s = sleeper_get();

	if (s == NULL)
		return;
	mutex_enter(&sleep_lock);
	s->s_state = state;
	mutex_exit(&sleep_lock);
}

long
schedule_timeout(long timeout)
{
	struct sleeper *s = sleeper_get();
	unsigned start = getticks();
	bool infinite = (timeout >= MAX_SCHEDULE_TIMEOUT);
	long left = timeout;
	int error = 0;

	if (s == NULL)
		return 0;
	mutex_enter(&sleep_lock);
	while (s->s_state != TASK_RUNNING && error == 0) {
		if (infinite) {
			error = (s->s_state & TASK_INTERRUPTIBLE) ?
			    cv_wait_sig(&sleep_cv, &sleep_lock) :
			    (cv_wait(&sleep_cv, &sleep_lock), 0);
		} else {
			left = timeout - (long)(getticks() - start);
			if (left <= 0) {
				left = 0;
				break;
			}
			error = (s->s_state & TASK_INTERRUPTIBLE) ?
			    cv_timedwait_sig(&sleep_cv, &sleep_lock,
				MIN(left, INT_MAX / 2)) :
			    cv_timedwait(&sleep_cv, &sleep_lock,
				MIN(left, INT_MAX / 2));
			if (error == EWOULDBLOCK)
				error = 0;
		}
	}
	/* A signal ends the sleep too, with the time that is left. */
	s->s_state = TASK_RUNNING;
	mutex_exit(&sleep_lock);
	if (infinite)
		return MAX_SCHEDULE_TIMEOUT;
	left = timeout - (long)(getticks() - start);
	return left > 0 ? left : 0;
}

int
wake_up_process(struct task_struct *task)
{
	struct proc *p = (struct proc *)task;
	struct sleeper *s;
	int woke = 0;

	if (RUN_ONCE(&sleep_once, sleep_init) != 0)
		return 0;
	mutex_enter(&sleep_lock);
	LIST_FOREACH(s, &sleepers, s_entry) {
		if (s->s_proc == p && s->s_state != TASK_RUNNING) {
			s->s_state = TASK_RUNNING;
			woke = 1;
		}
	}
	if (woke)
		cv_broadcast(&sleep_cv);
	mutex_exit(&sleep_lock);
	return woke;
}

/*
 * Before the module goes: the per lwp records (and their destructor, which is
 * in this module) are destroyed with the key.
 */
void
drm618_sleep_fini(void)
{

	if (!sleep_once.o_status)
		return;
	lwp_specific_key_delete(sleep_key);
	cv_destroy(&sleep_cv);
	mutex_destroy(&sleep_lock);
}
