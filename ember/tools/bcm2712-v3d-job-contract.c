/* Origin: EmberBSD actual-source TFU failure/lifetime contract, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
#define BCMV3D_JOB_CONTRACT
#include "bcm2712-v3d-bo-contract.c"
#include <time.h>
#include <stdarg.h>
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
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
#define atomic_load_acquire(p) (*(p))
#define atomic_store_release(p,v) (*(p)=(v))
#define atomic_store_relaxed(p,v) (*(p)=(v))
static unsigned atomic_cas_uint(volatile unsigned *p, unsigned old, unsigned v)
{ unsigned was=*p; if(was==old)*p=v; return was; }
static void atomic_inc_uint(volatile unsigned *p) { ++*p; }
struct acpi_resources { int dummy; };
struct acpi_irq { int ar_irq, ar_type; };
static const int acpi_resource_parse_ops_quiet;
static struct acpi_irq irq={282,ACPI_LEVEL_SENSITIVE};
static void mutex_init(kmutex_t *m,int a,int b) { *m=0; }
static void mutex_destroy(kmutex_t *m) { CHECK(*m==0); }
static void aprint_error_dev(device_t d,const char *fmt,...) { }
static void aprint_normal_dev(device_t d,const char *fmt,...) { }
static int acpi_resource_parse(device_t d,ACPI_HANDLE h,const char *s,
    struct acpi_resources *r,const int *p) { return 0; }
static struct acpi_irq *acpi_res_irq(struct acpi_resources *r,int n) { return &irq; }
static void acpi_resource_cleanup(struct acpi_resources *r) { }
static int (*handler)(void *);
static void *handler_arg;
static bool controller_masked, drained;
static unsigned reg_calls, fail_reg, starts, sleeps, model_case, writes;
static unsigned flushes;
static bool src_owned, dst_owned, scratch_owned, ptes_synced;
static bool retired, revoke_synced, revoked;
static struct drm_file *close_during_submit;
static uint64_t time_ns;
static uint32_t regs[0x1240/4];
static void *acpi_intr_establish_irq(device_t d,struct acpi_irq *r,int ipl,
    bool m,int (*h)(void *),void *a,const char *n)
{ CHECK(!handler); handler=h; handler_arg=a; return &irq; }
static void acpi_intr_mask(void *h) { controller_masked=true; drained=false; }
static void xc_barrier(int n) { CHECK(controller_masked); drained=true; }
static void acpi_intr_unmask(void *h) { CHECK(drained); controller_masked=false; }
static void acpi_intr_disestablish(void *h)
{ CHECK(controller_masked && drained); handler=NULL; handler_arg=NULL; }
static void wakeup(void *p) { }
static void delay(unsigned n) { time_ns+=(uint64_t)n*1000; }
static unsigned mstohz(unsigned n) { return n; }
static void getnanouptime(struct timespec *t)
{ t->tv_sec=time_ns/1000000000; t->tv_nsec=time_ns%1000000000; }
#define timespecsub(a,b,r) do { \
 (r)->tv_sec=(a)->tv_sec-(b)->tv_sec; (r)->tv_nsec=(a)->tv_nsec-(b)->tv_nsec; \
 if((r)->tv_nsec<0) { --(r)->tv_sec; (r)->tv_nsec+=1000000000; } } while(0)
static void bus_dmamap_sync(bus_dma_tag_t t,bus_dmamap_t m,size_t o,size_t n,int ops);
static int bcmv3d_takeover_hub_peek(bus_size_t r,uint32_t *v);
static int bcmv3d_takeover_hub_poke(bus_size_t r,uint32_t v);
static int tsleep(void *p,int f,const char *n,int ticks);
#include "job-body.h"

static void
bus_dmamap_sync(bus_dma_tag_t t,bus_dmamap_t m,size_t o,size_t n,int ops)
{
	struct bcmv3d_job *j=handler_arg;
	CHECK(o==0 && n==m->dm_segs[0].ds_len && j!=NULL);
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
	if (++reg_calls == fail_reg) return EIO;
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
		if(model_case!=1 && model_case!=11)complete();
	}
	return 0;
}
static int tsleep(void *p,int f,const char *n,int ticks)
{
	time_ns+=1000000; sleeps++;
	if(model_case==11 && sleeps==5)complete();
	return EWOULDBLOCK;
}

static struct drm_v3d_submit_tfu
setup(struct bcmv3d_device *c,struct drm_device *d,struct drm_file *f)
{
	struct drm_v3d_create_bo a={.size=V3D_COPY_SIZE},b=a;
	*c=(struct bcmv3d_device){.drm=d};
	*d=(struct drm_device){.dev_private=c}; *f=(struct drm_file){0};
	memset(regs,0,sizeof(regs)); regs[V3D_MMU_DEBUG/4]=0x550;
	reg_calls=fail_reg=starts=sleeps=writes=flushes=model_case=0;
	src_owned=dst_owned=scratch_owned=ptes_synced=false;
	retired=revoke_synced=revoked=false;
	close_during_submit=NULL;
	time_ns=0;
	CHECK(bcmv3d_job_init(c)==0);
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
