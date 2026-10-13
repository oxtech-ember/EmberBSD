/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD actual-source syncobj publication contract. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#define __NetBSD__ 1
#define KASSERT assert
#define KASSERTMSG(c,...) assert(c)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT 2
#define DRM_SYNCOBJ_WAIT_FOR_SUBMIT_TIMEOUT 5000000000ULL
#define MUTEX_DEFAULT 0
#define IPL_VM 1
#define ERESTARTSYS 512
#ifndef ERESTART
#define ERESTART 85
#endif
#ifndef ETIME
#define ETIME 62
#endif
#define rcu_dereference_protected(p,c) (p)
#define lockdep_is_held(m) (*(m)==1)
#define spin_is_locked(m) (*(m)==1)
#define DRIVER_SYNCOBJ_TIMELINE 8
#define CHECK(c) do { checks++; if (!(c)) { \
    fprintf(stderr,"FAIL case %u line %d: %s\n",scenario,__LINE__,#c); \
    exit(1); } } while (0)
typedef uint32_t u32;
typedef uint64_t u64;
typedef int kmutex_t;
typedef int kcondvar_t;
struct list_head { struct list_head *next; };
struct dma_fence { unsigned refs; uint64_t point; };
struct syncobj_wait_entry {
    struct list_head node;
    kmutex_t *lock;
    kcondvar_t *cv;
    bool *signalledp;
    uint64_t point;
    struct dma_fence *fence;
};
struct drm_syncobj {
    unsigned refs;
    int lock;
    struct dma_fence *fence;
    struct list_head cb_list;
};
struct drm_file { struct drm_syncobj *handle; };
struct drm_device { unsigned features; };
struct drm_syncobj_transfer {
    u32 src_handle, dst_handle;
    u64 src_point, dst_point;
    u32 flags, pad;
};
static unsigned checks, scenario, ticks, sleeps, resources, transfers;
static struct drm_syncobj object;
static struct dma_fence old_fence, new_fence, stub;
static struct drm_file file;
static struct syncobj_wait_entry *pending;
static void syncobj_wait_syncobj_func(struct drm_syncobj *,struct syncobj_wait_entry *);
static u64 nsecs_to_jiffies64(u64 n) { return n/10000000; }
static unsigned getticks(void) { return ticks; }
static void mutex_init(int *p,int t,int ipl) { *p=0; resources++; }
static void mutex_destroy(int *p) { CHECK(*p==0 && resources>0); resources--; }
static void cv_init(int *p,const char *name) { *p=1; resources++; }
static void cv_destroy(int *p) { CHECK(*p==1 && resources>0); *p=0; resources--; }
static void mutex_enter(int *p) { CHECK(p!=NULL && *p==0); *p=1; }
static void mutex_exit(int *p) { CHECK(p!=NULL && *p==1); *p=0; }
#define mutex_spin_enter mutex_enter
#define mutex_spin_exit mutex_exit
#define spin_lock mutex_enter
#define spin_unlock mutex_exit
static void cv_broadcast(int *p) { CHECK(p!=NULL && *p==1); }
static struct dma_fence *dma_fence_get(struct dma_fence *p)
{ if(p) { CHECK(p->refs>0); p->refs++; } return p; }
static void dma_fence_put(struct dma_fence *p)
{ if(p) { CHECK(p->refs>0); p->refs--; } }
static void drm_syncobj_put(struct drm_syncobj *p)
{
    CHECK(p->refs>0);
    if (--p->refs==0) {
        CHECK(pending==NULL && p->lock==0);
        dma_fence_put(p->fence); p->fence=NULL;
    }
}
static struct drm_syncobj *drm_syncobj_find(struct drm_file *p,u32 handle)
{
    if(handle!=1 || p->handle==NULL)return NULL;
    CHECK(p->handle->refs>0); p->handle->refs++; return p->handle;
}
static struct dma_fence *drm_syncobj_fence_get(struct drm_syncobj *p)
{ CHECK(p->refs>0); return dma_fence_get(p->fence); }
static int dma_fence_chain_find_seqno(struct dma_fence **p,u64 point)
{
    if(point==0)return 0;
    if((*p)->point<point)return -EINVAL;
    if(scenario==12) { dma_fence_put(*p); *p=NULL; }
    return 0;
}
static struct dma_fence *dma_fence_get_stub(void) { return dma_fence_get(&stub); }
static void list_add_tail(struct list_head *n,struct list_head *h)
{
    CHECK(pending==NULL && object.refs>0 && object.lock==1);
    n->next=h;
    pending=(struct syncobj_wait_entry *)((char *)n-
        offsetof(struct syncobj_wait_entry,node));
}
static void list_del_init(struct list_head *n)
{ CHECK(object.lock==1); n->next=n; pending=NULL; }
static void publish(void)
{
    spin_lock(&object.lock);
    dma_fence_put(object.fence);
    object.fence=dma_fence_get(&new_fence);
    if(pending)syncobj_wait_syncobj_func(&object,pending);
    spin_unlock(&object.lock);
}
static int cv_timedwait_sig(int *cv,int *lock,long timeout)
{
    CHECK(object.refs>0 && *cv==1 && object.lock==0);
    CHECK(++sleeps<=3 && timeout>0);
    mutex_exit(lock);
    if(scenario==5) { file.handle=NULL; drm_syncobj_put(&object); }
    if(scenario==5 || scenario==7 || scenario==8 || scenario==11 || scenario==12)
        publish();
    if(scenario==10 && sleeps==1) { ticks++; mutex_enter(lock); return 0; }
    ticks+=(scenario==6 || scenario==10)?(unsigned)timeout:1;
    mutex_enter(lock);
    if(scenario==7)return EINTR;
    if(scenario==8)return ERESTART;
    return (scenario==6 || scenario==10)?EWOULDBLOCK:0;
}
#include "add-body.h"
/* Test publication between the initial read and locked recheck. */
static void add_wait(struct drm_syncobj *s,struct syncobj_wait_entry *w)
{ if(scenario==9)publish(); drm_syncobj_fence_add_wait(s,w); }
#define drm_syncobj_fence_add_wait add_wait
#include "find-body.h"
#undef drm_syncobj_fence_add_wait
#include "callback-body.h"
static int drm_core_check_feature(struct drm_device *d,unsigned f)
{ return (d->features&f)!=0; }
static int drm_syncobj_transfer_to_timeline(struct drm_file *f,struct drm_syncobj_transfer *a)
{ transfers++; return 17; }
static int drm_syncobj_transfer_to_binary(struct drm_file *f,struct drm_syncobj_transfer *a)
{ transfers++; return 19; }
#include "transfer-body.h"
static void transfer_flags(void)
{
    struct drm_device dev={DRIVER_SYNCOBJ_TIMELINE};
    struct drm_syncobj_transfer a={0};
    for(unsigned n=0;n<32;n++) {
        unsigned before=transfers;
        a.flags=1u<<n;
        CHECK(drm_syncobj_transfer_ioctl(&dev,&a,&file)==(n==1?19:-EINVAL));
        CHECK(transfers==before+(n==1));
    }
    a.flags=0; CHECK(drm_syncobj_transfer_ioctl(&dev,&a,&file)==19);
    a.dst_point=4; CHECK(drm_syncobj_transfer_ioctl(&dev,&a,&file)==17);
    a.pad=1; CHECK(drm_syncobj_transfer_ioctl(&dev,&a,&file)==-EINVAL);
    dev.features=0; CHECK(drm_syncobj_transfer_ioctl(&dev,&a,&file)==-EOPNOTSUPP);
}
int main(int argc,char **argv)
{
    struct dma_fence *out=(void *)(uintptr_t)1;
    unsigned flags=DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT;
    u64 point=0;
    int expected=0;
    scenario=argc==2?(unsigned)strtoul(argv[1],NULL,0):1;
    old_fence=(struct dma_fence){1,1};
    new_fence=(struct dma_fence){1,9};
    stub=(struct dma_fence){1,0};
    object.refs=1; file.handle=&object;
    if(scenario==1)object.fence=dma_fence_get(&new_fence);
    if(scenario==2) { file.handle=NULL; expected=-ENOENT; }
    if(scenario==3 || scenario==11) { object.fence=dma_fence_get(&old_fence); point=9; }
    if(scenario==3 || scenario==4) { flags=0; expected=-EINVAL; }
    if(scenario==6 || scenario==10)expected=-ETIME;
    if(scenario==7 || scenario==8)expected=-ERESTARTSYS;
    if(scenario==12)point=9;
    int result=drm_syncobj_find_fence(&file,1,point,flags,&out);
    CHECK(result==expected);
    if(result==0) { CHECK(out!=NULL); dma_fence_put(out); }
    else CHECK(out==NULL);
    CHECK(pending==NULL && resources==0 && object.lock==0);
    CHECK(object.refs==(scenario==5?0u:1u));
    if(object.refs)drm_syncobj_put(&object);
    CHECK(old_fence.refs==1 && new_fence.refs==1 && stub.refs==1);
    if(scenario==6 || scenario==10)CHECK(ticks==500);
    transfer_flags();
    printf("PASS: syncobj find case %u (%u checks)\n",scenario,checks);
    return 0;
}
