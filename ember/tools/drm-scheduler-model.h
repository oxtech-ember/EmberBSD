/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Kernel boundaries only. Scheduler structs and algorithms come from source. */
#ifndef DRM_SCHEDULER_MODEL_H
#define DRM_SCHEDULER_MODEL_H
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef CHECK
static atomic_uint model_checks;
#define CHECK(e) do { atomic_fetch_add(&model_checks, 1); if (!(e)) { \
	fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #e); abort(); \
} } while (0)
#endif
#define KASSERT(e) CHECK(e)
#define WARN_ON(e) CHECK(!(e))
#define WARN_ON_ONCE(e) CHECK(!(e))
#ifndef container_of
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#endif
#define READ_ONCE(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define WRITE_ONCE(x,v) __atomic_store_n(&(x), (v), __ATOMIC_RELEASE)
#define cmpxchg(p,o,n) ({ __typeof__(*(p)) _old = (o); \
	__atomic_compare_exchange_n((p), &_old, (n), false, \
	    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); _old; })
#define unlikely(x) (x)
#define smp_wmb() atomic_thread_fence(memory_order_seq_cst)
#define smp_rmb() atomic_thread_fence(memory_order_seq_cst)
#define preempt_disable() ((void)0)
#define preempt_enable() ((void)0)
typedef atomic_int atomic_t;
typedef atomic_llong atomic64_t;
typedef atomic_long atomic_long_t;
#define atomic_set(p,v) atomic_store((p),(v))
#define atomic_read(p) atomic_load(p)
#define atomic_inc(p) ((void)atomic_fetch_add((p),1))
#define atomic_dec(p) ((void)atomic_fetch_sub((p),1))
#define atomic_inc_return(p) (atomic_fetch_add((p),1)+1)
#define atomic64_inc_return(p) (atomic_fetch_add((p),1)+1)
#define atomic64_set(p,v) atomic_store((p),(v))
#define atomic_long_set(p,v) atomic_store((p),(v))
#define atomic_long_xchg(p,v) atomic_exchange((p),(v))
#define atomic_long_cmpxchg(p,o,n) ({ long _old=(o); \
	atomic_compare_exchange_strong((p),&_old,(n)); _old; })

struct list_head { struct list_head *next, *prev; };
#define INIT_LIST_HEAD(h) ((h)->next = (h)->prev = (h))
static bool list_empty(const struct list_head *h) { return h->next == h; }
static void model_list_add(struct list_head *n, struct list_head *p,
    struct list_head *q)
{ n->prev=p; n->next=q; p->next=n; q->prev=n; }
static void list_add(struct list_head *n, struct list_head *h)
{ model_list_add(n,h,h->next); }
static void list_add_tail(struct list_head *n, struct list_head *h)
{ model_list_add(n,h->prev,h); }
static void list_del_init(struct list_head *n)
{ n->next->prev=n->prev; n->prev->next=n->next; INIT_LIST_HEAD(n); }
#define list_entry(p,t,m) container_of(p,t,m)
#define list_first_entry_or_null(h,t,m) \
	(list_empty(h) ? NULL : list_entry((h)->next,t,m))
#define list_for_each_entry(p,h,m) \
	for ((p)=list_entry((h)->next,__typeof__(*(p)),m); &(p)->m!=(h); \
	    (p)=list_entry((p)->m.next,__typeof__(*(p)),m))
#define list_for_each_entry_continue(p,h,m) \
	for ((p)=list_entry((p)->m.next,__typeof__(*(p)),m); &(p)->m!=(h); \
	    (p)=list_entry((p)->m.next,__typeof__(*(p)),m))
#define list_for_each_entry_safe(p,n,h,m) \
	for ((p)=list_entry((h)->next,__typeof__(*(p)),m), \
	    (n)=list_entry((p)->m.next,__typeof__(*(p)),m); &(p)->m!=(h); \
	    (p)=(n),(n)=list_entry((n)->m.next,__typeof__(*(n)),m))
#define list_for_each_entry_safe_reverse(p,n,h,m) \
	for ((p)=list_entry((h)->prev,__typeof__(*(p)),m), \
	    (n)=list_entry((p)->m.prev,__typeof__(*(p)),m); &(p)->m!=(h); \
	    (p)=(n),(n)=list_entry((n)->m.prev,__typeof__(*(n)),m))

static atomic_uint model_locks, model_conditions, model_fences, model_slabs;
static _Thread_local unsigned model_lock_depth;
#ifndef DRM_SCHED_MODEL_EXTERNAL_LOCKS
typedef struct { pthread_mutex_t mutex; pthread_t owner; bool held; } spinlock_t;
static void spin_lock_init(spinlock_t *l)
{ CHECK(pthread_mutex_init(&l->mutex,NULL)==0); l->held=false; atomic_inc(&model_locks); }
static void spin_lock_destroy(spinlock_t *l)
{ CHECK(!l->held); CHECK(pthread_mutex_destroy(&l->mutex)==0); atomic_dec(&model_locks); }
static void spin_lock(spinlock_t *l)
{ CHECK(pthread_mutex_lock(&l->mutex)==0); l->held=true; l->owner=pthread_self(); model_lock_depth++; }
static void spin_unlock(spinlock_t *l)
{ CHECK(l->held && pthread_equal(l->owner,pthread_self())); l->held=false; model_lock_depth--; CHECK(pthread_mutex_unlock(&l->mutex)==0); }
static void assert_spin_locked(spinlock_t *l)
{ CHECK(l->held && pthread_equal(l->owner,pthread_self())); }
#endif
#define spin_lock_irqsave(l,f) do { (f)=0; spin_lock(l); } while (0)
#define spin_unlock_irqrestore(l,f) do { (void)(f); spin_unlock(l); } while (0)
typedef pthread_cond_t drm_waitqueue_t;
static void DRM_INIT_WAITQUEUE(drm_waitqueue_t *q, const char *name)
{ CHECK(pthread_cond_init(q,NULL)==0); atomic_inc(&model_conditions); }
static void DRM_DESTROY_WAITQUEUE(drm_waitqueue_t *q)
{ CHECK(pthread_cond_destroy(q)==0); atomic_dec(&model_conditions); }
static void DRM_SPIN_WAKEUP_ALL(drm_waitqueue_t *q, spinlock_t *l)
{ assert_spin_locked(l); CHECK(pthread_cond_broadcast(q)==0); }
#define DRM_SPIN_WAKEUP_ONE DRM_SPIN_WAKEUP_ALL
static void model_wait(drm_waitqueue_t *q, spinlock_t *l)
{
	struct timespec deadline;
	assert_spin_locked(l);
	CHECK(clock_gettime(CLOCK_REALTIME,&deadline)==0);
	deadline.tv_sec += 10;
	l->held=false; model_lock_depth--;
	CHECK(pthread_cond_timedwait(q,&l->mutex,&deadline)==0);
	l->held=true; l->owner=pthread_self(); model_lock_depth++;
}
#define DRM_SPIN_WAIT_UNTIL(r,q,l,c) do { \
	while (!(c)) model_wait((q),(l)); (r)=0; } while (0)
#define DRM_SPIN_WAIT_NOINTR_UNTIL DRM_SPIN_WAIT_UNTIL

struct completion { spinlock_t lock; drm_waitqueue_t cv; bool done; };
static void init_completion(struct completion *c)
{ spin_lock_init(&c->lock); DRM_INIT_WAITQUEUE(&c->cv,"complete"); c->done=false; }
static void destroy_completion(struct completion *c)
{ DRM_DESTROY_WAITQUEUE(&c->cv); spin_lock_destroy(&c->lock); }
static void reinit_completion(struct completion *c)
{ spin_lock(&c->lock); c->done=false; spin_unlock(&c->lock); }
static void complete(struct completion *c)
{ spin_lock(&c->lock); c->done=true; DRM_SPIN_WAKEUP_ALL(&c->cv,&c->lock); spin_unlock(&c->lock); }
static void wait_for_completion(struct completion *c)
{ spin_lock(&c->lock); while (!c->done) model_wait(&c->cv,&c->lock); spin_unlock(&c->lock); }

struct rcu_head { unsigned unused; };
static void call_rcu(struct rcu_head *r, void (*fn)(struct rcu_head *)) { fn(r); }
static void rcu_barrier(void) { }
#ifndef DRM_SCHED_MODEL_EXTERNAL_FENCES
struct dma_fence;
struct dma_fence_ops {
	bool use_64bit_seqno;
	const char *(*get_driver_name)(struct dma_fence *);
	const char *(*get_timeline_name)(struct dma_fence *);
	void (*release)(struct dma_fence *);
};
struct dma_fence_cb {
	struct dma_fence_cb *next;
	void (*fn)(struct dma_fence *,struct dma_fence_cb *);
};
struct dma_fence {
	struct rcu_head rcu;
	const struct dma_fence_ops *ops;
	spinlock_t *lock;
	drm_waitqueue_t cv;
	atomic_uint refs;
	atomic_bool signaled;
	int error;
	uint64_t context, seqno;
	struct dma_fence_cb *callbacks;
#ifdef DRM_SCHED_MODEL_FENCE_EXTRA
	DRM_SCHED_MODEL_FENCE_EXTRA
#endif
};
static bool dma_fence_is_signaled(struct dma_fence *);
static struct dma_fence *dma_fence_get(struct dma_fence *);
static void dma_fence_put(struct dma_fence *);
static uint64_t dma_fence_context_alloc(unsigned n)
{ static atomic_ullong next; return atomic_fetch_add(&next,n); }
static void dma_fence_init(struct dma_fence *f,const struct dma_fence_ops *ops,
    spinlock_t *lock,uint64_t context,uint64_t seqno)
{
	f->ops=ops; f->lock=lock; f->context=context; f->seqno=seqno;
	f->callbacks=NULL; f->error=0; atomic_init(&f->refs,1);
	atomic_init(&f->signaled,false); DRM_INIT_WAITQUEUE(&f->cv,"fence");
	atomic_inc(&model_fences);
#ifdef DRM_SCHED_MODEL_FENCE_INIT
	DRM_SCHED_MODEL_FENCE_INIT(f);
#endif
}
static void dma_fence_destroy(struct dma_fence *f)
{
	CHECK(atomic_load(&f->refs)==0 && f->callbacks==NULL);
	DRM_DESTROY_WAITQUEUE(&f->cv); atomic_dec(&model_fences);
}
#ifndef DRM_SCHED_MODEL_EXTERNAL_FENCE_OPERATIONS
static struct dma_fence *dma_fence_get(struct dma_fence *f)
{ if (f) CHECK(atomic_fetch_add(&f->refs,1)>0); return f; }
static void dma_fence_put(struct dma_fence *f)
{ if (f && atomic_fetch_sub(&f->refs,1)==1) f->ops->release(f); }
static bool dma_fence_is_signaled(struct dma_fence *f)
{ return atomic_load(&f->signaled); }
static void dma_fence_set_error(struct dma_fence *f,int error)
{ CHECK(error<0 && !dma_fence_is_signaled(f)); f->error=error; }
static int dma_fence_get_status(struct dma_fence *f)
{ return dma_fence_is_signaled(f) ? (f->error ? f->error : 1) : 0; }
#endif
static int dma_fence_add_callback(struct dma_fence *f,struct dma_fence_cb *cb,
    void (*fn)(struct dma_fence *,struct dma_fence_cb *))
{
	spin_lock(f->lock);
	if (dma_fence_is_signaled(f)) { spin_unlock(f->lock); return -ENOENT; }
	cb->fn=fn; cb->next=f->callbacks; f->callbacks=cb;
	spin_unlock(f->lock); return 0;
}
static bool dma_fence_remove_callback(struct dma_fence *f,struct dma_fence_cb *cb)
{
	struct dma_fence_cb **p;
	bool found=false;
	spin_lock(f->lock);
	for (p=&f->callbacks; *p; p=&(*p)->next) {
		if (*p==cb) { *p=cb->next; found=true; break; }
	}
	spin_unlock(f->lock); return found;
}
static int model_dma_fence_signal(struct dma_fence *f)
{
	struct dma_fence_cb *cb;
	spin_lock(f->lock);
	if (dma_fence_is_signaled(f)) { spin_unlock(f->lock); return -EINVAL; }
	atomic_store(&f->signaled,true);
	while ((cb=f->callbacks)!=NULL) { f->callbacks=cb->next; cb->fn(f,cb); }
	DRM_SPIN_WAKEUP_ALL(&f->cv,f->lock); spin_unlock(f->lock); return 0;
}
#ifndef DRM_SCHED_MODEL_EXTERNAL_FENCE_OPERATIONS
static int dma_fence_signal(struct dma_fence *f)
{ return model_dma_fence_signal(f); }
#endif
static long dma_fence_wait(struct dma_fence *f,bool intr)
{
	CHECK(model_lock_depth==0);
	spin_lock(f->lock);
	while (!dma_fence_is_signaled(f)) model_wait(&f->cv,f->lock);
	spin_unlock(f->lock); return 0;
}
#endif

struct kmem_cache { size_t size; };
static bool model_fail_slab_alloc, model_fail_kthread;
static struct kmem_cache *kmem_cache_create(const char *name,size_t size,
    size_t align,unsigned flags,void *ctor)
{ struct kmem_cache *c=malloc(sizeof(*c)); CHECK(c!=NULL); c->size=size; return c; }
static void kmem_cache_destroy(struct kmem_cache *c)
{ CHECK(atomic_load(&model_slabs)==0); free(c); }
static void *kmem_cache_zalloc(struct kmem_cache *c,unsigned flags)
{ if (model_fail_slab_alloc) return NULL; void *p=calloc(1,c->size); CHECK(p!=NULL); atomic_inc(&model_slabs); return p; }
static void kmem_cache_free(struct kmem_cache *c,void *p)
{ atomic_dec(&model_slabs); free(p); }
#define GFP_KERNEL 0
#define SLAB_HWCACHE_ALIGN 0
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p) || IS_ERR(p))
#define MAX_SCHEDULE_TIMEOUT LONG_MAX
#define msecs_to_jiffies(v) (v)

struct work_struct { void (*fn)(struct work_struct *); };
struct delayed_work { struct work_struct work; };
#define INIT_DELAYED_WORK(w,f) ((w)->work.fn=(f))
#define system_wq NULL
static bool cancel_delayed_work(struct delayed_work *w) { return true; }
static bool schedule_delayed_work(struct delayed_work *w,unsigned long timeout)
{ CHECK(false); return false; }
static bool mod_delayed_work(void *q,struct delayed_work *w,unsigned long timeout)
{ CHECK(false); return false; }

struct task_struct {
	pthread_t tid;
	int (*fn)(void *);
	void *arg;
	atomic_bool stop;
	spinlock_t *lock;
	drm_waitqueue_t *cv;
	atomic_bool manual;
	bool step_active;
	unsigned tokens, completed;
	pthread_mutex_t step_lock;
	pthread_cond_t step_cv;
};
static bool model_manual_kthreads;
static _Thread_local struct task_struct *model_current;
#define current model_current
static void *model_kthread_entry(void *p)
{ model_current=p; model_current->fn(model_current->arg); return NULL; }
static struct task_struct *kthread_run(int (*fn)(void *),void *arg,
    const char *name,spinlock_t *lock,drm_waitqueue_t *cv)
{
	struct task_struct *t;
	if (model_fail_kthread) return ERR_PTR(-ENOMEM);
	t=calloc(1,sizeof(*t)); CHECK(t!=NULL);
	t->fn=fn; t->arg=arg; t->lock=lock; t->cv=cv;
	atomic_init(&t->manual,model_manual_kthreads);
	CHECK(pthread_mutex_init(&t->step_lock,NULL)==0);
	CHECK(pthread_cond_init(&t->step_cv,NULL)==0);
	atomic_init(&t->stop,false);
	CHECK(pthread_create(&t->tid,NULL,model_kthread_entry,t)==0); return t;
}
static bool kthread_should_stop(void)
{
	struct task_struct *t=model_current;
	/* Gate only the loop boundary, where no scheduler lock is held. */
	if (t->manual && model_lock_depth==0) {
		CHECK(pthread_mutex_lock(&t->step_lock)==0);
		if (t->step_active) {
			t->completed++; t->step_active=false;
			CHECK(pthread_cond_broadcast(&t->step_cv)==0);
		}
		while (t->manual && t->tokens==0 && !atomic_load(&t->stop))
			CHECK(pthread_cond_wait(&t->step_cv,&t->step_lock)==0);
		if (t->tokens!=0) { t->tokens--; t->step_active=true; }
		CHECK(pthread_mutex_unlock(&t->step_lock)==0);
	}
	return atomic_load(&t->stop);
}
static void model_kthread_steps(struct task_struct *t,unsigned count)
{
	unsigned target;
	CHECK(t->manual && model_lock_depth==0);
	CHECK(pthread_mutex_lock(&t->step_lock)==0);
	CHECK(t->tokens==0 && !t->step_active);
	target=t->completed+count; t->tokens=count;
	CHECK(pthread_cond_broadcast(&t->step_cv)==0);
	while (t->completed!=target) {
		struct timespec deadline;
		CHECK(clock_gettime(CLOCK_REALTIME,&deadline)==0); deadline.tv_sec+=10;
		CHECK(pthread_cond_timedwait(&t->step_cv,&t->step_lock,&deadline)==0);
	}
	CHECK(pthread_mutex_unlock(&t->step_lock)==0);
}
static void model_kthread_autorun(struct task_struct *t)
{
	CHECK(pthread_mutex_lock(&t->step_lock)==0);
	CHECK(t->tokens==0 && !t->step_active);
	atomic_store(&t->manual,false);
	CHECK(pthread_cond_broadcast(&t->step_cv)==0);
	CHECK(pthread_mutex_unlock(&t->step_lock)==0);
}
static void kthread_stop(struct task_struct *t)
{
	spin_lock(t->lock); atomic_store(&t->stop,true);
	DRM_SPIN_WAKEUP_ALL(t->cv,t->lock); spin_unlock(t->lock);
	CHECK(pthread_mutex_lock(&t->step_lock)==0);
	CHECK(pthread_cond_broadcast(&t->step_cv)==0);
	CHECK(pthread_mutex_unlock(&t->step_lock)==0);
	CHECK(pthread_join(t->tid,NULL)==0);
	CHECK(pthread_cond_destroy(&t->step_cv)==0);
	CHECK(pthread_mutex_destroy(&t->step_lock)==0); free(t);
}
static bool __kthread_should_park(struct task_struct *t) { return false; }
static bool kthread_should_park(void) { return false; }
static void kthread_parkme(void) { CHECK(false); }
static void kthread_park(struct task_struct *t) { CHECK(false); }
static void kthread_unpark(struct task_struct *t) { CHECK(false); }
#define sched_setscheduler(t,p,s) ((void)(t), (void)(p), (void)(s))
struct proc { unsigned p_sflag; };
static struct proc model_proc;
#define curproc (&model_proc)
#define PS_WEXIT 1

/* Select native branches only after loading the host's standard headers. */
#ifndef __NetBSD__
#define __NetBSD__ 1
#endif
#define __KERNEL_RCSID(a,b)
#define EXPORT_SYMBOL(s)
#define module_init(f)
#define module_exit(f)
#define MODULE_DESCRIPTION(s)
#define MODULE_LICENSE(s)
#define MODULE(a,b,c)
typedef int modcmd_t;
#define MODULE_CMD_INIT 1
#define MODULE_CMD_FINI 2
#define MODULE_CMD_AUTOUNLOAD 3
#define DRM_ERROR(...) ((void)0)
#define DRM_WARN(...) ((void)0)
#define DMA_FENCE_TRACE(...) ((void)0)
#define trace_drm_sched_job_wait_dep(...) ((void)0)
#define trace_drm_sched_job(...) ((void)0)
#define trace_drm_sched_process_job(...) ((void)0)
#include "spsc-queue-body.h"
#include "gpu-scheduler-body.h"
#endif
