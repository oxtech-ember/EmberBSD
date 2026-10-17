/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: scripted hardware outcomes for cold ADCK/DACK source contracts. */
#ifndef RTW8822C_DACK_MODEL_H
#define RTW8822C_DACK_MODEL_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { ADC_PRE, ADC_POST, DAC_PRE, DAC_POST };
struct dack_event { unsigned kind, address, value; };
static struct {
	uint8_t registers[65536];
	struct dack_event events[20000];
	unsigned count,cold_count,path,kind,active[2],fixture,samples,visits[2][4];
	unsigned errors,restored,recording,profile,ready_reads[8],skip;
	unsigned sample_reads[2][4];
	int invalid,unstable,not_ready,bad_vector,adc_fail,dac_fail;
	int replacement_invalid,transient_replacement,vector_overflow;
} model;
static const int measured[2][2][4][2]={
	{ {{0,0},{0,0},{0,0},{16,16}}, {{0,0},{0,0},{0,0},{16,16}} },
	{ {{4,-3},{0,0},{2,-3},{17,14}}, {{-5,6},{0,0},{-4,1},{15,18}} }
};
static const unsigned vectors[2][2][2]={{{128,128},{128,128}},{{123,135},{137,125}}};
static uint32_t
raw_read(unsigned a,unsigned n)
{
	uint32_t v=0;
	unsigned i;
	if(a>sizeof(model.registers)-n) abort();
	for(i=0;i<n;i++) v|=(uint32_t)model.registers[a+i]<<(8*i);
	return v;
}
static void
raw_write(unsigned a,unsigned n,uint32_t v)
{
	unsigned i;
	if(a>sizeof(model.registers)-n) abort();
	for(i=0;i<n;i++) model.registers[a+i]=(uint8_t)(v>>(8*i));
}
static void
model_event(unsigned kind,unsigned a,unsigned v)
{
	if(!model.recording) return;
	if(model.count==sizeof(model.events)/sizeof(model.events[0])) abort();
	model.events[model.count++]=(struct dack_event){kind,a,v};
}
static void
model_phase(unsigned path,unsigned kind)
{
	model.path=path; model.kind=kind; model.samples=0;
	model.visits[path][kind]++;
}
static uint32_t
model_read(unsigned a,unsigned n)
{
	uint32_t v=raw_read(a,n);
	unsigned p=model.path,k=model.kind,index;
	if(a==0x2dbc) {
		int iv,qv;

		model.sample_reads[p][k]++;
		iv=measured[model.fixture][p][k][0];
		qv=measured[model.fixture][p][k][1];
		if(model.invalid) iv=101;
		if(model.unstable) iv=(model.samples%2) ? 10 : -10;
		if(model.replacement_invalid) iv=model.samples<100 ? ((model.samples%2)?10:-10) : 101;
		if(model.vector_overflow && k==DAC_PRE) iv=model.vector_overflow;
		if(k==ADC_POST && model.adc_fail) iv=8;
		if(k==DAC_POST && model.dac_fail) iv=32;
		if(model.profile==2 || model.profile==3) {
			unsigned attempt=model.profile==3 ? 10 : 2;
			if(k==ADC_POST && model.visits[p][ADC_POST]<attempt) iv=8;
			if(k==DAC_POST && model.visits[p][DAC_PRE]<attempt) iv=32;
		}
		if(model.profile==4 && model.samples<100) {
			if(model.samples<10) iv=qv=-80;
			else if(model.samples>=90) iv=qv=80;
			else { iv+=(model.samples%2)?2:-2; qv+=(model.samples%2)?-2:2; }
		}
		/* Noise stays wider than the early-stop range on every batch. */
		if(model.profile==5) {
			iv+=(model.samples%2)?10:-10;
			qv+=(model.samples%2)?-10:10;
		}
		if(model.transient_replacement && model.samples==100) iv=101;
		if(model.profile==6 && p==0 && k==ADC_POST) iv=8;
		if(model.profile==7 && p==0 && k==DAC_POST) iv=32;
		if(model.samples<model.skip) iv=101;
		v=(((unsigned)iv&1023)<<12)|((unsigned)qv&1023);
		model.samples++;
	}
	if(a==0x2808 || a==0x2834 || a==0x4508 || a==0x4534) {
		v=model.not_ready ? 0 : 0x7fff80;
		index=(a>=0x4500?4:0)+(a==0x2834||a==0x4534);
		if((model.profile==1 || model.profile==3) && model.ready_reads[index]++<999) v=0;
	}
	if(a==0x2824 || a==0x2850 || a==0x4524 || a==0x4550) {
		p=a>=0x4500;
		k=(a==0x2850 || a==0x4550);
		v=(model.bad_vector ? 0 : vectors[model.fixture][p][k])<<19;
		index=2+4*p+k;
		if((model.profile==1 || model.profile==3) && model.ready_reads[index]++<999) v=0;
	}
	model_event('r'+n,a,v);
	return v;
}
static void
model_write(unsigned a,unsigned n,uint32_t v)
{
	unsigned base,p;
	model_event('w'+n,a,v);
	raw_write(a,n,v);
	if(a==0x1808 || a==0x4108)
		raw_write(a==0x1808?0x3c00:0x4c00,4,v&0xfffff);
	if(a>=0x1800 && a<=0x18ff) model.path=0;
	if(a>=0x4100 && a<=0x41ff) model.path=1;
	p=model.path; base=p?0x4100:0x1800;
	if(a==base+0xb0) model.active[p]=1;
	if(a==base+0xb8 && v==0x62000000) memset(model.ready_reads+4*p,0,4*sizeof(model.ready_reads[0]));
	if(a==0x1c3c) {
		if(v==0xa8003) { model.active[0]=0; model_phase(0,ADC_PRE); }
		else if(v==0x88003) { model.active[1]=0; model_phase(1,ADC_PRE); }
		else if(v==0xa8103) model_phase(0,ADC_POST);
		else if(v==0x88103) model_phase(p,model.active[p]?DAC_PRE:ADC_POST);
	}
	if(a==base+0x68 && model.active[p]) model_phase(p,DAC_POST);
}
static void model_delay(unsigned us) { model_event('d',0,us); }
static void
model_reset(unsigned fixture)
{
	unsigned i;
	memset(&model,0,sizeof(model));
	for(i=0;i<sizeof(model.registers);i++) model.registers[i]=(uint8_t)(i*37+11);
	/* Equal RF8f values keep the pinned alias bug out of parity fixtures. */
	raw_write(0x3e3c,4,0x12345); raw_write(0x4e3c,4,0x12345);
	model.fixture=fixture%2; model.profile=fixture/2; model.recording=1;
}
#endif
