/* Origin: EmberBSD native CSD dispatch contract, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
#define main tfu_contract_main
#include "bcm2712-v3d-job-contract.c"
#undef main

static struct drm_v3d_submit_csd
csd_setup(struct bcmv3d_device *c, struct drm_device *d, struct drm_file *f,
    uint32_t handles[2])
{
	struct drm_v3d_submit_tfu tfu = setup(c, d, f);
	handles[0] = tfu.bo_handles[1]; handles[1] = tfu.bo_handles[0];
	csd_model = true;
	return (struct drm_v3d_submit_csd){
	    .cfg={0x10000,0x10000,0x10000,0x110,1,tfu.iia|3,tfu.iia+256},
	    .bo_handles=(uintptr_t)handles,.bo_handle_count=2};
}

static void
csd_cleanup(struct bcmv3d_device *c, struct drm_file *f, bool poison)
{
	struct bcmv3d_job *j=c->job;
	CHECK(j->poisoned==poison && j->nbo==(poison?2:0));
	CHECK(core_mask_count==1 && controller_masked && drained);
	for(unsigned h=1;h<16;h++)if(f->handles[h])close_handle(f,h);
	if(poison) {
		CHECK(gems==4);
		for(unsigned i=0;i<j->nbo;i++) {
			CHECK(j->bos[i]->cma.base.refs==1 && j->bos[i]->error==EIO);
			j->bos[i]->busy=0; /* Model reboot, not runtime release. */
			drm_gem_object_put_unlocked(&j->bos[i]->cma.base);
		}
		j->nbo=0;
	} else CHECK(csd_owned==0 && !scratch_owned);
	j->published=false; bcmv3d_job_fini(c); empty(c);
}

int
main(void)
{
	struct bcmv3d_device c;
	struct drm_device d;
	struct drm_file f, other={0};
	uint32_t handles[2];
	struct drm_v3d_submit_csd a,b;
	unsigned calls;

	a=csd_setup(&c,&d,&f,handles);
	for(unsigned i=0;i<19;i++) {
		b=a;
		switch(i) {
		case 0:b.flags=1;break;
		case 1:b.pad=1;break;
		case 2:b.extensions=8;break;
		case 3:b.in_sync=1;break;
		case 4:b.out_sync=1;break;
		case 5:b.perfmon_id=1;break;
		case 6:b.bo_handle_count=0;break;
		case 7:b.bo_handle_count=65;break;
		case 8:case 9:case 10:case 11:b.coef[i-8]=1;break;
		case 12:case 13:case 14:b.cfg[i-12]=0;break;
		case 15:b.cfg[3]|=1U<<26;break;
		case 16:b.cfg[4]=UINT32_MAX;break;
		case 17:b.cfg[5]|=4;break;
		case 18:b.cfg[6]|=1;break;
		}
		calls=reg_calls; CHECK(bcmv3d_submit_csd(&d,&b,&f)==-EINVAL);
		CHECK(reg_calls==calls);
	}
	b=a;b.bo_handles=1;CHECK(bcmv3d_submit_csd(&d,&b,&f)==-EFAULT);
	CHECK(bcmv3d_submit_csd(&d,&a,&other)==-ENOENT);
	uint32_t saved=handles[1];
	handles[1]=15; CHECK(bcmv3d_submit_csd(&d,&a,&f)==-ENOENT);
	CHECK(f.handles[handles[0]]->refs==1);
	handles[1]=handles[0];CHECK(bcmv3d_submit_csd(&d,&a,&f)==-EINVAL);
	CHECK(f.handles[handles[0]]->refs==1);handles[1]=saved;
	b=a;b.cfg[5]=0;CHECK(bcmv3d_submit_csd(&d,&b,&f)==-EINVAL);
	b=a;b.cfg[6]=UINT32_MAX-3;CHECK(bcmv3d_submit_csd(&d,&b,&f)==-EINVAL);
	CHECK(c.job->nbo==0 && !c.job->poisoned && csd_starts==0);
	/* A failure during validation leaves the engine available. */
	reg_calls=0;CHECK(bcmv3d_submit_csd(&d,&a,&f)==0);calls=reg_calls;
	CHECK(csd_starts==1 && cache_phase==3 && revoked);
	/* Status is diagnostic; it must not use the older counter/idle layout. */
	core_regs[V3D_CSD_STATUS/4]=UINT32_MAX;
	/* Page-table replacement and alternating engine ownership. */
	for(unsigned i=0;i<260;i++)CHECK(bcmv3d_submit_csd(&d,&a,&f)==0);
	struct drm_v3d_submit_tfu tfu={.icfg=0x001d0001,
	    .iia=(uint32_t)bcmv3d_to_bo(f.handles[handles[0]])->gpu,
	    .ioa=(uint32_t)bcmv3d_to_bo(f.handles[handles[1]])->gpu,
	    .iis=64,.ios=0x00400040,.bo_handles={handles[1],handles[0]},
	    .v71={.ioc=0x00400000}};
	CHECK(bcmv3d_submit_tfu(&d,&tfu,&f)==0);
	CHECK(bcmv3d_submit_csd(&d,&a,&f)==0);
	csd_cleanup(&c,&f,false);
	a=csd_setup(&c,&d,&f,handles);
	core_regs[V3D_INT_STS/4]=V3D_CSD_DONE;
	CHECK(bcmv3d_submit_csd(&d,&a,&f)==-EBUSY && csd_starts==0);
	csd_cleanup(&c,&f,true);
	for(unsigned n=1;n<=calls;n++) {
		a=csd_setup(&c,&d,&f,handles);reg_calls=0;fail_reg=n;
		CHECK(bcmv3d_submit_csd(&d,&a,&f)!=0);
		unsigned starts_before=csd_starts;
		CHECK(bcmv3d_submit_csd(&d,&a,&f)==-EIO);
		CHECK(csd_starts==starts_before);
		struct drm_v3d_wait_bo w={.handle=handles[0]};
		CHECK(bcmv3d_wait_bo(&d,&w,&f)==-EIO);
		csd_cleanup(&c,&f,true);
	}
	const unsigned cases[]={1,2,3,4,5,6,7,8,9,20,21,22};
	for(unsigned i=0;i<__arraycount(cases);i++) {
		a=csd_setup(&c,&d,&f,handles);model_case=cases[i];
		CHECK(bcmv3d_submit_csd(&d,&a,&f)!=0);
		csd_cleanup(&c,&f,true);
	}
	a=csd_setup(&c,&d,&f,handles);model_case=11;
	CHECK(bcmv3d_submit_csd(&d,&a,&f)==0 && sleeps==5);
	csd_cleanup(&c,&f,false);
	a=csd_setup(&c,&d,&f,handles);close_during_submit=&f;
	CHECK(bcmv3d_submit_csd(&d,&a,&f)==0 && gems==2);
	csd_cleanup(&c,&f,false);
	printf("PASS: %u CSD checks; %u injected register failures\n",checks,calls);
	return 0;
}
