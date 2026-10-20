/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual read helper/shared publication. Controlled fence, WW, GEM and RCU. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifndef ETIME
#define ETIME 62
#endif
#define ERESTARTSYS 512
#define MAX_ERRNO 4095
#define GFP_KERNEL 0
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define atomic_store_relaxed(p,v) (*(p)=(v))
static unsigned checks;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"CHECK %d: %s\n",__LINE__,#x); exit(1); } checks++; } while(0)
#define KASSERT CHECK
struct rcu_head { void (*callback)(struct rcu_head *); struct rcu_head *next; };
typedef struct { bool live; } spinlock_t;
struct dma_fence;
struct dma_fence_ops {
	bool use_64bit_seqno;
	const char *(*get_driver_name)(struct dma_fence *);
	const char *(*get_timeline_name)(struct dma_fence *);
	void (*release)(struct dma_fence *);
};
struct dma_fence {
	const struct dma_fence_ops *ops;
	spinlock_t *lock;
	struct rcu_head rcu;
	uint64_t context, seqno;
	unsigned refs;
	int status;
	bool initialized;
};
struct dma_resv_list { uint32_t shared_count, shared_max; struct dma_fence *shared[8]; };
struct ww_acquire_ctx;
struct dma_resv { struct dma_resv_list *fence, *robj_prealloc; struct dma_fence *fence_excl; struct ww_acquire_ctx *ctx; bool locked; };
struct dma_resv_write_ticket { unsigned unused; };
struct drm_gem_object { struct dma_resv *resv; unsigned refs; };
struct ww_acquire_ctx { unsigned acquired; bool initialized, done; };
static int reservation_ww_class;
static struct dma_resv reservation;
static struct drm_gem_object object;
static struct dma_fence producer, writer;
static spinlock_t writer_lock;
static struct rcu_head *rcu_queue;
static unsigned allocations, locks_live, fences_live, waits, signals, lock_calls;
static bool fail_alloc, fail_reserve, complete_on_wait, rcu_running;
static int lock_error;
static long wait_result;
static uint64_t next_context;
static void *snapshot_array;
static void (*reader_progress)(void);
static void *kzalloc(size_t size, int flags)
{ CHECK(flags==0); if(fail_alloc)return NULL; void *p=calloc(1,size);CHECK(p);allocations++;return p; }
static void kfree(void *p) { if(!p)return; if(p==snapshot_array){snapshot_array=NULL;free(p);return;} CHECK(rcu_running && allocations); allocations--;free(p); }
static void spin_lock_init(spinlock_t *lock) { CHECK(!lock->live);lock->live=true;locks_live++; }
static void spin_lock_destroy(spinlock_t *lock)
{ CHECK(rcu_running && lock->live && locks_live);lock->live=false;locks_live--; }
static uint64_t dma_fence_context_alloc(unsigned n) { CHECK(n==1);return next_context++; }
static void dma_fence_init(struct dma_fence *f, const struct dma_fence_ops *ops,
    spinlock_t *lock, uint64_t context, uint64_t seqno)
{ CHECK(lock->live && ops->use_64bit_seqno && ops->release);f->ops=ops;f->lock=lock;f->context=context;f->seqno=seqno;f->refs=1;f->initialized=true;fences_live++; }
static void call_rcu(struct rcu_head *h, void (*cb)(struct rcu_head *))
{ CHECK(!h->callback);h->callback=cb;h->next=rcu_queue;rcu_queue=h; }
static void dma_fence_destroy(struct dma_fence *f)
{ CHECK(rcu_running && f->refs==0 && f->initialized && f->lock->live);f->initialized=false;CHECK(fences_live);fences_live--; }
static struct dma_fence *dma_fence_get(struct dma_fence *f) { if(f){CHECK(f->refs);f->refs++;}return f; }
static void dma_fence_put(struct dma_fence *f)
{ if(f){CHECK(f->refs);if(--f->refs==0){CHECK(f->ops && f->ops->release);f->ops->release(f);}} }
static int dma_fence_get_status(struct dma_fence *f) { CHECK(f && f->refs);return f->status; }
static long dma_fence_wait_timeout(struct dma_fence *f, bool intr, long timeout)
{ CHECK(f && f->refs>=3 && intr && timeout==17);waits++;if(f==&producer || f==&writer){CHECK(f->refs==3);}if(f==&producer || f==&writer){if(complete_on_wait)f->status=1;}else if(reader_progress)reader_progress();return wait_result; }
static void dma_fence_set_error(struct dma_fence *f,int error) { CHECK(f->status==0 && error<0);f->status=error; }
static int dma_fence_signal(struct dma_fence *f)
{ CHECK(f->refs && f->lock->live);signals++;if(!f->status)f->status=1;return 0; }
static void drain_rcu(void)
{
	CHECK(!rcu_running);rcu_running=true;
	while(rcu_queue){struct rcu_head *h=rcu_queue;rcu_queue=h->next;h->callback(h);}
	rcu_running=false;
}
static void clear_reservation(void)
{
	struct dma_resv_list *l=reservation.fence;
	CHECK(!reservation.locked);
	if(l){for(unsigned i=0;i<l->shared_count;i++)dma_fence_put(l->shared[i]);free(l);}
	free(reservation.robj_prealloc);reservation.fence=reservation.robj_prealloc=NULL;
	if(reservation.fence_excl){dma_fence_put(reservation.fence_excl);reservation.fence_excl=NULL;}
}
static void drm_gem_object_get(struct drm_gem_object *o) { CHECK(o==&object && o->refs);o->refs++; }
static void drm_gem_object_put_unlocked(struct drm_gem_object *o)
{ CHECK(o==&object && o->refs);if(--o->refs==0)clear_reservation(); }
static int dma_resv_lock_interruptible(struct dma_resv *r,struct ww_acquire_ctx *ctx)
{ CHECK(r==&reservation && !r->locked);lock_calls++;if(lock_error)return -lock_error;r->locked=true;r->ctx=ctx;if(ctx)ctx->acquired++;return 0; }
static void dma_resv_unlock(struct dma_resv *r) { CHECK(r->locked);if(r->ctx){CHECK(r->ctx->acquired);r->ctx->acquired--;}r->ctx=NULL;r->locked=false; }
static bool dma_resv_held(struct dma_resv *r) { return r->locked; }
static struct dma_fence *dma_resv_get_excl(struct dma_resv *r) { CHECK(r->locked);return r->fence_excl; }
static int dma_resv_reserve_shared(struct dma_resv *r,unsigned n)
{
	CHECK(r->locked && n==1);
	if(fail_reserve)return -ENOMEM;
	if(r->fence && r->fence->shared_count<r->fence->shared_max)return 0;
	CHECK(!r->robj_prealloc);r->robj_prealloc=calloc(1,sizeof(*r->robj_prealloc));CHECK(r->robj_prealloc);r->robj_prealloc->shared_max=8;return 0;
}
static void dma_resv_write_begin(struct dma_resv *r,struct dma_resv_write_ticket *t) { CHECK(r->locked);(void)t; }
static void dma_resv_write_commit(struct dma_resv *r,struct dma_resv_write_ticket *t) { CHECK(r->locked);(void)t; }
static void objlist_defer_free(struct dma_resv_list *l) { free(l); }
static void ww_acquire_init(struct ww_acquire_ctx *c,int *class)
{ CHECK(class==&reservation_ww_class);*c=(struct ww_acquire_ctx){.initialized=true}; }
static void ww_acquire_done(struct ww_acquire_ctx *c) { CHECK(c->initialized && !c->done);c->done=true; }
static void ww_acquire_fini(struct ww_acquire_ctx *c) { CHECK(c->initialized && c->done && !c->acquired);c->initialized=false; }
static int dma_resv_lock_slow_interruptible(struct dma_resv *r,struct ww_acquire_ctx *c)
{ return dma_resv_lock_interruptible(r,c); }
static struct ww_acquire_ctx *dma_resv_locking_ctx(struct dma_resv *r) { return r->ctx; }
static int dma_resv_get_fences_rcu(const struct dma_resv *r,struct dma_fence **excl,unsigned *n,struct dma_fence ***shared)
{
	CHECK(r->locked && !snapshot_array);*excl=dma_fence_get(r->fence_excl);
	*n=r->fence?r->fence->shared_count:0;*shared=NULL;
	if(*n){*shared=malloc(*n*sizeof(**shared));CHECK(*shared);snapshot_array=*shared;for(unsigned i=0;i<*n;i++)(*shared)[i]=dma_fence_get(r->fence->shared[i]);}
	return 0;
}
#include "gem-lock-body.h"
#include "shared-body.h"
#include "read-types.h"
#include "read-body.h"
#include "resv-types.h"
#include "resv-body.h"
static void reset(bool prior)
{
	CHECK(!allocations && !locks_live && !fences_live && !rcu_queue);
	memset(&reservation,0,sizeof(reservation));object=(struct drm_gem_object){.resv=&reservation,.refs=1};
	producer=(struct dma_fence){.refs=1,.status=1};
	if(prior)reservation.fence_excl=dma_fence_get(&producer);
	fail_alloc=fail_reserve=complete_on_wait=false;lock_error=0;wait_result=17;
	waits=signals=lock_calls=0;next_context=0;reader_progress=NULL;CHECK(!snapshot_array);
}
static void finish(void)
{
	CHECK(object.refs==1 && !reservation.locked);
	clear_reservation();CHECK(producer.refs==1);
	CHECK(allocations==locks_live && allocations==fences_live);
	drain_rcu();CHECK(!allocations && !locks_live && !fences_live && !rcu_queue);
}
static struct bcmv3d_read *waiting_reader;
static void finish_waiting_reader(void)
{
	CHECK(reservation.locked && reservation.ctx && reservation.ctx->done);
	CHECK(bcmv3d_read_end(waiting_reader)==0);
	reader_progress=NULL;
}
static void joined(void)
{
	struct bcmv3d_read read={0};
	struct bcmv3d_resv write={0};
	struct drm_gem_object *objects[]={&object};

	reset(true);
	CHECK(bcmv3d_read_begin(&read,&object,17)==0);
	waiting_reader=&read;reader_progress=finish_waiting_reader;
	CHECK(bcmv3d_resv_prepare(&write,objects,1,17)==0);
	CHECK(!read.owner && write.locked && object.refs==1 && !snapshot_array);
	writer_lock.live=true;writer=(struct dma_fence){.refs=1,.context=999,.seqno=1,.lock=&writer_lock};
	CHECK(bcmv3d_resv_publish(&write,&writer)==0);
	CHECK(reservation.fence_excl==&writer && reservation.fence->shared_count==0);
	bcmv3d_resv_unlock(&write);CHECK(dma_fence_signal(&writer)==0);
	CHECK(bcmv3d_read_begin(&read,&object,17)==0);
	CHECK(read.fence->context==1 && bcmv3d_read_end(&read)==0);
	clear_reservation();CHECK(producer.refs==1 && writer.refs==1 && object.refs==1);
	drain_rcu();CHECK(!allocations && !locks_live && !fences_live && !snapshot_array);
}
int main(void)
{
	struct bcmv3d_read a={0},b={0},copy;
	int expected, error;
	unsigned calls;

	reset(false);
	CHECK(bcmv3d_read_begin(NULL,&object,17)==EINVAL);
	CHECK(bcmv3d_read_begin(&a,NULL,17)==EINVAL);
	CHECK(bcmv3d_read_begin(&a,&object,0)==EINVAL);
	CHECK(bcmv3d_read_end(NULL)==EINVAL && bcmv3d_read_end(&a)==0);
	CHECK(bcmv3d_read_poison(&a,0)==EINVAL);
	CHECK(bcmv3d_read_begin(&a,&object,17)==0);
	CHECK(a.fence->context==0 && a.fence->seqno==1);
	CHECK(bcmv3d_read_begin(&a,&object,17)==EBUSY);
	copy=a;CHECK(bcmv3d_read_end(&copy)==EPERM && object.refs==2 && signals==0);
	CHECK(bcmv3d_read_poison(&copy,EIO)==EPERM);
	CHECK(bcmv3d_read_begin(&b,&object,17)==0);
	CHECK(reservation.fence->shared_count==2 && a.fence!=b.fence && a.fence->context!=b.fence->context);
	CHECK(a.fence->refs==2 && b.fence->refs==2 && object.refs==3 && waits==0);
	CHECK(bcmv3d_read_poison(&b,EIO)==0);
	CHECK(a.fence->status==0 && b.fence->status==-EIO && object.refs==3);
	CHECK(bcmv3d_read_end(&b)==EBUSY && bcmv3d_read_poison(&b,EIO)==EALREADY);
	/* Model another writer holding the BO reservation while waiting for A. */
	reservation.locked=true;calls=lock_calls;
	CHECK(bcmv3d_read_end(&a)==0 && lock_calls==calls && object.refs==2);
	reservation.locked=false;CHECK(bcmv3d_read_end(&a)==0);
	/* Model reboot only: production deliberately cannot release poisoned B. */
	drm_gem_object_put_unlocked(b.object);dma_fence_put(b.fence);memset(&b,0,sizeof(b));finish();
	for(unsigned mode=0;mode<=9;mode++) {
		reset(true);memset(&a,0,sizeof(a));expected=0;
		if(mode==1){producer.status=0;wait_result=0;expected=ETIME;}
		if(mode==2){producer.status=0;wait_result=-EINTR;expected=EINTR;}
		if(mode==3){producer.status=0;wait_result=-ERESTARTSYS;expected=ERESTARTSYS;}
		if(mode==4){producer.status=-EIO;expected=EIO;}
		if(mode==5){producer.status=0;expected=EIO;}
		if(mode==6){fail_alloc=true;expected=ENOMEM;}
		if(mode==7){lock_error=EINTR;expected=EINTR;}
		if(mode==8){fail_reserve=true;expected=ENOMEM;}
		if(mode==9){producer.status=0;complete_on_wait=true;}
		error=bcmv3d_read_begin(&a,&object,17);CHECK(error==expected);
		if(expected)CHECK(!a.owner && !a.object && !a.fence && !a.poisoned && object.refs==1 && !reservation.fence);
		else CHECK(bcmv3d_read_end(&a)==0 && object.refs==1);
		CHECK(!reservation.locked && producer.refs==2);finish();
	}
	reset(false);memset(&a,0,sizeof(a));
	CHECK(bcmv3d_read_begin(&a,&object,17)==0);drm_gem_object_put_unlocked(&object);
	CHECK(object.refs==1 && bcmv3d_read_end(&a)==0 && object.refs==0);
	CHECK(!reservation.fence && allocations==1 && locks_live==1);drain_rcu();
	CHECK(!allocations && !locks_live && !fences_live);
	joined();
	printf("PASS actual BO reader lifetime/shared publication: %u checks\n",checks);
	return 0;
}
