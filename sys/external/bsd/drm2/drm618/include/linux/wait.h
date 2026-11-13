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
#include <sys/systm.h>

#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/types.h>

typedef struct wait_queue_head {
	spinlock_t	lock;
	kcondvar_t	cv;
	unsigned long	seq;
} wait_queue_head_t;

static inline void
init_waitqueue_head(wait_queue_head_t *wq)
{

	spin_lock_init(&wq->lock);
	cv_init(&wq->cv, "lnxwq");
	wq->seq = 0;
}

static inline void
destroy_waitqueue_head(wait_queue_head_t *wq)
{

	cv_destroy(&wq->cv);
	spin_lock_destroy(&wq->lock);
}

/* True if somebody sleeps on the queue (a hint, as in Linux). */
static inline bool
waitqueue_active(wait_queue_head_t *wq)
{

	return cv_has_waiters(&wq->cv);
}

static inline void
wake_up_all_locked(wait_queue_head_t *wq)
{

	KASSERT(spin_is_locked(&wq->lock));
	wq->seq++;
	cv_broadcast(&wq->cv);
}
#define	wake_up_locked		wake_up_all_locked

static inline void
wake_up_all(wait_queue_head_t *wq)
{

	spin_lock(&wq->lock);
	wake_up_all_locked(wq);
	spin_unlock(&wq->lock);
}
#define	wake_up			wake_up_all
#define	wake_up_interruptible	wake_up_all
#define	wake_up_interruptible_all	wake_up_all
#define	wake_up_poll(wq, key)			wake_up_all(wq)
#define	wake_up_interruptible_poll(wq, key)	wake_up_all(wq)

/*
 * __lnx_wait_event(WQ, COND, TICKS, SIG, ULOCK)
 *
 *	TICKS < 0: no timeout, result 0 or -ERESTARTSYS.  Otherwise the
 *	result is as for wait_event_timeout.  SIG: sleep interruptibly.
 *	ULOCK: spinlock_t * the caller holds (dropped while asleep) or NULL.
 */
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
