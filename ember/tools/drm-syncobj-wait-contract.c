/* Origin: EmberBSD actual-source DRM wait progress contract, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#define __NetBSD__ 1
#define __user
#define GFP_KERNEL 0
#define MUTEX_DEFAULT 0
#define IPL_VM 1
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL 1
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT 2
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE 4
#ifndef ETIME
#define ETIME 62
#endif
#define ERESTARTSYS 512
#ifndef ERESTART
#define ERESTART 85
#endif
#define MIN(a,b) ((a)<(b)?(a):(b))
#define KASSERT assert
#define KASSERTMSG(c,...) assert(c)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define rcu_dereference_protected(p,c) (p)
#define spin_is_locked(p) (true)
#define list_del_init(p) ((void)(p))
typedef int kmutex_t;
typedef int kcondvar_t;
struct dma_fence;
struct dma_fence_cb { void (*func)(struct dma_fence *,struct dma_fence_cb *); };
struct dma_fence { int refs; bool signaled; struct dma_fence_cb *cb; };
struct syncobj_wait_entry {
    kmutex_t *lock; kcondvar_t *cv; bool *signalledp;
    uint64_t point; struct dma_fence *fence;
    struct dma_fence_cb fence_cb; int node;
};
struct drm_syncobj { struct dma_fence *fence; struct syncobj_wait_entry *wait; int lock; };
static unsigned checks, polls, sleeps, ticks, resources, allocations, fail_alloc;
static unsigned scenario;
static struct dma_fence f[2];
static struct drm_syncobj s[2];
#define CHECK(c) do { checks++; if(!(c)) { \
    fprintf(stderr,"FAIL case %u line %d: %s\n",scenario,__LINE__,#c); exit(1); } } while(0)
static void syncobj_wait_syncobj_func(struct drm_syncobj *,struct syncobj_wait_entry *);
static void mutex_init(kmutex_t *m,int a,int b) { *m=0; resources++; }
static void mutex_destroy(kmutex_t *m) { CHECK(!*m); resources--; }
static void cv_init(kcondvar_t *c,const char *n) { *c=0; resources++; }
static void cv_destroy(kcondvar_t *c) { resources--; }
static void mutex_enter(kmutex_t *m) { CHECK(!*m); *m=1; }
static void mutex_exit(kmutex_t *m) { CHECK(*m); *m=0; }
static void mutex_spin_enter(kmutex_t *m)
{
    /* Publication in the exact gap between polling and taking the interlock. */
    if(scenario==2 && s[0].wait && !s[0].fence) {
        s[0].fence=&f[0]; f[0].signaled=true;
        syncobj_wait_syncobj_func(&s[0],s[0].wait);
    }
    mutex_enter(m);
}
static void mutex_spin_exit(kmutex_t *m) { mutex_exit(m); }
static void cv_broadcast(kcondvar_t *c) { }
static unsigned getticks(void) { return ticks; }
static int cv_timedwait_sig(kcondvar_t *c,kmutex_t *m,long timeout)
{
    sleeps++; CHECK(sleeps<5); CHECK(scenario!=2);
    mutex_exit(m);
    if(scenario==1 && sleeps==1) {
        ticks++; f[0].signaled=true; CHECK(f[0].cb!=NULL);
        f[0].cb->func(&f[0],f[0].cb); mutex_enter(m); return 0;
    }
    ticks+=(unsigned)timeout; mutex_enter(m); return EWOULDBLOCK;
}
static void *kmalloc_array(size_t n,size_t z,int flag)
{ return ++allocations==fail_alloc?NULL:malloc(n*z); }
static void *kcalloc(size_t n,size_t z,int flag)
{ return ++allocations==fail_alloc?NULL:calloc(n,z); }
static void kfree(void *p) { free(p); }
static int copy_from_user(void *d,void *p,size_t n) { memcpy(d,p,n); return 0; }
static struct dma_fence *dma_fence_get(struct dma_fence *p)
{ if(p)p->refs++; return p; }
static void dma_fence_put(struct dma_fence *p) { if(p)CHECK(--p->refs>=1); }
static struct dma_fence *drm_syncobj_fence_get(struct drm_syncobj *p)
{ return dma_fence_get(p->fence); }
static int dma_fence_chain_find_seqno(struct dma_fence **p,uint64_t point) { return 0; }
static struct dma_fence *dma_fence_get_stub(void) { abort(); }
static bool dma_fence_is_signaled(struct dma_fence *p)
{ CHECK(++polls<100); return p->signaled; }
static int dma_fence_add_callback(struct dma_fence *p,struct dma_fence_cb *c,
    void (*fn)(struct dma_fence *,struct dma_fence_cb *))
{ if(p->signaled)return -ENOENT; c->func=fn; p->cb=c; return 0; }
static void dma_fence_remove_callback(struct dma_fence *p,struct dma_fence_cb *c)
{ CHECK(p->cb==c); p->cb=NULL; }
static void drm_syncobj_fence_add_wait(struct drm_syncobj *p,struct syncobj_wait_entry *w)
{ if(!w->fence)p->wait=w; }
static void drm_syncobj_remove_wait(struct drm_syncobj *p,struct syncobj_wait_entry *w)
{ p->wait=NULL; }
#include "wait-body.h"

int main(int argc,char **argv)
{
    struct drm_syncobj *objects[]={&s[0],&s[1]};
    scenario=argc==2?(unsigned)atoi(argv[1]):1;
    for(unsigned i=0;i<2;i++) { f[i].refs=1; s[i].fence=&f[i]; }
    if(scenario==2)s[0].fence=NULL;
    if(scenario==3)fail_alloc=1;
    if(scenario==4)fail_alloc=2;
    long ret=drm_syncobj_array_wait_timeout(objects,NULL,scenario==2?1:2,
        DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL|DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,10,NULL);
    if(scenario==1)CHECK(ret==-ETIME && sleeps==2 && ticks==10);
    if(scenario==2)CHECK(ret==10 && sleeps==0);
    if(scenario==3 || scenario==4)CHECK(ret==-ENOMEM);
    CHECK(resources==0 && f[0].refs==1 && f[1].refs==1);
    printf("PASS: DRM wait case %u (%u checks)\n",scenario,checks);
    return 0;
}
