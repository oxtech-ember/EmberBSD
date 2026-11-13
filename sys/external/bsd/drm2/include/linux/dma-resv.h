/*	$NetBSD: dma-resv.h,v 1.10 2021/12/19 12:21:30 riastradh Exp $	*/

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
 *
 * EmberBSD additions (the Linux 6.18 dma_resv API, AI-assisted): the
 * documentation of enum dma_resv_usage and the layout of struct
 * dma_resv_iter are derived from include/linux/dma-resv.h of Linux 6.18,
 * which carries this notice:
 *
 *   Copyright(C) 2011 Linaro Limited. All rights reserved.
 *   Copyright (C) 2012-2013 Canonical Ltd
 *   Copyright (C) 2012 Texas Instruments
 *   Authors: Rob Clark, Maarten Lankhorst, Thomas Hellstrom
 *   Based on bo.c, Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the
 *   "Software"), to deal in the Software without restriction, including
 *   without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sub license, and/or sell copies of the Software, and to
 *   permit persons to whom the Software is furnished to do so, subject to
 *   the following conditions:
 *
 *   The above copyright notice and this permission notice (including the
 *   next paragraph) shall be included in all copies or substantial portions
 *   of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 *   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 *   IN NO EVENT SHALL THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE
 *   LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 *   OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef	_LINUX_DMA_RESV_H_
#define	_LINUX_DMA_RESV_H_

#include <linux/dma-fence.h>
#include <linux/ktime.h>
#include <linux/rcupdate.h>
#include <linux/seqlock.h>
#include <linux/ww_mutex.h>

/*
 * How a fence of a dma_resv is used (Linux 6.x).  KERNEL < WRITE < READ <
 * BOOKKEEP: asking for the fences of one usage returns the fences of the
 * lower usages as well, so WRITE returns KERNEL and WRITE fences, READ
 * returns KERNEL, WRITE and READ fences, BOOKKEEP all of them.
 *
 * - KERNEL: in-kernel memory management only (clears, moves); everybody
 *   waits for these before touching the memory.
 * - WRITE: implicit write synchronisation of a submission.
 * - READ: implicit read synchronisation of a submission.
 * - BOOKKEEP: no implicit synchronisation (preemption fences, page table
 *   updates, TLB flushes, explicitly synchronised submissions).
 *
 * A fence already in the object can be promoted by adding it again with a
 * lower (stronger) usage, never degraded.
 */
enum dma_resv_usage {
	DMA_RESV_USAGE_KERNEL,
	DMA_RESV_USAGE_WRITE,
	DMA_RESV_USAGE_READ,
	DMA_RESV_USAGE_BOOKKEEP
};

/*
 * Implicit sync usage for a new access: a write must wait for existing
 * readers and writers (READ), a read only for existing writers (WRITE).
 */
static inline enum dma_resv_usage
dma_resv_usage_rw(bool write)
{

	return write ? DMA_RESV_USAGE_READ : DMA_RESV_USAGE_WRITE;
}

struct dma_resv_xlist;

/*
 * Storage (EmberBSD, transitional): the 5.6 layout is kept so the drivers
 * that still read fence_excl and the shared list directly work unchanged.
 *
 *  - fence_excl:  one fence of usage WRITE (the latest writer);
 *  - fence:       the "shared" list, every entry of usage READ;
 *  - xfence:      entries that carry their own usage tag: KERNEL, BOOKKEEP
 *                 and WRITE fences that were superseded as the latest
 *                 writer.
 *
 * The 5.6 functions see fence_excl and the shared list; the 6.x functions
 * see all three.  Iteration order is fence_excl, the shared list, then
 * xfence; no order between fences is promised.
 */
struct dma_resv {
	struct ww_mutex				lock;
	struct seqcount				seq;
	struct dma_fence __rcu			*fence_excl;
	struct dma_resv_list __rcu		*fence;

	struct dma_resv_list __rcu		*robj_prealloc;

	struct dma_resv_xlist __rcu		*xfence;
};

/*
 * Position in the fences of a dma_resv.  Do not touch the fields, use the
 * accessors.  The unlocked iterators can restart when the object is
 * modified: code that accumulates something must check
 * dma_resv_iter_is_restarted().
 */
struct dma_resv_iter {
	struct dma_resv		*obj;
	enum dma_resv_usage	usage;		/* return this usage or lower */
	struct dma_fence	*fence;		/* current (referenced if unlocked) */
	enum dma_resv_usage	fence_usage;	/* usage of the current fence */
	unsigned int		index;
	unsigned int		num_fences;	/* snapshot size (unlocked) */
	bool			is_restarted;

	/* private snapshot for the unlocked iterators */
	unsigned int			rs_version;
	struct dma_fence		*rs_excl;
	const struct dma_resv_list	*rs_slist;
	const struct dma_resv_xlist	*rs_xlist;
	unsigned int			rs_nshared;
	unsigned int			rs_nx;
};

struct dma_resv_list {
	struct rcu_head		rol_rcu;

	uint32_t		shared_count;
	uint32_t		shared_max;
	struct dma_fence __rcu	*shared[];
};

/* NetBSD addition */
struct dma_resv_poll {
	kmutex_t		rp_lock;
	struct selinfo		rp_selq;
	struct dma_fence_cb		rp_fcb;
	bool			rp_claimed;
};

#define	dma_resv_replace_excl_fence	linux_dma_resv_replace_excl_fence
#define	dma_resv_add_excl_fence		linux_dma_resv_add_excl_fence
#define	dma_resv_add_fence		linux_dma_resv_add_fence
#define	dma_resv_add_shared_fence	linux_dma_resv_add_shared_fence
#define	dma_resv_assert_held		linux_dma_resv_assert_held
#define	dma_resv_copy_fences		linux_dma_resv_copy_fences
#define	dma_resv_do_poll		linux_dma_resv_do_poll
#define	dma_resv_fini			linux_dma_resv_fini
#define	dma_resv_get_excl		linux_dma_resv_get_excl
#define	dma_resv_get_excl_rcu		linux_dma_resv_get_excl_rcu
#define	dma_resv_get_fences_rcu		linux_dma_resv_get_fences_rcu
#define	dma_resv_get_fences		linux_dma_resv_get_fences
#define	dma_resv_get_singleton		linux_dma_resv_get_singleton
#define	dma_resv_iter_first		linux_dma_resv_iter_first
#define	dma_resv_iter_first_unlocked	linux_dma_resv_iter_first_unlocked
#define	dma_resv_iter_next		linux_dma_resv_iter_next
#define	dma_resv_iter_next_unlocked	linux_dma_resv_iter_next_unlocked
#define	dma_resv_replace_fences		linux_dma_resv_replace_fences
#define	dma_resv_reserve_fences		linux_dma_resv_reserve_fences
#define	dma_resv_set_deadline		linux_dma_resv_set_deadline
#define	dma_resv_test_signaled		linux_dma_resv_test_signaled
#define	dma_resv_wait_timeout		linux_dma_resv_wait_timeout
#define	dma_resv_get_list		linux_dma_resv_get_list
#define	dma_resv_held			linux_dma_resv_held
#define	dma_resv_init			linux_dma_resv_init
#define	dma_resv_is_locked		linux_dma_resv_is_locked
#define	dma_resv_kqfilter		linux_dma_resv_kqfilter
#define	dma_resv_lock			linux_dma_resv_lock
#define	dma_resv_lock_interruptible	linux_dma_resv_lock_interruptible
#define	dma_resv_lock_slow		linux_dma_resv_lock_slow
#define	dma_resv_lock_slow_interruptible linux_dma_resv_lock_slow_interruptible
#define	dma_resv_locking_ctx		linux_dma_resv_locking_ctx
#define	dma_resv_reserve_shared		linux_dma_resv_reserve_shared
#define	dma_resv_test_signaled_rcu	linux_dma_resv_test_signaled_rcu
#define	dma_resv_trylock		linux_dma_resv_trylock
#define	dma_resv_unlock			linux_dma_resv_unlock
#define	dma_resv_wait_timeout_rcu	linux_dma_resv_wait_timeout_rcu
#define	dma_resv_poll_fini		linux_dma_resv_poll_fini
#define	dma_resv_poll_init		linux_dma_resv_poll_init
#define	reservation_ww_class		linux_reservation_ww_class

extern struct ww_class	reservation_ww_class;

void	dma_resv_init(struct dma_resv *);
void	dma_resv_fini(struct dma_resv *);
int	dma_resv_lock(struct dma_resv *,
	    struct ww_acquire_ctx *);
void	dma_resv_lock_slow(struct dma_resv *,
	    struct ww_acquire_ctx *);
int	dma_resv_lock_interruptible(struct dma_resv *,
	    struct ww_acquire_ctx *);
int	dma_resv_lock_slow_interruptible(struct dma_resv *,
	    struct ww_acquire_ctx *);
bool	dma_resv_trylock(struct dma_resv *) __must_check;
struct ww_acquire_ctx *
	dma_resv_locking_ctx(struct dma_resv *);
void	dma_resv_unlock(struct dma_resv *);
bool	dma_resv_is_locked(struct dma_resv *);
bool	dma_resv_held(struct dma_resv *);
void	dma_resv_assert_held(struct dma_resv *);
struct dma_fence *
	dma_resv_get_excl(struct dma_resv *);
struct dma_resv_list *
	dma_resv_get_list(struct dma_resv *);
int	dma_resv_reserve_shared(struct dma_resv *, unsigned int);
void	dma_resv_replace_excl_fence(struct dma_resv *, struct dma_fence *);
void	dma_resv_add_excl_fence(struct dma_resv *,
	    struct dma_fence *);
void	dma_resv_add_shared_fence(struct dma_resv *,
	    struct dma_fence *);

struct dma_fence *
	dma_resv_get_excl_rcu(const struct dma_resv *);
int	dma_resv_get_fences_rcu(const struct dma_resv *,
	    struct dma_fence **, unsigned *, struct dma_fence ***);

int	dma_resv_copy_fences(struct dma_resv *, struct dma_resv *);

/*
 * 5.6 names, implemented over the 6.x functions below: shared selects
 * READ (all fences) instead of WRITE (writers only).
 */
bool	dma_resv_test_signaled_rcu(const struct dma_resv *,
	    bool);
long	dma_resv_wait_timeout_rcu(const struct dma_resv *,
	    bool, bool, unsigned long);

/* Linux 6.x. */
int	dma_resv_reserve_fences(struct dma_resv *, unsigned int);
void	dma_resv_add_fence(struct dma_resv *, struct dma_fence *,
	    enum dma_resv_usage);
void	dma_resv_replace_fences(struct dma_resv *, uint64_t,
	    struct dma_fence *, enum dma_resv_usage);

struct dma_fence *
	dma_resv_iter_first_unlocked(struct dma_resv_iter *);
struct dma_fence *
	dma_resv_iter_next_unlocked(struct dma_resv_iter *);
struct dma_fence *
	dma_resv_iter_first(struct dma_resv_iter *);
struct dma_fence *
	dma_resv_iter_next(struct dma_resv_iter *);

int	dma_resv_get_fences(struct dma_resv *, enum dma_resv_usage,
	    unsigned int *, struct dma_fence ***);
int	dma_resv_get_singleton(struct dma_resv *, enum dma_resv_usage,
	    struct dma_fence **);
long	dma_resv_wait_timeout(struct dma_resv *, enum dma_resv_usage,
	    bool, unsigned long);
bool	dma_resv_test_signaled(struct dma_resv *, enum dma_resv_usage);
void	dma_resv_set_deadline(struct dma_resv *, enum dma_resv_usage,
	    ktime_t);

static inline void
dma_resv_iter_begin(struct dma_resv_iter *cursor, struct dma_resv *obj,
    enum dma_resv_usage usage)
{

	cursor->obj = obj;
	cursor->usage = usage;
	cursor->fence = NULL;
}

/* Drop the reference the unlocked iterators hold on the current fence. */
static inline void
dma_resv_iter_end(struct dma_resv_iter *cursor)
{

	dma_fence_put(cursor->fence);
}

static inline enum dma_resv_usage
dma_resv_iter_usage(struct dma_resv_iter *cursor)
{

	return cursor->fence_usage;
}

/* True for the first fence returned after a (re)start of the iteration. */
static inline bool
dma_resv_iter_is_restarted(struct dma_resv_iter *cursor)
{

	return cursor->is_restarted;
}

/*
 * Without the object lock, under RCU; takes a reference on the current
 * fence and skips signalled fences.  May restart, see struct dma_resv_iter.
 */
#define	dma_resv_for_each_fence_unlocked(cursor, fence)			\
	for (fence = dma_resv_iter_first_unlocked(cursor);		\
	     fence != NULL; fence = dma_resv_iter_next_unlocked(cursor))

/*
 * With the object locked: no references taken, signalled fences are
 * returned too; the fence is valid while the lock is held.
 */
#define	dma_resv_for_each_fence(cursor, obj, usage, fence)		\
	for (dma_resv_iter_begin(cursor, obj, usage),			\
	     fence = dma_resv_iter_first(cursor); fence != NULL;	\
	     fence = dma_resv_iter_next(cursor))

/* No fence slot reservation checks here. */
static inline void
dma_resv_reset_max_fences(struct dma_resv *obj __unused)
{
}

/* NetBSD additions */
void	dma_resv_poll_init(struct dma_resv_poll *);
void	dma_resv_poll_fini(struct dma_resv_poll *);
int	dma_resv_do_poll(const struct dma_resv *, int,
	    struct dma_resv_poll *);
int	dma_resv_kqfilter(const struct dma_resv *,
	    struct knote *, struct dma_resv_poll *);

static inline bool
dma_resv_has_excl_fence(const struct dma_resv *robj)
{
	return robj->fence_excl != NULL;
}

#endif	/* _LINUX_DMA_RESV_H_ */
