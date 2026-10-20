/* Origin: EmberBSD actual-source TFU failure/lifetime contract, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
#define BCMV3D_JOB_CONTRACT
#define BCMV3D_RESV_CONTRACT
#include "bcm2712-v3d-bo-contract.c"
#include "../../sys/external/bsd/drm2/bcmv3d/bcmv3d_cl_reject.h"
#include <time.h>
#include <limits.h>
#include <stdarg.h>
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define BUS_DMASYNC_PREWRITE 1
#define BUS_DMASYNC_PREREAD 2
#define BUS_DMASYNC_POSTWRITE 4
#define BUS_DMASYNC_POSTREAD 8
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define IPL_VM 1
#define PWAIT 0
#define PCATCH 1
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#ifndef ETIME
#define ETIME 62
#endif
#define ACPI_FAILURE(e) ((e) != 0)
#define ACPI_LEVEL_SENSITIVE 1
static void (*on_event_read)(const volatile unsigned *);
static unsigned contract_atomic_load(const volatile unsigned *p)
{
 unsigned value = *p;
 if (on_event_read != NULL) on_event_read(p);
 return value;
}
#define atomic_load_acquire(p) contract_atomic_load(p)
#define atomic_load_relaxed(p) (*(p))
#define atomic_store_release(p,v) (*(p)=(v))
#define atomic_store_relaxed(p,v) (*(p)=(v))
static unsigned atomic_cas_uint(volatile unsigned *p, unsigned old, unsigned v)
{ unsigned was=*p; if(was==old)*p=v; return was; }
static void atomic_inc_uint(volatile unsigned *p) { ++*p; }
struct acpi_resources { int dummy; };
struct acpi_irq { int ar_irq, ar_type; };
static const int acpi_resource_parse_ops_quiet;
static struct acpi_irq irq={282,ACPI_LEVEL_SENSITIVE};
static struct acpi_irq core_irq={281,ACPI_LEVEL_SENSITIVE};
static kmutex_t *irq_mutex;
static void mutex_init(kmutex_t *m,int a,int b)
{ *m=0; if (b == IPL_VM) irq_mutex=m; }
static void mutex_destroy(kmutex_t *m) { CHECK(*m==0); }
static char diagnostic_log[1024];
static void aprint_error_dev(device_t d,const char *fmt,...)
{
 va_list ap;
 va_start(ap,fmt);
 vsnprintf(diagnostic_log,sizeof(diagnostic_log),fmt,ap);
 va_end(ap);
}
static void aprint_normal_dev(device_t d,const char *fmt,...) { }
static int acpi_resource_parse(device_t d,ACPI_HANDLE h,const char *s,
    struct acpi_resources *r,const int *p) { return 0; }
static struct acpi_irq *acpi_res_irq(struct acpi_resources *r,int n) { return n?&core_irq:&irq; }
static void acpi_resource_cleanup(struct acpi_resources *r) { }
static int (*handler)(void *);
static void *handler_arg;
static int (*core_handler)(void *);
static void *core_arg;
static unsigned core_mask_count;
static bool controller_masked, drained;
static unsigned mask_count;
static unsigned reg_calls, fail_reg, starts, sleeps, model_case, writes;
static unsigned flushes;
static bool src_owned, dst_owned, scratch_owned, ptes_synced;
static bool retired, revoke_synced, revoked;
static struct drm_file *close_during_submit;
static uint64_t time_ns;
static uint32_t regs[0x1240/4];
static void *acpi_intr_establish_irq(device_t d,struct acpi_irq *r,int ipl,
    bool m,int (*h)(void *),void *a,const char *n)
{ if(r==&core_irq) { CHECK(!core_handler); core_handler=h; core_arg=a; return &core_irq; } CHECK(!handler); handler=h; handler_arg=a; return &irq; }
static void acpi_intr_mask(void *h)
{ if(h==&core_irq) { core_mask_count++; return; } mask_count++; controller_masked=true; drained=false; }
static void xc_barrier(int n) { CHECK(controller_masked); CHECK(irq_mutex == NULL || *irq_mutex == 0); drained=true; }
static void acpi_intr_unmask(void *h)
{ if(h==&core_irq) { CHECK(core_mask_count>0); --core_mask_count; return; } CHECK(drained && mask_count>0); controller_masked=--mask_count!=0; }
static void acpi_intr_disestablish(void *h)
{ if(h==&core_irq) { CHECK(core_mask_count>0); core_handler=NULL; core_arg=NULL; core_mask_count=0; return; } CHECK(controller_masked && drained); handler=NULL; handler_arg=NULL; mask_count=0; }
static void (*on_wakeup)(const void *);
static void (*on_engine_start)(void);
static int (*on_sleep)(const void *, int, const char *, int, kmutex_t *);
static void wakeup(void *p) { if (on_wakeup != NULL) on_wakeup(p); }
static void delay(unsigned n) { time_ns+=(uint64_t)n*1000; }
static unsigned clock_hz = 1000;
static unsigned mstohz(unsigned n) { return (n * clock_hz + 999) / 1000; }
static int getticks(void) { return (int)(unsigned)(time_ns / (1000000000 / clock_hz)); }
static void getnanouptime(struct timespec *t)
{ t->tv_sec=time_ns/1000000000; t->tv_nsec=time_ns%1000000000; }
#ifndef timespecsub
#define timespecsub(a,b,r) do { \
 (r)->tv_sec=(a)->tv_sec-(b)->tv_sec; (r)->tv_nsec=(a)->tv_nsec-(b)->tv_nsec; \
 if((r)->tv_nsec<0) { --(r)->tv_sec; (r)->tv_nsec+=1000000000; } } while(0)
#endif
static void bus_dmamap_sync(bus_dma_tag_t t,bus_dmamap_t m,size_t o,size_t n,int ops);
static int bcmv3d_takeover_hub_peek(bus_size_t r,uint32_t *v);
static int bcmv3d_takeover_hub_poke(bus_size_t r,uint32_t v);
static int tsleep(void *p,int f,const char *n,int ticks);
static int mtsleep(void *p,int f,const char *n,int ticks,kmutex_t *m);
static int bcmv3d_takeover_core_peek(bus_size_t r,uint32_t *v);
static int bcmv3d_takeover_core_poke(bus_size_t r,uint32_t v);
static int copyin(const void *src,void *dst,size_t n)
{ if((uintptr_t)src<4096)return EFAULT; memcpy(dst,src,n); return 0; }

struct dma_fence;
static long contract_resv_wait(struct dma_fence *, long);
#include "bcm2712-v3d-sync-model.h"
#include "bcmv3d-kheapsort-model.h"
#include "bcm2712-v3d-resv-model.h"
#include "job-body.h"

static long
contract_resv_wait(struct dma_fence *f, long timeout)
{
	CHECK(resv_engine_lock == NULL || *resv_engine_lock == 0);
	resv_waits++;
	return f->status ? timeout : resv_wait_fault ? resv_wait_result : input_wait_result;
}
static void
contract_resv_engine(struct bcmv3d_job *job)
{
	CHECK(resv_locks == 0 && resv_contexts == 0 && resv_arrays == 0);
	struct dma_fence *f = NULL;
	unsigned count = job->nbo ? job->nbo : 2;
	for (unsigned i = 0; i < count; i++) {
		struct bcmv3d_bo *bo = job->nbo ? bcmv3d_to_bo(job->bos[i]) : i ? job->dst : job->src;
		CHECK(bo != NULL && bo->cma.base.resv->owner == NULL);
		struct dma_fence *next = bo->cma.base.resv->exclusive;
		CHECK(next && next->status == 0 && next->context == job->fence_context &&
		    next->seqno == job->fence_seqno);
		if (f) CHECK(f == next);
		f = next;
	}
}

static uint32_t core_regs[0xa00/4];
static unsigned csd_starts, csd_writes, cache_phase, csd_owned;
static bool csd_model;
static bool cl_model, cl_render;
static unsigned cl_bins, cl_renders;
static void csd_complete(void);
static void cl_complete(void);
static int bcmv3d_takeover_core_peek(bus_size_t r,uint32_t *v)
{
 if(++reg_calls==fail_reg){ *v=0xbad0bad0;return EIO; }
 CHECK(r/4<__arraycount(core_regs)); *v=core_regs[r/4];
 if(r==0x30 && model_case==20 && cache_phase==1)*v|=1;
 if(r==0x30 && model_case==21 && cache_phase==2)*v|=0x100;
 if(r==0x30 && model_case==22 && cache_phase==3)*v|=1;
 return 0;
}
static int bcmv3d_takeover_core_poke(bus_size_t r,uint32_t v)
{
 if(++reg_calls==fail_reg)return EIO;
 CHECK(r/4<__arraycount(core_regs));
 if(r==V3D_INT_CLR)core_regs[V3D_INT_STS/4]&=~v;
 else core_regs[r/4]=v;
 if(r==0x30) {
  if(v==1) { CHECK(ptes_synced && csd_owned>0); cache_phase=1; }
  else if(v==0x100) { CHECK(retired && cache_phase==1); cache_phase=2; }
  else if(v==5) { CHECK(retired && cache_phase==2); cache_phase=3; }
  else CHECK(false);
  core_regs[r/4]=0;
 }
 if(cl_model && (r==0x168 || r==0x16c)) {
  struct bcmv3d_job *j=core_arg;
  CHECK(core_mask_count==0 && !controller_masked && cache_phase==1);
  CHECK(csd_owned==j->nbo && scratch_owned && ptes_synced);
  for(unsigned i=0;i<j->nbo;i++) {
   struct bcmv3d_bo *bo=bcmv3d_to_bo(j->bos[i]); uint32_t *pt=j->pt->cma.vaddr;
   CHECK(bo->busy && bo->cma.base.refs>=1);
   for(unsigned p=0;p<bo->cma.dmasize/PAGE_SIZE;p++)
    CHECK((pt[(bo->gpu>>12)+p]&(V3D_PTE_VALID|V3D_PTE_WRITE))==(V3D_PTE_VALID|V3D_PTE_WRITE));
  }
  cl_render=r==0x16c;
  if(cl_render)cl_renders++;else {
   cl_bins++;
   CHECK(core_regs[0x30c/4]==0 && core_regs[0x170/4]!=0 &&
       core_regs[0x174/4]!=0 && (core_regs[0x15c/4]&2)!=0);
  }
  if(close_during_submit) {
   for(unsigned h=1;h<16;h++)if(close_during_submit->handles[h])close_handle(close_during_submit,h);
   for(unsigned i=0;i<j->nbo;i++)CHECK(j->bos[i]->refs==1);
   close_during_submit=NULL;
  }
  contract_resv_engine(j); if(on_engine_start != NULL)on_engine_start();
  if(model_case!=1 && model_case!=11 && model_case<=40)cl_complete();
 }
 if(r>V3D_CSD_CFG0 && r<=V3D_CSD_CFG0+28)csd_writes++;
 if(r==V3D_CSD_CFG0) {
  struct bcmv3d_job *j=core_arg;
  CHECK(csd_writes==7 && core_regs[(V3D_CSD_CFG0+28)/4]==0);
  CHECK(core_mask_count==0 && !controller_masked && cache_phase==1);
  CHECK(csd_owned==j->nbo && scratch_owned && ptes_synced);
  for(unsigned i=0;i<j->nbo;i++) {
   struct bcmv3d_bo *bo=bcmv3d_to_bo(j->bos[i]); uint32_t *pt=j->pt->cma.vaddr;
   CHECK(bo->busy && bo->cma.base.refs>=2);
   for(unsigned p=0;p<bo->cma.dmasize/PAGE_SIZE;p++)
    CHECK((pt[(bo->gpu>>12)+p]&(V3D_PTE_VALID|V3D_PTE_WRITE))==(V3D_PTE_VALID|V3D_PTE_WRITE));
  }
  if(close_during_submit) {
   for(unsigned h=1;h<16;h++)if(close_during_submit->handles[h])close_handle(close_during_submit,h);
   for(unsigned i=0;i<j->nbo;i++)CHECK(j->bos[i]->refs==1);
   close_during_submit=NULL;
  }
  csd_writes=0; csd_starts++; core_regs[V3D_CSD_STATUS/4]|=2;
  contract_resv_engine(j); if(on_engine_start != NULL)on_engine_start();
  if(model_case!=1 && model_case!=11 && model_case<=40)csd_complete();
 }
 return 0;
}
static void cl_complete(void)
{
 struct bcmv3d_job *j=core_arg;
 uint32_t expected=cl_render?1:2;
 CHECK(core_mask_count==0 && core_handler);
 if(cl_render)retired=true;
 core_regs[V3D_INT_STS/4]=model_case==31?expected^3:expected;
 if(model_case==32)regs[V3D_MMU/4]|=1U<<12;
 if(model_case==33 && cl_render)((uint32_t *)j->scratch->cma.vaddr)[0]^=1;
 core_handler(core_arg);
 if(model_case==34) { core_regs[V3D_INT_STS/4]=expected;core_handler(core_arg); }
 if(model_case==35)core_regs[V3D_INT_STS/4]=expected;
 if(model_case==36)regs[V3D_INT_STS/4]=1;
}
static void csd_complete(void)
{
 CHECK(core_mask_count==0 && core_handler);
 retired=true;
 /* First CM5 V3D 7.1 CSDDONE snapshot, not the V3D 4.x counter layout. */
 core_regs[V3D_CSD_STATUS/4]=0x10002080;
 core_regs[V3D_INT_STS/4]=model_case==4?1:V3D_CSD_DONE;
 if(model_case==5)regs[V3D_MMU/4]|=1U<<12;
 core_handler(core_arg);
 if(model_case==2)core_regs[V3D_INT_STS/4]=1;
 if(model_case==3)regs[V3D_INT_STS/4]=1;
 if(model_case==6) { core_regs[V3D_INT_STS/4]=V3D_CSD_DONE; core_handler(core_arg); }
}
static void
bus_dmamap_sync
(bus_dma_tag_t t,bus_dmamap_t m,size_t o,size_t n,int ops)
{
	struct bcmv3d_job *j=handler_arg;
	CHECK(o==0 && n==m->dm_mapsize && j!=NULL);
 if(j->nbo) {
  if(m==j->pt->cma.dmamap) {
   uint32_t *pt=j->pt->cma.vaddr; CHECK(ops==1 && csd_owned==j->nbo);
   if(pt[bcmv3d_to_bo(j->bos[0])->gpu>>12]) { CHECK(!retired && scratch_owned); ptes_synced=true; }
   else { CHECK(retired && cache_phase==3 && !scratch_owned); revoke_synced=true; }
  } else if(m==j->scratch->cma.dmamap) {
   if(ops==3) { CHECK(!scratch_owned && csd_owned==j->nbo); scratch_owned=true; }
   else { CHECK(ops==12 && scratch_owned && cache_phase==3 && retired); scratch_owned=false; }
  } else {
   bool found=false; for(unsigned i=0;i<j->nbo;i++)if(m==bcmv3d_to_bo(j->bos[i])->cma.dmamap)found=true;
   CHECK(found);
   if(ops==3) { if(csd_owned==0)retired=revoke_synced=revoked=ptes_synced=false; csd_owned++; }
   else { CHECK(ops==12 && csd_owned>0 && revoked && cache_phase==3); csd_owned--; }
  }
  return;
 }

	if(m==j->src->cma.dmamap && ops==BUS_DMASYNC_PREWRITE) {
		CHECK(!src_owned && !dst_owned && !scratch_owned);
		src_owned=true; retired=revoke_synced=revoked=ptes_synced=false;
	} else if(m==j->dst->cma.dmamap && ops==BUS_DMASYNC_PREREAD) {
		CHECK(src_owned && !dst_owned); dst_owned=true;
	} else if(m==j->scratch->cma.dmamap && ops==3) {
		CHECK(src_owned && dst_owned && !scratch_owned); scratch_owned=true;
	} else if(m==j->scratch->cma.dmamap && ops==12) {
		CHECK(retired && src_owned && dst_owned && scratch_owned);
		scratch_owned=false;
	} else if(m==j->pt->cma.dmamap && ops==BUS_DMASYNC_PREWRITE) {
		uint32_t *pt=j->pt->cma.vaddr;
		CHECK(src_owned && dst_owned);
		if(pt[j->src->gpu>>12] || pt[j->dst->gpu>>12]) {
			CHECK(!retired && scratch_owned); ptes_synced=true;
		} else {
			CHECK(retired && !scratch_owned); revoke_synced=true;
		}
	} else if(m==j->src->cma.dmamap && ops==BUS_DMASYNC_POSTWRITE) {
		CHECK(retired && revoke_synced && revoked && src_owned); src_owned=false;
	} else if(m==j->dst->cma.dmamap && ops==BUS_DMASYNC_POSTREAD) {
		CHECK(retired && revoke_synced && revoked && dst_owned); dst_owned=false;
	} else CHECK(false);
}

static int
bcmv3d_takeover_hub_peek(bus_size_t r,uint32_t *v)
{
	if (++reg_calls == fail_reg) { *v=0xbad0bad0; return EIO; }
	CHECK(r/4 < __arraycount(regs));
	*v=regs[r/4];
	if (r==V3D_MMUC && model_case==7) *v|=4;
	if (r==V3D_MMU && model_case==8) *v|=128;
	if (r==V3D_MMU && revoke_synced && *v==V3D_MMU_VALUE)
		revoked=true;
	return 0;
}

static void
complete(void)
{
	CHECK(!controller_masked && handler!=NULL);
	retired=true;
	regs[V3D_TFU_CS/4]=(regs[V3D_TFU_CS/4]+0x10000)&0xff0000;
	if(model_case==2)regs[V3D_TFU_CS/4]|=1;
	if(model_case==3)regs[V3D_TFU_CS/4]-=0x10000;
	regs[V3D_INT_STS/4]=model_case==4?0x30:V3D_TFUC;
	if(model_case==5)regs[V3D_MMU/4]|=1U<<12;
	handler(handler_arg);
	if(model_case==6) {
		regs[V3D_INT_STS/4]=V3D_TFUC;
		handler(handler_arg);
	}
}

static int
bcmv3d_takeover_hub_poke(bus_size_t r,uint32_t v)
{
	if (++reg_calls == fail_reg) return EIO;
	CHECK(r/4 < __arraycount(regs));
	if (r==V3D_MMU) v&=~4U;
	if (r==V3D_MMUC && v==3) {
		flushes++;
		if(model_case==9 && flushes==2)return EIO;
		v=1;
	}
	if(r==V3D_INT_CLR)regs[V3D_INT_STS/4]&=~v;
	else regs[r/4]=v;
	if(r>=0x70c && r<=0x734)writes++;
	if(r==V3D_ICFG) {
		struct bcmv3d_job *j=handler_arg;
		uint32_t *pt=j->pt->cma.vaddr;
		CHECK(writes==11); writes=0;
		CHECK(src_owned && dst_owned && scratch_owned && ptes_synced);
		CHECK(j->src->cma.base.refs>=2 && j->dst->cma.base.refs>=2);
		CHECK(j->src->busy && j->dst->busy);
		CHECK((pt[j->src->gpu>>12]&V3D_PTE_VALID)!=0);
		CHECK((pt[j->src->gpu>>12]&V3D_PTE_WRITE)==0);
		CHECK((pt[j->dst->gpu>>12]&V3D_PTE_WRITE)!=0);
		CHECK(pt[(j->dst->gpu>>12)+4]==0);
		if(close_during_submit!=NULL) {
			for(unsigned h=1;h<16;h++)
				if(close_during_submit->handles[h])close_handle(close_during_submit,h);
			CHECK(j->src->cma.base.refs==1 && j->dst->cma.base.refs==1);
			CHECK(gems==4); close_during_submit=NULL;
		}
		starts++; regs[V3D_TFU_CS/4]|=1;
		if(model_case==10)((uint32_t *)j->scratch->cma.vaddr)[0]^=1;
		contract_resv_engine(j); if(on_engine_start != NULL)on_engine_start();
  if(model_case!=1 && model_case!=11 && model_case<=40)complete();
	}
	return 0;
}
static int tsleep(void *p,int f,const char *n,int ticks)
{
	if (on_sleep != NULL) return on_sleep(p, f, n, ticks, NULL);
	time_ns+=1000000; sleeps++;
	if(model_case==11 && sleeps%5==0) { if(cl_model)cl_complete(); else if(csd_model)csd_complete(); else complete(); }
	return EWOULDBLOCK;
}

static int
mtsleep(void *p,int f,const char *n,int ticks,kmutex_t *m)
{
 if (on_sleep != NULL) return on_sleep(p, f, n, ticks, m);
 CHECK(m == irq_mutex && *m == 1);
 mutex_exit(m);
 int error = tsleep(p, f, n, ticks);
 mutex_enter(m);
 return error;
}

static struct drm_v3d_submit_tfu
setup(struct bcmv3d_device *c,struct drm_device *d,struct drm_file *f)
{
	struct drm_v3d_create_bo a={.size=V3D_COPY_SIZE},b=a;
	*c=(struct bcmv3d_device){.drm=d,.bo_total=BCMV3D_BO_TOTAL};
	*d=(struct drm_device){.dev_private=c}; *f=(struct drm_file){0};
	memset(regs,0,sizeof(regs)); memset(core_regs,0,sizeof(core_regs)); regs[V3D_MMU_DEBUG/4]=0x550; csd_model=false; csd_starts=csd_writes=cache_phase=csd_owned=0;
	cl_model=cl_render=false;cl_bins=cl_renders=0;
	reg_calls=fail_reg=starts=sleeps=writes=flushes=model_case=0;
	src_owned=dst_owned=scratch_owned=ptes_synced=false;
	retired=revoke_synced=revoked=false;
	close_during_submit=NULL;
	time_ns=0;
	resv_engine_lock = NULL;
	CHECK(resv_locks == 0 && resv_contexts == 0 && resv_arrays == 0 && resv_objects == 0);
	resv_lock_error = 0; resv_snapshot_fail = resv_wait_fault = false;
	resv_lock_calls = resv_snapshot_calls = resv_fault_at = 0;
	resv_prepares = resv_publications = resv_waits = 0;
	CHECK(bcmv3d_job_init(c)==0);
	resv_engine_lock = &c->job->lock;
	CHECK(bcmv3d_create_bo(d,&a,f)==0);
	CHECK(bcmv3d_create_bo(d,&b,f)==0);
	return (struct drm_v3d_submit_tfu){.icfg=0x001d0001,.iia=a.offset,
	    .iis=64,.ioa=b.offset,.ios=0x00400040,.bo_handles={b.handle,a.handle},
	    .v71={.ioc=0x00400000}};
}
static void
cleanup(struct bcmv3d_device *c,struct drm_file *f,bool poisoned)
{
	struct bcmv3d_job *j=c->job;
	unsigned h;
	if(poisoned) {
		CHECK(j->poisoned && j->src && j->dst);
		CHECK(j->src->cma.base.refs==2 && j->dst->cma.base.refs==2);
		CHECK(controller_masked && drained);
	}
	else CHECK(!src_owned && !dst_owned && !scratch_owned);
	for(h=1;h<16;h++)if(f->handles[h])close_handle(f,h);
	if(poisoned) {
		CHECK(gems==4); /* PT, scratch and both failed-job objects survive close. */
		j->src->busy=j->dst->busy=0;
		drm_gem_object_put_unlocked(&j->src->cma.base);
		drm_gem_object_put_unlocked(&j->dst->cma.base);
		j->src=j->dst=NULL;
	}
	/* Model a reboot only: production never frees a published engine. */
	j->published=false; bcmv3d_job_fini(c); empty(c);
	CHECK(fence_count == 0 && resv_objects == 0 && resv_locks == 0 && resv_contexts == 0 && resv_arrays == 0);
	resv_engine_lock = NULL;
}
int
main(void)
{
	struct bcmv3d_device c;
	struct drm_device d;
	struct drm_file f,other={0};
	struct drm_v3d_submit_tfu a,b;
	unsigned count;
	a=setup(&c,&d,&f);
	CHECK(bcmv3d_submit_tfu(&d,&a,&other)==-ENOENT && starts==0);
	struct drm_v3d_wait_bo w={.handle=a.bo_handles[0]};
	CHECK(bcmv3d_wait_bo(&d,&w,&other)==-ENOENT);
	w.pad=1; CHECK(bcmv3d_wait_bo(&d,&w,&f)==-EINVAL); w.pad=0;
	CHECK(bcmv3d_wait_bo(&d,&w,&f)==0);
	struct bcmv3d_bo *waiting=bcmv3d_to_bo(f.handles[w.handle]);
	waiting->busy=1;
	CHECK(bcmv3d_wait_bo(&d,&w,&f)==-ETIME && sleeps==0);
	w.timeout_ns=3000000;
	CHECK(bcmv3d_wait_bo(&d,&w,&f)==-ETIME && sleeps==3);
	waiting->busy=0;
	b=a; b.bo_handles[0]=b.bo_handles[1]; b.ioa=b.iia;
	CHECK(bcmv3d_submit_tfu(&d,&b,&f)==-EINVAL && starts==0);
	struct drm_v3d_create_bo small={.size=PAGE_SIZE};
	CHECK(bcmv3d_create_bo(&d,&small,&f)==0);
	b=a; b.bo_handles[0]=small.handle; b.ioa=small.offset;
	CHECK(bcmv3d_submit_tfu(&d,&b,&f)==-EINVAL && starts==0);
	close_handle(&f,small.handle);
	/* Every unsupported payload field and both addresses must be checked. */
	for(unsigned word=0;word<sizeof(a)/4;word++) {
		b=a; ((uint32_t *)(void *)&b)[word]^=1;
		CHECK(bcmv3d_submit_tfu(&d,&b,&f)<0 && starts==0 && !c.job->poisoned);
	}
	for(unsigned n=0;n<260;n++) {
		CHECK(bcmv3d_submit_tfu(&d,&a,&f)==0);
		CHECK(!c.job->poisoned && c.job->src==NULL && c.job->dst==NULL);
		CHECK(((uint32_t *)c.job->pt->cma.vaddr)[a.iia>>12]==0);
		CHECK(((uint32_t *)c.job->pt->cma.vaddr)[a.ioa>>12]==0);
	}
	cleanup(&c,&f,false);
	/* Sorting reservations must not exchange TFU source and destination roles. */
	a=setup(&c,&d,&f);
	struct drm_gem_object *source=f.handles[a.bo_handles[1]], *destination=f.handles[a.bo_handles[0]];
	if ((uintptr_t)source->resv < (uintptr_t)destination->resv) {
		struct dma_resv *temporary=source->resv;
		source->resv=destination->resv; destination->resv=temporary;
	}
	struct drm_gem_object *ordered[2]={source,destination};
	struct bcmv3d_resv preflight={0};
	CHECK(bcmv3d_resv_prepare(&preflight,ordered,2,1000)==0);
	CHECK(ordered[0]==destination && ordered[1]==source);
	bcmv3d_resv_unlock(&preflight);
	CHECK(bcmv3d_submit_tfu(&d,&a,&f)==0);
	cleanup(&c,&f,false);
	a=setup(&c,&d,&f); close_during_submit=&f;

	CHECK(bcmv3d_submit_tfu(&d,&a,&f)==0 && gems==2);
	cleanup(&c,&f,false);
	a=setup(&c,&d,&f); reg_calls=0;
	CHECK(bcmv3d_submit_tfu(&d,&a,&f)==0); count=reg_calls;
	cleanup(&c,&f,false);
	for(unsigned n=1;n<=count;n++) {
		a=setup(&c,&d,&f); reg_calls=0; fail_reg=n;
		CHECK(bcmv3d_submit_tfu(&d,&a,&f)<0 && c.job->poisoned);
		unsigned before=reg_calls;
		CHECK(bcmv3d_submit_tfu(&d,&a,&f)==-EIO && reg_calls==before);
		struct drm_v3d_wait_bo w={.handle=a.bo_handles[0]};
		CHECK(bcmv3d_wait_bo(&d,&w,&f)==-EIO);
		cleanup(&c,&f,true);
	}
	for(unsigned n=1;n<=11;n++) {
		a=setup(&c,&d,&f); model_case=n;
		int error=bcmv3d_submit_tfu(&d,&a,&f);
		CHECK(n==11?error==0:error<0);
		cleanup(&c,&f,n!=11);
	}
	printf("PASS: %u TFU ownership/failure checks; %u injected register failures\n",checks,count);
	return 0;
}
