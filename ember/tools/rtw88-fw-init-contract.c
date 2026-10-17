/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: actual-source packet-H2C producer/DMA consumer contract. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rtw88-test-kernel.h"
#include <dev/ic/rtw88var.h>
int rtw88_fw_init(struct rtw88_softc *);
static unsigned assertions;
static unsigned bd_size,bd_buf_size,bd_psb_len,bd_dma;
static uint8_t reference_desc[48];
#define CHECK(x) do { assertions++; if (!(x)) { \
	fprintf(stderr,"%s:%d: %s\n",__func__,__LINE__,#x); exit(1); \
} } while(0)
static struct {
	struct rtw88_softc sc;
	uint8_t fw[80], ring[2048], dma_fw[80], dma_ring[2048], want[2][32];
	unsigned wp,rp,num,desa,kicks,consumed,ops,fail_at,cancel_at,after_error;
	unsigned polls,complete_after[2],delay,pre,post;
	int failed,invalid_rp,cancel_sync,cancel_post;
} f;
static uint32_t
get(const uint8_t *p,unsigned n)
{
	uint32_t v=0;
	unsigned i;
	for(i=0;i<n;i++) v|=(uint32_t)p[i]<<(i*8);
	return v;
}
static int
op(void)
{
	f.ops++;
	if(f.failed) f.after_error++;
	if(f.ops==f.cancel_at) f.sc.sc_generation++;
	if(f.ops==f.fail_at) { f.failed=1; return EIO; }
	return 0;
}
/* The fake device consumes the PREWRITE snapshots, never live CPU buffers. */
static void
consume(void)
{
	const uint8_t *bd=f.dma_ring+16*(f.wp-1);
	CHECK(f.consumed<2 && f.pre==f.kicks);
	CHECK(get(bd+bd_buf_size,2)==48 && get(bd+bd_psb_len,2)==1);
	CHECK(get(bd+bd_dma,4)==f.sc.sc_fw_dma.addr);
	CHECK(get(bd+bd_size+bd_buf_size,2)==32 && get(bd+bd_size+bd_psb_len,2)==0);
	CHECK(get(bd+bd_size+bd_dma,4)==f.sc.sc_fw_dma.addr+48);
	CHECK(memcmp(f.dma_fw,reference_desc,48)==0);
	CHECK(memcmp(f.dma_fw+48,f.want[f.consumed],32)==0);
	f.consumed++;
	f.rp=f.wp;
}
static int
rd2(void *cookie,uint32_t addr,uint16_t *out)
{
	int e=op();
	(void)cookie;
	CHECK(addr==0x1328);
	if(!e) *out=(uint16_t)f.num;
	return e;
}
static int
rd4(void *cookie,uint32_t addr,uint32_t *out)
{
	int e=op();
	(void)cookie;
	if(e) return e;
	if(addr==0x1320) { *out=f.desa; return 0; }
	CHECK(addr==0x132c);
	if(f.wp!=f.rp) {
		f.polls++;
		if(f.invalid_rp) { *out=(127U<<16)|f.wp; return 0; }
		if(f.complete_after[f.consumed]!=0 &&
		    f.polls>=f.complete_after[f.consumed]) consume();
	}
	*out=(f.rp<<16)|f.wp;
	return 0;
}
static int
wr2(void *cookie,uint32_t addr,uint16_t value)
{
	int e=op();
	(void)cookie;
	if(e) return e;
	CHECK(addr==0x132c && f.wp==f.rp);
	CHECK(value==f.wp+1 && value<=2);
	f.wp=value;
	f.kicks++;
	f.polls=0;
	return 0;
}
static void
sync_dma(void *cookie,struct rtw88_dma *map,int flags)
{
	(void)cookie;
	if(flags==BUS_DMASYNC_PREWRITE) {
		CHECK(f.wp==f.rp); /* CPU reuse must follow device completion. */
		if(map==&f.sc.sc_fw_dma) {
			memcpy(f.dma_fw,f.fw,sizeof(f.fw));
			f.pre++;
			if(f.cancel_sync) f.sc.sc_stop_requested=1;
		} else {
			CHECK(map==&f.sc.sc_ring[7]);
			memcpy(f.dma_ring,f.ring,sizeof(f.ring));
		}
	} else {
		CHECK(flags==BUS_DMASYNC_POSTWRITE && f.wp==f.rp);
		if(map==&f.sc.sc_fw_dma) {
			f.post++;
			if(f.cancel_post && f.post==2) f.sc.sc_generation++;
		}
	}
}
static void dly(void *cookie,unsigned us) { (void)cookie; CHECK(us==50); f.delay+=us; }
static const struct rtw88_bus_ops ops={.read_2=rd2,.read_4=rd4,
    .write_2=wr2,.dma_sync=sync_dma,.delay_us=dly};
static void
setup(unsigned rfe,unsigned cut,const uint8_t wanted[2][32])
{
	memset(&f,0,sizeof(f));
	f.sc.sc_ops=&ops;
	f.sc.sc_bus=&f;
	f.sc.sc_fw_dma=(struct rtw88_dma){.vaddr=f.fw,.addr=0x300000,.size=80};
	f.sc.sc_ring[7]=(struct rtw88_dma){.vaddr=f.ring,.addr=0x100000,.size=2048};
	f.sc.sc_fw_dma_owned=1;
	f.sc.sc_running=1;
	f.sc.sc_bootstrapping=1;
	f.sc.sc_generation=42;
	f.sc.sc_rfe=(uint8_t)rfe;
	f.sc.sc_cut=(uint8_t)cut;
	f.num=128;
	f.desa=0x100000;
	f.complete_after[0]=f.complete_after[1]=1;
	memcpy(f.want,wanted,sizeof(f.want));
}
static uint8_t vectors[7][7][2][32];
static void
load(const char *path)
{
	static const uint8_t literal_bd[16]={48,0,1,0,0,0,48,0,32,0,0,0,48,0,48,0};
	FILE *fp=fopen(path,"r");
	char line[128];
	unsigned r,c,i,k,n=0,v;
	CHECK(fp!=NULL);
	CHECK(fgets(line,sizeof(line),fp)!=NULL);
	CHECK(fgets(line,sizeof(line),fp)!=NULL);
	CHECK(sscanf(line,"layout %u %u %u %u",&bd_size,&bd_buf_size,&bd_psb_len,&bd_dma)==4);
	CHECK(bd_size==8 && bd_buf_size==0 && bd_psb_len==2 && bd_dma==4);
	CHECK(fgets(line,sizeof(line),fp)!=NULL && strncmp(line,"txdesc ",7)==0);
	CHECK(strlen(line)==104);
	for(i=0;i<48;i++) {
		CHECK(sscanf(line+7+2*i,"%2x",&v)==1);
		reference_desc[i]=(uint8_t)v;
	}
	/* Independent field checks: PCI packet-H2C has no data offset, LS or checksum. */
	CHECK(get(reference_desc,4)==32 && get(reference_desc+4,4)==0x1300);
	for(i=8;i<48;i++) CHECK(reference_desc[i]==0);
	CHECK(fgets(line,sizeof(line),fp)!=NULL && strncmp(line,"bd ",3)==0);
	for(i=0;i<16;i++) {
		CHECK(sscanf(line+3+2*i,"%2x",&v)==1);
		CHECK(v==literal_bd[i]);
	}
	for(r=0;r<7;r++) for(c=0;c<7;c++) {
		unsigned rr,cc;
		CHECK(fgets(line,sizeof(line),fp)!=NULL);
		CHECK(sscanf(line,"case %u %u",&rr,&cc)==2 && rr==r && cc==c);
		for(k=0;k<2;k++) {
			CHECK(fgets(line,sizeof(line),fp)!=NULL && strlen(line)==65);
			for(i=0;i<32;i++) {
				CHECK(sscanf(line+2*i,"%2x",&v)==1);
				vectors[r][c][k][i]=(uint8_t)v;
			}
			n++;
		}
	}
	CHECK(n==98);
	fclose(fp);
}
int
main(int argc,char **argv)
{
	static const uint8_t general[32]={1,255,13,0,12,0,0,0,[10]=56};
	unsigned r,c,i,total;
	CHECK(argc==2);
	load(argv[1]);
	for(r=0;r<7;r++) for(c=0;c<7;c++) {
		const uint8_t phydm[32]={1,255,17,0,16,0,1,0,(uint8_t)r,2,(uint8_t)c,0x33};
		CHECK(memcmp(vectors[r][c][0],general,32)==0);
		CHECK(memcmp(vectors[r][c][1],phydm,32)==0);
		setup(r,c,vectors[r][c]);
		CHECK(rtw88_fw_init(&f.sc)==0);
		CHECK(f.consumed==2 && f.kicks==2 && f.post==2);
		CHECK(f.delay==0 && f.sc.sc_started==0 && f.sc.sc_fw_dma_owned==1);
	}
	setup(5,2,vectors[5][2]); CHECK(rtw88_fw_init(&f.sc)==0); total=f.ops;
	for(i=1;i<=total;i++) {
		setup(5,2,vectors[5][2]); f.fail_at=i;
		CHECK(rtw88_fw_init(&f.sc)==EIO);
		CHECK(f.failed && f.after_error==0 && f.ops==i);
		setup(5,2,vectors[5][2]); f.cancel_at=i;
		CHECK(rtw88_fw_init(&f.sc)==EINTR);
	}
	setup(5,2,vectors[5][2]); f.complete_after[0]=0;
	CHECK(rtw88_fw_init(&f.sc)==ETIMEDOUT);
	CHECK(f.delay==1000000 && f.kicks==1 && f.pre==1 && f.post==0);
	CHECK(memcmp(f.fw+48,general,32)==0);
	setup(5,2,vectors[5][2]); f.complete_after[0]=20001;
	CHECK(rtw88_fw_init(&f.sc)==0 && f.delay==1000000);
	setup(5,2,vectors[5][2]); f.complete_after[0]=20001; f.complete_after[1]=2;
	CHECK(rtw88_fw_init(&f.sc)==ETIMEDOUT && f.delay==1000000);
	setup(5,2,vectors[5][2]); f.invalid_rp=1;
	CHECK(rtw88_fw_init(&f.sc)==EIO && f.kicks==1 && f.pre==1);
	setup(5,2,vectors[5][2]); f.cancel_sync=1;
	CHECK(rtw88_fw_init(&f.sc)==EINTR && f.kicks==0);
	setup(5,2,vectors[5][2]); f.cancel_post=1;
	CHECK(rtw88_fw_init(&f.sc)==EINTR && f.consumed==2);
	setup(5,2,vectors[5][2]); f.wp=f.rp=1;
	CHECK(rtw88_fw_init(&f.sc)==EBUSY && f.pre==0);
	setup(5,2,vectors[5][2]); f.num=127;
	CHECK(rtw88_fw_init(&f.sc)==EIO && f.pre==0);
	setup(5,2,vectors[5][2]); f.desa=0;
	CHECK(rtw88_fw_init(&f.sc)==EIO && f.pre==0);
	setup(5,2,vectors[5][2]); f.sc.sc_fw_dma.size=79;
	CHECK(rtw88_fw_init(&f.sc)==EINVAL && f.ops==0);
	setup(5,2,vectors[5][2]); f.sc.sc_fw_dma.addr=UINT64_C(0x100000000)-76;
	CHECK(rtw88_fw_init(&f.sc)==EOVERFLOW && f.ops==0);
	setup(5,2,vectors[5][2]); f.sc.sc_ring[7].addr=UINT64_C(0x100000000)-1024;
	CHECK(rtw88_fw_init(&f.sc)==EOVERFLOW && f.ops==0);
	setup(5,2,vectors[5][2]); f.sc.sc_fw_dma.addr=f.sc.sc_ring[7].addr;
	CHECK(rtw88_fw_init(&f.sc)==EINVAL && f.ops==0);
	setup(5,2,vectors[5][2]); f.sc.sc_started=1;
	CHECK(rtw88_fw_init(&f.sc)==EBUSY && f.ops==0);
	CHECK(rtw88_fw_init(NULL)==EINVAL);
	printf("%u packet-H2C assertions passed\n",assertions);
	return 0;
}
