/*	$NetBSD: dma-fence.h,v 1.17 2022/09/01 01:54:38 riastradh Exp $	*/

/*-
 * Copyright (c) 2018 The NetBSD Foundation, Inc.
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

#ifndef	_LINUX_DMA_FENCE_H_
#define	_LINUX_DMA_FENCE_H_

#include <sys/types.h>
#include <sys/atomic.h>
#include <sys/condvar.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/queue.h>
#include <sys/systm.h>

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/kref.h>
#include <linux/ktime.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/spinlock.h>

struct dma_fence_cb;

struct dma_fence {
	struct kref			refcount;
	spinlock_t			*lock;
	volatile unsigned long		flags;
	uint64_t			context;
	uint64_t			seqno;
	const struct dma_fence_ops	*ops;
	int				error;
	ktime_t				timestamp;
	struct rcu_head			rcu;

	TAILQ_HEAD(, dma_fence_cb)	f_callbacks;
	kcondvar_t			f_cv;
	uint64_t			f_magic;
};

#define	DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT	0
#define	DMA_FENCE_FLAG_SIGNALED_BIT		1
#define	DMA_FENCE_FLAG_TIMESTAMP_BIT		2
#define	DMA_FENCE_FLAG_SEQNO64_BIT		3
#define	DMA_FENCE_FLAG_USER_BITS		4

struct dma_fence_ops {
	bool		use_64bit_seqno;
	const char	*(*get_driver_name)(struct dma_fence *);
	const char	*(*get_timeline_name)(struct dma_fence *);
	bool		(*enable_signaling)(struct dma_fence *);
	bool		(*signaled)(struct dma_fence *);
	long		(*wait)(struct dma_fence *, bool, long);
	void		(*release)(struct dma_fence *);
	/* Linux 6.x: optional hint, called without the fence lock. */
	void		(*set_deadline)(struct dma_fence *, ktime_t);
};

typedef void (*dma_fence_func_t)(struct dma_fence *, struct dma_fence_cb *);

struct dma_fence_cb {
	dma_fence_func_t		func; /* Linux API name */
	TAILQ_ENTRY(dma_fence_cb)	fcb_entry;
	bool				fcb_onqueue;
};

#define	__dma_fence_is_later		linux___dma_fence_is_later
#define	__dma_fence_signal		linux___dma_fence_signal
#define	__dma_fence_signal_wake		linux___dma_fence_signal_wake
#define	dma_fence_add_callback		linux_dma_fence_add_callback
#define	dma_fence_context_alloc		linux_dma_fence_context_alloc
#define	dma_fence_default_wait		linux_dma_fence_default_wait
#define	dma_fence_destroy		linux_dma_fence_destroy
#define	dma_fence_enable_sw_signaling	linux_dma_fence_enable_sw_signaling
#define	dma_fence_free			linux_dma_fence_free
#define	dma_fence_get			linux_dma_fence_get
#define	dma_fence_get_rcu		linux_dma_fence_get_rcu
#define	dma_fence_get_rcu_safe		linux_dma_fence_get_rcu_safe
#define	dma_fence_get_status		linux_dma_fence_get_status
#define	dma_fence_get_stub		linux_dma_fence_get_stub
#define	dma_fence_init			linux_dma_fence_init
#define	dma_fence_init64		linux_dma_fence_init64
#define	dma_fence_allocate_private_stub	linux_dma_fence_allocate_private_stub
#define	dma_fence_is_array		linux_dma_fence_is_array
#define	dma_fence_is_chain		linux_dma_fence_is_chain
#define	dma_fence_set_deadline		linux_dma_fence_set_deadline
#define	dma_fence_signal_timestamp	linux_dma_fence_signal_timestamp
#define	dma_fence_signal_timestamp_locked linux_dma_fence_signal_timestamp_locked
#define	dma_fence_is_later		linux_dma_fence_is_later
#define	dma_fence_is_signaled		linux_dma_fence_is_signaled
#define	dma_fence_is_signaled_locked	linux_dma_fence_is_signaled_locked
#define	dma_fence_put			linux_dma_fence_put
#define	dma_fence_remove_callback	linux_dma_fence_remove_callback
#define	dma_fence_reset			linux_dma_fence_reset
#define	dma_fence_set_error		linux_dma_fence_set_error
#define	dma_fence_signal		linux_dma_fence_signal
#define	dma_fence_signal_locked		linux_dma_fence_signal_locked
#define	dma_fence_wait			linux_dma_fence_wait
#define	dma_fence_wait_any_timeout	linux_dma_fence_wait_any_timeout
#define	dma_fence_wait_timeout		linux_dma_fence_wait_timeout

extern int	linux_dma_fence_trace;

void	linux_dma_fences_init(void);
void	linux_dma_fences_fini(void);

void	dma_fence_init(struct dma_fence *, const struct dma_fence_ops *,
	    spinlock_t *, uint64_t, uint64_t);
void	dma_fence_init64(struct dma_fence *, const struct dma_fence_ops *,
	    spinlock_t *, uint64_t, uint64_t);
void	dma_fence_reset(struct dma_fence *, const struct dma_fence_ops *,
	    spinlock_t *, uint64_t, uint64_t); /* XXX extension */
void	dma_fence_destroy(struct dma_fence *);
void	dma_fence_free(struct dma_fence *);

uint64_t
	dma_fence_context_alloc(unsigned);
bool	__dma_fence_is_later(uint64_t, uint64_t, const struct dma_fence_ops *);
bool	dma_fence_is_later(struct dma_fence *, struct dma_fence *);

struct dma_fence *
	dma_fence_get_stub(void);
struct dma_fence *
	dma_fence_allocate_private_stub(ktime_t);

struct dma_fence *
	dma_fence_get(struct dma_fence *);
struct dma_fence *
	dma_fence_get_rcu(struct dma_fence *);
struct dma_fence *
	dma_fence_get_rcu_safe(struct dma_fence *volatile const *);
void	dma_fence_put(struct dma_fence *);

int	dma_fence_add_callback(struct dma_fence *, struct dma_fence_cb *,
	    dma_fence_func_t);
bool	dma_fence_remove_callback(struct dma_fence *, struct dma_fence_cb *);
void	dma_fence_enable_sw_signaling(struct dma_fence *);

bool	dma_fence_is_signaled(struct dma_fence *);
bool	dma_fence_is_signaled_locked(struct dma_fence *);
void	dma_fence_set_error(struct dma_fence *, int);
int	dma_fence_get_status(struct dma_fence *);
int	dma_fence_signal(struct dma_fence *);
int	dma_fence_signal_locked(struct dma_fence *);
int	dma_fence_signal_timestamp(struct dma_fence *, ktime_t);
int	dma_fence_signal_timestamp_locked(struct dma_fence *, ktime_t);
void	dma_fence_set_deadline(struct dma_fence *, ktime_t);
bool	dma_fence_is_array(struct dma_fence *);
bool	dma_fence_is_chain(struct dma_fence *);
long	dma_fence_default_wait(struct dma_fence *, bool, long);
long	dma_fence_wait(struct dma_fence *, bool);
long	dma_fence_wait_any_timeout(struct dma_fence **, uint32_t, bool, long,
	    uint32_t *);
long	dma_fence_wait_timeout(struct dma_fence *, bool, long);

/*
 * Linux 6.x inline helpers.  Written from the interface (dma-fence.h is
 * GPL-2.0-only; no source copied).
 */

static inline bool
dma_fence_is_later_or_same(struct dma_fence *f1, struct dma_fence *f2)
{

	return f1 == f2 || dma_fence_is_later(f1, f2);
}

/*
 * The later of two fences of one context, or NULL if that one has
 * already signalled (nothing left to wait for).
 */
static inline struct dma_fence *
dma_fence_later(struct dma_fence *f1, struct dma_fence *f2)
{

	if (f1->context != f2->context)
		return NULL;
	if (dma_fence_is_later(f1, f2))
		return dma_fence_is_signaled(f1) ? NULL : f1;
	return dma_fence_is_signaled(f2) ? NULL : f2;
}

/* Like dma_fence_get_status, for callers that hold fence->lock. */
static inline int
dma_fence_get_status_locked(struct dma_fence *fence)
{

	if (!dma_fence_is_signaled_locked(fence))
		return 0;
	return fence->error ? fence->error : 1;
}

/* Time the fence signalled; only valid after it has signalled. */
static inline ktime_t
dma_fence_timestamp(struct dma_fence *fence)
{

	KASSERT(test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags));
	while (!test_bit(DMA_FENCE_FLAG_TIMESTAMP_BIT, &fence->flags))
		SPINLOCK_BACKOFF_HOOK;
	membar_acquire();
	return fence->timestamp;
}

/*
 * Signalling critical section annotations (lockdep in Linux).  There is no
 * checker here, so they only keep the calls balanced.
 */
static inline bool
dma_fence_begin_signalling(void)
{

	return true;
}

static inline void
dma_fence_end_signalling(bool cookie __unused)
{
}

static inline void
__dma_fence_might_wait(void)
{
}

static inline bool
dma_fence_is_container(struct dma_fence *fence)
{

	return dma_fence_is_array(fence) || dma_fence_is_chain(fence);
}

/* i915 hacks */
bool	__dma_fence_signal(struct dma_fence *);
void	__dma_fence_signal_wake(struct dma_fence *, ktime_t);

static inline void __printflike(2, 3)
DMA_FENCE_TRACE(struct dma_fence *f, const char *fmt, ...)
{
	va_list va;

	if (__predict_false(linux_dma_fence_trace)) {
		va_start(va, fmt);
		printf("fence %"PRIu64"@%"PRIu64": ", f->context, f->seqno);
		vprintf(fmt, va);
		va_end(va);
	}
}

#endif	/* _LINUX_DMA_FENCE_H_ */
