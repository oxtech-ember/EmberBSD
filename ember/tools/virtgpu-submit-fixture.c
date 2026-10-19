/* Origin: EmberBSD; AI-assisted production asynchronous submit regression. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <linux/virtio_gpu.h>

#define HZ 100
static unsigned int fixture_ticks;
#define jiffies fixture_ticks
typedef int atomic_t;
#define atomic_inc(p) (++*(p))
#define atomic_dec(p) (--*(p))
#define atomic_dec_and_test(p) (--*(p)==0)
#define atomic_dec_return(p) (--*(p))
#define atomic_cmpxchg(p,o,n) __sync_val_compare_and_swap((p),(o),(n))
#define atomic_read(p) (*(p))
#define atomic_set(p,v) (*(p)=(v))
#define WARN_ON_ONCE(c) WARN_ON(c)
#define WARN_ON(c) (assert(!(c)), 0)
#define GFP_KERNEL 0
#define GFP_ATOMIC 0
#define KM_SLEEP 0
#define MUTEX_DEFAULT 0
#define IPL_VM 0
#define DTYPE_MISC 0
#define FREAD 1
#define FWRITE 2
#define MAX_INLINE_CMD_SIZE 96
#define MAX_INLINE_RESP_SIZE 24
#define __user
#define cpu_to_le32(x) (x)
#define cpu_to_le64(x) (x)
#define le32_to_cpu(x) (x)
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define ERR_CAST(p) ((void *)(p))
#define u64_to_user_ptr(p) ((void *)(uintptr_t)(p))
#define KASSERT(c) assert(c)
#define KASSERTMSG(c,...) assert(c)
#define BUG_ON(c) assert(!(c))
#define DRM_ERROR(...) do { if (0) fprintf(stderr, __VA_ARGS__); } while (0)
#define DRM_DEBUG(...) ((void)0)
#define le64_to_cpu(x) (x)
#define max(a,b) ((a)>(b)?(a):(b))
#define trace_dma_fence_emit(f) ((void)(f))
#define FENCE_MAGIC_BAD 0
#define FENCE_MAGIC_GOOD 42
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint64_t atomic64_t;
#define atomic64_set(p,v) (*(p)=(v))
struct mutex { int held; };
#ifdef COMPLETION_CONTRACT
static void completion_unlock(struct mutex *);
#endif
typedef struct mutex spinlock_t;
typedef struct mutex kmutex_t;
typedef struct { struct mutex lock; int cv; } wait_queue_head_t;
#define DRM_WAKEUP_ALL(cv,lock) ((void)(cv),(void)(lock))
static void (*lock_hook)(struct mutex *);
static void (*before_lock_hook)(struct mutex *);
static void mutex_lock(struct mutex *m) { if(before_lock_hook) before_lock_hook(m); assert(!m->held); m->held=1; if(lock_hook) lock_hook(m); }
static void mutex_unlock(struct mutex *m) {
	assert(m->held); m->held=0;
#ifdef COMPLETION_CONTRACT
	completion_unlock(m);
#endif
}
#define spin_lock(m) mutex_lock(m)
#define spin_unlock(m) mutex_unlock(m)
#define mutex_enter(m) mutex_lock(m)
#define mutex_exit(m) mutex_unlock(m)
#define spin_lock_irqsave(m,f) do { (f)=0; spin_lock(m); } while (0)
#define spin_unlock_irqrestore(m,f) do { (void)(f); spin_unlock(m); } while (0)
#define spin_is_locked(m) ((m)->held)
#define mutex_init(m,t,i) ((m)->held=0)
#define mutex_destroy(m) assert(!(m)->held)
struct list_head { struct list_head *next, *prev; };
#define INIT_LIST_HEAD(h) ((h)->next=(h)->prev=(h))
#define list_empty(h) ((h)->next==(h))
static void list_add_tail(struct list_head *p, struct list_head *h) {
	p->prev=h->prev; p->next=h; h->prev->next=p; h->prev=p;
}
#define list_for_each_entry(p,h,m) \
	for (p=container_of((h)->next,__typeof__(*p),m); &p->m!=(h); \
	    p=container_of(p->m.next,__typeof__(*p),m))
static void list_del(struct list_head *p) { p->prev->next=p->next; p->next->prev=p->prev; }
static void list_del_init(struct list_head *p) { list_del(p); INIT_LIST_HEAD(p); }
#define list_first_entry(h,t,m) container_of((h)->next,t,m)
#define list_for_each_entry_safe(p,n,h,m) \
	for (p=container_of((h)->next,__typeof__(*p),m), n=container_of(p->m.next,__typeof__(*p),m); \
	    &p->m!=(h); p=n,n=container_of(n->m.next,__typeof__(*n),m))
struct dma_fence {
	int refs, error, f_magic;
	const struct dma_fence_ops *ops;
	bool signaled, heap;
	uint64_t seqno;
	spinlock_t *lock;
};
struct dma_fence_cb { int unused; };
struct dma_fence_ops { bool use_64bit_seqno; };
static const struct dma_fence_ops virtio_fence_ops;
struct ww_class { uint64_t wwc_ticket; };
struct ww_acquire_ctx { struct ww_class *wwx_class; void *wwx_owner; uint64_t wwx_ticket; unsigned wwx_acquired; bool wwx_acquire_done; };
static struct ww_class reservation_ww_class;

struct work_struct { int unused; };
struct irq_work { void (*fn)(struct irq_work *); };
struct selinfo { int initialized; };
struct virtio_gpu_device;
struct virtio_gpu_vbuffer;
struct virtio_gpu_object;
typedef void (*virtio_gpu_resp_cb)(struct virtio_gpu_device *, struct virtio_gpu_vbuffer *);
#include "submit-layout.h"
struct virtio_device;
struct submit_config_ops {
	void (*reset)(struct virtio_device *);
	void (*del_vqs)(struct virtio_device *);
};
struct virtio_device { void *dmat; size_t max_request; struct submit_config_ops *config; };
struct netbsd_virtqueue { unsigned int num_free; };
struct linux_virtio_sg { void *addr; size_t size; void *map; };
struct virtio_gpu_device {
	bool has_virgl_3d, vqs_ready;
	int submit_error;
	atomic_t submitters;
	void *vbufs;
	struct virtio_device *vdev;
	struct { struct netbsd_virtqueue *vq; spinlock_t qlock; wait_queue_head_t ack_queue;
		struct work_struct dequeue_work; } ctrlq, cursorq;
	wait_queue_head_t resp_wq;
	void *cleanup_wq;
	struct work_struct reset_work, config_changed_work;
	struct mutex submit_lock;
	struct virtio_gpu_fence_driver fence_drv;
	spinlock_t obj_free_lock;
	spinlock_t dma_lock;
	bool dma_stopped;
	size_t exec_bytes;
	u64 next_context_key;
	struct list_head obj_free_list;
	struct work_struct obj_free_work;
};
struct drm_device { struct virtio_gpu_device *dev_private; };
struct drm_file { struct virtio_gpu_fpriv *driver_priv; };
struct dma_resv_list { unsigned int shared_count; struct dma_fence *shared[4]; };
struct dma_resv { bool locked; struct ww_acquire_ctx *owner; struct dma_fence *fence_excl; struct dma_resv_list *fence, shared; };
#define reservation dma_resv
struct dma_resv_write_ticket { int unused; };
#define dma_resv_held(r) ((r)->locked)
#define dma_resv_write_begin(r,t) ((void)(r), (void)(t))
#define dma_resv_write_commit(r,t) ((void)(r), (void)(t))
#define atomic_store_relaxed(p,v) (*(p)=(v))
#define atomic64_inc_return(p) (++*(p))
struct drm_gem_object { size_t size; unsigned refs; struct reservation *resv; };

static int alloc_calls, fail_alloc, live, copy_calls, fail_copy, lookup_calls, fail_lookup;
static int lock_error, fd_error, queue_error, queue_calls, accepted, wait_calls;
static int input_fd, output_fd=7, input_wait=1, sync_fail, early_mode, pressure;
static int installs, closes, aborts, reservations, close_on_exec, stops;
static int files_allocated, sync_created;
#ifdef TRANSFER_CONTRACT
static long (*transfer_wait_hook)(struct dma_fence *, long);
static struct mutex *transfer_attachment;
static int transfer_last_flags, transfer_zero_releases;
#endif
static struct dma_fence *input;
static struct virtio_gpu_vbuffer *pending;
static struct virtio_gpu_device device;
#if defined(EXEC_CONTRACT) && defined(EXEC_FOUNDATION)
static bool advanced, defer_free;
static int stop_pre, deny_bo;
static struct virtio_gpu_vbuffer *exec_pending[8];
static unsigned int exec_queued;
static long dependency_ticks;
static unsigned int dependency_advance;
static void (*dma_sync_hook)(int);
#endif
static struct reservation resv[2];
static struct drm_gem_object bos[2];
#ifdef EXEC_FOUNDATION
struct virtio_gpu_object {
	struct { struct drm_gem_object base; } base;
	struct { struct { void *sg_dmamap; } *sgl; } *pages;
	u32 hw_res_handle;
	enum virtgpu_dma_lease dma_lease;
	bool release_pending;
	bool dma_required, dma_eligible, private_console;
	void *dma_vaddr;
	u32 mapped, width, height;
	unsigned int dma_members, dma_retire_refs, exec_pending;
	struct list_head exec_members;
	int pre, post, pre_read, pre_write, post_read, post_write;
};
static struct virtio_gpu_object backing[2];
static struct virtio_gpu_object *gem_to_virtio_gpu_obj(struct drm_gem_object *o) {
	for(unsigned i=0;i<2;i++) if(o==&backing[i].base.base) return &backing[i];
	assert(o >= bos && o < bos + 2); return &backing[o - bos];
}
static bool virtio_gpu_object_dma_admitted(struct virtio_gpu_device *d, struct drm_gem_object *o) {
#if defined(EXEC_CONTRACT) && defined(EXEC_FOUNDATION)
	if(deny_bo && o==&bos[deny_bo-1]) return false;
#endif
	return true; }
static void drm_gem_object_get(struct drm_gem_object *o) { o->refs++; }
#define BUS_DMASYNC_PREREAD 1
#define BUS_DMASYNC_PREWRITE 2
#define BUS_DMASYNC_POSTREAD 4
#define BUS_DMASYNC_POSTWRITE 8
static void virtgpu_dma_sync(struct virtio_gpu_device *d, struct virtio_gpu_object *b, int ops) {
	assert(!d->dma_lock.held);
	if (ops & 3) { b->pre++; b->pre_read+=!!(ops&1); b->pre_write+=!!(ops&2);
#if defined(EXEC_CONTRACT) && defined(EXEC_FOUNDATION)
		if(stop_pre && --stop_pre==0) { d->dma_stopped=true; d->vqs_ready=false; }
#endif
	}
	else { assert((ops & 12) && b->pre > b->post); b->post++; b->post_read+=!!(ops&4); b->post_write+=!!(ops&8); }
#if defined(EXEC_CONTRACT) && defined(EXEC_FOUNDATION)
	if(dma_sync_hook) dma_sync_hook(ops);
#endif
}
#define to_virtio_fence(f) container_of(f,struct virtio_gpu_fence,f)
static int virtgpu_exec_prepare(struct virtio_gpu_vbuffer *);
static void virtgpu_exec_post(struct virtio_gpu_vbuffer *);
static void virtgpu_exec_finish(struct virtio_gpu_vbuffer *);
#endif

static void *test_alloc(size_t n) {
	void *p;
	if (++alloc_calls == fail_alloc) return NULL;
	p=calloc(1,n); assert(p); live++; return p;
}
static void test_free(void *p) { if (p) { assert(live>0); live--; free(p); } }
#define kmalloc(n,f) test_alloc(n)
#define kzalloc(n,f) test_alloc(n)
#ifdef TRANSFER_CONTRACT
static void *transfer_alloc(size_t n,int flags) {
	assert(!transfer_attachment || !transfer_attachment->held);
	transfer_last_flags=flags;
	return test_alloc(n);
}
#define kvmalloc(n,f) transfer_alloc(n,f)
#else
#define kvmalloc(n,f) test_alloc(n)
#endif
#define kvmalloc_array(n,s,f) test_alloc((n)*(s))
#define kfree(p) test_free(p)
#define kvfree(p) test_free(p)
#define kmem_cache_zalloc(c,f) test_alloc(sizeof(struct virtio_gpu_vbuffer)+MAX_INLINE_CMD_SIZE+MAX_INLINE_RESP_SIZE)
#define kmem_cache_free(c,p) test_free(p)
/* KM_SLEEP creation is no-fail; fallible-create testing is a separate seam. */
static void *kmem_zalloc(size_t n,int f) { void *p=calloc(1,n); assert(p); live++; return p; }
#define kmem_free(p,n) test_free(p)
static struct dma_fence *dma_fence_get(struct dma_fence *f) { assert(f->refs>0); f->refs++; return f; }
static void dma_fence_put(struct dma_fence *f) {
	assert(f->refs>0); if (--f->refs==0 && f->heap) test_free(f);
}
static void dma_fence_init(struct dma_fence *f,const struct dma_fence_ops *o,spinlock_t *l,uint64_t ctx,uint64_t seq) {
	f->ops=o; f->refs=1; f->heap=true; f->lock=l; f->seqno=seq; f->f_magic=FENCE_MAGIC_GOOD;
}
static void dma_fence_set_error(struct dma_fence *f,int e) { assert(e<0); f->error=e; }
static void dma_fence_signal_locked(struct dma_fence *f) { f->signaled=true; }
static bool dma_fence_is_signaled_locked(struct dma_fence *f) { return f->signaled; }
static void dma_fence_remove_callback(struct dma_fence *f,struct dma_fence_cb *c) { assert(!"private callback"); }
static void irq_work_queue(struct irq_work *w) { assert(w->fn); }
static long __attribute__((unused)) dma_fence_wait_timeout(struct dma_fence *f,bool intr,long ticks) {
	assert(intr); wait_calls++;
#ifdef TRANSFER_CONTRACT
	if(transfer_wait_hook) return transfer_wait_hook(f,ticks);
#endif
#if defined(EXEC_CONTRACT) && defined(EXEC_FOUNDATION)
	if (advanced) {
		assert(!device.dma_lock.held && !device.fence_drv.lock.held && !device.submit_lock.held);
		dependency_ticks=ticks;
		if(f==input) fixture_ticks += dependency_advance;
		if(f->signaled) return ticks? ticks:1;
		return input_wait;
	}
#endif
	assert(f==input && ticks==15*HZ); return input_wait;
}
/* Baseline-only API: marked unused so the fixed source does not need it. */
static int __attribute__((unused)) dma_fence_wait(struct dma_fence *f,bool intr) {
	assert(f==input && intr); wait_calls++; return input_wait<0?input_wait:0;
}
static struct dma_fence *sync_file_get_fence(int fd) { input_fd=fd; return input?dma_fence_get(input):NULL; }
static int copy_from_user(void *d,const void *s,size_t n) {
	if (++copy_calls == fail_copy)
		return 1;
	memcpy(d, s, n);
	return 0;
}
static struct drm_gem_object *drm_gem_object_lookup(struct drm_file *f,uint32_t h) {
#ifdef TRANSFER_CONTRACT
	assert(!transfer_attachment || !transfer_attachment->held);
#endif
	if (++lookup_calls==fail_lookup) return NULL;
	assert(h>=1 && h<=2); bos[h-1].refs++; return &bos[h-1];
}
static void drm_gem_object_put_unlocked(struct drm_gem_object *o) {
#ifdef TRANSFER_CONTRACT
	assert(o->refs); o->refs--;
	if(!o->refs) {
		struct virtio_gpu_object *bo=gem_to_virtio_gpu_obj(o);
		assert(bo->pre==bo->post && !bo->exec_pending && !bo->dma_members);
		transfer_zero_releases++;
	}
#else
	assert(o->refs>1); o->refs--;
#endif
}

static unsigned int ww_calls, ww_slow_calls, ww_deadlock_at;
static int ww_slow_error;
static int dma_resv_lock_interruptible(struct reservation *r,struct ww_acquire_ctx *ctx) {
	ww_calls++;
	if (lock_error) return lock_error;
	if (ww_calls == ww_deadlock_at) return -EDEADLK;
	if (r->locked) return -EALREADY;
	r->locked=true; r->owner=ctx;
	if (ctx) ctx->wwx_acquired++;
	return 0;
}
static void dma_resv_unlock(struct reservation *r) {
	assert(r->locked); r->locked=false;
	if (r->owner) { assert(r->owner->wwx_acquired); r->owner->wwx_acquired--; }
	r->owner=NULL;
}
static int dma_resv_lock_slow_interruptible(struct reservation *r,struct ww_acquire_ctx *ctx) {
	ww_slow_calls++; assert(!ctx->wwx_acquired);
	return ww_slow_error ? ww_slow_error : dma_resv_lock_interruptible(r,ctx);
}
static void virtio_gpu_array_put_free_work(struct work_struct *);
static void schedule_work(struct work_struct *w) {
#if defined(EXEC_CONTRACT) && defined(EXEC_FOUNDATION)
	if(defer_free) return;
#endif
	virtio_gpu_array_put_free_work(w);
}
#ifdef CONTROLLED_2D_CONTRACT
static void virtio_gpu_wait_done(struct virtio_gpu_vbuffer *,int);
static void virtio_gpu_wait_put(struct virtio_gpu_wait *);
#else
static void virtio_gpu_wait_done(struct virtio_gpu_vbuffer *b,int e) { assert(!b->wait); }
#endif
#if (defined(COMPLETION_CONTRACT) || defined(TRANSFER_CONTRACT)) && !defined(CONTROLLED_2D_FOUNDATION)
static void virtio_gpu_complete_transfer(struct virtio_gpu_device *, struct virtio_gpu_vbuffer *);
#endif
#ifdef DMA_LEASE_SOURCE
/* These unrelated contracts have no qualified backing operations. */
static void virtio_gpu_dma_finish(struct virtio_gpu_vbuffer *b, int error) {
#ifdef EXEC_FOUNDATION
	if (b->objs && b->objs->operation) virtgpu_exec_finish(b);
#endif
}
static void virtio_gpu_dma_stop(struct virtio_gpu_device *d) { d->dma_stopped=true; }
#ifdef COMPLETION_CONTRACT
static void virtio_gpu_dma_reset(struct virtio_gpu_device *d) { }
#elif !defined(TRANSFER_CONTRACT) && !defined(CONTROLLED_2D_FOUNDATION)
static void virtio_gpu_complete_transfer(struct virtio_gpu_device *d, struct virtio_gpu_vbuffer *b) { }
#endif
static int virtio_gpu_dma_prepare(struct virtio_gpu_vbuffer *b) {
#ifdef EXEC_FOUNDATION
	if (b->objs && b->objs->operation) return virtgpu_exec_prepare(b);
#endif
	return 0;
}
static void virtio_gpu_dma_post(struct virtio_gpu_vbuffer *b) {
#ifdef EXEC_FOUNDATION
	if (b->objs && b->objs->operation) virtgpu_exec_post(b);
#endif
}
#endif
static void virtio_gpu_release_object(struct virtio_gpu_object *o) { assert(!o); }
#ifdef COMPLETION_CONTRACT
static void virtio_gpu_stop(struct virtio_gpu_device *, int);
static void virtgpu_console_stop(struct virtio_gpu_device *);
static void virtgpu_console_drain(struct virtio_gpu_device *);
static void virtio_gpu_fail_capsets(struct virtio_gpu_device *, int);
static void wake_up_all(wait_queue_head_t *);
static void queue_work(void *, struct work_struct *);
static void flush_work(struct work_struct *);
#else
static void virtio_gpu_stop(struct virtio_gpu_device *d,int e) { stops++; }
static void wake_up_all(wait_queue_head_t *q) { }
#endif
static int virtqueue_add_sgs(struct netbsd_virtqueue *,struct linux_virtio_sg **,unsigned,unsigned,void *,int);
#ifdef CONTROLLED_2D_CONTRACT
static long (*controlled_pressure_hook)(long);
#endif
static int __attribute__((unused)) pressure_wait(long ticks) {
#ifdef CONTROLLED_2D_CONTRACT
	if(controlled_pressure_hook) return controlled_pressure_hook(ticks);
#endif
	assert(ticks==5*HZ);
#if defined(EXEC_CONTRACT) && defined(EXEC_FOUNDATION)
	if(advanced) {
		assert(backing[0].exec_pending && !backing[0].dma_members);
		assert(backing[0].pre==backing[0].post);
	}
#endif
	return pressure==2;
}
#ifdef COMPLETION_CONTRACT
static long completion_wait(long);
#define wait_event_timeout(q,c,t) ((void)(q),(c)?(long)(t):completion_wait(t))
#else
#define wait_event_timeout(q,c,t) ((void)(q),(c)?(long)(t):pressure_wait(t))
#endif

/* Native descriptor boundary: allocation reserves f_count=0; affix publishes. */
struct file;
struct fileops { int (*fo_close)(struct file *); };
struct file { int f_count,f_type,f_flag; void *f_cred,*f_data; const struct fileops *f_ops; };
typedef struct file file_t;
typedef struct { bool ff_exclose,ff_foclose; } fdfile_t;
struct fdtab { fdfile_t *dt_ff[8]; };
typedef struct { struct fdtab *fd_dt; fdfile_t *fd_dfdfile[8]; struct mutex fd_lock; } filedesc_t;
typedef struct { filedesc_t *p_fd; } proc_t;
static proc_t proc0, process;
#define curproc (&process)
static int lwp_marker; static void *curlwp = &lwp_marker;
static struct file *reserved_file;
static fdfile_t fdslots[8];
static struct fdtab fdtable;
static filedesc_t filedesc;
#define NDFDFILE 8
#define atomic_load_consume(p) (*(p))
static int file_cache;
static bool fd_isused(filedesc_t *d,unsigned fd) { return reservations==1 && fd==(unsigned)output_fd; }
static void fd_unused(filedesc_t *d,unsigned fd) { assert(fd_isused(d,fd)); reservations--; aborts++; }
static void pool_cache_put(int cache,struct file *fp) { assert(!fp->f_data); test_free(fp); reserved_file=NULL; }
#include "submit-fd-abort.h"
static int close_private(struct file *);
static const struct fileops sync_file_ops={close_private};
#define selinit(s) ((s)->initialized=1)
#define seldestroy(s) do { assert((s)->initialized); (s)->initialized=0; } while (0)
#define sync_file_create actual_sync_file_create
#include "submit-sync-create.h"
#undef sync_file_create
static struct sync_file *sync_file_create(struct dma_fence *f,struct file *fp) {
	if (sync_fail)
		return NULL;
	sync_created++;
	return actual_sync_file_create(f, fp);
}
static int fd_allocfile(struct file **fp,int *fd) {
	if(fd_error) return fd_error;
	/* File pool is an independent allocation boundary. */
	*fp=kmem_zalloc(sizeof(**fp),0); (*fp)->f_cred=&process;
	reserved_file=*fp; *fd=output_fd; reservations++; files_allocated++; return 0;
}
static void fd_set_exclose(void *l,int fd,bool value) {
	assert(fd==output_fd && !installs && value); close_on_exec=1;
}
static void fd_install(int fd,struct file *fp) {
	struct sync_file *sf=fp->f_data;
	assert(accepted && close_on_exec && sf->sf_fence->seqno!=0);
	assert(fd==output_fd && reservations==1 && fp->f_count==0);
	fp->f_count=1; reservations--; installs++;
}
#ifdef TRANSFER_CONTRACT
#include "virtgpu-transfer-seams.h"
#endif
#include "submit-native-resv.h"
#include "submit-production.h"
static int close_private(struct file *fp) {
	int ret=sync_file_close(fp); closes++; fp->f_data=NULL; return ret;
}
/* Transport scheduling can retire a cookie before queue acceptance returns. */
static void retire(struct virtio_gpu_vbuffer *b,int error) {
#ifdef COMPLETION_FOUNDATION
	if (error)
		virtio_gpu_fence_stop(&device, error);
	virtio_gpu_finish_vbuf(b, error);
	if (error)
		virtio_gpu_fail_fences(&device, error);
#else
	if(error) virtio_gpu_fail_fences(&device,error);
	else virtio_gpu_fence_event_process(&device,b->fence->f.seqno);
	virtio_gpu_cancel_vbuf(b);
#endif
}
#ifdef COMPLETION_CONTRACT
static int completion_transport(struct netbsd_virtqueue *, void *);
#endif
static int virtqueue_add_sgs(struct netbsd_virtqueue *q,struct linux_virtio_sg **sgs,unsigned out,unsigned in,void *cookie,int flags) {
#ifdef COMPLETION_CONTRACT
	return completion_transport(q, cookie);
#else
#if defined(EXEC_CONTRACT) && defined(EXEC_FOUNDATION)
	if (advanced) {
		struct virtio_gpu_vbuffer *b = cookie;
		queue_calls++;
#ifdef TRANSFER_CONTRACT
		assert(!transfer_attachment || transfer_attachment->held);
#endif
		#if !defined(TRANSFER_CONTRACT) || defined(TRANSFER_FOUNDATION)
		assert(b->objs->registered && b->objs->prepared);
#endif
		for (unsigned i=0;i<b->objs->nents;i++)
			assert(!b->objs->objs[i]->resv->locked);
		if (pressure && queue_calls==1) return -ENOSPC;
		if (queue_error) return queue_error;
		accepted++;
		if (early_mode) retire(b,0);
		else {
			assert(exec_queued<8);
			exec_pending[exec_queued++]=b;
		}
		return 0;
	}
#endif
	queue_calls++;
	assert(out==2 && in==1 && !installs);
	if(pressure && queue_calls==1) return -ENOSPC;
	if(queue_error) return queue_error;
	accepted++;
	if(early_mode) retire(cookie,early_mode==2?-ENODEV:0);
	else { assert(!pending); pending=cookie; }
	return 0;
#endif
}

static const char *const cases[] = {
	"accepted pending", "disabled", "unknown flags", "size zero",
	"max zero", "max one", "max 255", "max 256", "minimum aligned request exact",
	"aligned request overflow", "exact request boundary", "handle cap overflow",
	"handle cap exact lookup failure", "missing context", "zero context",
	"invalid input fd", "input timeout", "input interrupted", "input failed",
	"input successful", "input unexpectedly pending", "failed input array",
	"fd allocation", "handle allocation", "array allocation", "command allocation",
	"fence allocation", "vbuf allocation", "handle copy", "command copy",
	"first lookup", "partial lookup", "reservation lock", "sync create",
	"queue ENOMEM", "queue EMSGSIZE", "queue ENODEV", "queue pressure timeout",
	"queue pressure retry", "complete before publish", "reset before publish",
	"fd zero", "no output fd", "single BO", "single BO lock failure",
	"stopped queue", "late host error", "late malformed response",
	"vbuf failure without fd", "queue failure without fd", "failed input without output fd",
	"caller-only fence completion", "caller-only fence reset"
};

static void
run_case(unsigned which)
{
	struct virtio_device native={.max_request=4096};
	struct netbsd_virtqueue queue={16};
	struct virtio_gpu_fpriv priv={.ctx_id=3};
	struct drm_device drm={&device};
	struct drm_file client={&priv};
#ifdef EXEC_FOUNDATION
	struct virtio_gpu_attachment attachments[2];
	INIT_LIST_HEAD(&priv.attachments);
	priv.attachment_count=2; priv.software_key=1;
	for (unsigned i=0;i<2;i++) {
		attachments[i].obj=&bos[i]; attachments[i].handles=1;
		list_add_tail(&attachments[i].node,&priv.attachments);
		backing[i].dma_lease=VIRTGPU_LEASE_OPEN;
		backing[i].dma_required=true; backing[i].dma_eligible=true;
		INIT_LIST_HEAD(&backing[i].exec_members);
	}
#endif
	uint32_t handles[65536], command[1024]={0};
	struct drm_virtgpu_execbuffer args={.flags=VIRTGPU_EXECBUF_FENCE_FD_OUT,
	    .size=4,.command=(uintptr_t)command,.bo_handles=(uintptr_t)handles,
	    .num_bo_handles=2,.fence_fd=0};
	spinlock_t input_lock={0};
	struct dma_fence input_base={.refs=1,.signaled=true,.lock=&input_lock,
	    .f_magic=FENCE_MAGIC_GOOD};
	struct dma_fence_array *array=NULL;
	struct dma_fence child={.error=-EIO};
	int expected=0, ret;

	for(unsigned i=0;i<65536;i++) handles[i]=(i%2)+1;
	for(unsigned i=0;i<2;i++) { bos[i].refs=1; bos[i].resv=&resv[i]; resv[i].fence=&resv[i].shared; }
	for(unsigned i=0;i<8;i++) fdtable.dt_ff[i]=filedesc.fd_dfdfile[i]=&fdslots[i];
	filedesc.fd_dt=&fdtable; process.p_fd=&filedesc;
	device.has_virgl_3d=true; device.vqs_ready=true; device.vdev=&native;
	device.ctrlq.vq=&queue;
#ifdef COMPLETION_FOUNDATION
	device.fence_drv.vgdev=&device;
	device.fence_drv.limit=16;
#endif
	INIT_LIST_HEAD(&device.fence_drv.fences);
	INIT_LIST_HEAD(&device.obj_free_list);
	switch(which) {
	case 1: device.has_virgl_3d=false; expected=-ENOSYS; break;
	case 2: args.flags|=8; expected=-EINVAL; break;
	case 3: args.size=0; expected=-EINVAL; break;
	case 4: native.max_request=0; expected=-EINVAL; break;
	case 5: native.max_request=1; expected=-EINVAL; break;
	case 6: native.max_request=255; expected=-EINVAL; break;
	case 7: native.max_request=256; expected=-EINVAL; break;
	case 8: native.max_request=260; args.size=4; break;
	case 9: native.max_request=260; args.size=8; expected=-EINVAL; break;
	case 10: args.size=4096-256; break;
	case 11: args.num_bo_handles=65537; expected=-EINVAL; break;
	case 12: args.num_bo_handles=65536; fail_lookup=1; expected=-ENOENT; break;
	case 13: client.driver_priv=NULL; expected=-EINVAL; break;
	case 14: priv.ctx_id=0; expected=-EINVAL; break;
	case 15: args.flags|=VIRTGPU_EXECBUF_FENCE_FD_IN; expected=-EINVAL; break;
	case 16: input_wait=0; expected=-ETIMEDOUT; break;
	case 17: input_wait=-EINTR; expected=-EINTR; break;
	case 18: input_base.error=-ENODEV; expected=-ENODEV; break;
	case 20: input_base.signaled=false; expected=-EIO; break;
	case 21:
		array=calloc(1,sizeof(*array)+sizeof(array->dfa_cb[0])); assert(array);
		array->base=input_base; array->base.refs=2; array->base.error=0;
		array->base.signaled=false; array->base.lock=&array->dfa_lock;
		array->dfa_npending=1; array->dfa_work.fn=dma_fence_array_done;
		array->dfa_cb[0].dfac_array=array;
		/* A child callback does not own the array's lock. */
		dma_fence_array_done1(&child,&array->dfa_cb[0].dfac_cb);
		dma_fence_array_done(&array->dfa_work);
		assert(array->base.error==-EIO && array->base.refs==1);
		expected=-EIO; break;
	case 22: fd_error=EMFILE; expected=-EMFILE; break;
	case 23: fail_alloc=1; expected=-ENOMEM; break;
	case 24: fail_alloc=2; expected=-ENOMEM; break;
	case 25: fail_alloc=3; expected=-ENOMEM; break;
	case 26: fail_alloc=4; expected=-ENOMEM; break;
	case 27: fail_alloc=5; expected=-ENOMEM; break;
	case 28: fail_copy=1; expected=-EFAULT; break;
	case 29: fail_copy=2; expected=-EFAULT; break;
	case 30: fail_lookup=1; expected=-ENOENT; break;
	case 31: fail_lookup=2; expected=-ENOENT; break;
	case 32: lock_error=-EINTR; expected=-EINTR; break;
	case 33: sync_fail=1; expected=-ENOMEM; break;
	case 34: queue_error=-ENOMEM; expected=-ENOMEM; break;
	case 35: queue_error=-EMSGSIZE; expected=-EMSGSIZE; break;
	case 36: queue_error=-ENODEV; expected=-ENODEV; break;
	case 37: pressure=1; expected=-ETIMEDOUT; break;
	case 38: pressure=2; break;
	case 39: early_mode=1; break;
	case 40: early_mode=2; break;
	case 41: output_fd=0; break;
	case 42: args.flags=0; args.num_bo_handles=0; break;
	case 43: args.num_bo_handles=1; break;
	case 44: args.num_bo_handles=1; lock_error=-EINTR; expected=-EINTR; break;
	case 45: device.vqs_ready=false; expected=-ENODEV; break;
	case 48: args.flags=0; fail_alloc=5; expected=-ENOMEM; break;
	case 49: args.flags=0; queue_error=-ENODEV; expected=-ENODEV; break;
	case 50: args.flags=VIRTGPU_EXECBUF_FENCE_FD_IN; input=&input_base;
		input_base.error=-EIO; expected=-EIO; break;
	case 51: args.flags=0; args.num_bo_handles=0; early_mode=1; break;
	case 52: args.flags=0; args.num_bo_handles=0; early_mode=2; break;
	default: break;
	}
	if(which>=16 && which<=21) {
		input=array?&array->base:&input_base;
		args.flags|=VIRTGPU_EXECBUF_FENCE_FD_IN;
	}
	ret=virtio_gpu_execbuffer_ioctl(&drm,&args,&client);
	if(ret!=expected) fprintf(stderr,"%s: expected %d, got %d; live=%d accepted=%d installed=%d\n",cases[which],expected,ret,live,accepted,installs);
	assert(ret==expected);
	if(expected) {
		assert(!accepted && !installs && !pending);
		if(which!=1) assert(args.fence_fd==-1);
	} else {
		assert(accepted==1);
		if(args.flags&VIRTGPU_EXECBUF_FENCE_FD_OUT) {
			struct sync_file *sf=reserved_file->f_data;
			assert(installs==1 && args.fence_fd==output_fd);
			assert(sf->sf_fence->seqno!=0 && sf->sf_fence->refs>0);
			if(!early_mode) assert(!sf->sf_fence->signaled && pending);
			if(early_mode==2) assert(dma_fence_get_status(sf->sf_fence)==-ENODEV);
		} else assert(!installs && args.fence_fd==-1);
	}
	if(which>=16 && which<=21) assert(wait_calls==1 && input_fd==0 && input->refs==1 && !stops);
	if(which==38) assert(queue_calls==2);
	#ifdef EXEC_CONTRACT
	if (!expected && pending) {
		/* Actual attachment set contains both BOs; hints are not authority. */
		assert(pending->objs && pending->objs->nents == 2);
	}
#endif
	if(pending) {
		int error=0;
		if(which>=46) {
			struct virtio_gpu_ctrl_hdr *resp=(void *)pending->resp_buf;
			struct virtio_gpu_ctrl_hdr *cmd=(void *)pending->buf;
			*resp=*cmd; resp->type=which==46?VIRTIO_GPU_RESP_ERR_UNSPEC:VIRTIO_GPU_RESP_OK_NODATA;
			pending->resp_received=which==47?1:sizeof(*resp);
			error=virtio_gpu_response_error(pending); assert(error==-EIO);
		}
		retire(pending,error); pending=NULL;
		if(which>=46) assert(dma_fence_get_status(((struct sync_file *)reserved_file->f_data)->sf_fence)==-EIO);
	}
	for(unsigned i=0;i<2;i++) {
		assert(bos[i].refs==1 && !resv[i].locked);
		if(resv[i].fence_excl) dma_fence_put(resv[i].fence_excl);
	}
	if(installs) {
		assert(reserved_file->f_count==1);
		assert(reserved_file->f_ops->fo_close(reserved_file)==0);
		test_free(reserved_file); reserved_file=NULL;
	}
	assert(closes==sync_created && aborts+installs==files_allocated);
	#ifdef EXEC_FOUNDATION
	assert(!device.exec_bytes);
	for (unsigned i=0;i<2;i++) assert(backing[i].pre==backing[i].post && !backing[i].exec_pending && !backing[i].dma_members);
#endif
	assert(!reservations && !live && list_empty(&device.fence_drv.fences));
	assert(list_empty(&device.obj_free_list) && !device.submit_lock.held && !device.ctrlq.qlock.held);
	if(array) free(array);
}

#if defined(EXEC_CONTRACT) && defined(EXEC_FOUNDATION)
#include "virtgpu-exec-cases.h"
#endif

#ifdef FRAMING_CONTRACT
#include "virtgpu-exec-framing-cases.h"
#endif

#ifdef TRANSFER_CONTRACT
#include "virtgpu-transfer-cases.h"
#endif

#ifdef CONTROLLED_2D_CONTRACT
#include "virtgpu-controlled-2d-cases.h"
#endif

int
#ifdef COMPLETION_CONTRACT
submit_contract_main(void)
#else
main(void)
#endif
{
#ifdef FRAMING_CONTRACT
	return framing_contract_main();
#endif
#ifdef CONTROLLED_2D_CONTRACT
	return controlled_2d_main();
#endif
#ifdef TRANSFER_CONTRACT
	return transfer_contract_main();
#endif
	#if defined(EXEC_CONTRACT) && defined(EXEC_FOUNDATION)
	return exec_contract_main();
#endif
	unsigned failed=0, ran=0, count=sizeof(cases)/sizeof(cases[0]);
	for(unsigned i=0;i<count;i++) {
		#ifdef EXEC_CONTRACT
		if (i != 0 && i != 42 && i != 43) continue;
#endif
		ran++;
		pid_t pid=fork(); int status;
		assert(pid>=0);
		if(pid==0) { run_case(i); exit(0); }
		assert(waitpid(pid,&status,0)==pid);
		if(!WIFEXITED(status) || WEXITSTATUS(status)!=0) failed++;
		printf("%s %s\n", WIFEXITED(status)&&WEXITSTATUS(status)==0?"PASS":"FAIL",cases[i]);
		fflush(stdout);
	}
	printf("%u groups, %u failed\n",ran,failed);
	return failed?1:0;
}

#ifdef COMPLETION_CONTRACT
#include "virtgpu-completion-cases.h"
#endif
