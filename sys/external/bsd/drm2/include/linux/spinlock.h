/*	$NetBSD: spinlock.h,v 1.15 2022/10/25 23:33:29 riastradh Exp $	*/

/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Taylor R. Campbell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _LINUX_SPINLOCK_H_
#define _LINUX_SPINLOCK_H_

#include <sys/cdefs.h>
#include <sys/mutex.h>

#include <machine/limits.h>

#include <linux/atomic.h>
#include <linux/cleanup.h>
#include <linux/irqflags.h>
#include <linux/lockdep.h>
#include <linux/preempt.h>

typedef struct spinlock {
	kmutex_t sl_lock;
} spinlock_t;

static inline int
spin_is_locked(spinlock_t *spinlock)
{
	return mutex_owned(&spinlock->sl_lock);
}

static inline void
spin_lock(spinlock_t *spinlock)
{
	mutex_enter(&spinlock->sl_lock);
}

static inline void
spin_unlock(spinlock_t *spinlock)
{
	mutex_exit(&spinlock->sl_lock);
}

static inline void
spin_lock_bh(spinlock_t *spinlock)
{
	spin_lock(spinlock);
}

static inline void
spin_unlock_bh(spinlock_t *spinlock)
{
	spin_unlock(spinlock);
}

static inline void
spin_lock_irq(spinlock_t *spinlock)
{
	spin_lock(spinlock);
}

static inline void
spin_unlock_irq(spinlock_t *spinlock)
{
	spin_unlock(spinlock);
}

/* Must be a macro because the second argument is to be assigned.  */
#define	spin_lock_irqsave(SPINLOCK, FLAGS)				\
	do {								\
		(FLAGS) = 0;						\
		mutex_enter(&((spinlock_t *)(SPINLOCK))->sl_lock);	\
	} while (0)

#define	spin_trylock_irqsave(SPINLOCK, FLAGS)				\
		( (FLAGS) = 0,						\
		mutex_tryenter(&((spinlock_t *)(SPINLOCK))->sl_lock) )

static inline void
spin_unlock_irqrestore(spinlock_t *spinlock, unsigned long __unused flags)
{
	mutex_exit(&spinlock->sl_lock);
}

static inline void
spin_lock_nested(spinlock_t *spinlock, int subclass)
{
	spin_lock(spinlock);
}

#define	spin_lock_irqsave_nested(SPINLOCK, FLAGS, SUBCLASS)		      \
	spin_lock_irqsave(SPINLOCK, FLAGS)

static inline void
spin_lock_init(spinlock_t *spinlock)
{
	/* XXX What's the right IPL?  IPL_DRM...?  */
	mutex_init(&spinlock->sl_lock, MUTEX_DEFAULT, IPL_VM);
}

/*
 * XXX Linux doesn't ever destroy spin locks, it seems.  We'll have to
 * kludge it up.
 */

static inline void
spin_lock_destroy(spinlock_t *spinlock)
{
	mutex_destroy(&spinlock->sl_lock);
}

/* This is a macro to make the panic message clearer.  */
#define	assert_spin_locked(spinlock)	\
	KASSERT(mutex_owned(&(spinlock)->sl_lock))

/*
 * Stupid reader/writer spin locks.  No attempt to avoid writer
 * starvation.  Must allow recursive readers.  We use mutex and state
 * instead of compare-and-swap for expedience and LOCKDEBUG support.
 */

typedef struct linux_rwlock {
	kmutex_t	rw_lock;
	unsigned	rw_nreaders;
} rwlock_t;

static inline void
rwlock_init(rwlock_t *rw)
{

	mutex_init(&rw->rw_lock, MUTEX_DEFAULT, IPL_VM);
	rw->rw_nreaders = 0;
}

static inline void
rwlock_destroy(rwlock_t *rw)
{

	KASSERTMSG(rw->rw_nreaders == 0,
	    "rwlock still held by %u readers", rw->rw_nreaders);
	mutex_destroy(&rw->rw_lock);
}

static inline void
write_lock_irq(rwlock_t *rw)
{

	for (;;) {
		mutex_spin_enter(&rw->rw_lock);
		if (rw->rw_nreaders == 0)
			break;
		mutex_spin_exit(&rw->rw_lock);
	}
}

static inline void
write_unlock_irq(rwlock_t *rw)
{

	KASSERT(rw->rw_nreaders == 0);
	mutex_spin_exit(&rw->rw_lock);
}

/* The spin lock is taken at IPL_VM either way: the plain forms are the same. */
static inline void
write_lock(rwlock_t *rw)
{

	write_lock_irq(rw);
}

static inline void
write_unlock(rwlock_t *rw)
{

	write_unlock_irq(rw);
}

static inline void
read_lock(rwlock_t *rw)
{

	mutex_spin_enter(&rw->rw_lock);
	KASSERT(rw->rw_nreaders < UINT_MAX);
	rw->rw_nreaders++;
	mutex_spin_exit(&rw->rw_lock);
}

static inline void
read_unlock(rwlock_t *rw)
{

	mutex_spin_enter(&rw->rw_lock);
	KASSERT(0 < rw->rw_nreaders);
	rw->rw_nreaders--;
	mutex_spin_exit(&rw->rw_lock);
}

static inline void
local_bh_disable(void)
{
}

static inline void
local_bh_enable(void)
{
}

#define	atomic_dec_and_lock_irqsave(A, L, F)				      \
	_atomic_dec_and_lock_irqsave(A, L, &(F))

static inline bool __must_check
_atomic_dec_and_lock_irqsave(atomic_t *atomic, spinlock_t *lock,
    unsigned long *flagsp)
{
	unsigned old, new;

	do {
		old = atomic_read(atomic);
		KASSERT(old);
		if (old == 1) {
			spin_lock_irqsave(lock, *flagsp);
			if (atomic_dec_return(atomic) == 0)
				return true;
			spin_unlock_irqrestore(lock, *flagsp);
			return false;
		}
		new = old - 1;
	} while (atomic_cmpxchg(atomic, old, new) != old);

	KASSERT(old != 1);
	KASSERT(new != 0);
	return false;
}

/* guard(spinlock)(&l), scoped_guard(spinlock_irqsave, &l) and the like */
DEFINE_LOCK_GUARD_1(spinlock, spinlock_t,
    spin_lock(_T->lock),
    spin_unlock(_T->lock))

DEFINE_LOCK_GUARD_1(spinlock_irq, spinlock_t,
    spin_lock_irq(_T->lock),
    spin_unlock_irq(_T->lock))

DEFINE_LOCK_GUARD_1(spinlock_irqsave, spinlock_t,
    spin_lock_irqsave(_T->lock, _T->flags),
    spin_unlock_irqrestore(_T->lock, _T->flags),
    unsigned long flags)

/*
 * EmberBSD addition, AI-assisted.  raw_spinlock_t for
 * the Linux 6.18 DRM core (drm_mode_config.panic_lock): a spin lock that
 * may be taken from any context, hence IPL_HIGH.
 */

struct raw_spinlock {
	kmutex_t rsl_lock;
};
typedef struct raw_spinlock raw_spinlock_t;

static inline void
raw_spin_lock_init(raw_spinlock_t *l)
{
	mutex_init(&l->rsl_lock, MUTEX_DEFAULT, IPL_HIGH);
}

static inline void
raw_spin_lock_destroy(raw_spinlock_t *l)
{
	mutex_destroy(&l->rsl_lock);
}

static inline void
raw_spin_lock(raw_spinlock_t *l)
{
	mutex_enter(&l->rsl_lock);
}

static inline int
raw_spin_trylock(raw_spinlock_t *l)
{
	return mutex_tryenter(&l->rsl_lock);
}

static inline void
raw_spin_unlock(raw_spinlock_t *l)
{
	mutex_exit(&l->rsl_lock);
}

static inline void
raw_spin_lock_irq(raw_spinlock_t *l)
{
	mutex_enter(&l->rsl_lock);
}

static inline void
raw_spin_unlock_irq(raw_spinlock_t *l)
{
	mutex_exit(&l->rsl_lock);
}

#define	raw_spin_lock_irqsave(LOCK, FLAGS)				\
	do {								\
		(FLAGS) = 0;						\
		mutex_enter(&((raw_spinlock_t *)(LOCK))->rsl_lock);	\
	} while (0)

#define	raw_spin_trylock_irqsave(LOCK, FLAGS)				\
	( (FLAGS) = 0,							\
	  mutex_tryenter(&((raw_spinlock_t *)(LOCK))->rsl_lock) )

static inline void
raw_spin_unlock_irqrestore(raw_spinlock_t *l, unsigned long __unused flags)
{
	mutex_exit(&l->rsl_lock);
}


#endif  /* _LINUX_SPINLOCK_H_ */
