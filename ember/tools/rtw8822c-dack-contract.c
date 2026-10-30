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
static int fail_measure_kind;
static unsigned first_b_call, snapshot_call;
static int
operation(void)
{
	calls++;
	if(failed) after_error++;
	if(calls==cancel_at) sc.sc_generation++;
	if(calls==fail_at) { failed=1; return EIO; }
	return 0;
}
static int read4(void*c,uint32_t a,uint32_t*v) {
 int e=operation(); (void)c;
 if(!e && fail_measure_kind>=0 && a==0x2dbc && model.kind==(unsigned)fail_measure_kind && !model.samples) { failed=1; e=EIO; }
 if(a==0x4130 && model.visits[0][DAC_POST] && !first_b_call) first_b_call=calls;
 if(a==0x4168 && model.kind==ADC_POST && model.visits[1][ADC_POST]==10) snapshot_call=calls;
 if(!e) *v=model_read(a,4);
 return e;
}
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
	fail_measure_kind=-1;
	delays=cancel_delay=canceled_calls=first_b_call=snapshot_call=0;
}
static void
check_failure(int error)
{
	CHECK(rtw8822c_dack(&sc,&result)==error);
	CHECK(!result.restored && result.reset_required==1);
	CHECK(result.operations<=1000000 && result.delay_us<=2000000);
	CHECK(after_error==0);
}
int main(int argc,char **argv)
{
	static const uint16_t adc[2][2][2]={{{0,0},{0,0}},{{1020,3},{5,1018}}};
	static const int16_t ar[2][2][2]={{{0,0},{0,0}},{{0,0},{0,0}}};
	static const int16_t dr[2][2][2]={{{0,0},{0,0}},{{1,-2},{-1,2}}};
	const unsigned fixtures[]={0,1,2,3,4,5,8,10,12,14};
	FILE *fp;
	char line[256];
	unsigned total,cold,i,j,kind,addr,value,n,scenario;
	unsigned complete_calls=0,complete_delays=0,snapshot_calls,warning_b_call,advisory_calls;
	CHECK(argc==2); fp=fopen(argv[1],"r"); CHECK(fp!=NULL);
	CHECK(fgets(line,sizeof(line),fp)!=NULL);
	for(scenario=0;scenario<sizeof(fixtures)/sizeof(fixtures[0]);scenario++) {
		unsigned fixture=fixtures[scenario], native_i=0;

		CHECK(fgets(line,sizeof(line),fp)!=NULL);
		CHECK(sscanf(line,"case %u events %u cold %u",&n,&total,&cold)==3 && n==fixture);
		setup(fixture);
		CHECK(rtw8822c_dack(&sc,&result)==0);
		CHECK(result.completed_paths==3 && result.restored && result.reset_required==0);
		CHECK(result.stage==RTW8822C_DACK_DONE && result.path==1);
		CHECK(result.adc_measured==3 && result.dac_measured==3);
		CHECK(result.adc_converged==(fixture==12?2:3));
		CHECK(result.dac_converged==(fixture==14?2:3));
		CHECK(result.adc_nonconverged==(fixture==12?1:0));
		CHECK(result.dac_nonconverged==(fixture==14?1:0));
		if(fixture==12) CHECK(result.adc_attempts[0]==10 && result.adc_attempts[1]==1);
		if(fixture==14) CHECK(result.dac_attempts[0]==10 && result.dac_attempts[1]==1);

		CHECK(result.sample_count==100 && result.mean_valid==1);
		CHECK(result.sample_reads==(fixture==10?302:fixture==8?120:100));
		CHECK(model.count==cold+(fixture==12?5:0) && cold<=total);
		CHECK(result.operations==calls && result.delay_us>104000);
		CHECK(memcmp(result.adc,adc[fixture%2],sizeof(result.adc))==0);
		if(fixture==12) {
			CHECK(result.adc_residual[0][0]==8 && result.adc_residual[0][1]==0);
			CHECK(result.adc_residual[1][0]==0 && result.adc_residual[1][1]==0);
		} else CHECK(memcmp(result.adc_residual,ar[fixture%2],sizeof(result.adc_residual))==0);
		if(fixture==14) {
			CHECK(result.dac_residual[0][0]==16 && result.dac_residual[0][1]==0);
			CHECK(result.dac_residual[1][0]==0 && result.dac_residual[1][1]==0);
		} else CHECK(memcmp(result.dac_residual,dr[fixture%2],sizeof(result.dac_residual))==0);
		for(i=0;i<2;i++) for(j=0;j<2;j++) CHECK(result.dac[i][j]==vectors[fixture%2][i][j]);
		if(fixture==10)
			for(i=0;i<2;i++) for(j=0;j<4;j++)
				CHECK(model.sample_reads[i][j]==302);
		/* Keep fault/cancel coverage on the two-attempt convergence path. */
		if(fixture==5) { complete_calls=calls; complete_delays=delays; }
		for(i=0;i<total;i++) {
			CHECK(fgets(line,sizeof(line),fp)!=NULL);
			CHECK(sscanf(line,"%u %x %x",&kind,&addr,&value)==3);
			/* Preserve the reference cache-export tail in the fixture, but
			 * the native cold-only stage ends after its control restore. */
			if(i<cold) {
				/* The native tenth-attempt snapshot is an explicit read-only
				 * diagnostic extension to the unchanged reference trace. */
				if(fixture==12 && model.events[native_i].kind=='r'+4 && model.events[native_i].address==0x1868) {
					const unsigned a[5]={0x1868,0x1c3c,0x1c24,0x1830,0x1860};
					for(j=0;j<5;j++) CHECK(model.events[native_i+j].kind=='r'+4 && model.events[native_i+j].address==a[j]);
					native_i+=5;
				}
				CHECK(model.events[native_i].kind==kind && model.events[native_i].address==addr && model.events[native_i].value==value);
				native_i++;
			}

		}
		CHECK(native_i==model.count);
	}
	CHECK(fgets(line,sizeof(line),fp)==NULL); fclose(fp);
	CHECK(complete_calls!=0 && complete_delays!=0);
	/* Every bus failure and generation change must stop the actual sequence. */
	for(i=1;i<=complete_calls;i++) {
		setup(5); fail_at=i;
		CHECK(rtw8822c_dack(&sc,&result)==EIO);
		CHECK(calls==i && after_error==0 && !result.restored);
		CHECK(result.stage>0 && result.stage<RTW8822C_DACK_DONE);
		if(i==1) CHECK(result.stage==RTW8822C_DACK_SNAPSHOT);
		if(i>=20) CHECK(result.reset_required==1);
		setup(5); cancel_at=i;
		CHECK(rtw8822c_dack(&sc,&result)==EINTR);
		CHECK(calls==i && !result.restored);
	}
	for(i=1;i<=complete_delays;i++) {
		setup(5); cancel_delay=i;
		CHECK(rtw8822c_dack(&sc,&result)==EINTR);
		CHECK(calls==canceled_calls && !result.restored && result.reset_required==1);
	}
	setup(0); model.invalid=1; model.recording=0; check_failure(ERANGE);
	CHECK(model.samples==10000);
	CHECK(result.stage==RTW8822C_DACK_ADC_PRE && result.path==0);
	CHECK(result.sample_count==0 && result.sample_reads==10000 && !result.mean_valid);
	CHECK(model.visits[0][ADC_POST]==0 && model.visits[0][DAC_PRE]==0);
	setup(0); model.unstable=1; model.recording=0; CHECK(rtw8822c_dack(&sc,&result)==0);
	CHECK(result.dac_nonconverged==3 && result.completed_paths==3 && result.restored);
	CHECK(model.sample_reads[0][DAC_POST]==3020 && model.sample_reads[1][DAC_POST]==3020);
	CHECK(model.visits[0][ADC_POST]==1 && model.visits[0][DAC_PRE]==10);
	/* Transient replacement outliers must be skipped, never averaged. */
	setup(10); model.transient_replacement=1;
	CHECK(rtw8822c_dack(&sc,&result)==0 && result.completed_paths==3 && result.restored);
	for(i=0;i<2;i++) {
		CHECK(result.adc[i][0]==0 && result.adc[i][1]==0);
		CHECK(result.dac[i][0]==128 && result.dac[i][1]==128);
		CHECK(result.adc_residual[i][0]==0 && result.adc_residual[i][1]==0);
		CHECK(result.dac_residual[i][0]==0 && result.dac_residual[i][1]==0);
		for(j=0;j<4;j++) CHECK(model.sample_reads[i][j]==303);
	}
	setup(10); model.transient_replacement=1; fail_at=151; check_failure(EIO);
	CHECK(calls==151);
	CHECK(result.stage==RTW8822C_DACK_ADC_PRE && result.sample_reads==102);
	CHECK(model.samples==101 && result.sample_count==100 && !result.mean_valid);
	setup(10); model.transient_replacement=1; cancel_at=151; check_failure(EINTR);
	CHECK(calls==151);
	CHECK(result.stage==RTW8822C_DACK_ADC_PRE && result.sample_reads==102);
	CHECK(model.samples==102 && result.sample_count==100 && !result.mean_valid);
	setup(0); model.replacement_invalid=1; model.recording=0; check_failure(ERANGE);
	CHECK(model.samples==10202);
	CHECK(result.stage==RTW8822C_DACK_ADC_PRE && result.sample_reads==10202);
	CHECK(result.sample_count==100 && result.spread[0]==20 && !result.mean_valid);
	setup(0); model.not_ready=1; model.recording=0; check_failure(ETIMEDOUT);
	setup(0); model.bad_vector=1; model.recording=0; check_failure(ETIMEDOUT);
	setup(0); model.adc_fail=1; model.recording=0; CHECK(rtw8822c_dack(&sc,&result)==0);
	CHECK(result.adc_nonconverged==3 && result.adc_converged==0 && result.restored);
	CHECK(model.visits[0][ADC_POST]==10);
	CHECK(result.stage==RTW8822C_DACK_DONE && result.adc_attempts[0]==10);
	CHECK(result.sample_stage==RTW8822C_DACK_DAC_POST && result.sample_path==1);
	CHECK(result.adc_attempts[1]==10 && result.dac_attempts[0]==1 && result.dac_attempts[1]==1);
	CHECK(result.mean_valid && result.mean[0]==16 && result.mean[1]==16);
	CHECK(result.sample_count==100 && result.sample_reads==100 && result.filter_passes==0);
	CHECK(result.adc_snapshot_valid && !result.adc_snapshot_error && result.adc_snapshot_path==1);
	CHECK(result.adc_snapshot[0]==0 && result.adc_snapshot[1]==0x88103);
	CHECK(result.adc_snapshot[2]==0x10002 && result.adc_snapshot[4]==0xf0040ff0);
	CHECK((result.adc_snapshot[3]&(1U<<30))==0);
	setup(1); model.adc_fail=1; model.recording=0; CHECK(rtw8822c_dack(&sc,&result)==0);
	CHECK(result.adc_before[0][0]==4 && result.adc_before[0][1]==-3);
	CHECK(result.adc[0][0]==1020 && result.adc[0][1]==3);
	CHECK(result.adc_residual[0][0]==8 && result.adc_residual[0][1]==0);
	CHECK(result.adc_snapshot[0]==(5U|(1018U<<10)));
	snapshot_calls=snapshot_call+4; CHECK(snapshot_call!=0);
	for(i=0;i<5;i++) {
		setup(1); model.adc_fail=1; model.recording=0;
		fail_at=snapshot_calls-4+i;
		check_failure(EIO);
		CHECK(calls==fail_at && !result.adc_snapshot_valid);
		CHECK(result.adc_snapshot_error==EIO);
		CHECK(result.stage==RTW8822C_DACK_ADC_POST);
		setup(1); model.adc_fail=1; model.recording=0;
		cancel_at=snapshot_calls-4+i;
		check_failure(EINTR);
		CHECK(calls==cancel_at && !result.adc_snapshot_valid);
		CHECK(result.adc_snapshot_error==EINTR);
	}
	setup(0); model.dac_fail=1; model.recording=0; CHECK(rtw8822c_dack(&sc,&result)==0);
	CHECK(result.dac_nonconverged==3 && result.dac_converged==0 && result.restored);
	CHECK(model.visits[0][DAC_PRE]==10);
	CHECK(result.stage==RTW8822C_DACK_DONE && result.dac_attempts[0]==10);
	setup(0); model.vector_overflow=100; model.recording=0; check_failure(ERANGE);
	CHECK(model.visits[0][DAC_PRE]==1 && result.dac[0][0]==0);
	setup(0); model.vector_overflow=-100; model.recording=0; check_failure(ERANGE);
	CHECK(model.visits[0][DAC_PRE]==1 && result.dac[0][0]==0);
	/* A failed read must not count its zero-filled fallback as a sample. */
	setup(0); fail_measure_kind=ADC_PRE; check_failure(EIO);
	CHECK(result.stage==RTW8822C_DACK_ADC_PRE && result.path==0);
	CHECK(result.sample_reads==1 && result.sample_count==0 && !result.mean_valid);
	/* A later hard fault must not erase an earlier path's quality warning. */
	setup(0); fail_measure_kind=ADC_POST; check_failure(EIO);
	CHECK(!result.adc_measured && !result.dac_measured && !result.adc_converged && !result.dac_converged);
	setup(0); fail_measure_kind=DAC_POST; check_failure(EIO);
	CHECK(result.adc_measured==1 && result.adc_converged==1);
	CHECK(!result.dac_measured && !result.dac_converged && !result.completed_paths);
	setup(12); CHECK(rtw8822c_dack(&sc,&result)==0); warning_b_call=first_b_call; advisory_calls=calls;
	CHECK(warning_b_call!=0 && result.adc_nonconverged==1);
	for(i=0;i<2;i++) {
		setup(12); if(i==0) fail_at=warning_b_call; else cancel_at=warning_b_call;
		check_failure(i==0?EIO:EINTR);
		CHECK(result.adc_nonconverged==1 && result.adc_measured==1 && result.dac_measured==1);
		CHECK(result.completed_paths==1 && !result.restored && result.reset_required);
		CHECK(calls==warning_b_call);
	}
	setup(12); fail_at=advisory_calls; check_failure(EIO);
	CHECK(result.adc_nonconverged==1 && result.completed_paths==3);
	CHECK(result.adc_measured==3 && result.dac_measured==3 && !result.restored);

	/* All local maxima can run together inside the total software budgets. */
	setup(6); model.skip=9900; model.recording=0;
	CHECK(rtw8822c_dack(&sc,&result)==0);
	CHECK(result.completed_paths==3 && result.restored && !result.reset_required);
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
