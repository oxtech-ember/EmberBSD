/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted source contract. */
/* Real HWRT source and FWIF; only allocation, DMA and firmware are modeled. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif.h>
#include "pvr_backend.h"
#include "pvr_hwrt.h"
#define BUS_DMASYNC_PREREAD 1
#define BUS_DMASYNC_PREWRITE 4
#define KM_SLEEP 0
#define roundup2(x,n) (((x)+(n)-1)&~((n)-1))
#ifndef __arraycount
#define __arraycount(a) (sizeof(a)/sizeof((a)[0]))
#endif
static unsigned checks, heap_objects;
#define CHECK(e) do { checks++; if (!(e)) { \
	fprintf(stderr,"FAIL %d: %s\n",__LINE__,#e); abort(); \
} } while (0)
static void *kmem_zalloc(size_t size,int flags)
{
	void *p;
	CHECK(flags==KM_SLEEP); p=calloc(1,size); CHECK(p!=NULL);
	heap_objects++; return p;
}
static void kmem_free(void *p,size_t size)
{ CHECK(p!=NULL && size>0 && heap_objects>0); heap_objects--; free(p); }
#include "pvr-hwrt-body.h"

struct allocation {
	struct pvr_fw_object *slot;
	struct pvr_fw_object valid;
	bool pre, exposed;
};
struct model {
	struct pvr_backend backend;
	struct allocation objects[7];
	unsigned allocated, freed, live, commands, checks;
	unsigned alloc_fail, owned_error, free_fail, busy, command_error, check_fail;
	unsigned malformed_alloc, malformed_field;
	bool fail_called, invalid_result, reset;
};
static struct allocation *find_address(struct model *m,uint32_t address)
{
	unsigned i;
	for(i=0;i<__arraycount(m->objects);i++)
		if(m->objects[i].slot && m->objects[i].slot->address==address)
			return &m->objects[i];
	CHECK(false); return NULL;
}
static struct allocation *find_slot(struct model *m,const struct pvr_fw_object *obj)
{
	unsigned i;
	for(i=0;i<__arraycount(m->objects);i++)
		if(m->objects[i].slot==obj) return &m->objects[i];
	CHECK(false); return NULL;
}
static int model_alloc(void *cookie,size_t size,struct pvr_fw_object *obj)
{
	struct model *m=cookie;
	unsigned i;
	void *p=NULL;
	CHECK(obj->owner==NULL && size>0 && !m->fail_called);
	if(++m->allocated==m->alloc_fail) return ENOMEM;
	for(i=0;i<__arraycount(m->objects) && m->objects[i].slot;i++);
	CHECK(i<__arraycount(m->objects));
	CHECK(posix_memalign(&p,64,roundup2(size,64))==0 && p!=NULL);
	memset(p,0,roundup2(size,64));
	obj->cpu=p; obj->owner=&m->objects[i]; obj->size=size;
	obj->address=0xc0400000U+i*0x10000U;
	m->objects[i].slot=obj; m->objects[i].valid=*obj; m->live++;
	if(m->allocated==m->malformed_alloc) {
		switch(m->malformed_field) {
		case 0: obj->owner=NULL; break;
		case 1: obj->cpu=NULL; break;
		case 2: obj->size=size-1; break;
		case 3: obj->address=0; break;
		case 4: obj->address=UINT32_MAX-(uint32_t)size+2; break;
		case 5: obj->address++; break;
		case 6: obj->cpu=(char *)p+1; break;
		default: CHECK(false);
		}
	}
	return m->allocated==m->owned_error ? ENOMEM : 0;
}
static int model_free(void *cookie,struct pvr_fw_object *obj)
{
	struct model *m=cookie;
	struct allocation *a=find_slot(m,obj);
	unsigned i;
	CHECK(!m->fail_called || m->reset);
	for(i=0;i<__arraycount(m->objects);i++)
		CHECK(!m->objects[i].exposed || m->reset);
	if(++m->freed==m->free_fail) return EIO;
	CHECK(m->live>0); m->live--;
	free(a->valid.cpu); memset(obj,0,sizeof(*obj)); memset(a,0,sizeof(*a));
	return 0;
}
static void model_sync(void *cookie,struct pvr_fw_object *obj,int operations)
{
	struct model *m=cookie;
	CHECK(operations==(BUS_DMASYNC_PREWRITE|BUS_DMASYNC_PREREAD));
	find_slot(m,obj)->pre=true;
}
static int model_check(void *cookie)
{
	struct model *m=cookie;
	m->checks++;
	return m->checks==m->check_fail || m->fail_called ? EIO : 0;
}
static void model_fail(void *cookie) { ((struct model *)cookie)->fail_called=true; }
static int model_command(void *cookie,const struct rogue_fwif_kccb_cmd *command,uint32_t *result)
{
	struct model *m=cookie;
	struct allocation *a;
	CHECK(command->cmd_type==ROGUE_FWIF_KCCB_CMD_CLEANUP);
	CHECK(command->kccb_flags==0);
	CHECK(command->cmd_data.cleanup_data.cleanup_type==ROGUE_FWIF_CLEANUP_HWRTDATA);
	a=find_address(m,command->cmd_data.cleanup_data.cleanup_data.hwrt_data_fw_addr);
	CHECK(a->slot->size==384 && a->pre && a->exposed);
	m->commands++;
	if(m->commands==m->command_error) return ETIMEDOUT;
	if(m->invalid_result) { *result=0; return 0; }
	*result=ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED;
	if(m->commands==m->busy) *result|=ROGUE_FWIF_KCCB_RTN_SLOT_CLEANUP_BUSY;
	else a->exposed=false;
	return 0;
}
static void init_model(struct model *m)
{
	CHECK(heap_objects==0); memset(m,0,sizeof(*m));
	m->backend=(struct pvr_backend){.cookie=m,.alloc=model_alloc,.free=model_free,
	    .sync=model_sync,.command=model_command,.check=model_check,.fail=model_fail};
}
static struct pvr_hwrt_config config(unsigned layers)
{
	struct pvr_hwrt_config c={
		.bvnc=UINT64_C(0x00240038006800b7),
		.tpc_dev_addr=UINT64_C(0x8000010000),
		.vheap_table_dev_addr=UINT64_C(0x8000020000),
		.rtc_dev_addr=layers>1 ? UINT64_C(0x8000020600) : 0,
		.rt={{UINT64_C(0x8000030000),0,UINT64_C(0xf200000000)},
		     {UINT64_C(0x8000031000),0,UINT64_C(0xf200001000)}},
		.freelist_fw_addr={0xc0010000U,0xc0020000U},
		.width=32,.height=48,.samples=1,.layers=layers,
		.tpc_size=4096*layers,.tpc_stride=layers>1 ? 1 : 0,
		.region_header_size=16,.local_pages=28,.min_local_pages=25,
		.isp_merge_lower_x=1,.isp_merge_lower_y=2,
		.isp_merge_upper_x=3,.isp_merge_upper_y=4,
		.isp_merge_scale_x=5,.isp_merge_scale_y=6,
	};
	return c;
}
static struct rogue_fwif_hwrtdata *target(struct model *m,struct pvr_hwrt *hwrt,unsigned index)
{
	struct pvr_hwrt_target t;
	struct allocation *a;
	CHECK(pvr_hwrt_target(hwrt,index,&t)==0);
	CHECK(t.cleanup_address==t.fw_address+320 && (t.fw_address&63)==0);
	a=find_address(m,t.fw_address); CHECK(a->slot->size==384 && a->pre);
	a->exposed=true; return a->slot->cpu;
}
static void finish(struct model *m,struct pvr_hwrt **owner)
{
	CHECK(pvr_hwrt_destroy(owner)==0 && *owner==NULL);
	CHECK(m->live==0 && heap_objects==0);
}
static void byte_checks(struct model *m,struct pvr_hwrt *hwrt,const struct pvr_hwrt_config *c)
{
	struct rogue_fwif_hwrtdata expected;
	struct rogue_fwif_hwrtdata *fw;
	struct rogue_fwif_hwrtdata_common common={0},*actual;
	unsigned i;
	for(i=0;i<2;i++) {
		fw=target(m,hwrt,i); memset(&expected,0,sizeof(expected));
		expected.hwrt_data_common_fw_addr=hwrt->common.address;
		expected.freelists_fw_addr[0]=c->freelist_fw_addr[0];
		expected.freelists_fw_addr[1]=c->freelist_fw_addr[1];
		expected.tail_ptrs_dev_addr=c->tpc_dev_addr;
		expected.vheap_table_dev_addr=c->vheap_table_dev_addr;
		expected.rtc_dev_addr=c->rtc_dev_addr;
		expected.pm_mlist_dev_addr=c->rt[i].pm_mlist_dev_addr;
		expected.macrotile_array_dev_addr=c->rt[i].macrotile_array_dev_addr;
		expected.rgn_header_dev_addr=c->rt[i].region_header_dev_addr;
		expected.rta_ctl.max_rts=c->layers;
		expected.rta_ctl.valid_render_targets_fw_addr=hwrt->rt[i].srtc.address;
		expected.rta_ctl.rta_num_partial_renders_fw_addr=hwrt->rt[i].raa.address;
		CHECK(memcmp(fw,&expected,sizeof(expected))==0);
		if(c->layers>1) {
			struct allocation *s=find_address(m,fw->rta_ctl.valid_render_targets_fw_addr);
			struct allocation *r=find_address(m,fw->rta_ctl.rta_num_partial_renders_fw_addr);
			CHECK(s->slot->size==4*c->layers && r->slot->size==4*c->layers);
			CHECK(s->pre && r->pre);
			for(unsigned j=0;j<c->layers;j++) {
				CHECK(((uint32_t *)s->slot->cpu)[j]==0);
				CHECK(((uint32_t *)r->slot->cpu)[j]==0);
			}
		}
	}
	/* Fixed 32x48, one-sample reference from the pinned profile. */
	common.screen_pixel_max=UINT32_C(0x002f001f);
	common.multi_sample_ctl=common.flipped_multi_sample_ctl=0x88;
	common.tpc_stride=c->tpc_stride; common.tpc_size=c->tpc_size;
	common.te_screen=0x3001; common.mtile_stride=4;
	common.te_mtile1=common.te_mtile2=0x80000;
	common.isp_merge_lower_x=1; common.isp_merge_lower_y=2;
	common.isp_merge_upper_x=3; common.isp_merge_upper_y=4;
	common.isp_merge_scale_x=5; common.isp_merge_scale_y=6;
	common.rgn_header_size=16; common.isp_mtile_size=0x20002;
	actual=hwrt->common.cpu;
	CHECK(memcmp(actual,&common,sizeof(common))==0);
}
static void success_cases(void)
{
	struct model m;
	struct pvr_hwrt *owner=NULL;
	struct pvr_hwrt_config c;
	struct rogue_fwif_hwrtdata_common *fw;
	struct pvr_hwrt_target t;
	unsigned layers[]={1,2,2048},i;
	for(i=0;i<__arraycount(layers);i++) {
		init_model(&m); c=config(layers[i]);
		CHECK(pvr_hwrt_create(&m.backend,&c,&owner)==0);
		CHECK(m.live==(c.layers==1 ? 3U : 7U)); byte_checks(&m,owner,&c);
		CHECK(pvr_hwrt_target(owner,2,&t)==EINVAL);
		finish(&m,&owner); CHECK(m.commands==2);
	}
	init_model(&m); c=config(1); c.samples=2;
	CHECK(pvr_hwrt_create(&m.backend,&c,&owner)==0); fw=owner->common.cpu;
	CHECK(fw->multi_sample_ctl==0x44cc && fw->flipped_multi_sample_ctl==0xc44c);
	CHECK(fw->teaa==4 && fw->isp_mtile_size==0x20004);
	finish(&m,&owner); CHECK(m.commands==0);
	init_model(&m); c=config(1); c.samples=4;
	CHECK(pvr_hwrt_create(&m.backend,&c,&owner)==0); fw=owner->common.cpu;
	CHECK(fw->multi_sample_ctl==0xeaa26e26 && fw->flipped_multi_sample_ctl==0x2a62aee6);
	CHECK(fw->teaa==6 && fw->isp_mtile_size==0x40004);
	(void)target(&m,owner,0); finish(&m,&owner); CHECK(m.commands==1);
	init_model(&m); c=config(1); c.width=c.height=8192; c.samples=4;
	CHECK(pvr_hwrt_create(&m.backend,&c,&owner)==0); fw=owner->common.cpu;
	CHECK(fw->te_screen==0x1ff1ff && fw->te_mtile1==0x2000000 && fw->isp_mtile_size==0x1000100);
	finish(&m,&owner);
	init_model(&m); c=config(1); c.width=c.height=1;
	CHECK(pvr_hwrt_create(&m.backend,&c,&owner)==0); fw=owner->common.cpu;
	CHECK(fw->screen_pixel_max==0 && fw->te_screen==0x1001);
	finish(&m,&owner);
}
static void allocation_cases(void)
{
	struct model m;
	struct pvr_hwrt *owner=NULL;
	struct pvr_hwrt_config c=config(2);
	struct pvr_hwrt_target t;
	unsigned i;
	for(i=1;i<=7;i++) {
		init_model(&m); m.alloc_fail=i;
		CHECK(pvr_hwrt_create(&m.backend,&c,&owner)==ENOMEM);
		CHECK(owner==NULL && m.live==0 && heap_objects==0 && m.commands==0);
		init_model(&m); m.owned_error=i;
		CHECK(pvr_hwrt_create(&m.backend,&c,&owner)==ENOMEM);
		CHECK(owner==NULL && m.live==0 && heap_objects==0 && m.commands==0);
	}
	init_model(&m); m.alloc_fail=5; m.free_fail=1;
	CHECK(pvr_hwrt_create(&m.backend,&c,&owner)==ENOMEM && owner!=NULL);
	CHECK(m.live==4 && pvr_hwrt_target(owner,0,&t)==EIO);
	finish(&m,&owner); CHECK(m.commands==0);
	for(i=1;i<=7;i++) {
		init_model(&m); CHECK(pvr_hwrt_create(&m.backend,&c,&owner)==0);
		(void)target(&m,owner,0); (void)target(&m,owner,1); m.free_fail=i;
		CHECK(pvr_hwrt_destroy(&owner)==EIO && owner!=NULL);
		CHECK(m.commands==2 && m.live==8-i);
		CHECK(pvr_hwrt_target(owner,0,&t)==EIO); finish(&m,&owner);
		CHECK(m.commands==2);
	}
}
static void descriptor_cases(void)
{
	struct model m;
	struct pvr_hwrt *owner=NULL;
	struct pvr_hwrt_config c=config(2);
	struct pvr_hwrt_target t;
	unsigned allocation,field,i;
	for(allocation=1;allocation<=7;allocation++) {
		for(field=0;field<7;field++) {
			init_model(&m); m.malformed_alloc=allocation; m.malformed_field=field;
			CHECK(pvr_hwrt_create(&m.backend,&c,&owner)==EPROTO && owner!=NULL);
			CHECK(m.fail_called && m.live==allocation && m.freed==0 && m.commands==0);
			CHECK(pvr_hwrt_target(owner,0,&t)==EIO);
			CHECK(pvr_hwrt_destroy(&owner)==EIO && m.freed==0);
			/* The model repairs its bad descriptor only after a proven reset. */
			m.reset=true; m.fail_called=false; owner->failed=false;
			for(i=0;i<__arraycount(m.objects);i++)
				if(m.objects[i].slot) *m.objects[i].slot=m.objects[i].valid;
			finish(&m,&owner);
		}
	}
}
static void cleanup_cases(void)
{
	struct model m;
	struct pvr_hwrt *owner=NULL;
	struct pvr_hwrt_config c=config(2);
	struct pvr_hwrt_target t;
	unsigned i;
	for(i=1;i<=2;i++) {
		init_model(&m); CHECK(pvr_hwrt_create(&m.backend,&c,&owner)==0);
		(void)target(&m,owner,0); (void)target(&m,owner,1); m.busy=i;
		CHECK(pvr_hwrt_destroy(&owner)==EBUSY && owner!=NULL && m.freed==0 && m.live==7);
		CHECK(pvr_hwrt_target(owner,0,&t)==EIO); finish(&m,&owner); CHECK(m.commands==3);
		init_model(&m); CHECK(pvr_hwrt_create(&m.backend,&c,&owner)==0);
		(void)target(&m,owner,0); (void)target(&m,owner,1); m.command_error=i;
		CHECK(pvr_hwrt_destroy(&owner)==ETIMEDOUT && owner!=NULL && m.freed==0);
		/* The next successful cleanup acknowledges any earlier in-flight request. */
		finish(&m,&owner); CHECK(m.commands==3);
	}
	init_model(&m); CHECK(pvr_hwrt_create(&m.backend,&c,&owner)==0);
	(void)target(&m,owner,0); m.check_fail=2;
	CHECK(pvr_hwrt_destroy(&owner)==EIO && owner!=NULL && m.freed==0 && m.commands==0);
	finish(&m,&owner);
	init_model(&m); CHECK(pvr_hwrt_create(&m.backend,&c,&owner)==0);
	(void)target(&m,owner,0); m.invalid_result=true;
	CHECK(pvr_hwrt_destroy(&owner)==EPROTO && owner!=NULL && m.fail_called && m.freed==0);
	CHECK(pvr_hwrt_destroy(&owner)==EIO && m.commands==1 && m.live==7);
	/* Only the test's independently proven reset permits reclamation. */
	m.reset=true; m.fail_called=false; m.invalid_result=false;
	owner->failed=false; owner->rt[0].exposed=false;
	finish(&m,&owner);
}
static void validation_cases(void)
{
	struct model m;
	struct pvr_hwrt *owner=NULL;
	struct pvr_hwrt_config c;
	unsigned i;
	for(i=0;i<25;i++) {
		init_model(&m); c=config(2);
		switch(i) {
		case 0: c.bvnc=0; break;
		case 1: c.width=0; break;
		case 2: c.height=8193; break;
		case 3: c.samples=8; break;
		case 4: c.layers=0; break;
		case 5: c.layers=2049; break;
		case 6: c.local_pages=24; break;
		case 7: c.min_local_pages=0; break;
		case 8: c.tpc_stride=0; break;
		case 9: c.tpc_size=0; break;
		case 10: c.region_header_size=0; break;
		case 11: c.rtc_dev_addr=0; break;
		case 12: c.freelist_fw_addr[0]=0; break;
		case 13: c.tpc_dev_addr=0; break;
		case 14: c.tpc_dev_addr=UINT64_C(1)<<40; break;
		case 15: c.tpc_dev_addr+=8; break;
		case 16: c.vheap_table_dev_addr++; break;
		case 17: c.rtc_dev_addr+=8; break;
		case 18: c.freelist_fw_addr[1]+=4; break;
		case 19: c.rt[0].pm_mlist_dev_addr++; break;
		case 20: c.rt[1].region_header_dev_addr+=8; break;
		case 21: c.rt[1].macrotile_array_dev_addr=8; break;
		case 22: c.rt[1].pm_mlist_dev_addr=0; break;
		case 23: c.rt[0].region_header_dev_addr=0; break;
		case 24: c.rtc_dev_addr=UINT64_C(1)<<40; break;
		}
		CHECK(pvr_hwrt_create(&m.backend,&c,&owner)==(i==0 ? EOPNOTSUPP : EINVAL));
		CHECK(owner==NULL && m.allocated==0 && heap_objects==0);
	}
}
int main(void)
{
	success_cases(); allocation_cases(); descriptor_cases(); cleanup_cases(); validation_cases();
	printf("pvr HWRT: %u checks passed\n",checks); return 0;
}
