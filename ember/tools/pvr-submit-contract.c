/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Actual submit and decoder. Model only kernel/DRM/firmware boundaries. */
#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif_client.h>
#include "pvr_backend.h"
#include "pvr_context.h"
#include "pvr_exec.h"
#include "pvr_file.h"
#include "pvr_stream.h"
#define DRM_COMMAND_BASE 0x40
#define DRM_IOWR(n,t) (n)
#define DRM_IOW(n,t) (n)
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
typedef int32_t __s32;
#include "pvr-uapi-body.h"

static atomic_uint checks, allocations, fence_count;
#define CHECK(e) do { atomic_fetch_add(&checks,1); if (!(e)) { \
	fprintf(stderr,"FAIL %d: %s\n",__LINE__,#e); abort(); \
} } while (0)
#define KASSERT(e) CHECK(e)
#ifndef ERESTART
#define ERESTART (-3) /* Native kernel restart sentinel. */
#endif
#ifndef ERESTARTSYS
#define ERESTARTSYS 1024 /* Positive Linux boundary-model sentinel. */
#endif
#define KM_NOSLEEP 0
#define GFP_NOWAIT 0
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#ifndef timespeccmp
#define timespeccmp(a,b,op) ((a)->tv_sec == (b)->tv_sec ? \
    (a)->tv_nsec op (b)->tv_nsec : (a)->tv_sec op (b)->tv_sec)
#endif
#ifndef timespecsub
static void timespecsub(const struct timespec *a,const struct timespec *b,struct timespec *out)
{ out->tv_sec=a->tv_sec-b->tv_sec; out->tv_nsec=a->tv_nsec-b->tv_nsec; if(out->tv_nsec<0){out->tv_sec--;out->tv_nsec+=1000000000;} }
#endif
static const int hz=100;
static _Thread_local bool gated, platform;
static _Thread_local int64_t clock_ns;
static _Thread_local bool waiting_thread;
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER, sync_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t rendezvous=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static bool concurrent, waiter_entered, waiter_released, frozen_clock, complete_on_wait;
static unsigned allocate_calls, allocation_fault, copy_calls, copy_fault;
static unsigned prepared, completed, computed, poisoned, signals, wait_calls, polls;
static unsigned compute_error_at, next_id, transferred, fragmented;
static bool triple_model;
static int fragment_error, after_pair_prepare, after_pair_backend, after_pair_target;
static int transfer_error;
static int prepare_error, backend_error, pause_error, wait_error, id_error;
static bool backend_changed, switch_backend_on_pause, backend_absent;
static void *retained;

struct drm_device { unsigned dummy; };
struct drm_file { unsigned dummy; };
struct pvr_file_vm { unsigned prepared; bool failed; };
struct pvr_context { uint32_t type; };
struct pvr_exec_context {
	unsigned refs;
	bool handle_live, destroyed;
	struct pvr_file_vm vm;
	struct pvr_context firmware;
};
static struct drm_device device;
static struct drm_file file;
static struct pvr_exec_context contexts[4];

struct dma_fence {
	atomic_uint refs;
	int status;
	uint64_t seqno;
	bool chain, immortal;
};
struct dma_fence_chain {
	struct dma_fence base;
	struct dma_fence *dfc_fence, *prev;
};
struct drm_syncobj {
	unsigned refs;
	bool handle_live;
	struct dma_fence *fence;
};
static struct drm_syncobj syncobjs[8];
static struct dma_fence stub;

struct user_range { const uint8_t *p; size_t size; };
static struct user_range ranges[16];
static unsigned nranges;
static void register_user(const void *p,size_t size)
{ CHECK(nranges<16); ranges[nranges++]=(struct user_range){p,size}; }
static int copyin(const void *p,void *out,size_t size)
{
	unsigned i;
	CHECK(!gated); copy_calls++;
	if(copy_calls==copy_fault) return EFAULT;
	for(i=0;i<nranges;i++) {
		uintptr_t start=(uintptr_t)ranges[i].p, ptr=(uintptr_t)p;
		if(ptr>=start && ptr-start<=ranges[i].size && size<=ranges[i].size-(ptr-start)) {
			memcpy(out,p,size); return 0;
		}
	}
	return EFAULT;
}
static void *kmem_zalloc(size_t size,int flags)
{
	void *p;
	CHECK(flags==KM_NOSLEEP && size!=0); allocate_calls++;
	/* Model allocation failure, not a production count or memory limit. */
	if(allocate_calls==allocation_fault || size>1048576) return NULL;
	p=calloc(1,size); CHECK(p!=NULL); atomic_fetch_add(&allocations,1); return p;
}
static void kmem_free(void *p,size_t size)
{ CHECK(p!=NULL && atomic_fetch_sub(&allocations,1)!=0); free(p); }
static void *kzalloc(size_t size,int flags)
{ CHECK(gated && platform && flags==GFP_NOWAIT); return kmem_zalloc(size,KM_NOSLEEP); }
static void kfree(void *p)
{ if(p!=NULL) kmem_free(p,0); }

static struct dma_fence *dma_fence_get(struct dma_fence *f)
{ if(f) CHECK(atomic_fetch_add(&f->refs,1)!=0); return f; }
static void dma_fence_put(struct dma_fence *f)
{
	struct dma_fence_chain *chain;
	if(!f) return;
	CHECK(atomic_load(&f->refs)!=0);
	if(atomic_fetch_sub(&f->refs,1)!=1) return;
	CHECK(f != &stub && !f->immortal);
	atomic_fetch_sub(&fence_count,1);
	if(f->chain) {
		chain=(struct dma_fence_chain *)f;
		dma_fence_put(chain->dfc_fence); dma_fence_put(chain->prev); kfree(chain);
	} else free(f);
}
static struct dma_fence *new_fence(int status)
{
	struct dma_fence *f=calloc(1,sizeof(*f)); CHECK(f!=NULL);
	atomic_init(&f->refs,1); f->status=status; atomic_fetch_add(&fence_count,1); return f;
}
static struct dma_fence_chain *to_dma_fence_chain(struct dma_fence *f)
{ return f && f->chain ? (struct dma_fence_chain *)f : NULL; }
static int dma_fence_get_status(struct dma_fence *f)
{
	struct dma_fence_chain *c;
	CHECK(f!=NULL && atomic_load(&f->refs)!=0);
	c=to_dma_fence_chain(f);
	if(c) {
		if(dma_fence_get_status(c->dfc_fence)==0 || (c->prev && dma_fence_get_status(c->prev)==0)) return 0;
		return 1; /* Native chain base does not propagate child.error. */
	}
	return f->status;
}
static unsigned rendered;
static struct dma_fence *dma_fence_get_stub(void)
{ CHECK(gated && computed+rendered+transferred+fragmented!=0 && completed==computed+rendered+transferred+fragmented); return dma_fence_get(&stub); }
static int dma_fence_chain_find_seqno(struct dma_fence **fp,uint64_t seqno)
{
	struct dma_fence *f=*fp;
	struct dma_fence_chain *c;
	if(seqno==0) return 0;
	c=to_dma_fence_chain(f);
	if(c==NULL || f->seqno<seqno) return -EINVAL;
	while(c->prev && c->prev->chain && c->prev->seqno>=seqno) {
		f=c->prev; c=(struct dma_fence_chain *)f;
	}
	if(f!=*fp) { dma_fence_get(f); dma_fence_put(*fp); *fp=f; }
	return 0;
}
static void pause_for_actor(void)
{
	CHECK(!gated && !platform);
	if(!concurrent || !waiting_thread) return;
	CHECK(pthread_mutex_lock(&rendezvous)==0);
	waiter_entered=true; CHECK(pthread_cond_broadcast(&changed)==0);
	while(!waiter_released) CHECK(pthread_cond_wait(&changed,&rendezvous)==0);
	CHECK(pthread_mutex_unlock(&rendezvous)==0);
}
static long dma_fence_wait_timeout(struct dma_fence *f,bool intr,long timeout)
{
	CHECK(!gated && !platform && intr && timeout>0); wait_calls++;
	pause_for_actor();
	if(wait_error) return -wait_error;
	/* Model an existing external fence completing inside the real wait. */
	if(complete_on_wait) {
		struct dma_fence *completion=f;
		while(completion->chain) completion=((struct dma_fence_chain *)completion)->dfc_fence;
		CHECK(completion->status==0); completion->status=1;
	}
	return dma_fence_get_status(f)!=0 ? 1 : 0;
}
static void getnanouptime(struct timespec *ts)
{ ts->tv_sec=(time_t)(clock_ns/1000000000); ts->tv_nsec=(long)(clock_ns%1000000000); }
static int tstohz(const struct timespec *ts)
{ return (int)(ts->tv_sec*hz+(ts->tv_nsec+9999999)/10000000); }
static int kpause(const char *name,bool intr,int ticks,void *lock)
{
	CHECK(!gated && !platform && intr && ticks==1 && lock==NULL); polls++;
	pause_for_actor();
	if(!frozen_clock) clock_ns+=10000000;
	if(switch_backend_on_pause) backend_changed=true;
	return pause_error ? pause_error : EWOULDBLOCK;
}

static int check_backend(void *p) { CHECK(gated && platform); return backend_error; }
static const struct pvr_backend backend={.check=check_backend}, other_backend={.check=check_backend};
const struct pvr_backend *pvr_device_enter(struct drm_device *dev)
{
	CHECK(dev==&device && !gated && !platform); CHECK(pthread_mutex_lock(&gate)==0);
	gated=platform=true;
	return backend_absent ? NULL : backend_changed ? &other_backend : &backend;
}
void pvr_device_leave(struct drm_device *dev)
{ CHECK(dev==&device && gated && platform); gated=platform=false; CHECK(pthread_mutex_unlock(&gate)==0); }
void pvr_device_retain(struct drm_device *dev,void *owner)
{ CHECK(gated && platform && retained==NULL); retained=owner; }
int pvr_device_next_id(struct drm_device *dev,uint32_t *id)
{ CHECK(gated && platform); if(id_error) return id_error; *id=++next_id; return 0; }
struct pvr_exec_context *pvr_exec_context_get(struct drm_file *f,uint32_t handle)
{
	CHECK(gated && platform && f==&file);
	if(handle==0 || handle>=4 || !contexts[handle].handle_live) return NULL;
	CHECK(!contexts[handle].destroyed); contexts[handle].refs++; return &contexts[handle];
}
void pvr_exec_context_put(struct drm_device *dev,struct pvr_exec_context *c)
{ CHECK(gated && platform && c->refs!=0); if(--c->refs==0) c->destroyed=true; }
struct pvr_file_vm *pvr_exec_context_vm(struct pvr_exec_context *c)
{ CHECK(gated && platform && c->refs!=0 && !c->destroyed); return &c->vm; }
struct pvr_context *pvr_exec_context_firmware(struct pvr_exec_context *c)
{ CHECK(gated && platform && c->refs!=0 && !c->destroyed); return &c->firmware; }
uint32_t pvr_context_type(const struct pvr_context *c) { return c->type; }
int pvr_file_vm_prepare(struct pvr_file_vm *vm)
{ CHECK(gated && platform && !vm->failed); prepared++; vm->prepared++; return prepare_error; }
void pvr_file_vm_complete(struct pvr_file_vm *vm)
{ CHECK(gated && platform && vm->prepared!=0); completed++; }
void pvr_file_vm_fail(struct pvr_file_vm *vm)
{ CHECK(gated && platform); vm->failed=true; poisoned++; }
int pvr_context_compute(struct pvr_context *c,const struct rogue_fwif_cmd_compute *cmd,uint32_t id)
{
	CHECK(gated && platform && c->type==DRM_PVR_CTX_TYPE_COMPUTE && id!=0);
	CHECK(cmd->regs.cdm_ctrl_stream_base==UINT64_C(0x8000001000));
	computed++;
	if(computed==compute_error_at) return EIO;
	return 0;
}
int
pvr_context_transfer(struct pvr_context *c,
    const struct rogue_fwif_cmd_transfer *cmd, uint32_t id)
{
	CHECK(gated && platform && c->type == DRM_PVR_CTX_TYPE_TRANSFER_FRAG);
	CHECK(id != 0 && cmd->regs.pds_bgnd0_base == UINT64_C(0x9002000000));
	CHECK(contexts[2].refs == 2 && contexts[2].vm.prepared == 1);
	CHECK(cmd->flags == ROGUE_TRANSFER_FLAGS_SINGLE_CORE);
	transferred++;
	return transfer_error;
}
static unsigned render_error_at;
static uint32_t last_geom_hwrt, last_frag_hwrt, last_cleanup, last_frag_type;
int pvr_context_render_pair(struct pvr_context *c,const struct rogue_fwif_cmd_geom *g,
    const struct rogue_fwif_cmd_frag *f,uint32_t ftype,uint32_t cleanup,
    uint32_t gid,uint32_t fid)
{
	CHECK(gated && platform && c->type==DRM_PVR_CTX_TYPE_RENDER);
	CHECK(gid!=0 && fid!=0 && cleanup!=0 && (cleanup&63)==0);
	CHECK(g->cmd_shared.hwrt_data_fw_addr!=0);
	CHECK(g->cmd_shared.hwrt_data_fw_addr==f->cmd_shared.hwrt_data_fw_addr);
	CHECK(ftype==ROGUE_FWIF_CCB_CMD_TYPE_FRAG||ftype==ROGUE_FWIF_CCB_CMD_TYPE_FRAG_PR);
	last_geom_hwrt=g->cmd_shared.hwrt_data_fw_addr;
	last_frag_hwrt=f->cmd_shared.hwrt_data_fw_addr;
	last_cleanup=cleanup; last_frag_type=ftype;
	rendered++;
	if (triple_model) {
		CHECK(contexts[1].refs == 4 && prepared == 1 && completed == 0);
		CHECK(gid == 1 && fid == 2 && ftype == ROGUE_FWIF_CCB_CMD_TYPE_FRAG_PR);
		CHECK(g->flags == (ROGUE_GEOM_FLAGS_FIRSTKICK | ROGUE_GEOM_FLAGS_LASTKICK));
		CHECK(f->flags == 0 && f->regs.usc_clear_register[0] == 0x11223344);
	}
	if(rendered==render_error_at) return EIO;
	prepare_error = after_pair_prepare;
	backend_error = after_pair_backend;
	return 0;

}
static struct drm_syncobj *drm_syncobj_find(struct drm_file *f,uint32_t handle)
{
	struct drm_syncobj *s=NULL;
	CHECK(gated && platform && f==&file); CHECK(pthread_mutex_lock(&sync_lock)==0);
	if(handle>0 && handle<8 && syncobjs[handle].handle_live) { s=&syncobjs[handle]; s->refs++; }
	CHECK(pthread_mutex_unlock(&sync_lock)==0); return s;
}
static void drm_syncobj_put(struct drm_syncobj *s)
{
	CHECK(pthread_mutex_lock(&sync_lock)==0); CHECK(s->refs!=0);
	if(--s->refs==0) { dma_fence_put(s->fence); s->fence=NULL; }
	CHECK(pthread_mutex_unlock(&sync_lock)==0);
}
static struct dma_fence *drm_syncobj_fence_get(struct drm_syncobj *s)
{
	struct dma_fence *f;
	CHECK(!gated && !platform); CHECK(pthread_mutex_lock(&sync_lock)==0);
	CHECK(s->refs!=0); f=dma_fence_get(s->fence); CHECK(pthread_mutex_unlock(&sync_lock)==0); return f;
}
static void drm_syncobj_replace_fence(struct drm_syncobj *s,struct dma_fence *f)
{
	CHECK(gated && platform && computed+rendered+transferred+fragmented==completed && computed+rendered+transferred+fragmented!=0);
	CHECK(pthread_mutex_lock(&sync_lock)==0); CHECK(s->refs!=0);
	dma_fence_put(s->fence); s->fence=dma_fence_get(f); signals++;
	CHECK(pthread_mutex_unlock(&sync_lock)==0);
}
static void drm_syncobj_add_point(struct drm_syncobj *s,struct dma_fence_chain *c,struct dma_fence *f,uint64_t point)
{
	CHECK(gated && platform && c!=NULL && point!=0 && computed+rendered+transferred+fragmented==completed && computed+rendered+transferred+fragmented!=0);
	CHECK(pthread_mutex_lock(&sync_lock)==0); CHECK(s->refs!=0);
	atomic_init(&c->base.refs,1); c->base.chain=true; c->base.seqno=point;
	c->dfc_fence=dma_fence_get(f); c->prev=s->fence;
	s->fence=&c->base; atomic_fetch_add(&fence_count,1); signals++;
	CHECK(pthread_mutex_unlock(&sync_lock)==0);
}
#include "pvr-exec-hwrt-body.h"
#include "pvr-hwrt-body.h"
static unsigned hwrt_gets, hwrt_puts, hwrt_targets;
static void *last_hwrt_file;
static uint32_t last_hwrt_handle;
struct pvr_exec_hwrt { unsigned refs; };
static struct pvr_exec_hwrt hwrt_model[4];
static struct pvr_file_vm *hwrt_model_vm;
static int hwrt_target_error;
struct pvr_exec_hwrt *
pvr_exec_hwrt_get(struct drm_file *file, uint32_t handle)
{
	hwrt_gets++;
	last_hwrt_file = file;
	last_hwrt_handle = handle;
	if (handle == 0 || handle > 4)
		return NULL;
	CHECK(gated && platform);
	hwrt_model[handle - 1].refs++;
	return &hwrt_model[handle - 1];
}
void
pvr_exec_hwrt_put(struct drm_device *dev, struct pvr_exec_hwrt *hwrt)
{
	CHECK(gated && platform && hwrt->refs != 0);
	hwrt->refs--;
	hwrt_puts++;
}
struct pvr_file_vm *
pvr_exec_hwrt_vm(const struct pvr_exec_hwrt *hwrt)
{
	CHECK(gated && platform && hwrt->refs != 0);
	return hwrt_model_vm;
}
int
pvr_exec_hwrt_target(struct pvr_exec_hwrt *hwrt, unsigned index,
    struct pvr_hwrt_target *target)
{
	CHECK(gated && platform && hwrt->refs != 0);
	hwrt_targets++;
	if (hwrt_target_error != 0)
		return hwrt_target_error;
	if (hwrt_targets == 2 && after_pair_target != 0)
		return after_pair_target;
	if (index > 1)
		return EINVAL;
	target->fw_address = 0xc0aa0000;
	target->cleanup_address = 0xc0aa0040;
	return 0;
}
int
pvr_context_fragment(struct pvr_context *c,
    const struct rogue_fwif_cmd_frag *cmd, uint32_t cleanup, uint32_t id)
{
	CHECK(gated && platform && c->type == DRM_PVR_CTX_TYPE_RENDER);
	CHECK(triple_model && contexts[1].refs == 4 && hwrt_model[0].refs == 1);
	CHECK(rendered == 1 && fragmented == 0 && prepared == 2 && completed == 1);
	CHECK(id == 3 && cmd->cmd_shared.hwrt_data_fw_addr == last_geom_hwrt);
	CHECK(cleanup == last_cleanup && cleanup == 0xc0aa0040);
	CHECK(cmd->regs.isp_scissor_base == UINT64_C(0x9003000000));
	CHECK(cmd->regs.usc_clear_register[0] == 0x55667788);
	CHECK(cmd->flags == ROGUE_FRAG_FLAGS_PREVENT_CDM_OVERLAP);
	fragmented++;
	return fragment_error;
}
#include "pvr-submit-body.h"


static struct drm_pvr_job jobs[4];
static struct drm_pvr_sync_op ops[4][4];
static uint8_t streams[4][384];
static struct drm_pvr_ioctl_submit_jobs_args arguments;
static void put32(uint8_t *p,uint32_t v)
{ unsigned i; for(i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8)); }
static void put64(uint8_t *p,uint64_t v)
{ unsigned i; for(i=0;i<8;i++)p[i]=(uint8_t)(v>>(i*8)); }
static void initialize(unsigned count)
{
	unsigned i;
	CHECK(!gated && !platform && atomic_load(&allocations)==0 && atomic_load(&fence_count)==0);
	memset(contexts,0,sizeof(contexts)); memset(syncobjs,0,sizeof(syncobjs));
	for(i=1;i<4;i++) { contexts[i].refs=1; contexts[i].handle_live=true; contexts[i].firmware.type=DRM_PVR_CTX_TYPE_COMPUTE; }
	for(i=1;i<8;i++) { syncobjs[i].refs=1; syncobjs[i].handle_live=true; }
	memset(jobs,0,sizeof(jobs)); memset(ops,0,sizeof(ops)); memset(streams,0,sizeof(streams));
	allocate_calls=allocation_fault=copy_calls=copy_fault=0;
	prepared=completed=computed=poisoned=signals=wait_calls=polls=0; rendered=hwrt_gets=hwrt_puts=hwrt_targets=0;
	memset(hwrt_model,0,sizeof(hwrt_model)); hwrt_model_vm=NULL; hwrt_target_error=0; render_error_at=0;
	fragmented = 0; triple_model = false;
	fragment_error = after_pair_prepare = after_pair_backend = after_pair_target = 0;
	transferred=0; transfer_error=0; compute_error_at=next_id=0; prepare_error=backend_error=pause_error=wait_error=id_error=0;
	backend_changed=switch_backend_on_pause=backend_absent=false; retained=NULL; concurrent=waiter_entered=waiter_released=frozen_clock=complete_on_wait=false;
	clock_ns=0; waiting_thread=false; nranges=0;
	atomic_init(&stub.refs,1); stub.status=1; stub.immortal=true; stub.chain=false;
	for(i=0;i<count;i++) {
		put32(streams[i],44); put64(streams[i]+16,UINT64_C(0x8000001000));
		jobs[i].type=DRM_PVR_JOB_TYPE_COMPUTE; jobs[i].context_handle=1;
		jobs[i].cmd_stream=(uintptr_t)streams[i]; jobs[i].cmd_stream_len=44;
		jobs[i].sync_ops=(struct drm_pvr_obj_array){16,0,(uintptr_t)ops[i]};
	}
	arguments.jobs=(struct drm_pvr_obj_array){48,count,(uintptr_t)jobs};
	register_user(jobs,sizeof(jobs)); register_user(ops,sizeof(ops)); register_user(streams,sizeof(streams));
}
static void finish(void)
{
	unsigned i;
	CHECK(!gated && !platform && retained==NULL);
	for(i=1;i<8;i++) {
		CHECK(syncobjs[i].refs==(unsigned)syncobjs[i].handle_live);
		if(syncobjs[i].handle_live) { syncobjs[i].handle_live=false; drm_syncobj_put(&syncobjs[i]); }
	}
	for(i=1;i<4;i++) CHECK(contexts[i].refs==(unsigned)contexts[i].handle_live);
	for(i=0;i<4;i++) CHECK(hwrt_model[i].refs==0);
	CHECK(atomic_load(&allocations)==0 && atomic_load(&fence_count)==0 && atomic_load(&stub.refs)==1);
}
static void add_op(unsigned job,unsigned handle,uint32_t flags,uint64_t value)
{
	unsigned n=jobs[job].sync_ops.count++;
	CHECK(n<4); ops[job][n]=(struct drm_pvr_sync_op){handle,flags,value};
}
static int submit(void) { return pvr_submit_jobs(&device,&arguments,&file); }
static void initialize_pair(void)
{
	initialize(2);
	contexts[1].firmware.type=DRM_PVR_CTX_TYPE_RENDER;
	memset(streams,0,sizeof(streams));
	put32(streams[0],40); put64(streams[0]+8,UINT64_C(0x9000000000));
	put32(streams[1],384); put64(streams[1]+8,UINT64_C(0x9001000000));
	jobs[0].type=DRM_PVR_JOB_TYPE_GEOMETRY; jobs[0].hwrt.set_handle=1;
	jobs[0].cmd_stream_len=40;
	jobs[1].type=DRM_PVR_JOB_TYPE_FRAGMENT; jobs[1].hwrt.set_handle=1;
	jobs[1].cmd_stream_len=384;
	hwrt_model_vm=&contexts[1].vm;
}
/* Mesa emits GEOMETRY, FRAG_PR, then an independent final FRAGMENT. */
static void
initialize_triple(void)
{
	initialize_pair();
	arguments.jobs.count = 3;
	jobs[2] = jobs[1];
	jobs[2].cmd_stream = (uintptr_t)streams[2];
	jobs[2].sync_ops = (struct drm_pvr_obj_array){16, 0, (uintptr_t)ops[2]};
	put32(streams[2], PVR_STREAM_FRAGMENT_SIZE);
	put64(streams[2] + 8, UINT64_C(0x9003000000));
	put32(streams[1] + 304, 0x11223344);
	put32(streams[2] + 304, 0x55667788);
	jobs[0].flags = DRM_PVR_SUBMIT_JOB_GEOM_CMD_FIRST | DRM_PVR_SUBMIT_JOB_GEOM_CMD_LAST;
	jobs[1].flags = DRM_PVR_SUBMIT_JOB_FRAG_CMD_PARTIAL_RENDER;
	jobs[2].flags = DRM_PVR_SUBMIT_JOB_FRAG_CMD_PREVENT_CDM_OVERLAP;
	add_op(0, 1, DRM_PVR_SYNC_OP_FLAG_SIGNAL, 0);
	add_op(1, 1, 0, 0);
	add_op(2, 2, DRM_PVR_SYNC_OP_FLAG_SIGNAL, 0);
	triple_model = true;
}

static void
triple_cases(void)
{
	unsigned i;
	int result;

	initialize_triple();
	CHECK(submit() == 0 && arguments.jobs.count == 3);
	CHECK(rendered == 1 && fragmented == 1 && computed == 0 && transferred == 0);
	CHECK(prepared == 2 && completed == 2 && signals == 2 && poisoned == 0);
	CHECK(wait_calls == 0 && polls == 0 && hwrt_targets == 2);
	CHECK(hwrt_gets == 1 && hwrt_puts == 1 && syncobjs[2].fence == &stub);
	finish();

	/* A final wait on the geometry signal is a real, already completed wait. */
	initialize_triple();
	add_op(2, 1, 0, 0);
	CHECK(submit() == 0 && fragmented == 1 && wait_calls == 1);
	finish();
	for (i = 0; i < 5; i++) {
		initialize_triple();
		add_op(2, 3, 0, 0);
		syncobjs[3].fence = new_fence(i == 0 ? 1 : i == 2 ? -EIO : 0);
		complete_on_wait = i == 1;
		if (i == 4)
			wait_error = ERESTARTSYS;
		result = i < 2 ? 0 : i == 2 ? -EIO : i == 3 ? -ETIMEDOUT : -EINTR;
		CHECK(submit() == result && arguments.jobs.count == (i < 2 ? 3U : 2U));
		CHECK(wait_calls == 1 && rendered == 1 && fragmented == (i < 2 ? 1U : 0U));
		CHECK(completed == 1 + fragmented && signals == 1 + fragmented);
		CHECK(hwrt_targets == 1 + fragmented && hwrt_puts == 1 && poisoned == 0);
		CHECK((syncobjs[2].fence != NULL) == (i < 2));
		finish();
	}
	/* Every malformed third job fails before even the first pair is published. */
	for (i = 0; i < 11; i++) {
		initialize_triple();
		result = -EINVAL;
		switch (i) {
		case 0: jobs[2].hwrt.set_handle = 2; break;
		case 1: jobs[2].hwrt.data_index = 1; break;
		case 2:
			jobs[2].context_handle = 2;
			contexts[2].firmware.type = DRM_PVR_CTX_TYPE_RENDER;
			break;
		case 3: jobs[2].cmd_stream_len--; break;
		case 4: jobs[2].cmd_stream = 0; break;
		case 5: ops[2][0].flags = UINT32_MAX; break;
		case 6: ops[2][0].handle = 0; break;
		case 7:
			jobs[2].flags |= DRM_PVR_SUBMIT_JOB_FRAG_CMD_PARTIAL_RENDER;
			result = -EOPNOTSUPP; break;
		case 8:
			jobs[1].flags = 0; /* A normal first fragment is not a PR. */
			result = -EOPNOTSUPP; break;
		case 9:
			jobs[3] = jobs[2]; arguments.jobs.count = 4;
			result = -EOPNOTSUPP; break;
		case 10: jobs[2].flags |= 256; result = -EOPNOTSUPP; break;
		}
		CHECK(submit() == result);
		CHECK(rendered == 0 && fragmented == 0 && prepared == 0 && completed == 0);
		CHECK(hwrt_targets == 0 && hwrt_gets == hwrt_puts && signals == 0 && poisoned == 0);
		finish();
	}
	for (i = 0; i < 5; i++) {
		initialize_triple();
		switch (i) {
		case 0: render_error_at = 1; break;
		case 1: after_pair_prepare = EIO; break;
		case 2: after_pair_backend = EIO; break;
		case 3: after_pair_target = EIO; break;
		case 4: fragment_error = EIO; break;
		}
		CHECK(submit() == -EIO && arguments.jobs.count == (i == 0 ? 0U : 2U));
		CHECK(rendered == 1 && fragmented == (i == 4 ? 1U : 0U));
		CHECK(completed == (i == 0 ? 0U : 1U) && signals == completed);
		CHECK(poisoned == (i == 0 || i >= 3 ? 1U : 0U));
		CHECK(hwrt_gets == 1 && hwrt_puts == 1 && syncobjs[2].fence == NULL);
		finish();
	}
	/* A backend change during the final wait retains all three job owners. */
	initialize_triple();
	add_op(2, 3, 0, 0);
	switch_backend_on_pause = true;
	pause_error = EINTR;
	CHECK(submit() == -EIO && retained != NULL && arguments.jobs.count == 2);
	CHECK(rendered == 1 && fragmented == 0 && signals == 1);
	CHECK(contexts[1].refs == 4 && hwrt_model[0].refs == 1 && hwrt_puts == 0);
	pvr_device_enter(&device);
	pvr_submit_release(&device, retained, true);
	retained = NULL;
	pvr_device_leave(&device);
	finish();
}

static void success_cases(void)
{
	initialize(2);
	add_op(0,1,DRM_PVR_SYNC_OP_FLAG_SIGNAL|1,7);
	add_op(1,1,1,7); add_op(1,2,DRM_PVR_SYNC_OP_FLAG_SIGNAL,0);
	CHECK(submit()==0 && arguments.jobs.count==2);
	CHECK(computed==2 && completed==2 && signals==2 && poisoned==0 && wait_calls==1);
	CHECK(syncobjs[1].fence->seqno==7 && syncobjs[2].fence==&stub); finish();
	initialize(1); syncobjs[1].fence=new_fence(1);
	add_op(0,1,0,0); add_op(0,2,DRM_PVR_SYNC_OP_FLAG_SIGNAL|1,0);
	CHECK(submit()==0 && signals==1 && syncobjs[2].fence==&stub); finish();
}
static void
set_transfer(unsigned index)
{
	contexts[2].firmware.type = DRM_PVR_CTX_TYPE_TRANSFER_FRAG;
	memset(streams[index], 0, sizeof(streams[index]));
	put32(streams[index], PVR_STREAM_TRANSFER_SIZE);
	put64(streams[index] + 8, UINT64_C(0x9002000000));
	jobs[index].type = DRM_PVR_JOB_TYPE_TRANSFER_FRAG;
	jobs[index].context_handle = 2;
	jobs[index].flags = DRM_PVR_SUBMIT_JOB_TRANSFER_CMD_SINGLE_CORE;
	jobs[index].cmd_stream_len = PVR_STREAM_TRANSFER_SIZE;
}

static void
transfer_cases(void)
{
	unsigned i, copies, allocs;
	int error;

	initialize(1); set_transfer(0);
	add_op(0, 1, DRM_PVR_SYNC_OP_FLAG_SIGNAL, 0);
	CHECK(submit() == 0 && arguments.jobs.count == 1);
	CHECK(transferred == 1 && computed == 0 && rendered == 0);
	CHECK(prepared == 1 && completed == 1 && signals == 1 && poisoned == 0);
	CHECK(hwrt_gets == 0 && hwrt_targets == 0 && syncobjs[1].fence == &stub);
	copies = copy_calls; allocs = allocate_calls; finish();

	for (i = 0; i < 12; i++) {
		initialize(1); set_transfer(0);
		switch (i) {
		case 0: jobs[0].cmd_stream_len--; break;
		case 1: jobs[0].cmd_stream_len++; break;
		case 2: jobs[0].flags |= 2; break;
		case 3: jobs[0].hwrt.set_handle = 1; break;
		case 4: jobs[0].hwrt.data_index = 1; break;
		case 5: streams[0][0]--; break;
		case 6: jobs[0].cmd_stream = UINT64_MAX - 4; break;
		case 7: jobs[0].cmd_stream = 0; break;
		case 8: contexts[2].firmware.type = DRM_PVR_CTX_TYPE_COMPUTE; break;
		case 9: contexts[2].firmware.type = DRM_PVR_CTX_TYPE_RENDER; break;
		case 10: jobs[0].type += 0x100; break;
		case 11: jobs[0].context_handle = 7; break;
		}
		CHECK(submit() < 0 && arguments.jobs.count == 0);
		CHECK(transferred == 0 && prepared == 0 && signals == 0 && poisoned == 0);
		CHECK(hwrt_gets == 0 && hwrt_targets == 0); finish();
	}
	/* An unknown high-bit compute type must not narrow to a known opcode. */
	initialize(1); jobs[0].type += 0x100;
	CHECK(submit() == -EOPNOTSUPP && computed == 0); finish();
	for (i = 1; i <= copies; i++) {
		initialize(1); set_transfer(0); add_op(0, 1, DRM_PVR_SYNC_OP_FLAG_SIGNAL, 0);
		copy_fault = i;
		CHECK(submit() == -EFAULT && transferred == 0 && signals == 0); finish();
	}
	for (i = 1; i <= allocs; i++) {
		initialize(1); set_transfer(0); add_op(0, 1, DRM_PVR_SYNC_OP_FLAG_SIGNAL, 0);
		allocation_fault = i;
		CHECK(submit() == -ENOMEM && transferred == 0 && signals == 0); finish();
	}
	initialize(1); set_transfer(0); prepare_error = EIO;
	CHECK(submit() == -EIO && transferred == 0 && poisoned == 0); finish();
	initialize(1); set_transfer(0); backend_error = EIO;
	CHECK(submit() == -EIO && transferred == 0 && prepared == 0); finish();
	for (i = 0; i < 2; i++) {
		initialize(1); set_transfer(0); add_op(0, 1, DRM_PVR_SYNC_OP_FLAG_SIGNAL, 0);
		error = i == 0 ? ETIMEDOUT : EIO; transfer_error = error;
		CHECK(submit() == -error && arguments.jobs.count == 0);
		CHECK(transferred == 1 && completed == 0 && poisoned == 1 && signals == 0);
		CHECK(contexts[2].vm.failed && syncobjs[1].fence == NULL); finish();
	}
	/* Compute can publish the external point consumed by a later transfer. */
	initialize(2); set_transfer(1);
	add_op(0, 1, DRM_PVR_SYNC_OP_FLAG_SIGNAL | 1, 9);
	add_op(1, 1, 1, 9); add_op(1, 2, DRM_PVR_SYNC_OP_FLAG_SIGNAL, 0);
	CHECK(submit() == 0 && arguments.jobs.count == 2);
	CHECK(computed == 1 && transferred == 1 && completed == 2 && signals == 2);
	CHECK(wait_calls == 1); finish();
	for (i = 0; i < 4; i++) {
		initialize(1); set_transfer(0); add_op(0, 1, 0, 0);
		add_op(0, 2, DRM_PVR_SYNC_OP_FLAG_SIGNAL, 0);
		syncobjs[1].fence = new_fence(i == 0 ? -EIO : 0);
		if (i == 2) wait_error = ERESTARTSYS;
		if (i == 3) complete_on_wait = true;
		error = i == 0 ? EIO : i == 1 ? ETIMEDOUT : i == 2 ? EINTR : 0;
		CHECK(submit() == -error);
		CHECK(transferred == (i == 3) && signals == (i == 3)); finish();
	}
	initialize(1); set_transfer(0); add_op(0, 1, 1, 9);
	switch_backend_on_pause = true; pause_error = EINTR;
	CHECK(submit() == -EIO && retained != NULL && transferred == 0);
	CHECK(contexts[2].refs == 2 && syncobjs[1].refs == 2);
	/* Only the model's separately proven reset can reclaim this owner. */
	pvr_device_enter(&device); pvr_submit_release(&device, retained, true);
	retained = NULL; pvr_device_leave(&device); finish();
}
static void preflight_cases(void)
{
	unsigned i;
	for(i=0;i<15;i++) {
		initialize(2); add_op(1,1,DRM_PVR_SYNC_OP_FLAG_SIGNAL|1,3);
		switch(i) {
		case 0: jobs[1].type=DRM_PVR_JOB_TYPE_FRAGMENT; break;
		case 1: jobs[1].hwrt.set_handle=1; break;
		case 2: jobs[1].cmd_stream_len=43; break;
		case 3: jobs[1].cmd_stream_len=45; break;
		case 4: jobs[1].flags=4; break;
		case 5: streams[1][0]=43; break;
		case 6: jobs[1].sync_ops.stride=15; break;
		case 7: jobs[1].sync_ops.stride=17; break;
		case 8: ops[1][0].flags=2; break;
		case 9: ops[1][0].flags=16; break;
		case 10: ops[1][0].flags=0; ops[1][0].value=1; break;
		case 11: jobs[1].context_handle=0; break;
		case 12: ops[1][0].handle=0; break;
		case 13: jobs[1].cmd_stream=UINT64_MAX-4; break;
		case 14: jobs[1].sync_ops.array=UINT64_MAX-4; break;
		}
		CHECK(submit()<0 && arguments.jobs.count==1 && computed==0 && signals==0 && poisoned==0); finish();
	}
	initialize(1); arguments.jobs.stride=49;
	CHECK(submit()==-EOPNOTSUPP && arguments.jobs.count==0); finish();
	initialize(1); arguments.jobs.count=0;
	CHECK(submit()==-EINVAL && arguments.jobs.count==0); finish();
	initialize(1); arguments.jobs.array=UINT64_MAX-1;
	CHECK(submit()==-EFAULT && computed==0); finish();
	initialize(1); arguments.jobs.count=UINT32_MAX;
	CHECK(submit()==-ENOMEM && computed==0); finish();
	for(i=1;i<=3;i++) {
		initialize(1); add_op(0,1,DRM_PVR_SYNC_OP_FLAG_SIGNAL|1,1); copy_fault=i;
		CHECK(submit()==-EFAULT && computed==0 && signals==0); finish();
	}
	for(i=1;i<=5;i++) {
		initialize(2); add_op(0,1,DRM_PVR_SYNC_OP_FLAG_SIGNAL|1,1);
		add_op(1,2,DRM_PVR_SYNC_OP_FLAG_SIGNAL|1,1); allocation_fault=i;
		CHECK(submit()==-ENOMEM && computed==0 && signals==0);
		CHECK(arguments.jobs.count==(unsigned)(i==3 || i==5)); finish();
	}
	initialize(1); contexts[1].firmware.type=DRM_PVR_CTX_TYPE_RENDER;
	CHECK(submit()==-EINVAL && computed==0); finish();
	/* Render pair through the HWRT reference and target model. */
	initialize_pair();
	CHECK(submit()==0 && rendered==1 && hwrt_gets==1 && hwrt_puts==1);
	CHECK(last_geom_hwrt==0xc0aa0000 && last_cleanup==0xc0aa0040);
	CHECK(last_frag_type==ROGUE_FWIF_CCB_CMD_TYPE_FRAG || last_frag_type==ROGUE_FWIF_CCB_CMD_TYPE_FRAG_PR);
	finish();
	/* Fragment without a preceding geometry is rejected. */
	initialize(1);
	contexts[1].firmware.type=DRM_PVR_CTX_TYPE_RENDER;
	jobs[0].type=DRM_PVR_JOB_TYPE_FRAGMENT;
	CHECK(submit()<0 && rendered==0 && hwrt_gets==0);
	finish();
	initialize(1); id_error=EOVERFLOW;
	CHECK(submit()==-EOVERFLOW && computed==0); finish();
}
static void error_cases(void)
{
	struct dma_fence_chain *chain;
	unsigned i;
	initialize(2); add_op(0,1,DRM_PVR_SYNC_OP_FLAG_SIGNAL,0); add_op(1,2,DRM_PVR_SYNC_OP_FLAG_SIGNAL,0);
	compute_error_at=2;
	CHECK(submit()==-EIO && arguments.jobs.count==1 && computed==2 && signals==1 && poisoned==1); finish();
	initialize(2); add_op(0,2,DRM_PVR_SYNC_OP_FLAG_SIGNAL,0);
	add_op(1,1,0,0); syncobjs[1].fence=new_fence(1); wait_error=ERESTARTSYS;
	CHECK(submit()==-EINTR && arguments.jobs.count==1 && computed==1 && signals==1 && poisoned==0); finish();
	initialize(1); prepare_error=EIO;
	CHECK(submit()==-EIO && computed==0 && poisoned==0); finish();
	initialize(1); backend_error=EIO;
	CHECK(submit()==-EIO && computed==0 && poisoned==0); finish();
	for(i=0;i<4;i++) {
		initialize(1); add_op(0,1,0,0); syncobjs[1].fence=new_fence(i==0 ? -EIO : i==1 ? 0 : 1);
		if(i==2) wait_error=EINTR;
		if(i==3) wait_error=ERESTARTSYS;
		CHECK(submit()==-(i==0 ? EIO : i==1 ? ETIMEDOUT : i==2 ? EINTR : EINTR));
		CHECK(computed==0 && signals==0 && poisoned==0); finish();
	}
	initialize(1); add_op(0,1,1,5);
	chain=calloc(1,sizeof(*chain)); CHECK(chain!=NULL); atomic_fetch_add(&allocations,1);
	atomic_init(&chain->base.refs,1); chain->base.chain=true; chain->base.seqno=5;
	chain->dfc_fence=new_fence(-EIO); syncobjs[1].fence=&chain->base; atomic_fetch_add(&fence_count,1);
	CHECK(submit()==-EIO && computed==0 && poisoned==0); finish();
	initialize(1); add_op(0,1,1,9); frozen_clock=true;
	CHECK(submit()==-ETIMEDOUT && polls==1001 && computed==0 && poisoned==0); finish();
	initialize(1); add_op(0,1,1,9); pause_error=EINTR;
	CHECK(submit()==-EINTR && computed==0 && poisoned==0); finish();
	initialize(1); add_op(0,1,1,9); pause_error=ERESTART;
	CHECK(submit()==-EINTR && computed==0 && poisoned==0); finish();
}
static void backend_lifetime_case(void)
{
	initialize(1); backend_absent=true;
	CHECK(submit()==-EOPNOTSUPP && computed==0); finish();
	initialize(1); add_op(0,1,1,9); switch_backend_on_pause=true; pause_error=EINTR;
	CHECK(submit()==-EIO && retained!=NULL && computed==0 && poisoned==0);
	CHECK(contexts[1].refs==2 && syncobjs[1].refs==2);
	/* A separately proven reset permits the model to reclaim retained state. */
	pvr_device_enter(&device); pvr_submit_release(&device,retained,true);
	retained=NULL; pvr_device_leave(&device); finish();
}
/* The pair may replace host waits only for its exact retained sync point. */
static void
paired_dependencies(void)
{
	const uint32_t signal = DRM_PVR_SYNC_OP_FLAG_SIGNAL;
	unsigned i;

	for (i = 0; i < 4; i++) {
		uint32_t type;
		uint64_t point;

		initialize_pair();
		type = i == 0 ? 0 : 1;
		point = i < 2 ? 0 : 7;
		add_op(0, 1, signal | type, point);
		add_op(1, 1, type, point);
		if (i == 3) {
			/* Repeated references to the same timeline point are exact. */
			add_op(0, 1, signal | type, point);
			add_op(1, 1, type, point);
		}
		add_op(1, 2, signal, 0);
		CHECK(submit() == 0 && arguments.jobs.count == 2);
		CHECK(rendered == 1 && completed == 1 && computed == 0);
		CHECK(signals == (i == 3 ? 3U : 2U) && wait_calls == 0 && polls == 0);
		CHECK(hwrt_gets == 1 && hwrt_puts == 1 && hwrt_targets == 1);
		finish();
	}
	for (i = 0; i < 3; i++) {
		initialize_pair();
		add_op(0, 1, signal | 1, i == 1 ? 0 : 7);
		/* A different object, type or point must remain a host wait. */
		add_op(1, i == 0 ? 2 : 1, i == 1 ? 0 : 1,
		    i == 0 ? 7 : i == 1 ? 0 : 8);
		CHECK(submit() == -ETIMEDOUT && rendered == 0 && signals == 0);
		CHECK(hwrt_targets == 0 && hwrt_puts == 1 && poisoned == 0);
		finish();
	}
	initialize_pair();
	add_op(0, 1, signal | 1, 6);
	add_op(0, 1, signal | 1, 8);
	add_op(1, 1, 1, 7);
	CHECK(submit() == -EDEADLK && rendered == 0 && polls == 0);
	CHECK(hwrt_targets == 0 && signals == 0);
	finish();

	for (i = 0; i < 4; i++) {
		initialize_pair();
		add_op(0, 1, signal, 0);
		add_op(1, 1, 0, 0);
		add_op(1, 2, 0, 0);
		syncobjs[2].fence = new_fence(i == 0 ? 1 : i == 1 ? -EIO : 0);
		if (i == 3)
			wait_error = ERESTARTSYS;
		CHECK(submit() == (i == 0 ? 0 : i == 1 ? -EIO :
		    i == 2 ? -ETIMEDOUT : -EINTR));
		CHECK(wait_calls == 1 && rendered == (i == 0 ? 1U : 0U));
		CHECK(hwrt_targets == rendered && signals == rendered && poisoned == 0);
		finish();
	}
	for (i = 0; i < 4; i++) {
		initialize_pair();
		add_op(0, 1, signal, 0);
		add_op(1, 1, 0, 0);
		switch (i) {
		case 0: jobs[1].hwrt.set_handle = 2; break;
		case 1: jobs[1].hwrt.data_index = 1; break;
		case 2:
			jobs[1].context_handle = 2;
			contexts[2].firmware.type = DRM_PVR_CTX_TYPE_RENDER;
			break;
		case 3: hwrt_model_vm = &contexts[2].vm; break;
		}
		CHECK(submit() == -EINVAL && rendered == 0 && signals == 0);
		CHECK(hwrt_targets == 0 && hwrt_puts == 1 && poisoned == 0);
		finish();
	}
	for (i = 0; i < 3; i++) {
		initialize_pair();
		add_op(0, 1, signal | 1, 7);
		add_op(1, 1, 1, 7);
		add_op(1, 2, signal | 1, 9);
		if (i == 0)
			render_error_at = 1;
		else if (i == 1)
			hwrt_target_error = EIO;
		else
			prepare_error = EIO;
		CHECK(submit() == -EIO && arguments.jobs.count == 0 && signals == 0);
		CHECK(rendered == (i == 0 ? 1U : 0U) && completed == 0);
		CHECK(poisoned == (i < 2 ? 1U : 0U) && hwrt_puts == 1);
		finish();
	}
	/* A lost backend must keep both contexts, syncs and the single HWRT hold. */
	initialize_pair();
	add_op(0, 1, signal, 0);
	add_op(1, 1, 0, 0);
	add_op(1, 2, 0, 0);
	switch_backend_on_pause = true;
	pause_error = EINTR;
	CHECK(submit() == -EIO && retained != NULL && rendered == 0);
	CHECK(contexts[1].refs == 3 && syncobjs[1].refs == 3 && hwrt_model[0].refs == 1);
	CHECK(hwrt_puts == 0 && signals == 0);
	pvr_device_enter(&device);
	pvr_submit_release(&device, retained, true);
	retained = NULL;
	pvr_device_leave(&device);
	finish();
}

static void
forward_dependencies(void)
{
	const uint32_t signal = DRM_PVR_SYNC_OP_FLAG_SIGNAL;
	unsigned i;

	for (i = 0; i < 4; i++) {
		initialize(2);
		add_op(0, 1, 0, 0);
		add_op(1, 1, signal, 0);
		if (i != 0)
			syncobjs[1].fence = new_fence(i == 1 ? 0 : i == 2 ? 1 : -EIO);
		CHECK(submit() == (i == 2 ? 0 : i == 3 ? -EIO : i == 1 ? -ETIMEDOUT : -EDEADLK));
		CHECK(computed == (i == 2 ? 2U : 0U) && polls == 0);
		CHECK(signals == (i == 2 ? 1U : 0U));
		finish();
	}
	/* An existing pending fence may have an independent external producer. */
	for (i = 0; i < 4; i++) {
		unsigned producer = i % 2;
		uint32_t type = i < 2 ? 0 : 1;
		uint64_t point = i < 2 ? 0 : 7;

		initialize(producer + 1);
		add_op(0, 1, type, point);
		add_op(producer, 1, signal | type, point);
		syncobjs[1].fence = new_fence(0);
		if (point != 0) {
			struct dma_fence_chain *chain = calloc(1, sizeof(*chain));

			CHECK(chain != NULL);
			atomic_fetch_add(&allocations, 1);
			atomic_init(&chain->base.refs, 1);
			chain->base.chain = true;
			chain->base.seqno = point;
			chain->dfc_fence = syncobjs[1].fence;
			syncobjs[1].fence = &chain->base;
			atomic_fetch_add(&fence_count, 1);
		}
		complete_on_wait = true;
		CHECK(submit() == 0 && computed == producer + 1 && wait_calls == 1);
		CHECK(signals == 1 && polls == 0 && poisoned == 0);
		finish();
	}
	initialize(1);
	add_op(0, 1, 1, 7);
	add_op(0, 1, signal | 1, 7);
	CHECK(submit() == -EDEADLK && computed == 0 && signals == 0 && polls == 0);
	finish();
	initialize_pair();
	add_op(0, 2, 0, 0);
	add_op(0, 1, signal, 0);
	add_op(1, 1, 0, 0);
	add_op(1, 2, signal, 0);
	CHECK(submit() == -EDEADLK && rendered == 0 && hwrt_targets == 0);
	finish();
	/* A completed external timeline point survives a later duplicate signal. */
	initialize(3);
	add_op(0, 1, signal | 1, 7);
	add_op(1, 1, 1, 7);
	add_op(2, 1, signal | 1, 7);
	CHECK(submit() == 0 && computed == 3 && wait_calls == 1 && signals == 2);
	finish();
	initialize(3);
	add_op(0, 1, signal | 1, 7);
	add_op(1, 1, 1, 9);
	add_op(2, 1, signal | 1, 9);
	CHECK(submit() == -EDEADLK && arguments.jobs.count == 1);
	CHECK(computed == 1 && signals == 1 && polls == 0);
	finish();
	/* Do not infer a producer from equal values on different objects. */
	initialize(2);
	add_op(0, 1, 1, 7);
	add_op(1, 2, signal | 1, 7);
	CHECK(submit() == -ETIMEDOUT && computed == 0 && signals == 0);
	finish();
}

static void
producer_search_bounds(void)
{
	struct pvr_submit_batch *batch;
	struct pvr_submit_sync wait = {0}, signal = {0};
	uint32_t budget;
	bool found;

	/* Different user handles can refer to one retained object. */
	wait.object = &syncobjs[1];
	wait.input.handle = 1;
	signal.object = wait.object;
	signal.input.handle = 2;
	signal.input.flags = DRM_PVR_SYNC_OP_FLAG_SIGNAL;
	batch = calloc(1, sizeof(*batch) + 3 * sizeof(batch->jobs[0]));
	CHECK(batch != NULL);
	batch->count = 3;
	batch->jobs[2].syncs = &signal;
	batch->jobs[2].sync_count = 1;
	budget = 3;
	CHECK(pvr_submit_producer(batch, 0, 3, &wait, true, &budget, &found) == E2BIG);
	CHECK(budget == 0 && !found);
	budget = 4;
	CHECK(pvr_submit_producer(batch, 0, 3, &wait, true, &budget, &found) == 0);
	CHECK(budget == 0 && found);
	/* The shared budget is not renewed for a second dependency. */
	CHECK(pvr_submit_producer(batch, 0, 3, &wait, true, &budget, &found) == E2BIG);
	CHECK(budget == 0 && !found);
	free(batch);
}

struct worker_args { struct drm_pvr_ioctl_submit_jobs_args args; int result; };
static void *worker(void *cookie)
{
	struct worker_args *w=cookie; waiting_thread=true; clock_ns=0;
	w->result=pvr_submit_jobs(&device,&w->args,&file); return NULL;
}
static void concurrency_case(void)
{
	pthread_t thread;
	struct worker_args worker_data;
	struct drm_pvr_ioctl_submit_jobs_args actor;
	initialize(2); concurrent=true;
	add_op(0,1,1,7); add_op(0,2,DRM_PVR_SYNC_OP_FLAG_SIGNAL,0);
	jobs[1].context_handle=2; add_op(1,1,DRM_PVR_SYNC_OP_FLAG_SIGNAL|1,7);
	worker_data=(struct worker_args){{{48,1,(uintptr_t)&jobs[0]}},0};
	actor=(struct drm_pvr_ioctl_submit_jobs_args){{48,1,(uintptr_t)&jobs[1]}};
	CHECK(pthread_create(&thread,NULL,worker,&worker_data)==0);
	CHECK(pthread_mutex_lock(&rendezvous)==0);
	while(!waiter_entered) CHECK(pthread_cond_wait(&changed,&rendezvous)==0);
	CHECK(pthread_mutex_unlock(&rendezvous)==0);
	/* The second submit must enter the real device gate while the first waits. */
	pvr_device_enter(&device);
	CHECK(contexts[1].refs==2); contexts[1].handle_live=false;
	put64(streams[0]+16,0); jobs[0].context_handle=0; /* Inputs were already copied. */
	pvr_exec_context_put(&device,&contexts[1]); CHECK(!contexts[1].destroyed);
	/* Remove the handle after publication. The waiter still owns its reference. */
	pvr_device_leave(&device);
	CHECK(pvr_submit_jobs(&device,&actor,&file)==0 && computed==1);
	CHECK(pthread_mutex_lock(&sync_lock)==0);
	syncobjs[1].handle_live=false;
	CHECK(pthread_mutex_unlock(&sync_lock)==0); drm_syncobj_put(&syncobjs[1]);
	CHECK(syncobjs[1].refs==1);
	CHECK(pthread_mutex_lock(&rendezvous)==0); waiter_released=true;
	CHECK(pthread_cond_broadcast(&changed)==0); CHECK(pthread_mutex_unlock(&rendezvous)==0);
	CHECK(pthread_join(thread,NULL)==0);
	CHECK(worker_data.result==0 && worker_data.args.jobs.count==1 && computed==2 && signals==2);
	CHECK(contexts[1].destroyed && contexts[1].refs==0 && syncobjs[1].refs==0);
	finish();
}
int main(void)
{
	triple_cases(); success_cases(); transfer_cases(); preflight_cases(); error_cases(); backend_lifetime_case(); paired_dependencies(); forward_dependencies(); producer_search_bounds(); concurrency_case();
	printf("pvr submit: %u checks passed\n",atomic_load(&checks)); return 0;
}
