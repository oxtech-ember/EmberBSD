/*	$NetBSD: linux_dma_resv.c,v 1.22 2022/02/15 22:51:03 riastradh Exp $	*/

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

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: linux_dma_resv.c,v 1.22 2022/02/15 22:51:03 riastradh Exp $");

#include <sys/param.h>
#include <sys/poll.h>
#include <sys/select.h>

#include <linux/dma-fence.h>
#include <linux/dma-fence-array.h>
#include <linux/dma-resv.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/seqlock.h>
#include <linux/ww_mutex.h>

DEFINE_WW_CLASS(reservation_ww_class __cacheline_aligned);

static struct dma_resv_list *
objlist_alloc(uint32_t n)
{
	struct dma_resv_list *list;

	list = kmem_alloc(offsetof(typeof(*list), shared[n]), KM_SLEEP);
	list->shared_max = n;

	return list;
}

static void
objlist_free(struct dma_resv_list *list)
{
	uint32_t n = list->shared_max;

	kmem_free(list, offsetof(typeof(*list), shared[n]));
}

static void
objlist_free_cb(struct rcu_head *rcu)
{
	struct dma_resv_list *list = container_of(rcu,
	    struct dma_resv_list, rol_rcu);

	objlist_free(list);
}

static void
objlist_defer_free(struct dma_resv_list *list)
{

	call_rcu(&list->rol_rcu, objlist_free_cb);
}

struct dma_resv_xent {
	struct dma_fence	*fence;
	uint32_t		usage;
};

struct dma_resv_xlist {
	struct rcu_head		rxl_rcu;
	uint32_t		count;
	uint32_t		max;
	struct dma_resv_xent	ent[];
};

static struct dma_resv_xlist *
xlist_alloc(uint32_t n)
{
	struct dma_resv_xlist *list;

	list = kmem_alloc(offsetof(struct dma_resv_xlist, ent[n]), KM_SLEEP);
	list->count = 0;
	list->max = n;

	return list;
}

static void
xlist_free(struct dma_resv_xlist *list)
{

	kmem_free(list, offsetof(struct dma_resv_xlist, ent[list->max]));
}

static void
xlist_free_cb(struct rcu_head *rcu)
{

	xlist_free(container_of(rcu, struct dma_resv_xlist, rxl_rcu));
}

static void
xlist_defer_free(struct dma_resv_xlist *list)
{

	call_rcu(&list->rxl_rcu, xlist_free_cb);
}

/*
 * dma_resv_init(robj)
 *
 *	Initialize a reservation object.  Caller must later destroy it
 *	with dma_resv_fini.
 */
void
dma_resv_init(struct dma_resv *robj)
{

	ww_mutex_init(&robj->lock, &reservation_ww_class);
	seqcount_init(&robj->seq);
	robj->fence_excl = NULL;
	robj->fence = NULL;
	robj->robj_prealloc = NULL;
	robj->xfence = NULL;
}

/*
 * dma_resv_fini(robj)
 *
 *	Destroy a reservation object, freeing any memory that had been
 *	allocated for it.  Caller must have exclusive access to it.
 */
void
dma_resv_fini(struct dma_resv *robj)
{
	unsigned i;

	if (robj->robj_prealloc) {
		objlist_free(robj->robj_prealloc);
		robj->robj_prealloc = NULL; /* paranoia */
	}
	if (robj->fence) {
		for (i = 0; i < robj->fence->shared_count; i++) {
			dma_fence_put(robj->fence->shared[i]);
			robj->fence->shared[i] = NULL; /* paranoia */
		}
		objlist_free(robj->fence);
		robj->fence = NULL; /* paranoia */
	}
	if (robj->fence_excl) {
		dma_fence_put(robj->fence_excl);
		robj->fence_excl = NULL; /* paranoia */
	}
	if (robj->xfence) {
		for (i = 0; i < robj->xfence->count; i++) {
			dma_fence_put(robj->xfence->ent[i].fence);
			robj->xfence->ent[i].fence = NULL; /* paranoia */
		}
		xlist_free(robj->xfence);
		robj->xfence = NULL; /* paranoia */
	}
	ww_mutex_destroy(&robj->lock);
}

/*
 * dma_resv_lock(robj, ctx)
 *
 *	Acquire a reservation object's lock.  Return 0 on success,
 *	-EALREADY if caller already holds it, -EDEADLK if a
 *	higher-priority owner holds it and the caller must back out and
 *	retry.
 */
int
dma_resv_lock(struct dma_resv *robj,
    struct ww_acquire_ctx *ctx)
{

	return ww_mutex_lock(&robj->lock, ctx);
}

/*
 * dma_resv_lock_slow(robj, ctx)
 *
 *	Acquire a reservation object's lock.  Caller must not hold
 *	this lock or any others -- this is to be used in slow paths
 *	after dma_resv_lock or dma_resv_lock_interruptible has failed
 *	and the caller has backed out all other locks.
 */
void
dma_resv_lock_slow(struct dma_resv *robj,
    struct ww_acquire_ctx *ctx)
{

	ww_mutex_lock_slow(&robj->lock, ctx);
}

/*
 * dma_resv_lock_interruptible(robj, ctx)
 *
 *	Acquire a reservation object's lock.  Return 0 on success,
 *	-EALREADY if caller already holds it, -EDEADLK if a
 *	higher-priority owner holds it and the caller must back out and
 *	retry, -EINTR if interrupted.
 */
int
dma_resv_lock_interruptible(struct dma_resv *robj,
    struct ww_acquire_ctx *ctx)
{

	return ww_mutex_lock_interruptible(&robj->lock, ctx);
}

/*
 * dma_resv_lock_slow_interruptible(robj, ctx)
 *
 *	Acquire a reservation object's lock.  Caller must not hold
 *	this lock or any others -- this is to be used in slow paths
 *	after dma_resv_lock or dma_resv_lock_interruptible has failed
 *	and the caller has backed out all other locks.  Return 0 on
 *	success, -EINTR if interrupted.
 */
int
dma_resv_lock_slow_interruptible(struct dma_resv *robj,
    struct ww_acquire_ctx *ctx)
{

	return ww_mutex_lock_slow_interruptible(&robj->lock, ctx);
}

/*
 * dma_resv_trylock(robj)
 *
 *	Try to acquire a reservation object's lock without blocking.
 *	Return true on success, false on failure.
 */
bool
dma_resv_trylock(struct dma_resv *robj)
{

	return ww_mutex_trylock(&robj->lock);
}

/*
 * dma_resv_locking_ctx(robj)
 *
 *	Return a pointer to the ww_acquire_ctx used by the owner of
 *	the reservation object's lock, or NULL if it is either not
 *	owned or if it is locked without context.
 */
struct ww_acquire_ctx *
dma_resv_locking_ctx(struct dma_resv *robj)
{

	return ww_mutex_locking_ctx(&robj->lock);
}

/*
 * dma_resv_unlock(robj)
 *
 *	Release a reservation object's lock.
 */
void
dma_resv_unlock(struct dma_resv *robj)
{

	return ww_mutex_unlock(&robj->lock);
}

/*
 * dma_resv_is_locked(robj)
 *
 *	True if robj is locked.
 */
bool
dma_resv_is_locked(struct dma_resv *robj)
{

	return ww_mutex_is_locked(&robj->lock);
}

/*
 * dma_resv_held(robj)
 *
 *	True if robj is locked.
 */
bool
dma_resv_held(struct dma_resv *robj)
{

	return ww_mutex_is_locked(&robj->lock);
}

/*
 * dma_resv_assert_held(robj)
 *
 *	Panic if robj is not held, in DIAGNOSTIC builds.
 */
void
dma_resv_assert_held(struct dma_resv *robj)
{

	KASSERT(dma_resv_held(robj));
}

/*
 * dma_resv_get_excl(robj)
 *
 *	Return a pointer to the exclusive fence of the reservation
 *	object robj.
 *
 *	Caller must have robj locked.
 */
struct dma_fence *
dma_resv_get_excl(struct dma_resv *robj)
{

	KASSERT(dma_resv_held(robj));
	return robj->fence_excl;
}

/*
 * dma_resv_get_list(robj)
 *
 *	Return a pointer to the shared fence list of the reservation
 *	object robj.
 *
 *	Caller must have robj locked.
 */
struct dma_resv_list *
dma_resv_get_list(struct dma_resv *robj)
{

	KASSERT(dma_resv_held(robj));
	return robj->fence;
}

/*
 * dma_resv_reserve_shared(robj, num_fences)
 *
 *	Reserve space in robj to add num_fences shared fences.  To be
 *	used only once before calling dma_resv_add_shared_fence.
 *
 *	Caller must have robj locked.
 *
 *	Internally, we start with room for four entries and double if
 *	we don't have enough.  This is not guaranteed.
 */
int
dma_resv_reserve_shared(struct dma_resv *robj, unsigned int num_fences)
{
	struct dma_resv_list *list, *prealloc;
	uint32_t n, nalloc;

	KASSERT(dma_resv_held(robj));

	list = robj->fence;
	prealloc = robj->robj_prealloc;

	/* If there's an existing list, check it for space.  */
	if (list) {
		/* If there's too many already, give up.  */
		if (list->shared_count > UINT32_MAX - num_fences)
			return -ENOMEM;

		/* Add some more. */
		n = list->shared_count + num_fences;

		/* If there's enough for one more, we're done.  */
		if (n <= list->shared_max)
			return 0;
	} else {
		/* No list already.  We need space for num_fences.  */
		n = num_fences;
	}

	/* If not, maybe there's a preallocated list ready.  */
	if (prealloc != NULL) {
		/* If there's enough room in it, stop here.  */
		if (n <= prealloc->shared_max)
			return 0;

		/* Try to double its capacity.  */
		nalloc = n > UINT32_MAX/2 ? UINT32_MAX : 2*n;
		prealloc = objlist_alloc(nalloc);

		/* Swap the new preallocated list and free the old one.  */
		objlist_free(robj->robj_prealloc);
		robj->robj_prealloc = prealloc;
	} else {
		/* Start with some spare.  */
		nalloc = n > UINT32_MAX/2 ? UINT32_MAX : MAX(2*n, 4);
		prealloc = objlist_alloc(nalloc);

		/* Save the new preallocated list.  */
		robj->robj_prealloc = prealloc;
	}

	/* Success!  */
	return 0;
}

struct dma_resv_write_ticket {
};

/*
 * dma_resv_write_begin(robj, ticket)
 *
 *	Begin an atomic batch of writes to robj, and initialize opaque
 *	ticket for it.  The ticket must be passed to
 *	dma_resv_write_commit to commit the writes.
 *
 *	Caller must have robj locked.
 *
 *	Implies membar_producer, i.e. store-before-store barrier.  Does
 *	NOT serve as an acquire operation, however.
 */
static void
dma_resv_write_begin(struct dma_resv *robj,
    struct dma_resv_write_ticket *ticket)
{

	KASSERT(dma_resv_held(robj));

	write_seqcount_begin(&robj->seq);
}

/*
 * dma_resv_write_commit(robj, ticket)
 *
 *	Commit an atomic batch of writes to robj begun with the call to
 *	dma_resv_write_begin that returned ticket.
 *
 *	Caller must have robj locked.
 *
 *	Implies membar_producer, i.e. store-before-store barrier.  Does
 *	NOT serve as a release operation, however.
 */
static void
dma_resv_write_commit(struct dma_resv *robj,
    struct dma_resv_write_ticket *ticket)
{

	KASSERT(dma_resv_held(robj));

	write_seqcount_end(&robj->seq);
}

struct dma_resv_read_ticket {
	unsigned version;
};

/*
 * dma_resv_read_begin(robj, ticket)
 *
 *	Begin a read section, and initialize opaque ticket for it.  The
 *	ticket must be passed to dma_resv_read_exit, and the
 *	caller must be prepared to retry reading if it fails.
 */
static void
dma_resv_read_begin(const struct dma_resv *robj,
    struct dma_resv_read_ticket *ticket)
{

	ticket->version = read_seqcount_begin(&robj->seq);
}

/*
 * dma_resv_read_valid(robj, ticket)
 *
 *	Test whether the read sections are valid.  Return true on
 *	success, or false on failure if the read ticket has been
 *	invalidated.
 */
static bool
dma_resv_read_valid(const struct dma_resv *robj,
    struct dma_resv_read_ticket *ticket)
{

	return !read_seqcount_retry(&robj->seq, ticket->version);
}

/*
 * dma_resv_get_shared_reader(robj, listp, shared_countp, ticket)
 *
 *	Set *listp and *shared_countp to a snapshot of the pointer to
 *	and length of the shared fence list of robj and return true, or
 *	set them to NULL/0 and return false if a writer intervened so
 *	the caller must start over.
 *
 *	Both *listp and *shared_countp are unconditionally initialized
 *	on return.  They may be NULL/0 even on success, if there is no
 *	shared list at the moment.  Does not take any fence references.
 */
static bool
dma_resv_get_shared_reader(const struct dma_resv *robj,
    const struct dma_resv_list **listp, unsigned *shared_countp,
    struct dma_resv_read_ticket *ticket)
{
	struct dma_resv_list *list;
	unsigned shared_count = 0;

	/*
	 * Get the list and, if it is present, its length.  If the list
	 * is present, it has a valid length.  The atomic_load_consume
	 * pairs with the membar_producer in dma_resv_write_begin.
	 */
	list = atomic_load_consume(&robj->fence);
	shared_count = list ? atomic_load_relaxed(&list->shared_count) : 0;

	/*
	 * We are done reading from robj and list.  Validate our
	 * parking ticket.  If it's invalid, do not pass go and do not
	 * collect $200.
	 */
	if (!dma_resv_read_valid(robj, ticket))
		goto fail;

	/* Success!  */
	*listp = list;
	*shared_countp = shared_count;
	return true;

fail:	*listp = NULL;
	*shared_countp = 0;
	return false;
}

/*
 * dma_resv_get_excl_reader(robj, fencep, ticket)
 *
 *	Set *fencep to the exclusive fence of robj and return true, or
 *	set it to NULL and return false if either
 *	(a) a writer intervened, or
 *	(b) the fence is scheduled to be destroyed after this RCU grace
 *	    period,
 *	in either case meaning the caller must restart.
 *
 *	The value of *fencep is unconditionally initialized on return.
 *	It may be NULL, if there is no exclusive fence at the moment.
 *	If nonnull, *fencep is referenced; caller must dma_fence_put.
 */
static bool
dma_resv_get_excl_reader(const struct dma_resv *robj,
    struct dma_fence **fencep,
    struct dma_resv_read_ticket *ticket)
{
	struct dma_fence *fence;

	/*
	 * Get the candidate fence pointer.  The atomic_load_consume
	 * pairs with the membar_consumer in dma_resv_write_begin.
	 */
	fence = atomic_load_consume(&robj->fence_excl);

	/*
	 * The load of robj->fence_excl is atomic, but the caller may
	 * have previously loaded the shared fence list and should
	 * restart if its view of the entire dma_resv object is not a
	 * consistent snapshot.
	 */
	if (!dma_resv_read_valid(robj, ticket))
		goto fail;

	/*
	 * If the fence is already scheduled to away after this RCU
	 * read section, give up.  Otherwise, take a reference so it
	 * won't go away until after dma_fence_put.
	 */
	if (fence != NULL &&
	    (fence = dma_fence_get_rcu(fence)) == NULL)
		goto fail;

	/* Success!  */
	*fencep = fence;
	return true;

fail:	*fencep = NULL;
	return false;
}

/*
 * dma_resv_add_excl_fence(robj, fence)
 *
 *	Empty and release all of robj's shared fences, and clear and
 *	release its exclusive fence.  If fence is nonnull, acquire a
 *	reference to it and save it as robj's exclusive fence.
 *
 *	Caller must have robj locked.
 */
void
dma_resv_add_excl_fence(struct dma_resv *robj,
    struct dma_fence *fence)
{
	struct dma_fence *old_fence = robj->fence_excl;
	struct dma_resv_list *old_list = robj->fence, *spare = NULL;
	uint32_t old_shared_count = 0;
	struct dma_resv_write_ticket ticket;

	KASSERT(dma_resv_held(robj));

	/*
	 * If we are setting rather than just removing a fence, acquire
	 * a reference for ourselves.
	 */
	if (fence)
		(void)dma_fence_get(fence);

	/*
	 * If there are any shared fences, remember how many.  Readers
	 * of the 5.6 API walk list->shared[i] over a snapshot of the
	 * count without revalidating each entry, so the list they hold
	 * must keep its count and entries: the object gets a fresh
	 * empty list of the same capacity (copy on write) and the old
	 * one is freed after a grace period.  May sleep, so allocate
	 * before the write section.
	 */
	if (old_list != NULL) {
		old_shared_count = old_list->shared_count;
		if (old_shared_count != 0) {
			spare = objlist_alloc(old_list->shared_max);
			spare->shared_count = 0;
		}
	}

	/* Begin an update.  Implies membar_producer for fence.  */
	dma_resv_write_begin(robj, &ticket);

	/* Replace the fence and the shared list.  */
	atomic_store_relaxed(&robj->fence_excl, fence);
	if (spare != NULL)
		atomic_store_relaxed(&robj->fence, spare);

	/* Commit the update.  */
	dma_resv_write_commit(robj, &ticket);

	/* Release the old exclusive fence, if any.  */
	if (old_fence) {
		dma_fence_put(old_fence);
		old_fence = NULL; /* paranoia */
	}

	/*
	 * Release any old shared fences.  The entries stay in the old
	 * list for readers that still hold it; fences are freed only
	 * after a grace period.
	 */
	if (spare != NULL) {
		while (old_shared_count--)
			dma_fence_put(old_list->shared[old_shared_count]);
		objlist_defer_free(old_list);
	}
}

/*
 * EmberBSD addition, AI-assisted. Replace the exclusive fence without
 * discarding independent shared dependencies. The caller holds the lock.
 */
void
dma_resv_replace_excl_fence(struct dma_resv *robj, struct dma_fence *fence)
{
	struct dma_resv_write_ticket ticket;
	struct dma_fence *old;

	KASSERT(dma_resv_held(robj));
	old = robj->fence_excl;
	dma_fence_get(fence);
	dma_resv_write_begin(robj, &ticket);
	atomic_store_relaxed(&robj->fence_excl, fence);
	dma_resv_write_commit(robj, &ticket);
	dma_fence_put(old);
}

/*
 * dma_resv_get_excl_rcu(robj)
 *
 *	Note: Caller need not call this from an RCU read section.
 */
struct dma_fence *
dma_resv_get_excl_rcu(const struct dma_resv *robj)
{
	struct dma_fence *fence;

	rcu_read_lock();
	fence = dma_fence_get_rcu_safe(&robj->fence_excl);
	rcu_read_unlock();

	return fence;
}

/*
 * dma_resv_get_fences_rcu(robj, fencep, nsharedp, sharedp)
 *
 *	Get a snapshot of the exclusive and shared fences of robj.  The
 *	shared fences are returned as a pointer *sharedp to an array,
 *	to be freed by the caller with kfree, of *nsharedp elements.
 *	If fencep is null, then add the exclusive fence, if any, at the
 *	end of the array instead.
 *
 *	Returns zero on success, negative (Linux-style) error code on
 *	failure.  On failure, *fencep, *nsharedp, and *sharedp are
 *	untouched.
 */
int
dma_resv_get_fences_rcu(const struct dma_resv *robj,
    struct dma_fence **fencep, unsigned *nsharedp, struct dma_fence ***sharedp)
{
	const struct dma_resv_list *list = NULL;
	struct dma_fence *fence = NULL;
	struct dma_fence **shared = NULL;
	unsigned shared_alloc = 0, shared_count, i;
	struct dma_resv_read_ticket ticket;

top:	KASSERT(fence == NULL);

	/* Enter an RCU read section and get a read ticket.  */
	rcu_read_lock();
	dma_resv_read_begin(robj, &ticket);

	/* If there is a shared list, grab it.  */
	if (!dma_resv_get_shared_reader(robj, &list, &shared_count, &ticket))
		goto restart;
	if (list != NULL) {

		/*
		 * Avoid arithmetic overflow with `+ 1' below.
		 * Strictly speaking we don't need this if the caller
		 * specified fencep or if there is no exclusive fence,
		 * but it is simpler to not have to consider those
		 * cases.
		 */
		KASSERT(shared_count <= list->shared_max);
		if (list->shared_max == UINT_MAX)
			return -ENOMEM;

		/* Check whether we have a buffer.  */
		if (shared == NULL) {
			/*
			 * We don't have a buffer yet.  Try to allocate
			 * one without waiting.
			 */
			shared_alloc = list->shared_max + 1;
			shared = kcalloc(shared_alloc, sizeof(shared[0]),
			    GFP_NOWAIT);
			if (shared == NULL) {
				/*
				 * Couldn't do it immediately.  Back
				 * out of RCU and allocate one with
				 * waiting.
				 */
				rcu_read_unlock();
				shared = kcalloc(shared_alloc,
				    sizeof(shared[0]), GFP_KERNEL);
				if (shared == NULL)
					return -ENOMEM;
				goto top;
			}
		} else if (shared_alloc < list->shared_max + 1) {
			/*
			 * We have a buffer but it's too small.  We're
			 * already racing in this case, so just back
			 * out and wait to allocate a bigger one.
			 */
			shared_alloc = list->shared_max + 1;
			rcu_read_unlock();
			kfree(shared);
			shared = kcalloc(shared_alloc, sizeof(shared[0]),
			    GFP_KERNEL);
			if (shared == NULL)
				return -ENOMEM;
			goto top;
		}

		/*
		 * We got a buffer large enough.  Copy into the buffer
		 * and record the number of elements.  Could safely use
		 * memcpy here, because even if we race with a writer
		 * it'll invalidate the read ticket and we'll start
		 * over, but atomic_load in a loop will pacify kcsan.
		 */
		for (i = 0; i < shared_count; i++)
			shared[i] = atomic_load_relaxed(&list->shared[i]);

		/* If anything changed while we were copying, restart.  */
		if (!dma_resv_read_valid(robj, &ticket))
			goto restart;
	}

	/* If there is an exclusive fence, grab it.  */
	KASSERT(fence == NULL);
	if (!dma_resv_get_excl_reader(robj, &fence, &ticket))
		goto restart;

	/*
	 * Try to get a reference to all of the shared fences.
	 */
	for (i = 0; i < shared_count; i++) {
		if (dma_fence_get_rcu(atomic_load_relaxed(&shared[i])) == NULL)
			goto put_restart;
	}

	/* Success!  */
	rcu_read_unlock();
	KASSERT(shared_count <= shared_alloc);
	KASSERT(shared_alloc == 0 || shared_count < shared_alloc);
	KASSERT(shared_alloc <= UINT_MAX);
	if (fencep) {
		*fencep = fence;
	} else if (fence) {
		if (shared_count) {
			shared[shared_count++] = fence;
		} else {
			shared = kmalloc(sizeof(shared[0]), GFP_KERNEL);
			shared[0] = fence;
			shared_count = 1;
		}
	}
	*nsharedp = shared_count;
	*sharedp = shared;
	return 0;

put_restart:
	/* Back out.  */
	while (i --> 0) {
		dma_fence_put(shared[i]);
		shared[i] = NULL; /* paranoia */
	}
	if (fence) {
		dma_fence_put(fence);
		fence = NULL;
	}

restart:
	KASSERT(fence == NULL);
	rcu_read_unlock();
	goto top;
}

/*
 * Linux 6.x usage-tagged API (EmberBSD addition, AI-assisted).
 *
 * The 5.6 storage (fence_excl and the shared list) is kept, see struct
 * dma_resv.  Fences that need their own usage tag live in xfence.  All
 * changes happen under the object lock inside one write section of the
 * seqcount; lockless readers validate against the seqcount and restart.
 */

/*
 * dma_resv_xlist_reserve(robj, n)
 *
 *	Make room for n more tagged entries.  Caller holds the lock.
 */
static int
dma_resv_xlist_reserve(struct dma_resv *robj, uint32_t n)
{
	struct dma_resv_xlist *old = robj->xfence, *new;
	struct dma_resv_write_ticket ticket;
	uint32_t count = old != NULL ? old->count : 0, max, i;

	if (count > UINT32_MAX - n)
		return -ENOMEM;
	if (old != NULL && count + n <= old->max)
		return 0;

	max = count + n;
	max = max > UINT32_MAX/2 ? UINT32_MAX : MAX(2*max, 4);
	new = xlist_alloc(max);
	for (i = 0; i < count; i++)
		new->ent[i] = old->ent[i];	/* the references move over */
	new->count = count;

	dma_resv_write_begin(robj, &ticket);
	atomic_store_relaxed(&robj->xfence, new);
	dma_resv_write_commit(robj, &ticket);

	if (old != NULL)
		xlist_defer_free(old);
	return 0;
}

/*
 * dma_resv_reserve_fences(robj, num_fences)
 *
 *	Reserve slots for num_fences more fences of any usage.  Caller
 *	holds the lock.  -EINVAL for zero, like Linux.
 */
int
dma_resv_reserve_fences(struct dma_resv *robj, unsigned int num_fences)
{
	int error;

	KASSERT(dma_resv_held(robj));

	if (num_fences == 0)
		return -EINVAL;
	error = dma_resv_reserve_shared(robj, num_fences);
	if (error)
		return error;
	return dma_resv_xlist_reserve(robj, num_fences);
}

/*
 * resv_nth_locked(robj, n, usagep)
 *
 *	The n-th fence in iteration order (excl, shared list, tagged
 *	list) and its usage, or NULL past the end.  Caller holds the
 *	lock.
 */
static struct dma_fence *
resv_nth_locked(const struct dma_resv *robj, unsigned n,
    enum dma_resv_usage *usagep)
{
	const struct dma_resv_list *sl = robj->fence;
	const struct dma_resv_xlist *xl = robj->xfence;
	unsigned sc = sl != NULL ? sl->shared_count : 0;
	unsigned xc = xl != NULL ? xl->count : 0;

	if (robj->fence_excl != NULL) {
		if (n == 0) {
			*usagep = DMA_RESV_USAGE_WRITE;
			return robj->fence_excl;
		}
		n--;
	}
	if (n < sc) {
		*usagep = DMA_RESV_USAGE_READ;
		return sl->shared[n];
	}
	n -= sc;
	if (n < xc) {
		*usagep = xl->ent[n].usage;
		return xl->ent[n].fence;
	}
	return NULL;
}

/*
 * resv_victim_spare(robj, n)
 *
 *	If removing the n-th fence changes the shared list, return a
 *	new list with the same capacity to replace it (copy on write),
 *	else NULL.  Readers of the 5.6 API (dma_resv_do_poll,
 *	dma_resv_get_fences_rcu, the *_rcu waits) walk list->shared[i]
 *	over a snapshot of the count without revalidating each entry,
 *	so a shared list may only grow in place, never shift or clear
 *	entries.  May sleep; call before dma_resv_write_begin.
 */
static struct dma_resv_list *
resv_victim_spare(struct dma_resv *robj, unsigned n)
{
	struct dma_resv_list *sl = robj->fence;
	unsigned sc = sl != NULL ? sl->shared_count : 0;

	if (robj->fence_excl != NULL) {
		if (n == 0)
			return NULL;
		n--;
	}
	if (n >= sc)
		return NULL;
	return objlist_alloc(sl->shared_max);
}

/*
 * resv_remove_nth_ws(robj, n, spare, oldlistp)
 *
 *	Remove the n-th fence; return it with its reference (the caller
 *	puts it after the write section).  Caller holds the lock and is
 *	inside a write section.  If the fence was in the shared list,
 *	spare (from resv_victim_spare) becomes the new list and the old
 *	one is returned in *oldlistp for objlist_defer_free after the
 *	write section.
 */
static struct dma_fence *
resv_remove_nth_ws(struct dma_resv *robj, unsigned n,
    struct dma_resv_list *spare, struct dma_resv_list **oldlistp)
{
	struct dma_resv_list *sl = robj->fence;
	struct dma_resv_xlist *xl = robj->xfence;
	unsigned sc = sl != NULL ? sl->shared_count : 0;
	struct dma_fence *fence;
	unsigned i, j;

	*oldlistp = NULL;
	if (robj->fence_excl != NULL) {
		if (n == 0) {
			fence = robj->fence_excl;
			atomic_store_relaxed(&robj->fence_excl, NULL);
			return fence;
		}
		n--;
	}
	if (n < sc) {
		KASSERT(spare != NULL);
		fence = sl->shared[n];
		for (i = j = 0; i < sc; i++) {
			if (i != n)
				spare->shared[j++] = sl->shared[i];
		}
		spare->shared_count = sc - 1;
		atomic_store_relaxed(&robj->fence, spare);
		*oldlistp = sl;
		return fence;
	}
	n -= sc;
	KASSERT(xl != NULL && n < xl->count);
	fence = xl->ent[n].fence;
	for (i = n + 1; i < xl->count; i++) {
		atomic_store_relaxed(&xl->ent[i - 1].fence, xl->ent[i].fence);
		atomic_store_relaxed(&xl->ent[i - 1].usage, xl->ent[i].usage);
	}
	atomic_store_relaxed(&xl->count, xl->count - 1);
	return fence;
}

/*
 * dma_resv_add_fence(robj, fence, usage)
 *
 *	Add fence with the given usage, taking a reference.  Like
 *	Linux, an existing fence of the same context that is not
 *	stronger (usage not lower than the new one) and not later is
 *	replaced, and so is any fence that has already signalled.
 *	Caller holds the lock and has reserved a slot with
 *	dma_resv_reserve_fences; if it did not, a slot is allocated
 *	here (this may sleep).
 */
void
dma_resv_add_fence(struct dma_resv *robj, struct dma_fence *fence,
    enum dma_resv_usage usage)
{
	struct dma_resv_write_ticket ticket;
	struct dma_resv_list *list, *prealloc, *old_list = NULL;
	struct dma_resv_list *spare = NULL, *removed_list = NULL;
	struct dma_resv_xlist *xl;
	struct dma_fence *old = NULL, *f;
	enum dma_resv_usage old_usage;
	bool victim = false, victim_shared = false, victim_x = false;
	bool need_x, need_shared;
	unsigned n = 0, victim_n = 0, i, sc;

	KASSERT(dma_resv_held(robj));
	KASSERT(fence != NULL);
	KASSERT(usage <= DMA_RESV_USAGE_BOOKKEEP);

	(void)dma_fence_get(fence);

	/* Find the one entry the new fence replaces, if any.  */
	while ((f = resv_nth_locked(robj, n, &old_usage)) != NULL) {
		if ((f->context == fence->context && old_usage >= usage &&
			dma_fence_is_later_or_same(fence, f)) ||
		    dma_fence_is_signaled(f)) {
			victim = true;
			victim_n = n;
			break;
		}
		n++;
	}
	if (victim) {
		/* Which storage does the victim free a slot in?  */
		sc = robj->fence != NULL ? robj->fence->shared_count : 0;
		i = victim_n - (robj->fence_excl != NULL ? 1 : 0);
		if (robj->fence_excl != NULL && victim_n == 0) {
			victim_shared = false;
			victim_x = false;
		} else if (i < sc) {
			victim_shared = true;
			victim_x = false;
		} else {
			victim_shared = false;
			victim_x = true;
		}
	}

	/* Make sure the destination has room, before the write section.  */
	need_x = need_shared = false;
	switch (usage) {
	case DMA_RESV_USAGE_READ:
		need_shared = true;
		break;
	case DMA_RESV_USAGE_WRITE:
		/* The previous latest writer moves to the tagged list.  */
		need_x = robj->fence_excl != NULL &&
		    !(victim && victim_n == 0);
		break;
	default:
		need_x = true;
		break;
	}
	if (need_x) {
		xl = robj->xfence;
		if ((xl == NULL || xl->count >= xl->max) && !victim_x)
			(void)dma_resv_xlist_reserve(robj, 1);
	}
	if (need_shared) {
		list = robj->fence;
		if ((list == NULL || list->shared_count >= list->shared_max) &&
		    robj->robj_prealloc == NULL && !victim_shared)
			(void)dma_resv_reserve_shared(robj, 1);
	}

	if (victim)
		spare = resv_victim_spare(robj, victim_n);

	dma_resv_write_begin(robj, &ticket);
	if (victim)
		old = resv_remove_nth_ws(robj, victim_n, spare, &removed_list);

	switch (usage) {
	case DMA_RESV_USAGE_READ:
		list = robj->fence;
		prealloc = robj->robj_prealloc;
		if (prealloc == NULL) {
			KASSERT(list != NULL);
			KASSERT(list->shared_count < list->shared_max);
			atomic_store_relaxed(&list->shared[list->shared_count],
			    fence);
			atomic_store_relaxed(&list->shared_count,
			    list->shared_count + 1);
		} else {
			sc = list != NULL ? list->shared_count : 0;
			KASSERT(sc < prealloc->shared_max);
			for (i = 0; i < sc; i++)
				prealloc->shared[i] = list->shared[i];
			prealloc->shared[sc] = fence;
			prealloc->shared_count = sc + 1;
			atomic_store_relaxed(&robj->fence, prealloc);
			robj->robj_prealloc = NULL;
			old_list = list;
		}
		break;
	case DMA_RESV_USAGE_WRITE:
		if (robj->fence_excl != NULL) {
			xl = robj->xfence;
			KASSERT(xl != NULL && xl->count < xl->max);
			xl->ent[xl->count].fence = robj->fence_excl;
			xl->ent[xl->count].usage = DMA_RESV_USAGE_WRITE;
			atomic_store_relaxed(&xl->count, xl->count + 1);
		}
		atomic_store_relaxed(&robj->fence_excl, fence);
		break;
	default:
		xl = robj->xfence;
		KASSERT(xl != NULL && xl->count < xl->max);
		xl->ent[xl->count].fence = fence;
		xl->ent[xl->count].usage = usage;
		atomic_store_relaxed(&xl->count, xl->count + 1);
		break;
	}
	dma_resv_write_commit(robj, &ticket);

	if (removed_list != NULL)
		objlist_defer_free(removed_list);
	if (old_list != NULL)
		objlist_defer_free(old_list);
	dma_fence_put(old);	/* NULL is fine */
}

/*
 * dma_resv_add_shared_fence(robj, fence)
 *
 *	5.6 name: add fence as a reader.  Caller holds the lock and has
 *	called dma_resv_reserve_shared.
 */
void
dma_resv_add_shared_fence(struct dma_resv *robj, struct dma_fence *fence)
{

	dma_resv_add_fence(robj, fence, DMA_RESV_USAGE_READ);
}

/*
 * dma_resv_replace_fences(robj, context, replacement, usage)
 *
 *	Replace every fence of the given context by replacement with
 *	the given usage.  Caller holds the lock.
 */
void
dma_resv_replace_fences(struct dma_resv *robj, uint64_t context,
    struct dma_fence *replacement, enum dma_resv_usage usage)
{
	struct dma_resv_write_ticket ticket;
	struct dma_fence *f, *old;
	struct dma_resv_list *spare, *removed;
	enum dma_resv_usage u;
	unsigned n;
	bool found = false;

	KASSERT(dma_resv_held(robj));

restart:
	for (n = 0; (f = resv_nth_locked(robj, n, &u)) != NULL; n++) {
		if (f->context != context)
			continue;
		spare = resv_victim_spare(robj, n);
		dma_resv_write_begin(robj, &ticket);
		old = resv_remove_nth_ws(robj, n, spare, &removed);
		dma_resv_write_commit(robj, &ticket);
		if (removed != NULL)
			objlist_defer_free(removed);
		dma_fence_put(old);
		found = true;
		goto restart;
	}
	if (found)
		dma_resv_add_fence(robj, replacement, usage);
}

/*
 * Iterators.
 */

struct dma_fence *
dma_resv_iter_first(struct dma_resv_iter *cursor)
{
	struct dma_fence *fence;

	KASSERT(dma_resv_held(cursor->obj));

	cursor->index = 0;
	fence = dma_resv_iter_next(cursor);
	cursor->is_restarted = true;
	return fence;
}

struct dma_fence *
dma_resv_iter_next(struct dma_resv_iter *cursor)
{
	struct dma_fence *fence;
	enum dma_resv_usage usage;

	KASSERT(dma_resv_held(cursor->obj));

	cursor->is_restarted = false;
	while ((fence = resv_nth_locked(cursor->obj, cursor->index++,
		    &usage)) != NULL) {
		if (usage <= cursor->usage) {
			cursor->fence_usage = usage;
			return fence;
		}
	}
	cursor->index--;	/* stay at the end */
	return NULL;
}

/*
 * dma_resv_iter_restart_unlocked(cursor)
 *
 *	Take a new snapshot of the object for the lockless walk.
 *	Caller is in an RCU read section.
 */
static void
dma_resv_iter_restart_unlocked(struct dma_resv_iter *cursor)
{
	struct dma_resv *robj = cursor->obj;
	const struct dma_resv_list *sl;
	const struct dma_resv_xlist *xl;

	cursor->rs_version = read_seqcount_begin(&robj->seq);
	cursor->rs_excl = atomic_load_consume(&robj->fence_excl);
	sl = atomic_load_consume(&robj->fence);
	xl = atomic_load_consume(&robj->xfence);
	cursor->rs_slist = sl;
	cursor->rs_xlist = xl;
	cursor->rs_nshared = sl != NULL ? atomic_load_relaxed(&sl->shared_count) : 0;
	cursor->rs_nx = xl != NULL ? atomic_load_relaxed(&xl->count) : 0;
	cursor->index = 0;
	cursor->num_fences = (cursor->rs_excl != NULL ? 1 : 0) +
	    cursor->rs_nshared + cursor->rs_nx;
	cursor->is_restarted = true;
}

/*
 * dma_resv_iter_walk_unlocked(cursor)
 *
 *	Advance to the next unsignalled fence whose usage is within the
 *	cursor's, take a reference to it and leave it in cursor->fence,
 *	or leave NULL there at the end.  Restart when the object
 *	changed.  Caller is in an RCU read section.
 */
static void
dma_resv_iter_walk_unlocked(struct dma_resv_iter *cursor)
{
	struct dma_resv *robj = cursor->obj;
	struct dma_fence *fence;
	enum dma_resv_usage usage;
	unsigned n;

	for (;;) {
		/* Drop the reference from the previous round.  */
		dma_fence_put(cursor->fence);
		cursor->fence = NULL;

		/* The snapshot is only good while the object is unchanged.  */
		if (read_seqcount_retry(&robj->seq, cursor->rs_version)) {
			dma_resv_iter_restart_unlocked(cursor);
			continue;
		}
		if (cursor->index >= cursor->num_fences)
			return;

		n = cursor->index;
		if (cursor->rs_excl != NULL) {
			if (n == 0) {
				fence = cursor->rs_excl;
				usage = DMA_RESV_USAGE_WRITE;
				goto got;
			}
			n--;
		}
		if (n < cursor->rs_nshared) {
			fence = atomic_load_relaxed(&cursor->rs_slist->shared[n]);
			usage = DMA_RESV_USAGE_READ;
		} else {
			n -= cursor->rs_nshared;
			fence = atomic_load_relaxed(&cursor->rs_xlist->ent[n].fence);
			usage = atomic_load_relaxed(&cursor->rs_xlist->ent[n].usage);
		}
got:		fence = fence != NULL ? dma_fence_get_rcu(fence) : NULL;
		if (fence == NULL ||
		    read_seqcount_retry(&robj->seq, cursor->rs_version)) {
			dma_fence_put(fence);
			dma_resv_iter_restart_unlocked(cursor);
			continue;
		}
		cursor->index++;
		cursor->fence = fence;
		cursor->fence_usage = usage;
		if (!dma_fence_is_signaled(fence) && usage <= cursor->usage)
			return;
	}
}

struct dma_fence *
dma_resv_iter_first_unlocked(struct dma_resv_iter *cursor)
{

	rcu_read_lock();
	dma_resv_iter_restart_unlocked(cursor);
	dma_resv_iter_walk_unlocked(cursor);
	rcu_read_unlock();

	return cursor->fence;
}

struct dma_fence *
dma_resv_iter_next_unlocked(struct dma_resv_iter *cursor)
{

	rcu_read_lock();
	cursor->is_restarted = false;
	dma_resv_iter_walk_unlocked(cursor);
	rcu_read_unlock();

	return cursor->fence;
}

/*
 * dma_resv_get_fences(robj, usage, nfencesp, fencesp)
 *
 *	Without the lock, collect referenced fences of the given usage
 *	or lower that have not signalled.  *fencesp is a kmalloc'ed
 *	array of *nfencesp fences (NULL if none); caller puts each
 *	fence and kfrees the array.  Returns 0 or -ENOMEM.
 */
int
dma_resv_get_fences(struct dma_resv *robj, enum dma_resv_usage usage,
    unsigned int *nfencesp, struct dma_fence ***fencesp)
{
	struct dma_resv_iter cursor;
	struct dma_fence *fence, **fences = NULL;
	unsigned int n = 0, cap = 0;

	dma_resv_iter_begin(&cursor, robj, usage);
	dma_resv_for_each_fence_unlocked(&cursor, fence) {
		if (dma_resv_iter_is_restarted(&cursor)) {
			while (n)
				dma_fence_put(fences[--n]);
			if (cursor.num_fences + 1 > cap) {
				kfree(fences);
				cap = cursor.num_fences + 1;
				fences = kmalloc_array(cap, sizeof(*fences),
				    GFP_KERNEL);
				if (fences == NULL) {
					dma_resv_iter_end(&cursor);
					*fencesp = NULL;
					*nfencesp = 0;
					return -ENOMEM;
				}
			}
		}
		KASSERT(n < cap);
		fences[n++] = dma_fence_get(fence);
	}
	dma_resv_iter_end(&cursor);

	if (n == 0) {
		kfree(fences);
		fences = NULL;
	}
	*fencesp = fences;
	*nfencesp = n;
	return 0;
}

/*
 * dma_resv_get_singleton(robj, usage, fencep)
 *
 *	One referenced fence that stands for all the unsignalled
 *	fences of the given usage or lower: NULL if there are none,
 *	the fence itself if one, otherwise a fence array.  Returns 0 or
 *	-ENOMEM.
 */
int
dma_resv_get_singleton(struct dma_resv *robj, enum dma_resv_usage usage,
    struct dma_fence **fencep)
{
	struct dma_fence_array *array;
	struct dma_fence **fences;
	unsigned int count;
	int error;

	error = dma_resv_get_fences(robj, usage, &count, &fences);
	if (error)
		return error;
	if (count == 0) {
		*fencep = NULL;
		return 0;
	}
	if (count == 1) {
		*fencep = fences[0];
		kfree(fences);
		return 0;
	}
	array = dma_fence_array_create(count, fences,
	    dma_fence_context_alloc(1), 1, false);
	if (array == NULL) {
		while (count--)
			dma_fence_put(fences[count]);
		kfree(fences);
		return -ENOMEM;
	}
	*fencep = &array->base;
	return 0;
}

/*
 * dma_resv_wait_timeout(robj, usage, intr, timeout)
 *
 *	Wait for the fences of the given usage or lower.  Returns
 *	-ERESTARTSYS if interrupted, 0 on timeout, otherwise the time
 *	left (1 for a zero timeout, which only tests).
 */
long
dma_resv_wait_timeout(struct dma_resv *robj, enum dma_resv_usage usage,
    bool intr, unsigned long timeout)
{
	struct dma_resv_iter cursor;
	struct dma_fence *fence;
	long ret = timeout ? timeout : 1;

	dma_resv_iter_begin(&cursor, robj, usage);
	dma_resv_for_each_fence_unlocked(&cursor, fence) {
		ret = dma_fence_wait_timeout(fence, intr, timeout);
		if (ret <= 0)
			break;
		/* Even for a zero timeout the return value is 1.  */
		if (timeout)
			timeout = ret;
	}
	dma_resv_iter_end(&cursor);

	return ret;
}

bool
dma_resv_test_signaled(struct dma_resv *robj, enum dma_resv_usage usage)
{
	struct dma_resv_iter cursor;
	struct dma_fence *fence;

	dma_resv_iter_begin(&cursor, robj, usage);
	dma_resv_for_each_fence_unlocked(&cursor, fence) {
		dma_resv_iter_end(&cursor);
		return false;
	}
	dma_resv_iter_end(&cursor);
	return true;
}

void
dma_resv_set_deadline(struct dma_resv *robj, enum dma_resv_usage usage,
    ktime_t deadline)
{
	struct dma_resv_iter cursor;
	struct dma_fence *fence;

	dma_resv_iter_begin(&cursor, robj, usage);
	dma_resv_for_each_fence_unlocked(&cursor, fence)
		dma_fence_set_deadline(fence, deadline);
	dma_resv_iter_end(&cursor);
}

/*
 * 5.6 names over the functions above.  The 5.6 "shared" flag selects
 * every fence (READ); without it only writers (WRITE).  Unlike 5.6 this
 * also waits for the exclusive fence when shared fences are present.
 */
bool
dma_resv_test_signaled_rcu(const struct dma_resv *robj, bool shared)
{

	return dma_resv_test_signaled(__UNCONST(robj),
	    shared ? DMA_RESV_USAGE_READ : DMA_RESV_USAGE_WRITE);
}

long
dma_resv_wait_timeout_rcu(const struct dma_resv *robj, bool shared,
    bool intr, unsigned long timeout)
{

	return dma_resv_wait_timeout(__UNCONST(robj),
	    shared ? DMA_RESV_USAGE_READ : DMA_RESV_USAGE_WRITE, intr, timeout);
}

/*
 * dma_resv_copy_fences(dst, src)
 *
 *	Replace the fences of dst by the unsignalled fences of src,
 *	with their usage.  Caller holds dst's lock; src need not be
 *	locked.  Returns 0 or -ENOMEM.
 */
int
dma_resv_copy_fences(struct dma_resv *dst, struct dma_resv *src)
{
	struct dma_resv_iter cursor;
	struct dma_resv_write_ticket ticket;
	struct dma_fence *fence, **fv = NULL;
	enum dma_resv_usage *uv = NULL;
	struct dma_resv_list *nlist = NULL, *olist;
	struct dma_resv_xlist *nx = NULL, *ox;
	struct dma_fence *oexcl, *nexcl = NULL;
	unsigned cap = 0, n = 0, i, nshared = 0, nxn = 0, osc;
	bool have_excl = false;

	KASSERT(dma_resv_held(dst));

	dma_resv_iter_begin(&cursor, src, DMA_RESV_USAGE_BOOKKEEP);
	dma_resv_for_each_fence_unlocked(&cursor, fence) {
		if (dma_resv_iter_is_restarted(&cursor)) {
			while (n)
				dma_fence_put(fv[--n]);
			if (cursor.num_fences + 1 > cap) {
				kfree(fv);
				kfree(uv);
				cap = cursor.num_fences + 1;
				fv = kmalloc_array(cap, sizeof(*fv),
				    GFP_KERNEL);
				uv = kmalloc_array(cap, sizeof(*uv),
				    GFP_KERNEL);
				if (fv == NULL || uv == NULL) {
					dma_resv_iter_end(&cursor);
					kfree(fv);
					kfree(uv);
					return -ENOMEM;
				}
			}
		}
		fv[n] = dma_fence_get(fence);
		uv[n] = dma_resv_iter_usage(&cursor);
		n++;
	}
	dma_resv_iter_end(&cursor);

	/* Sort the copies into the three storages: first writer is excl.  */
	for (i = 0; i < n; i++) {
		if (uv[i] == DMA_RESV_USAGE_READ)
			nshared++;
		else if (uv[i] == DMA_RESV_USAGE_WRITE && !have_excl)
			have_excl = true;
		else
			nxn++;
	}
	if (nshared) {
		nlist = objlist_alloc(nshared);
		nlist->shared_count = 0;
	}
	if (nxn)
		nx = xlist_alloc(nxn);
	have_excl = false;
	for (i = 0; i < n; i++) {
		if (uv[i] == DMA_RESV_USAGE_READ) {
			nlist->shared[nlist->shared_count++] = fv[i];
		} else if (uv[i] == DMA_RESV_USAGE_WRITE && !have_excl) {
			have_excl = true;
			nexcl = fv[i];
		} else {
			nx->ent[nx->count].fence = fv[i];
			nx->ent[nx->count].usage = uv[i];
			nx->count++;
		}
	}
	kfree(fv);
	kfree(uv);

	oexcl = dst->fence_excl;
	olist = dst->fence;
	ox = dst->xfence;
	osc = olist != NULL ? olist->shared_count : 0;

	dma_resv_write_begin(dst, &ticket);
	atomic_store_relaxed(&dst->fence_excl, nexcl);
	atomic_store_relaxed(&dst->fence, nlist);
	atomic_store_relaxed(&dst->xfence, nx);
	dma_resv_write_commit(dst, &ticket);

	/* Release what dst had.  */
	dma_fence_put(oexcl);
	if (olist != NULL) {
		for (i = 0; i < osc; i++)
			dma_fence_put(olist->shared[i]);
		objlist_defer_free(olist);
	}
	if (ox != NULL) {
		for (i = 0; i < ox->count; i++)
			dma_fence_put(ox->ent[i].fence);
		xlist_defer_free(ox);
	}
	return 0;
}

/*
 * dma_resv_poll_init(rpoll, lock)
 *
 *	Initialize reservation poll state.
 */
void
dma_resv_poll_init(struct dma_resv_poll *rpoll)
{

	mutex_init(&rpoll->rp_lock, MUTEX_DEFAULT, IPL_VM);
	selinit(&rpoll->rp_selq);
	rpoll->rp_claimed = 0;
}

/*
 * dma_resv_poll_fini(rpoll)
 *
 *	Release any resource associated with reservation poll state.
 */
void
dma_resv_poll_fini(struct dma_resv_poll *rpoll)
{

	KASSERT(rpoll->rp_claimed == 0);
	seldestroy(&rpoll->rp_selq);
	mutex_destroy(&rpoll->rp_lock);
}

/*
 * dma_resv_poll_cb(fence, fcb)
 *
 *	Callback to notify a reservation poll that a fence has
 *	completed.  Notify any waiters and allow the next poller to
 *	claim the callback.
 *
 *	If one thread is waiting for the exclusive fence only, and we
 *	spuriously notify them about a shared fence, tough.
 */
static void
dma_resv_poll_cb(struct dma_fence *fence, struct dma_fence_cb *fcb)
{
	struct dma_resv_poll *rpoll = container_of(fcb,
	    struct dma_resv_poll, rp_fcb);

	mutex_enter(&rpoll->rp_lock);
	selnotify(&rpoll->rp_selq, 0, NOTE_SUBMIT);
	rpoll->rp_claimed = 0;
	mutex_exit(&rpoll->rp_lock);
}

/*
 * dma_resv_do_poll(robj, events, rpoll)
 *
 *	Poll for reservation object events using the reservation poll
 *	state in rpoll:
 *
 *	- POLLOUT	wait for all fences shared and exclusive
 *	- POLLIN	wait for the exclusive fence
 *
 *	Return the subset of events in events that are ready.  If any
 *	are requested but not ready, arrange to be notified with
 *	selnotify when they are.
 */
int
dma_resv_do_poll(const struct dma_resv *robj, int events,
    struct dma_resv_poll *rpoll)
{
	struct dma_resv_read_ticket ticket;
	const struct dma_resv_list *list;
	struct dma_fence *fence = NULL;
	uint32_t i, shared_count;
	int revents;
	bool recorded = false;	/* curlwp is on the selq */
	bool claimed = false;	/* we claimed the callback */
	bool callback = false;	/* we requested a callback */

	/*
	 * Start with the maximal set of events that could be ready.
	 * We will eliminate the events that are definitely not ready
	 * as we go at the same time as we add callbacks to notify us
	 * that they may be ready.
	 */
	revents = events & (POLLIN|POLLOUT);
	if (revents == 0)
		return 0;

top:	KASSERT(fence == NULL);

	/* Enter an RCU read section and get a read ticket.  */
	rcu_read_lock();
	dma_resv_read_begin(robj, &ticket);

	/* If we want to wait for all fences, get the shared list.  */
	if (events & POLLOUT) {
		if (!dma_resv_get_shared_reader(robj, &list, &shared_count,
			&ticket))
			goto restart;
	} else {
		list = NULL;
		shared_count = 0;
	}
	if (list != NULL) do {
		/*
		 * For each fence, if it is going away, restart.
		 * Otherwise, acquire a reference to it to test whether
		 * it is signalled.  Stop and request a callback if we
		 * find any that is not signalled.
		 */
		for (i = 0; i < shared_count; i++) {
			KASSERT(fence == NULL);
			fence = atomic_load_relaxed(&list->shared[i]);
			if ((fence = dma_fence_get_rcu(fence)) == NULL)
				goto restart;
			if (!dma_fence_is_signaled(fence)) {
				dma_fence_put(fence);
				fence = NULL;
				break;
			}
			dma_fence_put(fence);
			fence = NULL;
		}

		/* If all shared fences have been signalled, move on.  */
		if (i == shared_count)
			break;

		/* Put ourselves on the selq if we haven't already.  */
		if (!recorded)
			goto record;

		/*
		 * If someone else claimed the callback, or we already
		 * requested it, we're guaranteed to be notified, so
		 * assume the event is not ready.
		 */
		if (!claimed || callback) {
			revents &= ~POLLOUT;
			break;
		}

		/*
		 * Otherwise, find the first fence that is not
		 * signalled, request the callback, and clear POLLOUT
		 * from the possible ready events.  If they are all
		 * signalled, leave POLLOUT set; we will simulate the
		 * callback later.
		 */
		for (i = 0; i < shared_count; i++) {
			KASSERT(fence == NULL);
			fence = atomic_load_relaxed(&list->shared[i]);
			if ((fence = dma_fence_get_rcu(fence)) == NULL)
				goto restart;
			if (!dma_fence_add_callback(fence, &rpoll->rp_fcb,
				dma_resv_poll_cb)) {
				dma_fence_put(fence);
				fence = NULL;
				revents &= ~POLLOUT;
				callback = true;
				break;
			}
			dma_fence_put(fence);
			fence = NULL;
		}
	} while (0);

	/* We always wait for at least the exclusive fence, so get it.  */
	KASSERT(fence == NULL);
	if (!dma_resv_get_excl_reader(robj, &fence, &ticket))
		goto restart;
	if (fence != NULL) do {
		/*
		 * Test whether it is signalled.  If not, stop and
		 * request a callback.
		 */
		if (dma_fence_is_signaled(fence))
			break;

		/* Put ourselves on the selq if we haven't already.  */
		if (!recorded) {
			dma_fence_put(fence);
			fence = NULL;
			goto record;
		}

		/*
		 * If someone else claimed the callback, or we already
		 * requested it, we're guaranteed to be notified, so
		 * assume the event is not ready.
		 */
		if (!claimed || callback) {
			revents = 0;
			break;
		}

		/*
		 * Otherwise, try to request the callback, and clear
		 * all possible ready events.  If the fence has been
		 * signalled in the interim, leave the events set; we
		 * will simulate the callback later.
		 */
		if (!dma_fence_add_callback(fence, &rpoll->rp_fcb,
			dma_resv_poll_cb)) {
			revents = 0;
			callback = true;
			break;
		}
	} while (0);
	if (fence != NULL) {
		dma_fence_put(fence);
		fence = NULL;
	}

	/* All done reading the fences.  */
	rcu_read_unlock();

	if (claimed && !callback) {
		/*
		 * We claimed the callback but we didn't actually
		 * request it because a fence was signalled while we
		 * were claiming it.  Call it ourselves now.  The
		 * callback doesn't use the fence nor rely on holding
		 * any of the fence locks, so this is safe.
		 */
		dma_resv_poll_cb(NULL, &rpoll->rp_fcb);
	}
	return revents;

restart:
	KASSERT(fence == NULL);
	rcu_read_unlock();
	goto top;

record:
	KASSERT(fence == NULL);
	rcu_read_unlock();
	mutex_enter(&rpoll->rp_lock);
	selrecord(curlwp, &rpoll->rp_selq);
	if (!rpoll->rp_claimed)
		claimed = rpoll->rp_claimed = true;
	mutex_exit(&rpoll->rp_lock);
	recorded = true;
	goto top;
}

/*
 * dma_resv_kqfilter(robj, kn, rpoll)
 *
 *	Kqueue filter for reservation objects.  Currently not
 *	implemented because the logic to implement it is nontrivial,
 *	and userland will presumably never use it, so it would be
 *	dangerous to add never-tested complex code paths to the kernel.
 */
int
dma_resv_kqfilter(const struct dma_resv *robj,
    struct knote *kn, struct dma_resv_poll *rpoll)
{

	return EINVAL;
}
