/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: actual-source cold ADCK/DACK consumer contract, AI-assisted. */
#include "rtw88-test-kernel.h"
#include <dev/ic/rtw8822c.h>
#include <dev/ic/rtw88var.h>
#include "rtw8822c-dack-model.h"
static unsigned assertions;
#define CHECK(x) do { assertions++; if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__func__,__LINE__,#x); exit(1); } } while(0)
static struct rtw88_softc sc;
static struct rtw8822c_dack_result result;
static unsigned calls,fail_at,cancel_at,after_error,delays,cancel_delay,canceled_calls;
static int failed;
static int
operation(void)
{
	calls++;
	if(failed) after_error++;
	if(calls==cancel_at) sc.sc_generation++;
	if(calls==fail_at) { failed=1; return EIO; }
	return 0;
}
static int read4(void*c,uint32_t a,uint32_t*v) { int e=operation(); (void)c; if(!e)*v=model_read(a,4); return e; }
static int write4(void*c,uint32_t a,uint32_t v) { int e=operation(); (void)c; if(!e)model_write(a,4,v); return e; }
static int write1(void*c,uint32_t a,uint8_t v) { int e=operation(); (void)c; if(!e)model_write(a,1,v); return e; }
static void dack_delay(void*c,unsigned us) { (void)c; model_delay(us); if(++delays==cancel_delay) { sc.sc_stop_requested=1; canceled_calls=calls; } }
static const struct rtw88_bus_ops ops={.read_4=read4,.write_4=write4,.write_1=write1,.delay_us=dack_delay};
static void
setup(unsigned fixture)
{
	model_reset(fixture);
	memset(&sc,0,sizeof(sc)); memset(&result,0xa5,sizeof(result));
	sc.sc_ops=&ops; sc.sc_bus=&sc;
	/* Real PHY-post phase: power/resources ready, HCI/interrupts not started. */
	sc.sc_resources_ready=sc.sc_powered=sc.sc_bootstrapping=1;
	sc.sc_running=0; sc.sc_generation=42;
	calls=fail_at=cancel_at=after_error=0; failed=0;
	delays=cancel_delay=canceled_calls=0;
}
static void
check_failure(int error)
{
	CHECK(rtw8822c_dack(&sc,&result)==error);
	CHECK(result.valid_paths==0 && result.reset_required==1);
	CHECK(result.operations<=1000000 && result.delay_us<=2000000);
	CHECK(after_error==0);
}
int main(int argc,char **argv)
{
	static const uint16_t adc[2][2][2]={{{0,0},{0,0}},{{1020,3},{5,1018}}};
	static const int16_t ar[2][2][2]={{{0,0},{0,0}},{{0,0},{0,0}}};
	static const int16_t dr[2][2][2]={{{0,0},{0,0}},{{1,-2},{-1,2}}};
	const unsigned fixtures[]={0,1,2,3,4,5,8};
	FILE *fp;
	char line[256];
	unsigned total,cold,i,j,kind,addr,value,n,scenario;
	unsigned complete_calls=0,complete_delays=0;
	CHECK(argc==2); fp=fopen(argv[1],"r"); CHECK(fp!=NULL);
	CHECK(fgets(line,sizeof(line),fp)!=NULL);
	for(scenario=0;scenario<sizeof(fixtures)/sizeof(fixtures[0]);scenario++) {
		unsigned fixture=fixtures[scenario];

		CHECK(fgets(line,sizeof(line),fp)!=NULL);
		CHECK(sscanf(line,"case %u events %u cold %u",&n,&total,&cold)==3 && n==fixture);
		setup(fixture);
		CHECK(rtw8822c_dack(&sc,&result)==0);
		CHECK(result.valid_paths==3 && result.reset_required==0);
		CHECK(model.count==cold && cold<=total);
		CHECK(result.operations==calls && result.delay_us>104000);
		CHECK(memcmp(result.adc,adc[fixture%2],sizeof(result.adc))==0);
		CHECK(memcmp(result.adc_residual,ar[fixture%2],sizeof(result.adc_residual))==0);
		CHECK(memcmp(result.dac_residual,dr[fixture%2],sizeof(result.dac_residual))==0);
		for(i=0;i<2;i++) for(j=0;j<2;j++) CHECK(result.dac[i][j]==vectors[fixture%2][i][j]);
		/* Keep fault/cancel coverage on the two-attempt convergence path. */
		if(fixture==5) { complete_calls=calls; complete_delays=delays; }
		for(i=0;i<total;i++) {
			CHECK(fgets(line,sizeof(line),fp)!=NULL);
			CHECK(sscanf(line,"%u %x %x",&kind,&addr,&value)==3);
			/* Preserve the reference cache-export tail in the fixture, but
			 * the native cold-only stage ends after its control restore. */
			if(i<cold) CHECK(model.events[i].kind==kind && model.events[i].address==addr && model.events[i].value==value);
		}
	}
	CHECK(fgets(line,sizeof(line),fp)==NULL); fclose(fp);
	CHECK(complete_calls!=0 && complete_delays!=0);
	/* Every bus failure and generation change must stop the actual sequence. */
	for(i=1;i<=complete_calls;i++) {
		setup(5); fail_at=i;
		CHECK(rtw8822c_dack(&sc,&result)==EIO);
		CHECK(calls==i && after_error==0 && result.valid_paths==0);
		if(i>=20) CHECK(result.reset_required==1);
		setup(5); cancel_at=i;
		CHECK(rtw8822c_dack(&sc,&result)==EINTR);
		CHECK(calls==i && result.valid_paths==0);
	}
	for(i=1;i<=complete_delays;i++) {
		setup(5); cancel_delay=i;
		CHECK(rtw8822c_dack(&sc,&result)==EINTR);
		CHECK(calls==canceled_calls && result.valid_paths==0 && result.reset_required==1);
	}
	setup(0); model.invalid=1; model.recording=0; check_failure(ERANGE);
	CHECK(model.samples==10000);
	CHECK(model.visits[0][ADC_POST]==0 && model.visits[0][DAC_PRE]==0);
	setup(0); model.unstable=1; model.recording=0; check_failure(ERANGE);
	CHECK(model.samples==302);
	setup(0); model.replacement_invalid=1; model.recording=0; check_failure(ERANGE);
	CHECK(model.samples==101);
	setup(0); model.not_ready=1; model.recording=0; check_failure(ETIMEDOUT);
	setup(0); model.bad_vector=1; model.recording=0; check_failure(ETIMEDOUT);
	setup(0); model.adc_fail=1; model.recording=0; check_failure(ERANGE);
	CHECK(model.visits[0][ADC_POST]==10);
	setup(0); model.dac_fail=1; model.recording=0; check_failure(ERANGE);
	CHECK(model.visits[0][DAC_PRE]==10);
	setup(0); model.vector_overflow=100; model.recording=0; check_failure(ERANGE);
	CHECK(model.visits[0][DAC_PRE]==1 && result.dac[0][0]==0);
	setup(0); model.vector_overflow=-100; model.recording=0; check_failure(ERANGE);
	CHECK(model.visits[0][DAC_PRE]==1 && result.dac[0][0]==0);
	/* All local maxima can run together inside the total software budgets. */
	setup(6); model.skip=9900; model.recording=0;
	CHECK(rtw8822c_dack(&sc,&result)==0);
	CHECK(result.valid_paths==3 && !result.reset_required);
	printf("local-max fixture: %u operations, %u us explicit delay\n",result.operations,result.delay_us);
	for(i=0;i<2;i++) printf("path%u samples: %u %u %u %u\n",i,model.sample_reads[i][0],model.sample_reads[i][1],model.sample_reads[i][2],model.sample_reads[i][3]);
	CHECK(result.operations>880000 && result.operations<=1000000);
	CHECK(result.delay_us>1830000 && result.delay_us<=2000000);
	CHECK(model.visits[0][ADC_POST]==10 && model.visits[1][ADC_POST]==10);
	CHECK(model.visits[0][DAC_PRE]==10 && model.visits[1][DAC_PRE]==10);
	/* The source alias defect must not restore B's RF8f into path A. */
	setup(0); raw_write(0x3e3c,4,0x13579); raw_write(0x4e3c,4,0x2468a);
	CHECK(rtw8822c_dack(&sc,&result)==0);
	CHECK(raw_read(0x3e3c,4)==0x13579 && raw_read(0x4e3c,4)==0x2468a);
	setup(0); sc.sc_stop_requested=1;
	CHECK(rtw8822c_dack(&sc,&result)==EINTR && calls==0 && !result.reset_required);
	setup(0); sc.sc_started=1;
	CHECK(rtw8822c_dack(&sc,&result)==EBUSY && calls==0);
	setup(0); sc.sc_running=1;
	CHECK(rtw8822c_dack(&sc,&result)==EBUSY && calls==0);
	setup(0); sc.sc_resources_ready=0;
	CHECK(rtw8822c_dack(&sc,&result)==ENXIO && calls==0);
	setup(0); sc.sc_powered=0;
	CHECK(rtw8822c_dack(&sc,&result)==ENXIO && calls==0);
	setup(0); sc.sc_bootstrapping=0;
	CHECK(rtw8822c_dack(&sc,&result)==ENXIO && calls==0);
	setup(0); sc.sc_ops=NULL;
	CHECK(rtw8822c_dack(&sc,&result)==EINVAL && calls==0);
	CHECK(rtw8822c_dack(NULL,&result)==EINVAL);
	CHECK(rtw8822c_dack(&sc,NULL)==EINVAL);
	printf("%u cold ADCK/DACK assertions passed\n",assertions);
	return 0;
}
