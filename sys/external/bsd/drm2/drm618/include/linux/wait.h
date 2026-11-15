/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the Linux
 * interface, no Linux code.
 *
 * Shadows the empty <linux/wait.h> of the shared layer for the Linux 6.18
 * DRM core only (drm618/include comes first in its include path).  The
 * 5.6 sources of dist/ (i915, qxl, via, virtio, amdkfd) name
 * wait_queue_head_t and wait_queue_entry_t themselves, with other layouts,
 * so the layer's header must stay empty for them.
 */
#ifndef _DRM618_LINUX_WAIT_H_
#define _DRM618_LINUX_WAIT_H_

/*
 * The Linux wait queue API the Linux 6.18 DRM core uses (wait_queue_head_t,
 * wait_event*, wake_up*), written to the interface on top of a spin lock and
 * a condition variable.  Only non-exclusive waiters exist, so every wake_up
 * wakes all waiters (as Linux does for them).
 *
 * seq counts the wake-ups.  A waiter reads it under the lock, then evaluates
 * the condition without the lock (it may take other locks or be slow) and
 * sleeps only if seq has not changed since, so a wake-up that follows the
 * change of the condition is never lost.  The condition is evaluated with
 * no lock of the queue held, except in the *_locked variants (the caller
 * holds wq.lock, as in Linux) and wait_event_lock_irq (the caller holds its
 * own lock, which is dropped while asleep).
 *
 * Returns follow Linux: wait_event_*timeout give the remaining ticks (at
 * least 1 when the condition holds), 0 when the time ran out with the
 * condition false; the interruptible forms give -ERESTARTSYS on a signal.
 * The old drm_waitqueue_t of the 5.6 core is unrelated and unchanged.
 */

#include <sys/param.h>
#include <sys/condvar.h>
#include <sys/kernel.h>
#include <sys/select.h>
#include <sys/selinfo.h>
#include <sys/systm.h>

#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/list.h>

/*
 * Entries with a function that a wake-up calls (the i915 sw fence puts its
 * own function on a queue, and a waiter may use autoremove_wake_function).
 * A sleeper in wait_event* is not an entry: it waits on the condition
 * variable of the queue, which every wake-up broadcasts.
 */
#define	WQ_FLAG_EXCLUSIVE	0x01
#define	WQ_FLAG_WOKEN		0x02
#define	WQ_FLAG_CUSTOM		0x04
#define	WQ_FLAG_DONE		0x08

struct wait_queue_entry;
typedef int (*wait_queue_func_t)(struct wait_queue_entry *, unsigned int,
    int, void *);

struct wait_queue_entry {
	unsigned int		flags;
	void			*private;
	wait_queue_func_t	func;
	struct list_head	entry;
};
typedef struct wait_queue_entry wait_queue_entry_t;

typedef struct wait_queue_head {
	spinlock_t	lock;
	kcondvar_t	cv;
	unsigned long	seq;
	struct selinfo	wq_sel;		/* poll_wait (linux/poll.h) */
	struct list_head head;		/* the entries (wait_queue_entry.entry) */
} wait_queue_head_t;

static inline void
init_waitqueue_head(wait_queue_head_t *wq)
{

	spin_lock_init(&wq->lock);
	cv_init(&wq->cv, "lnxwq");
	selinit(&wq->wq_sel);
	wq->seq = 0;
	INIT_LIST_HEAD(&wq->head);
}

static inline void
destroy_waitqueue_head(wait_queue_head_t *wq)
{

	KASSERT(list_empty(&wq->head));
	seldestroy(&wq->wq_sel);
	cv_destroy(&wq->cv);
	spin_lock_destroy(&wq->lock);
}

/* True if somebody sleeps on the queue (a hint, as in Linux). */
static inline bool
waitqueue_active(wait_queue_head_t *wq)
{

	return cv_has_waiters(&wq->cv);
}

static inline int
default_wake_function(struct wait_queue_entry *entry __unused,
    unsigned int mode __unused, int flags __unused, void *key __unused)
{

	/* the task of an entry sleeps on the condition variable: the broadcast wakes it */
	return 1;
}

static inline int
autoremove_wake_function(struct wait_queue_entry *entry, unsigned int mode,
    int flags, void *key)
{
	int ret = default_wake_function(entry, mode, flags, key);

	if (ret)
		list_del_init(&entry->entry);
	return ret;
}

static inline int
woken_wake_function(struct wait_queue_entry *entry, unsigned int mode,
    int flags, void *key)
{

	entry->flags |= WQ_FLAG_WOKEN;
	return default_wake_function(entry, mode, flags, key);
}

static inline void
init_waitqueue_func_entry(struct wait_queue_entry *entry,
    wait_queue_func_t func)
{

	entry->flags = 0;
	entry->private = NULL;
	entry->func = func;
}

static inline void
init_waitqueue_entry(struct wait_queue_entry *entry, void *task)
{

	entry->flags = 0;
	entry->private = task;
	entry->func = default_wake_function;
}

#define	DEFINE_WAIT_FUNC(name, function)				      \
	struct wait_queue_entry name = {				      \
		.private = NULL,					      \
		.func = (function),					      \
		.entry = LIST_HEAD_INIT((name).entry),			      \
	}
#define	DEFINE_WAIT(name)	DEFINE_WAIT_FUNC(name, autoremove_wake_function)

/* the caller holds the lock of the queue */
static inline void
__add_wait_queue(wait_queue_head_t *wq, struct wait_queue_entry *entry)
{

	list_add(&entry->entry, &wq->head);
}

static inline void
__add_wait_queue_entry_tail(wait_queue_head_t *wq,
    struct wait_queue_entry *entry)
{

	list_add_tail(&entry->entry, &wq->head);
}

static inline void
__add_wait_queue_exclusive(wait_queue_head_t *wq,
    struct wait_queue_entry *entry)
{

	entry->flags |= WQ_FLAG_EXCLUSIVE;
	__add_wait_queue_entry_tail(wq, entry);
}

static inline void
__remove_wait_queue(wait_queue_head_t *wq __unused,
    struct wait_queue_entry *entry)
{

	list_del(&entry->entry);
}

static inline void
add_wait_queue(wait_queue_head_t *wq, struct wait_queue_entry *entry)
{

	entry->flags &= ~WQ_FLAG_EXCLUSIVE;
	spin_lock(&wq->lock);
	__add_wait_queue(wq, entry);
	spin_unlock(&wq->lock);
}

static inline void
add_wait_queue_exclusive(wait_queue_head_t *wq, struct wait_queue_entry *entry)
{

	spin_lock(&wq->lock);
	__add_wait_queue_exclusive(wq, entry);
	spin_unlock(&wq->lock);
}

static inline void
remove_wait_queue(wait_queue_head_t *wq, struct wait_queue_entry *entry)
{

	spin_lock(&wq->lock);
	__remove_wait_queue(wq, entry);
	spin_unlock(&wq->lock);
}

/*
 * Call the functions of the entries in order.  An entry may remove itself.
 * A function that returns a negative number stops the walk; an exclusive
 * entry whose function says it woke its task counts against NR_EXCLUSIVE
 * (0 wakes every entry).  The caller holds the lock of the queue.
 */
static inline void
__wake_up_common(wait_queue_head_t *wq, unsigned int mode, int nr_exclusive,
    int wake_flags, void *key)
{
	struct wait_queue_entry *curr, *next;

	KASSERT(spin_is_locked(&wq->lock));
	list_for_each_entry_safe(curr, next, &wq->head, entry) {
		unsigned int flags = curr->flags;
		int ret = curr->func(curr, mode, wake_flags, key);

		if (ret < 0)
			break;
		if (ret && (flags & WQ_FLAG_EXCLUSIVE) && !--nr_exclusive)
			break;
	}
}

static inline void
wake_up_all_locked(wait_queue_head_t *wq)
{

	KASSERT(spin_is_locked(&wq->lock));
	wq->seq++;
	cv_broadcast(&wq->cv);
	selnotify(&wq->wq_sel, 0, 0);
	__wake_up_common(wq, 0, 0, 0, NULL);
}
#define	wake_up_locked		wake_up_all_locked

/* wake the sleepers, and call the entries: NR_EXCLUSIVE exclusive ones at most (0: all) */
static inline void
__wake_up(wait_queue_head_t *wq, unsigned int mode, int nr_exclusive,
    void *key)
{

	spin_lock(&wq->lock);
	wq->seq++;
	cv_broadcast(&wq->cv);
	selnotify(&wq->wq_sel, 0, 0);
	__wake_up_common(wq, mode, nr_exclusive, 0, key);
	spin_unlock(&wq->lock);
}

static inline void
wake_up_all(wait_queue_head_t *wq)
{

	__wake_up(wq, 0, 0, NULL);
}
#define	wake_up(wq)			__wake_up(wq, 0, 1, NULL)
#define	wake_up_interruptible(wq)	__wake_up(wq, 0, 1, NULL)
#define	wake_up_interruptible_all	wake_up_all
#define	wake_up_poll(wq, key)			wake_up_all(wq)
#define	wake_up_interruptible_poll(wq, key)	wake_up_all(wq)

#define	__lnx_wait_event(WQ, COND, TICKS, SIG, ULOCK)			      \
({									      \
	wait_queue_head_t *__lw_wq = &(WQ);				      \
	spinlock_t *__lw_ul = (ULOCK);					      \
	long __lw_ticks = (TICKS);					      \
	unsigned __lw_start = getticks();				      \
	unsigned long __lw_seq;						      \
	long __lw_ret = 0, __lw_left;					      \
	int __lw_err;							      \
									      \
	for (;;) {							      \
		spin_lock(&__lw_wq->lock);				      \
		__lw_seq = __lw_wq->seq;				      \
		spin_unlock(&__lw_wq->lock);				      \
		if (COND) {						      \
			__lw_ret = 0;					      \
			if (__lw_ticks >= 0) {				      \
				__lw_left = __lw_ticks -		      \
				    (long)(getticks() - __lw_start);	      \
				__lw_ret = __lw_left > 0 ? __lw_left : 1;     \
			}						      \
			break;						      \
		}							      \
		__lw_left = 0;						      \
		if (__lw_ticks >= 0) {					      \
			__lw_left = __lw_ticks -			      \
			    (long)(getticks() - __lw_start);		      \
			if (__lw_left <= 0) {				      \
				__lw_ret = 0;				      \
				break;					      \
			}						      \
		}							      \
		if (__lw_ul != NULL)					      \
			spin_unlock(__lw_ul);				      \
		spin_lock(&__lw_wq->lock);				      \
		__lw_err = 0;						      \
		if (__lw_wq->seq == __lw_seq) {				      \
			if (__lw_ticks >= 0) {				      \
				__lw_err = (SIG) ?			      \
				    cv_timedwait_sig(&__lw_wq->cv,	      \
				        &__lw_wq->lock.sl_lock,		      \
				        MIN(__lw_left, INT_MAX/2)) :	      \
				    cv_timedwait(&__lw_wq->cv,		      \
				        &__lw_wq->lock.sl_lock,		      \
				        MIN(__lw_left, INT_MAX/2));	      \
			} else {					      \
				__lw_err = (SIG) ?			      \
				    cv_wait_sig(&__lw_wq->cv,		      \
				        &__lw_wq->lock.sl_lock) :	      \
				    (cv_wait(&__lw_wq->cv,		      \
				        &__lw_wq->lock.sl_lock), 0);	      \
			}						      \
		}							      \
		spin_unlock(&__lw_wq->lock);				      \
		if (__lw_ul != NULL)					      \
			spin_lock(__lw_ul);				      \
		if (__lw_err != 0 && __lw_err != EWOULDBLOCK) {		      \
			__lw_ret = -ERESTARTSYS;			      \
			break;						      \
		}							      \
	}								      \
	__lw_ret;							      \
})

#define	wait_event(WQ, COND)						      \
	((void)__lnx_wait_event(WQ, COND, -1, false, NULL))
/*
 * Killable: Linux wakes a killable sleep for SIGKILL only.  NetBSD's
 * cv_wait_sig wakes for any signal, so a killable wait here is as
 * interruptible as wait_event_interruptible (a superset: callers handle the
 * error as a kill).
 */
#define	wait_event_killable(WQ, COND)					      \
	((int)__lnx_wait_event(WQ, COND, -1, true, NULL))
#define	wait_event_interruptible(WQ, COND)				      \
	((int)__lnx_wait_event(WQ, COND, -1, true, NULL))
#define	wait_event_timeout(WQ, COND, TICKS)				      \
	__lnx_wait_event(WQ, COND, ((TICKS) < 0 ? 0 : (TICKS)), false, NULL)
#define	wait_event_interruptible_timeout(WQ, COND, TICKS)		      \
	__lnx_wait_event(WQ, COND, ((TICKS) < 0 ? 0 : (TICKS)), true, NULL)

/*
 * The caller holds LOCK (a spinlock_t, named as in Linux) and the condition
 * is evaluated with it held; LOCK is dropped while asleep and held again on
 * return.  The check of seq is taken before the unlock, so the wake-up of a
 * waker that holds LOCK while changing the condition is not lost.
 */
#define	wait_event_lock_irq(WQ, COND, LOCK)				      \
	((void)__lnx_wait_event(WQ, COND, -1, false, &(LOCK)))
#define	wait_event_interruptible_lock_irq(WQ, COND, LOCK)		      \
	((int)__lnx_wait_event(WQ, COND, -1, true, &(LOCK)))

/* The caller holds WQ.lock; the condition is evaluated under it. */
#define	wait_event_interruptible_locked(WQ, COND)			      \
({									      \
	wait_queue_head_t *__lwl_wq = &(WQ);				      \
	int __lwl_ret = 0, __lwl_err;					      \
									      \
	KASSERT(spin_is_locked(&__lwl_wq->lock));			      \
	while (!(COND)) {						      \
		__lwl_err = cv_wait_sig(&__lwl_wq->cv,			      \
		    &__lwl_wq->lock.sl_lock);				      \
		if (__lwl_err != 0) {					      \
			__lwl_ret = -ERESTARTSYS;			      \
			break;						      \
		}							      \
	}								      \
	__lwl_ret;							      \
})
#define	wait_event_locked(WQ, COND)					      \
({									      \
	wait_queue_head_t *__lwl_wq = &(WQ);				      \
									      \
	KASSERT(spin_is_locked(&__lwl_wq->lock));			      \
	while (!(COND))							      \
		cv_wait(&__lwl_wq->cv, &__lwl_wq->lock.sl_lock);	      \
})


#endif
