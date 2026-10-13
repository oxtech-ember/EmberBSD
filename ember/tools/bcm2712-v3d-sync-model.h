/* Origin: EmberBSD native fence ownership model, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
typedef kmutex_t spinlock_t;
#define GFP_KERNEL 0
struct dma_fence;
struct dma_fence_ops {
    bool use_64bit_seqno;
    const char *(*get_driver_name)(struct dma_fence *);
    const char *(*get_timeline_name)(struct dma_fence *);
};
struct dma_fence { unsigned refs; int status; uint64_t context, seqno; };
struct drm_syncobj { unsigned refs; struct dma_fence *fence; };
static unsigned fence_count, sync_count;
static bool fail_fence_alloc;
static long input_wait_result=1;
static void (*on_wait)(void), (*on_signal)(struct dma_fence *);
static void spin_lock_init(spinlock_t *p) { *p=0; }
static void spin_lock_destroy(spinlock_t *p) { CHECK(*p==0); }
static void *kzalloc(size_t n,int flag) { return fail_fence_alloc?NULL:calloc(1,n); }
static uint64_t dma_fence_context_alloc(unsigned n)
{ static uint64_t next=1; uint64_t c=next; next+=n; return c; }
static void dma_fence_init(struct dma_fence *f,const struct dma_fence_ops *ops,
    spinlock_t *lock,uint64_t context,uint64_t seqno)
{ CHECK(ops->use_64bit_seqno); f->refs=1; f->context=context; f->seqno=seqno; fence_count++; }
static struct dma_fence *dma_fence_get(struct dma_fence *p)
{ if(p) { CHECK(p->refs>0); p->refs++; } return p; }
static void dma_fence_put(struct dma_fence *p)
{ if(p) { CHECK(p->refs>0); if(--p->refs==0) { fence_count--; free(p); } } }
static struct drm_syncobj *drm_syncobj_find(struct drm_file *f,uint32_t h)
{ if(h>=4 || !f->syncs[h])return NULL; f->syncs[h]->refs++; return f->syncs[h]; }
static void drm_syncobj_put(struct drm_syncobj *s)
{ CHECK(s && s->refs>0); if(--s->refs==0) { dma_fence_put(s->fence); sync_count--; free(s); } }
static struct dma_fence *drm_syncobj_fence_get(struct drm_syncobj *s)
{ return dma_fence_get(s->fence); }
static void drm_syncobj_replace_fence(struct drm_syncobj *s,struct dma_fence *f)
{ struct dma_fence *old=s->fence; s->fence=dma_fence_get(f); dma_fence_put(old); }
static long dma_fence_wait_timeout(struct dma_fence *f,bool intr,long timeout)
{ CHECK(intr && timeout>0); if(on_wait)on_wait(); return f->status?1:input_wait_result; }
static int dma_fence_get_status(struct dma_fence *f) { return f->status; }
static void dma_fence_set_error(struct dma_fence *f,int err)
{ CHECK(err<0 && !f->status); f->status=err; }
static int dma_fence_signal(struct dma_fence *f)
{ if(on_signal)on_signal(f); if(!f->status)f->status=1; return 0; }
