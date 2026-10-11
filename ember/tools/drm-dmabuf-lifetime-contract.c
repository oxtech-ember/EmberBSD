/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD actual-source DMA-BUF lifecycle and failure injection. */
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
/* Select the native bodies after importing host libc headers. */
#define __NetBSD__ 1
#define __diagused
#define KASSERT assert
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define KM_SLEEP 0
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define DTYPE_MISC 7
#define O_CLOEXEC 0x400000
struct dma_buf;
struct dma_resv { int active; };
struct poll { int active; };
struct dma_buf_ops { void (*release)(struct dma_buf *); };
struct dma_buf {
 void *priv; const struct dma_buf_ops *ops; size_t size;
 struct dma_resv *resv; unsigned db_refcnt; int db_lock;
 struct poll db_resv_poll; struct dma_resv db_resv_int[];
};
struct dma_buf_export_info { void *priv; const struct dma_buf_ops *ops; size_t size; struct dma_resv *resv; };
struct file { int f_type,f_flag; const void *f_ops; void *f_data; };
struct drm_device { unsigned refs; };
struct drm_gem_object { struct drm_device *dev; struct dma_buf *dma_buf; unsigned refs; };
static int dmabuf_fileops,curlwp,curproc,allocation_error,affixed,cloexec;
static unsigned allocations,freed,gem_puts,dev_puts,poll_fini,resv_fini;
static struct file files[2];
static void *kmem_zalloc(size_t n,int flags) { void *p=calloc(1,n); assert(p); allocations++; return p; }
static void kmem_free(void *p,size_t n) { assert(n>=sizeof(struct dma_buf)); freed++; free(p); }
static void mutex_init(int *m,int a,int b) { *m=1; }
static void mutex_destroy(int *m) { assert(*m==1); *m=0; }
static void dma_resv_poll_init(struct poll *p) { p->active=1; }
static void dma_resv_poll_fini(struct poll *p) { assert(p->active); p->active=0; poll_fini++; }
static void dma_resv_init(struct dma_resv *r) { r->active=1; }
static void dma_resv_fini(struct dma_resv *r) { assert(r->active); r->active=0; resv_fini++; }
static unsigned atomic_inc_uint_nv(unsigned *v) { return __atomic_add_fetch(v,1,__ATOMIC_SEQ_CST); }
static unsigned atomic_dec_uint_nv(unsigned *v) { return __atomic_sub_fetch(v,1,__ATOMIC_SEQ_CST); }
static void membar_release(void) { __atomic_thread_fence(__ATOMIC_RELEASE); }
static void membar_acquire(void) { __atomic_thread_fence(__ATOMIC_ACQUIRE); }
static int fd_allocfile(struct file **f,int *fd)
{ if(allocation_error) return EMFILE; assert(affixed<2); *fd=affixed; *f=&files[affixed]; return 0; }
static void fd_set_exclose(int l,int fd,int set) { cloexec=set; }
static void fd_affix(int p,struct file *f,int fd) { assert(f==&files[fd]); affixed++; }
static void drm_dev_get(struct drm_device *d) { d->refs++; }
static void drm_dev_put(struct drm_device *d) { assert(d->refs>1); d->refs--; dev_puts++; }
static void drm_gem_object_get(struct drm_gem_object *o) { o->refs++; }
static void drm_gem_object_put_unlocked(struct drm_gem_object *o) { assert(o->refs>1); o->refs--; gem_puts++; }
#include "bodies.h"
static void *worker(void *p)
{
 for(unsigned i=0;i<10000;i++) { get_dma_buf(p); dma_buf_put(p); }
 return NULL;
}
int main(void)
{
 const struct dma_buf_ops ops={drm_gem_dmabuf_release};
 const struct dma_buf_ops missing={NULL};
 struct dma_buf_export_info bad={0};
 assert(dma_buf_export(&bad)==ERR_PTR(-EINVAL));
 bad.ops=&missing;
 assert(dma_buf_export(&bad)==ERR_PTR(-EINVAL) && allocations==0);
 for(unsigned mode=0;mode<2;mode++) {
  struct drm_device dev={1}; struct drm_gem_object obj={&dev,NULL,1};
  struct dma_resv external={1};
  struct dma_buf_export_info info={&obj,&ops,4096,mode?&external:NULL};
  struct dma_buf *buf=drm_gem_dmabuf_export(&dev,&info);
  pthread_t threads[4];
  assert(buf->db_refcnt==1 && obj.refs==2 && dev.refs==2);
  /* Failure consumes nothing: caller can retry or put the same reference. */
  allocation_error=1;
  assert(dma_buf_fd(buf,O_CLOEXEC)==-EMFILE && buf->db_refcnt==1);
  allocation_error=0; affixed=0;
  obj.dma_buf=buf; get_dma_buf(buf); /* Actual export cache ownership. */
  assert(dma_buf_fd(buf,O_CLOEXEC)==0 && cloexec && buf->db_refcnt==2);
  get_dma_buf(buf);
  assert(dma_buf_fd(buf,0)==1 && !cloexec && buf->db_refcnt==3);
  for(unsigned i=0;i<4;i++) assert(pthread_create(&threads[i],NULL,worker,buf)==0);
  for(unsigned i=0;i<4;i++) assert(pthread_join(threads[i],NULL)==0);
  assert(buf->db_refcnt==3);
  assert(dmabuf_fop_close(&files[0])==0 && buf->db_refcnt==2);
  drm_gem_object_exported_dma_buf_free(&obj);
  assert(obj.dma_buf==NULL && buf->db_refcnt==1);
  assert(obj.refs==2 && dev.refs==2 && freed==mode);
  assert(dmabuf_fop_close(&files[1])==0);
  assert(obj.refs==1 && dev.refs==1 && freed==mode+1);
  assert(gem_puts==mode+1 && dev_puts==mode+1 && poll_fini==mode+1);
  assert(external.active==1 && resv_fini==1);
 }
 assert(allocations==2 && freed==2);
 puts("PASS: fd failure/transfer, repeated export, 80000 concurrent get/put pairs, cache break and one final release");
 return 0;
}
