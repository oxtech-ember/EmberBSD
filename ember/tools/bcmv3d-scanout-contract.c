/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual native HVS owner with controlled MMU4, DMA, fences and kernel services. */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "target-errno.h"
static unsigned long checks;
#define CHECK(v) do { checks++; if (!(v)) { \
	fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#v);exit(1); \
} } while (0)
#define KASSERT CHECK
#define ASSERT_SLEEPABLE() CHECK(!in_irq)
#define current (0)
#define KM_SLEEP 1
#define MUTEX_DEFAULT 1
#define IPL_NONE 0
#define IPL_VM 1
#define WQ_MPSAFE 1
#define PRI_NONE 0
#ifdef __KERNEL_RCSID
#undef __KERNEL_RCSID
#endif
#define __KERNEL_RCSID(...)
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
typedef void *bus_space_tag_t;
typedef unsigned bus_space_handle_t;
typedef struct { bool live, held; int ipl; } kmutex_t;
struct work { int unused; };
struct workqueue {
	void (*fn)(struct work *,void *);
	void *arg;
	struct work *pending;
};
struct drm_device { unsigned refs; };
struct drm_gem_object { unsigned refs, id; };
struct bcmv3d_bo { struct { struct drm_gem_object base; } cma; };
struct bcmv3d_mmu4_native { int unused; };
struct bcmv3d_vblank_native { int unused; };
struct bcmv3d_device {
	struct drm_device *drm;
	bus_space_tag_t bst;
	kmutex_t memory_lock;
	struct bcmv3d_mmu4_native *mmu4_native;
	struct bcmv3d_vblank_native *vblank_native;
	struct bcmv3d_scanout *scanout;
};
#include "display-types.h"
#include "flip-types.h"
#include "flip-body.h"
#include "read-types.h"
#include "pv-types.h"
#include "scanout-types.h"
#include "hvs-types.h"
#include "hvs-body.h"
struct dma_fence { bool pending, poisoned; unsigned refs; int error; };
struct slot { struct drm_gem_object *object; struct bcmv3d_read *reader;
    bool synced, published, exposed; };
static struct slot slots[3];
static struct bcmv3d_device device;
static struct drm_device drm;
static struct bcmv3d_mmu4_native mmu;
static struct bcmv3d_vblank_native pv;
static struct bcmv3d_vblank_snapshot snapshot;
static struct bcmv3d_bo bo[4];
static struct bcm2712_display_contract contract;
static struct workqueue *queue;
static void (*pv_notify)(void *,const struct bcmv3d_vblank_snapshot *);
static void *pv_arg;
static uint32_t regs[0x1a000/4];
static unsigned process_locks, irq_locks, io_calls, io_fail_at, begins, ends;
static unsigned poisons, unloads, notifies, latch_notifies, barriers, allocations, waits;
static bool in_irq, begin_fail, begin_route_fault, stage_fail, sync_fail;
static bool publish_fail, retire_fail, expose_fail, bind_fail, wq_fail;
static bool reservation_locked, unmap_fail, wait_route_fault, reserve_reentry;
static bool writer_error, reserve_fail;
static struct dma_fence original_writer;
static unsigned retirement_mode, retirement_waits;
static void retirement_progress(struct dma_fence *);
static bool retire_route_fault, expose_route_fault, end_route_fault, construction_delay;
static bool read_completion_committed(struct bcmv3d_read *);
static struct bcmv3d_read *last_reader;
static int hz=100;
static int cpu_intr_p(void) { return in_irq; }
static int cpu_softintr_p(void) { return 0; }
static void mutex_init(kmutex_t *m,int t,int ipl)
{ CHECK(t==MUTEX_DEFAULT);m->live=true;m->held=false;m->ipl=ipl; }
static bool mutex_owned(kmutex_t *m) { CHECK(m->live);return m->held; }
static void mutex_enter(kmutex_t *m)
{
	CHECK(m->live&&!m->held);CHECK(!in_irq||m->ipl==IPL_VM);m->held=true;
	if(m->ipl==IPL_NONE) process_locks++; else irq_locks++;
}
static void mutex_exit(kmutex_t *m)
{
	CHECK(m->live&&m->held);m->held=false;
	if(m->ipl==IPL_NONE) { CHECK(process_locks>0);process_locks--; }
	else { CHECK(irq_locks>0);irq_locks--; }
}
static void mutex_destroy(kmutex_t *m)
{ CHECK(m->live&&!m->held);m->live=false; }
static void *kmem_zalloc(size_t size,int flags)
{ void *p;CHECK(flags==KM_SLEEP&&!in_irq);p=calloc(1,size);CHECK(p);allocations++;return p; }
static void kmem_free(void *p,size_t size)
{ CHECK(p&&size&&allocations);allocations--;free(p); }
static void drm_dev_get(struct drm_device *d) { CHECK(d==&drm);d->refs++; }
static void drm_gem_object_put_unlocked(struct drm_gem_object *o)
{ CHECK(o&&o->refs>0&&!in_irq);o->refs--; }
static int bcmv3d_bo_reference(struct bcmv3d_device *d,
    struct drm_gem_object *o,struct bcmv3d_bo **out)
{
	unsigned i;CHECK(d==&device&&!in_irq);
	for(i=0;i<4;i++) if(o==&bo[i].cma.base&&o->refs) {
		o->refs++;*out=&bo[i];return 0;
	}
	return EINVAL;
}
static int workqueue_create(struct workqueue **out,const char *name,
    void (*fn)(struct work *,void *),void *arg,int pri,int ipl,int flags)
{
	CHECK(!in_irq&&strcmp(name,"v3dscan")==0&&pri==PRI_NONE&&
	    ipl==IPL_VM&&flags==WQ_MPSAFE);
	if(wq_fail) return ENOMEM;
	if(construction_delay) { snapshot.sequence+=80;snapshot.frame_count=(snapshot.frame_count+80)&63; }
	queue=calloc(1,sizeof(*queue));CHECK(queue);queue->fn=fn;queue->arg=arg;
	*out=queue;return 0;
}
static void workqueue_destroy(struct workqueue *q)
{ CHECK(q==queue&&q->pending==NULL);free(q);queue=NULL; }
static void workqueue_enqueue(struct workqueue *q,struct work *w,void *ci)
{ CHECK(q==queue&&w&&ci==NULL&&q->pending==NULL);q->pending=w; }
static void drain(void)
{
	unsigned count=0;struct work *w;
	while(queue&&queue->pending) {
		CHECK(++count<8&&!in_irq&&process_locks==0&&irq_locks==0);
		w=queue->pending;queue->pending=NULL;queue->fn(w,queue->arg);
	}
}
int bcmv3d_vblank_native_snapshot(struct bcmv3d_vblank_native *v,
    struct bcmv3d_vblank_snapshot *out)
{ CHECK(v==&pv);*out=snapshot;return 0; }
int bcmv3d_vblank_native_bind_snapshot(struct bcmv3d_vblank_native *v,
    bcmv3d_vblank_notify_fn fn,void *arg,struct bcmv3d_vblank_snapshot *initial)
{
	CHECK(v==&pv&&fn&&arg&&pv_notify==NULL);
	if(bind_fail) return EIO;
	CHECK(initial&&irq_locks==1);*initial=snapshot;pv_notify=fn;pv_arg=arg;return 0;
}
static void emit(uint32_t head,unsigned advance,int error)
{
	CHECK(pv_notify&&!in_irq);snapshot.sequence++;
	snapshot.frame_count=(snapshot.frame_count+advance)&63;
	snapshot.active_list=head;snapshot.first_error=error;
	regs[(snapshot.channel_status==0x53?0x48:0x11c)/4]=head;
	in_irq=true;pv_notify(pv_arg,&snapshot);in_irq=false;
}
static struct dma_fence *dma_fence_get(struct dma_fence *f)
{
	CHECK(f&&f->refs>0&&process_locks==1&&irq_locks==0);
	f->refs++;return f;
}
static void dma_fence_put(struct dma_fence *f)
{
	if(f==NULL)return;
	CHECK(f->refs>0&&irq_locks==0);
	if(--f->refs==0)free(f);
}
static long dma_fence_wait_timeout(struct dma_fence *f,bool intr,long timeout)
{
	CHECK(!in_irq&&process_locks==0&&irq_locks==0&&!reservation_locked);
	CHECK(f&&f->refs==2&&intr&&timeout==5*hz);retirement_waits++;
	if(retirement_mode==1)return 0;
	if(retirement_mode==2)return -EINTR;
	if(retirement_mode==13)return -ERESTARTSYS;
	if(retirement_mode==15)return -(ELAST+2);
	if(retirement_mode==14||retirement_mode==16) {
		f->pending=false;f->error=retirement_mode==14?ERESTARTSYS:ELAST+2;return 1;
	}
	if(retirement_mode==3){f->pending=false;f->poisoned=true;return 1;}
	if(retirement_mode==5)return 1;
	if(retirement_mode==7){f->pending=false;return 1;}
	retirement_progress(f);return 1;
}
static int dma_fence_get_status(struct dma_fence *f)
{
	CHECK(f&&f->refs>0&&process_locks==0&&irq_locks==0);
	return f->pending?0:f->error?-f->error:f->poisoned?-EIO:1;
}
int bcmv3d_read_reserve(struct bcmv3d_read *r,struct drm_gem_object *o)

{
	struct bcmv3d_scanout_frame *other=NULL;
	CHECK(!in_irq&&process_locks==0&&irq_locks==0&&!reservation_locked);
	CHECK(r->owner==NULL&&r->object==NULL&&r->fence==NULL&&!r->poisoned);
	begins++;
	if(reserve_fail) return ENOMEM;
	if(reserve_reentry) {
		reserve_reentry=false;
		CHECK(bcmv3d_scanout_reserve(device.scanout,o,&other)==EBUSY && other==NULL);
	}
	if(begin_route_fault) { emit(32,1,EIO);drain(); }
	r->fence=calloc(1,sizeof(*r->fence));CHECK(r->fence);
	r->fence->pending=true;r->fence->refs=1;r->object=o;r->owner=r;r->prior=&original_writer;
	o->refs++;last_reader=r;return 0;
}
int bcmv3d_read_wait(struct bcmv3d_read *r,long timeout)
{
	CHECK(!in_irq&&process_locks==0&&irq_locks==0&&timeout==5*hz);
	CHECK(r->owner==r&&r->fence&&r->prior==&original_writer&&!r->poisoned);
	waits++;
	if(wait_route_fault) { emit(32,1,EIO);drain(); }
	if(begin_fail) return ETIME;
	if(writer_error) return EIO;
	r->prior=NULL;return 0;
}
int bcmv3d_read_end(struct bcmv3d_read *r)
{
	unsigned i;struct dma_fence *f;
	CHECK(!in_irq&&irq_locks==0);
	if(!r->fence) { CHECK(!r->owner&&!r->object&&!r->poisoned);return 0; }
	CHECK(r->owner==r&&!r->poisoned&&r->fence->pending);
	for(i=0;i<3;i++) CHECK(slots[i].reader!=r);
	if(end_route_fault) { CHECK(read_completion_committed(r));emit(snapshot.active_list,0,EIO); }
	f=r->fence;f->pending=false;r->fence=NULL;r->prior=NULL;dma_fence_put(f);
	drm_gem_object_put_unlocked(r->object);r->owner=NULL;r->object=NULL;ends++;
	return 0;
}
int bcmv3d_read_poison(struct bcmv3d_read *r,int error)
{
	CHECK(!in_irq&&irq_locks==0&&error>0&&r->owner==r);
	if(!r->poisoned) poisons++;
	r->poisoned=true;r->fence->pending=false;r->fence->poisoned=true;return 0;
}
static void mmu_context(struct bcmv3d_mmu4_native *n,unsigned slot)
{ CHECK(n==&mmu&&slot<3&&!in_irq&&irq_locks==0); }
static int bcmv3d_mmu4_native_stage_gem(struct bcmv3d_mmu4_native *n,
    unsigned slot,struct drm_gem_object *o)
{
	unsigned i;mmu_context(n,slot);CHECK(slots[slot].object==NULL);
	if(stage_fail) return ENOMEM;
	for(i=0;i<3;i++) if(slots[i].object==o) return EEXIST;
	CHECK(last_reader&&last_reader->object==o);
	slots[slot].object=o;slots[slot].reader=last_reader;o->refs++;return 0;
}
static int bcmv3d_mmu4_native_prepare_gem_read(struct bcmv3d_mmu4_native *n,
    unsigned slot,struct drm_gem_object *o)
{
	mmu_context(n,slot);CHECK(slots[slot].object==o && waits>0 &&
	    slots[slot].reader->prior==NULL && !slots[slot].exposed);
	if(sync_fail) return EINVAL;
	slots[slot].synced=true;return 0;
}
static int bcmv3d_mmu4_native_publish_staged_gem(struct bcmv3d_mmu4_native *n,
    unsigned slot)
{
	mmu_context(n,slot);CHECK(slots[slot].object && !slots[slot].synced);
	slots[slot].published=true;return publish_fail?EIO:0;
}
static void drop_map(unsigned slot)
{
	CHECK(slots[slot].object);drm_gem_object_put_unlocked(slots[slot].object);
	memset(&slots[slot],0,sizeof(slots[slot]));unloads++;
}
static int bcmv3d_mmu4_native_discard_staged_gem(struct bcmv3d_mmu4_native *n,
    unsigned slot)
{ mmu_context(n,slot);CHECK(!slots[slot].published&&!slots[slot].exposed);drop_map(slot);return 0; }
static int bcmv3d_mmu4_native_unmap_gem(struct bcmv3d_mmu4_native *n,
    unsigned slot)
{ mmu_context(n,slot);CHECK(slots[slot].published&&!slots[slot].exposed);if(unmap_fail)return EIO;drop_map(slot);return 0; }
static int bcmv3d_mmu4_native_expose_gem(struct bcmv3d_mmu4_native *n,
    unsigned slot,struct drm_gem_object *o)
{
	mmu_context(n,slot);CHECK(slots[slot].object==o&&slots[slot].published&&slots[slot].synced);
	if(expose_fail) return EIO;
	slots[slot].exposed=true;
	if(expose_route_fault) emit(62,0,0);
	return 0;
}
static int bcmv3d_mmu4_native_retire_gem(struct bcmv3d_mmu4_native *n,
    unsigned slot,struct bcmv3d_scanout_frame *f)
{
	mmu_context(n,slot);CHECK(slots[slot].exposed);
	CHECK(bcmv3d_scanout_retirement_check(f,n,slot,slots[slot].object)==0);
	if(retire_fail) return EIO;
	drop_map(slot);
	if(retire_route_fault) emit(snapshot.active_list,0,EIO);
	return 0;
}
static int bus_space_peek_4(bus_space_tag_t tag,bus_space_handle_t handle,
    uint32_t offset,uint32_t *out)
{
	CHECK(tag==device.bst&&handle==7&&!in_irq&&irq_locks==1);
	CHECK(offset<0x1a000&&(offset&3)==0);io_calls++;
	if(io_calls==io_fail_at) return -1;
	*out=regs[offset/4];return 0;
}
static int bus_space_poke_4(bus_space_tag_t tag,bus_space_handle_t handle,
    uint32_t offset,uint32_t value)
{
	CHECK(tag==device.bst&&handle==7&&!in_irq&&irq_locks==1);
	CHECK(offset<0x1a000&&(offset&3)==0);io_calls++;
	if(io_calls==io_fail_at) return -1;
	regs[offset/4]=value;return 0;
}
static void bus_space_barrier(bus_space_tag_t tag,bus_space_handle_t handle,
    uint32_t offset,uint32_t length,int flags)
{
	CHECK(tag==device.bst&&handle==7&&!in_irq&&irq_locks==1);
	CHECK(offset==0&&length==0x1a000&&flags==3);barriers++;
}
#include "scanout-body.h"

static void retirement_progress(struct dma_fence *f)
{
	struct bcmv3d_scanout *n=device.scanout;
	CHECK(n->frame[0].read.fence==f&&f->pending);
	if(retirement_mode==4) { emit(52,1,EIO);drain();return; }
	if(n->flip.slot[0].state==BCMV3D_FLIP_RETIRING)emit(52,1,0);
	drain();
	CHECK(!f->pending&&f->refs==1&&n->frame[0].read.fence==NULL);
	CHECK(n->frame[0].completion_committed&&n->flip.slot[0].state==BCMV3D_FLIP_FREE);
	if(retirement_mode==6)emit(52,0,EIO);
	if(retirement_mode==17)emit(52,0,ERESTARTSYS);
	if(retirement_mode==12)n->flip.slot[0].state=BCMV3D_FLIP_RETIRED;
}

static bool
read_completion_committed(struct bcmv3d_read *r)
{
	unsigned i;
	for(i=0;i<3;i++) if(r==&device.scanout->frame[i].read)
		return device.scanout->frame[i].completion_committed;
	CHECK(false);return false;
}

static void
setup(unsigned variant)
{
	unsigned i;
	device.drm=&drm;device.bst=(void *)(uintptr_t)0xbeef;
	device.mmu4_native=&mmu;device.vblank_native=&pv;
	mutex_init(&device.memory_lock,MUTEX_DEFAULT,IPL_NONE);
	contract.version=BCM2712_DISPLAY_VERSION;
	contract.memory[3].address=UINT64_C(0x107c580000);
	contract.memory[3].length=0x1a000;
	snapshot.version=1;snapshot.length=80;snapshot.prepared=1;
	snapshot.arm_attempted=1;snapshot.routed=1;snapshot.active_list=32;
	/* This controlled field selects the HVS variant for the register double. */
	snapshot.channel_status=variant;regs[(variant==0x53?0x48:0x11c)/4]=32;
	for(i=0;i<4;i++) { bo[i].cma.base.refs=1;bo[i].cma.base.id=i; }
}
static struct bcmv3d_scanout *
start(unsigned variant)
{
	struct bcmv3d_scanout *n=NULL;
	setup(variant);
	CHECK(bcmv3d_scanout_create(&device,&contract,7,variant,4096,&n)==0);
	CHECK(n&&device.scanout==n&&drm.refs==1&&allocations==1);
	CHECK(io_calls==0&&pv_notify==scanout_vblank&&pv_arg==n);
	return n;
}
static void notify(void *arg,uint64_t seq,bool latched,int error)
{
	struct bcmv3d_scanout *n=arg;
	CHECK(in_irq&&irq_locks==0&&!n->lock.held);
	CHECK(seq==snapshot.sequence);notifies++;
	if(latched) { CHECK(error==0);latch_notifies++; }
}
static void life(unsigned variant)
{
	struct bcmv3d_scanout *n=start(variant);
	struct bcmv3d_scanout_frame *a,*again,*b,copy;
	struct bcmv3d_scanout_status status;
	unsigned prior;
	CHECK(bcmv3d_scanout_bind(n,notify,n)==0);
	CHECK(bcmv3d_scanout_bind(n,notify,n)==EBUSY);
	CHECK(bcmv3d_scanout_prepare(n,&bo[0].cma.base,&a)==0);
	CHECK(bo[0].cma.base.refs==4&&begins==1&&io_calls==0);
	copy=*a;CHECK(bcmv3d_scanout_get(&copy)==EPERM);
	CHECK(bcmv3d_scanout_put(&copy)==EPERM);
	CHECK(!bcmv3d_scanout_matches(&copy,&bo[0].cma.base));
	mutex_enter(&n->process_lock);
	CHECK(bcmv3d_scanout_retirement_check(a,&mmu,0,&bo[0].cma.base)==EPERM);
	CHECK(bcmv3d_scanout_retirement_check(&copy,&mmu,0,&bo[0].cma.base)==EPERM);
	mutex_exit(&n->process_lock);
	CHECK(bcmv3d_scanout_submit(n,a)==0);prior=io_calls;
	CHECK(ends==0&&a->read.fence->pending&&slots[0].exposed);
	CHECK(bcmv3d_scanout_submit(n,a)==EBUSY&&io_calls==prior);
	emit(32,1,0);CHECK(latch_notifies==0&&ends==0);
	emit(42,1,0);CHECK(latch_notifies==1&&ends==0);
	CHECK(bcmv3d_scanout_prepare(n,&bo[0].cma.base,&again)==0);
	CHECK(again==a&&begins==1&&a->refs==2);
	CHECK(bcmv3d_scanout_submit(n,again)==0&&io_calls==prior+1);
	/* Same BO submit performs an actual-head read, but rewrites no SRAM. */
	emit(42,1,0);CHECK(latch_notifies==2&&ends==0);
	CHECK(bcmv3d_scanout_put(again)==0);
	CHECK(bcmv3d_scanout_prepare(n,&bo[1].cma.base,&b)==0);
	CHECK(bcmv3d_scanout_submit(n,b)==0);
	emit(52,1,0);drain();CHECK(ends==0&&slots[0].object!=NULL);
	emit(52,0,0);drain();CHECK(ends==0);
	emit(52,1,0);CHECK(ends==0);drain();
	CHECK(ends==1&&slots[0].object==NULL&&a->read.fence==NULL);
	CHECK(bcmv3d_scanout_put(a)==0&&bo[0].cma.base.refs==1);
	CHECK(bcmv3d_scanout_put(b)==0&&bo[1].cma.base.refs==4);
	CHECK(bcmv3d_scanout_submit(n,NULL)==0);
	emit(72,1,0);drain();CHECK(ends==1);
	emit(72,1,0);drain();CHECK(ends==2&&bo[1].cma.base.refs==1);
	CHECK(regs[(variant==0x53?0x34:0x104)/4]&0x100);
	CHECK(bcmv3d_scanout_cached_status(n,&status)==0);
	CHECK(status.first_error==0&&status.current_slot==3&&status.retired==2);
	CHECK(status.latches==4&&status.prepared==2&&status.pending_slot==-1);
}
static void cancelled(unsigned failure)
{
	struct bcmv3d_scanout *n=start(0x54);
	struct bcmv3d_scanout_frame *f=(void *)(uintptr_t)0x1000;
	int expected=0;
	if(failure==1) { begin_fail=true;expected=ETIME; }
	if(failure==2) { stage_fail=true;expected=ENOMEM; }
	if(failure==3) { sync_fail=true;expected=EINVAL; }
	if(failure==4) { begin_route_fault=true;expected=EIO; }
	CHECK(bcmv3d_scanout_prepare(n,&bo[0].cma.base,&f)==expected);
	if(failure) CHECK(f==(void *)(uintptr_t)0x1000);
	else CHECK(bcmv3d_scanout_put(f)==0);
	if(failure==4) {
		drain();CHECK(bo[0].cma.base.refs==3 && slots[0].object==NULL && io_calls==0);
		CHECK(poisons==1 && ends==0 && n->flip.fault==EIO);
	} else {
		CHECK(bo[0].cma.base.refs==1&&slots[0].object==NULL&&io_calls==0);
		CHECK(poisons==0);
	}
}
static void retained(unsigned failure)
{
	struct bcmv3d_scanout *n=start(0x54);
	struct bcmv3d_scanout_frame *f,*next;
	struct bcmv3d_scanout_status status;
	unsigned before;
	if(failure==1) {
		publish_fail=true;
		CHECK(bcmv3d_scanout_prepare(n,&bo[0].cma.base,&f)==EIO);
	} else {
		CHECK(bcmv3d_scanout_prepare(n,&bo[0].cma.base,&f)==0);
		if(failure==2) expose_fail=true;
		if(failure==3) io_fail_at=io_calls+3;
		if(failure==2||failure==3) CHECK(bcmv3d_scanout_submit(n,f)==EIO);
		else {
			CHECK(bcmv3d_scanout_submit(n,f)==0);emit(42,1,0);
			if(failure==4) emit(62,1,0);
			else if(failure==5) emit(42,1,EIO);
			else {
				CHECK(bcmv3d_scanout_prepare(n,&bo[1].cma.base,&next)==0);
				CHECK(bcmv3d_scanout_submit(n,next)==0);emit(52,1,0);
				if(failure==7) retire_route_fault=true; else retire_fail=true;
				emit(52,1,0);
			}
		}
	}
	drain();before=io_calls;
	CHECK(bcmv3d_scanout_cached_status(n,&status)==0&&status.first_error!=0);
	CHECK(poisons>=1&&ends==0);
	CHECK(unloads==(failure==7?1U:0U)&&bo[0].cma.base.refs==(failure==7?3U:4U));
	CHECK(bcmv3d_scanout_submit(n,NULL)==(int)status.first_error);
	CHECK(io_calls==before);
}
static void io_fault(unsigned position)
{
	unsigned variant=position<26?0x53:0x54;
	unsigned at=position<26?position+1:position-26+1;
	struct bcmv3d_scanout *n=start(variant);
	struct bcmv3d_scanout_frame *f;
	CHECK(bcmv3d_scanout_prepare(n,&bo[0].cma.base,&f)==0);
	io_fail_at=at;
	CHECK(bcmv3d_scanout_submit(n,f)==EIO&&io_calls==at);
	drain();
	CHECK(n->flip.fault==EIO&&ends==0&&unloads==0&&poisons==1);
	CHECK(bo[0].cma.base.refs==4);
}
static void constructor(unsigned scenario)
{
	struct bcmv3d_scanout *out=(void *)(uintptr_t)0x1000;
	uint32_t words=4096,variant=0x54;
	int expected=ENXIO;
	setup(variant);
	switch(scenario) {
	case 0: contract.memory[3].address++;break;
	case 1: words=72;break;
	case 2: variant=0x55;break;
	case 3: snapshot.routed=0;expected=EBUSY;break;
	case 4: snapshot.first_error=EIO;expected=EBUSY;break;
	case 5: wq_fail=true;expected=ENOMEM;break;
	case 6: bind_fail=true;expected=EIO;break;
	case 7: mutex_enter(&device.memory_lock);expected=EDEADLK;break;
	case 8: in_irq=true;expected=EWOULDBLOCK;break;
	}
	CHECK(bcmv3d_scanout_create(&device,&contract,7,variant,words,&out)==expected);
	if(scenario==7) mutex_exit(&device.memory_lock);
	in_irq=false;
	CHECK(out==(void *)(uintptr_t)0x1000);
	if(scenario==6) {
		CHECK(device.scanout&&drm.refs==1&&allocations==1);drain();
		CHECK(device.scanout->flip.fault==EIO);
	} else CHECK(device.scanout==NULL&&allocations==0&&drm.refs==0);
	CHECK(io_calls==0);
}
static void races(unsigned which)
{
	struct bcmv3d_scanout *n;
	struct bcmv3d_scanout_frame *a,*b;
	struct bcmv3d_vblank_snapshot delayed;
	if(which==2) construction_delay=true;
	n=start(0x54);
	if(which==2) {
		emit(32,1,0);CHECK(n->flip.fault==0&&n->flip.sequence==81);return;
	}
	CHECK(bcmv3d_scanout_prepare(n,&bo[0].cma.base,&a)==0);
	if(which==0) {
		expose_route_fault=true;
		CHECK(bcmv3d_scanout_submit(n,a)==EPROTO&&io_calls==0);
		drain();CHECK(poisons==1&&unloads==0&&ends==0);return;
	}
	CHECK(bcmv3d_scanout_submit(n,a)==0);emit(42,1,0);
	if(which==1) {
		/* Capture a real event, but defer its callback across submit. */
		snapshot.sequence++;snapshot.frame_count=(snapshot.frame_count+1)&63;
		delayed=snapshot;
		CHECK(bcmv3d_scanout_submit(n,a)==0);
		in_irq=true;pv_notify(pv_arg,&delayed);in_irq=false;
		CHECK(n->flip.pending_slot==0&&n->latches==1);
		emit(42,1,0);CHECK(n->flip.pending_slot==-1&&n->latches==2);return;
	}
	CHECK(which==3);
	CHECK(bcmv3d_scanout_prepare(n,&bo[1].cma.base,&b)==0);
	CHECK(bcmv3d_scanout_submit(n,b)==0);emit(52,1,0);
	end_route_fault=true;emit(52,1,0);drain();
	CHECK(n->flip.fault==EIO&&unloads==1&&ends==1&&poisons==1);
	CHECK(a->completion_committed&&a->read.fence==NULL);
	CHECK(b->read.poisoned&&slots[1].exposed&&slots[0].object==NULL);
	CHECK(bcmv3d_scanout_submit(n,NULL)==EIO);
}

static void reserved(unsigned scenario)
{
	struct bcmv3d_scanout *n=start(0x54);
	struct bcmv3d_scanout_frame *a,*again=NULL,*b;
	unsigned before;
	if(scenario==7) {
		reserve_fail=true;
		CHECK(bcmv3d_scanout_reserve(n,&bo[0].cma.base,&again)==ENOMEM);
		CHECK(again==NULL && bo[0].cma.base.refs==1 && ends==0 && waits==0);
		CHECK(n->flip.slot[0].state==BCMV3D_FLIP_FREE && n->frame[0].object==NULL);
		return;
	}
	if(scenario==6) reserve_reentry=true;
	CHECK(bcmv3d_scanout_reserve(n,&bo[0].cma.base,&a)==0);
	CHECK(n->flip.slot[0].state==BCMV3D_FLIP_RESERVED && slots[0].published &&
	    !slots[0].synced && !slots[0].exposed && waits==0 && io_calls==0);
	CHECK(a->read.prior==&original_writer && n->prepared==0);
	CHECK(bcmv3d_scanout_matches(a,&bo[0].cma.base));
	CHECK(bcmv3d_scanout_submit(n,a)==EBUSY && io_calls==0);
	reservation_locked=true;before=begins;
	CHECK(bcmv3d_scanout_reserve(n,&bo[0].cma.base,&again)==0 && again==a);
	CHECK(begins==before && a->refs==2 && a->read.prior==&original_writer);
	CHECK(bcmv3d_scanout_put(again)==0);reservation_locked=false;
	if(scenario==0 || scenario==6) {
		CHECK(bcmv3d_scanout_put(a)==0 && bo[0].cma.base.refs==1);
		CHECK(ends==1 && unloads==1 && n->flip.slot[0].state==BCMV3D_FLIP_FREE);
		CHECK(n->prepared==0 && waits==0);return;
	}
	if(scenario==1) {
		begin_fail=true;
		CHECK(bcmv3d_scanout_finalize(a,5*hz)==ETIME && !slots[0].synced);
		CHECK(a->read.prior==&original_writer && n->flip.fault==0);
		begin_fail=false;
		CHECK(bcmv3d_scanout_finalize(a,5*hz)==0 && waits==2);
		before=waits;CHECK(bcmv3d_scanout_finalize(a,5*hz)==0 && waits==before);
		CHECK(n->prepared==1 && slots[0].synced);
		CHECK(bcmv3d_scanout_put(a)==0 && ends==1 && unloads==1);return;
	}
	if(scenario==2) {
		writer_error=true;
		CHECK(bcmv3d_scanout_finalize(a,5*hz)==EIO && !slots[0].synced);
		CHECK(bcmv3d_scanout_put(a)==0 && ends==1 && unloads==1 && n->flip.fault==0);return;
	}
	if(scenario==3) {
		unmap_fail=true;
		CHECK(bcmv3d_scanout_put(a)==EIO);drain();
		CHECK(ends==0 && unloads==0 && poisons==1 && bo[0].cma.base.refs==4);return;
	}
	if(scenario==4) {
		wait_route_fault=true;
		CHECK(bcmv3d_scanout_finalize(a,5*hz)==EIO);drain();
		CHECK(!slots[0].synced && waits==1 && ends==0 && unloads==0 && poisons==1);
		CHECK(bcmv3d_scanout_put(a)==EIO && io_calls==0);return;
	}
	CHECK(scenario==5);
	CHECK(bcmv3d_scanout_finalize(a,5*hz)==0);
	CHECK(bcmv3d_scanout_submit(n,a)==0);emit(42,1,0);
	CHECK(bcmv3d_scanout_prepare(n,&bo[1].cma.base,&b)==0);
	CHECK(bcmv3d_scanout_submit(n,b)==0);emit(52,1,0);
	CHECK(n->flip.slot[0].state==BCMV3D_FLIP_RETIRING);
	reservation_locked=true;before=begins;
	CHECK(bcmv3d_scanout_reserve(n,&bo[0].cma.base,&again)==EBUSY && begins==before);
	reservation_locked=false;emit(52,1,0);drain();
	CHECK(bcmv3d_scanout_put(a)==0 && bo[0].cma.base.refs==1);
}

static void retirement(unsigned scenario)
{
	struct bcmv3d_scanout *n=start(0x54);
	struct bcmv3d_scanout_frame *a,*b,copy;
	unsigned refs,io_before;
	int expected=0;
	CHECK(bcmv3d_scanout_prepare(n,&bo[0].cma.base,&a)==0);
	copy=*a;
	CHECK(bcmv3d_scanout_wait_retired(NULL,5*hz)==EINVAL);
	CHECK(bcmv3d_scanout_wait_retired(a,0)==EINVAL);
	CHECK(bcmv3d_scanout_wait_retired(a,-1)==EINVAL);
	mutex_enter(&device.memory_lock);
	CHECK(bcmv3d_scanout_wait_retired(a,5*hz)==EDEADLK);
	mutex_exit(&device.memory_lock);
	in_irq=true;CHECK(bcmv3d_scanout_wait_retired(a,5*hz)==EWOULDBLOCK);in_irq=false;
	CHECK(bcmv3d_scanout_wait_retired(&copy,5*hz)==EPERM);
	CHECK(bcmv3d_scanout_wait_retired(a,5*hz)==EBUSY);
	CHECK(bcmv3d_scanout_submit(n,a)==0);
	CHECK(bcmv3d_scanout_wait_retired(a,5*hz)==EBUSY&&retirement_waits==0);
	emit(42,1,0);
	CHECK(bcmv3d_scanout_wait_retired(a,5*hz)==EBUSY&&retirement_waits==0);
	CHECK(bcmv3d_scanout_prepare(n,&bo[1].cma.base,&b)==0);
	CHECK(bcmv3d_scanout_submit(n,b)==0);emit(52,1,0);
	CHECK(n->flip.slot[0].state==BCMV3D_FLIP_RETIRING);
	refs=a->refs;io_before=io_calls;
	if(scenario==18)emit(52,0,ERESTARTSYS);
	if(scenario==8||scenario==9) {emit(52,1,0);if(scenario==8)drain();}
	if(scenario==10||scenario==11) {
		emit(52,1,0);drain();
		if(scenario==10) {
			a->completion_committed=false;
			CHECK(bcmv3d_scanout_wait_retired(a,5*hz)==EBUSY);
			a->completion_committed=true;
		} else {
			a->staged=true;CHECK(bcmv3d_scanout_wait_retired(a,5*hz)==EBUSY);a->staged=false;
			a->published=true;CHECK(bcmv3d_scanout_wait_retired(a,5*hz)==EBUSY);a->published=false;
			a->exposed=true;CHECK(bcmv3d_scanout_wait_retired(a,5*hz)==EBUSY);a->exposed=false;
			a->read.owner=&a->read;CHECK(bcmv3d_scanout_wait_retired(a,5*hz)==EBUSY);a->read.owner=NULL;
			a->read.object=a->object;CHECK(bcmv3d_scanout_wait_retired(a,5*hz)==EBUSY);a->read.object=NULL;
			a->read.fence=&original_writer;CHECK(bcmv3d_scanout_wait_retired(a,5*hz)==EBUSY);a->read.fence=NULL;
			a->read.prior=&original_writer;CHECK(bcmv3d_scanout_wait_retired(a,5*hz)==EBUSY);a->read.prior=NULL;
			a->read.poisoned=true;CHECK(bcmv3d_scanout_wait_retired(a,5*hz)==EBUSY);a->read.poisoned=false;
		}
		CHECK(bcmv3d_scanout_wait_retired(a,5*hz)==0&&retirement_waits==0);
	} else {
		retirement_mode=scenario;
		if(scenario==1)expected=ETIME;
		if(scenario==2||scenario==13)expected=EINTR;
		if((scenario>=3&&scenario<=6)||(scenario>=14&&scenario<=18))expected=EIO;
		if(scenario==7||scenario==12)expected=EBUSY;
		CHECK(bcmv3d_scanout_wait_retired(a,5*hz)==expected);
		CHECK(retirement_waits==((scenario==8||scenario==18)?0U:1U));
	}
	CHECK(a->refs==refs&&a->object==&bo[0].cma.base&&io_calls==io_before);
	if(expected==0) {
		CHECK(n->flip.slot[0].state==BCMV3D_FLIP_FREE&&a->read.fence==NULL);
		CHECK(ends==1&&unloads==1&&a->completion_committed);
		CHECK(bcmv3d_scanout_put(a)==0&&bo[0].cma.base.refs==1);
	} else if(scenario==1||scenario==2||scenario==5) {
		CHECK(a->read.fence&&a->read.fence->refs==1&&ends==0&&unloads==0);
	} else if(scenario==7) {
		CHECK(a->read.fence&&a->read.fence->refs==1&&ends==0&&unloads==0);
	}
}

static void run(unsigned scenario)
{
	if(scenario<2) life(scenario==0?0x53:0x54);
	else if(scenario<7) cancelled(scenario-2);
	else if(scenario<14) retained(scenario-6);
	else if(scenario<67) io_fault(scenario-14);
	else if(scenario<76) constructor(scenario-67);
	else if(scenario<80) races(scenario-76);
	else if(scenario<88) reserved(scenario-80);
	else retirement(scenario-88);
	CHECK(process_locks==0&&irq_locks==0);
}
int main(void)
{
	unsigned i;int status;pid_t pid;
	for(i=0;i<107;i++) {
		pid=fork();CHECK(pid>=0);
		if(pid==0) { run(i);exit(0); }
		CHECK(waitpid(pid,&status,0)==pid);
		CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==0);
	}
	puts("PASS actual native scanout:107 isolated boots,53 I/O fault positions, consumer ordering,"
	    "same-BO lifetime, actual-head latch, extra frame retirement and quarantine");
	return 0;
}
