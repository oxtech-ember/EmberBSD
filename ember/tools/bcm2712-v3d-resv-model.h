/* SPDX-License-Identifier: BSD-2-Clause */
/* Controlled WW, reservation and fence services for actual job/helper bodies. */
#ifndef ERESTARTSYS
#define ERESTARTSYS 512
#endif
#define EXPORT_SYMBOL(n)
struct ww_acquire_ctx { unsigned acquired; bool initialized, done; };
static int reservation_ww_class;
struct dma_resv {
	struct ww_acquire_ctx *owner;
	struct dma_fence *exclusive, *shared;
};
static kmutex_t *resv_engine_lock;
static unsigned resv_objects, resv_locks, resv_contexts, resv_arrays;
static unsigned resv_prepares, resv_publications, resv_waits;
static unsigned resv_lock_calls, resv_snapshot_calls, resv_fault_at;
static int resv_lock_error;
static bool resv_snapshot_fail;
static long resv_wait_result;
static bool resv_wait_fault;
static void
contract_resv_init(struct drm_gem_object *o)
{
	o->resv = calloc(1, sizeof(*o->resv));
	CHECK(o->resv != NULL); resv_objects++;
}
static void
contract_resv_fini(const struct drm_gem_object *o)
{
	CHECK(o->resv != NULL && o->resv->owner == NULL && resv_objects > 0);
	dma_fence_put(o->resv->exclusive); dma_fence_put(o->resv->shared);
	free(o->resv); resv_objects--;
}
static void
ww_acquire_init(struct ww_acquire_ctx *c, int *class)
{
	CHECK(class == &reservation_ww_class);
	CHECK(resv_engine_lock == NULL || *resv_engine_lock == 0);
	*c = (struct ww_acquire_ctx){.initialized = true}; resv_contexts++;
	resv_prepares++;
}
static void ww_acquire_done(struct ww_acquire_ctx *c)
{ CHECK(c->initialized && !c->done); c->done = true; }
static void ww_acquire_fini(struct ww_acquire_ctx *c)
{ CHECK(c->initialized && c->done && c->acquired == 0 && resv_contexts > 0); c->initialized = false; resv_contexts--; }
static int
dma_resv_lock_interruptible(struct dma_resv *r, struct ww_acquire_ctx *c)
{
	CHECK((resv_engine_lock == NULL || *resv_engine_lock == 0) && !r->owner);
	CHECK(c->initialized && !c->done); resv_lock_calls++;
	if (resv_lock_error && resv_lock_calls == resv_fault_at)
		return -resv_lock_error;
	r->owner = c; c->acquired++; resv_locks++;
	return 0;
}
static int dma_resv_lock_slow_interruptible(struct dma_resv *r, struct ww_acquire_ctx *c)
{ return dma_resv_lock_interruptible(r, c); }
static void dma_resv_unlock(struct dma_resv *r)
{ CHECK(r->owner && r->owner->acquired && resv_locks); r->owner->acquired--; r->owner = NULL; resv_locks--; }
#include "gem-lock-body.h"
static int
dma_resv_get_fences_rcu(const struct dma_resv *r, struct dma_fence **excl,
    unsigned *count, struct dma_fence ***shared)
{
	CHECK(r->owner && (resv_engine_lock == NULL || *resv_engine_lock == 0));
	resv_snapshot_calls++;
	if (resv_snapshot_fail && resv_snapshot_calls == resv_fault_at)
		return -ENOMEM;
	*excl = dma_fence_get(r->exclusive); *count = r->shared != NULL;
	*shared = NULL;
	if (*count) {
		*shared = malloc(sizeof(**shared)); CHECK(*shared != NULL); resv_arrays++;
		(*shared)[0] = dma_fence_get(r->shared);
	}
	return 0;
}
static void kfree(void *p) { if (p) { CHECK(resv_arrays); resv_arrays--; free(p); } }
static bool dma_resv_held(struct dma_resv *r) { return r->owner != NULL; }
static struct ww_acquire_ctx *dma_resv_locking_ctx(struct dma_resv *r) { return r->owner; }
static void
dma_resv_add_excl_fence(struct dma_resv *r, struct dma_fence *f)
{
	CHECK(resv_engine_lock && *resv_engine_lock && r->owner && r->owner->done);
	CHECK(f && f->status == 0 && f->context != 0 && f->seqno != 0);
	dma_fence_put(r->exclusive); dma_fence_put(r->shared);
	r->exclusive = dma_fence_get(f); r->shared = NULL; resv_publications++;
}
#include "resv-types.h"
#include "resv-body.h"
