/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual RUN, state requests, worker and core; service models are explicit. */
#define RTW88_RA_MAIN legacy_ra_main
#include "rtw88-ra-contract.c"
#undef RTW88_RA_MAIN

/* Leave/notification is external to this restart fault model. The focused
 * manual-scan contract executes the actual shared and userland consumers. */
static void ieee80211_sta_leave(struct ieee80211com *ic,
    struct ieee80211_node *ni) { (void)ic; (void)ni; }
#include "native-reconnect-scan.inc"

/* Shared parent state outcome is modeled; its queue ownership consumer is actual. */
static int
reconnect_parent(struct ieee80211com *ic,enum ieee80211_state state,int arg)
{
	struct rtw88_net *net=ic->ic_ifp->if_softc;
	struct rtw88_softc *sc=net->core;
	(void)arg;CHECK(!state_lock.held);ic->ic_state=state;
	if (state==IEEE80211_S_INIT) ieee80211_drain_ifq(&ic->ic_mgtq);
	if (state==IEEE80211_S_RUN) {
		unsigned box=(sc->sc_h2c_box-1)&3;
		CHECK(net->policy.accepting && !net->policy.dying);
		CHECK(mmio32(0x1d0+4*box)==(sc->sc_channel>14?UINT32_C(0x40270040):UINT32_C(0x40260040)));
		CHECK(mmio32(0x1f0+4*box)==(sc->sc_channel>14?0xff0U:0xff5U));
		ra_parent_data++;
	}
	return 0;
}
static void
reconnect_setup_peer(struct state_fixture *s,unsigned band)
{
	ra_setup_peer(s);
	if (band) {
		s->net.ic.ic_curmode=IEEE80211_MODE_11A;
		s->net.ic.ic_sup_rates[IEEE80211_MODE_11A].rs_nrates=8;
		memcpy(s->net.ic.ic_sup_rates[IEEE80211_MODE_11A].rs_rates,ra_legacy_rates+4,8);
		s->ni.ni_rates=s->net.ic.ic_sup_rates[IEEE80211_MODE_11A];
		for (unsigned i=0;i<5;i+=2) s->ni.ni_rates.rs_rates[i]|=IEEE80211_RATE_BASIC;
	}
	s->net.parent_state=reconnect_parent;
}

static unsigned reconnect_reset_writes;
static int
reconnect_write4(void *cookie,uint32_t reg,uint32_t value)
{
	int e=state_write4(cookie,reg,value);
	if (e==0 && reg==0x039c && value==UINT32_MAX) {
		/* Documented DMA reset service model, not a completed RX frame. */
		f.rx_hw_idx=0;
		memset(f.tx_hw_rp,0,sizeof(f.tx_hw_rp));
		reconnect_reset_writes++;
	}
	return e;
}
static int
reconnect_bus_master(void *cookie,int enable)
{
	CHECK(!state_lock.held);
	if (enable) {
		CHECK(state_current_net->ic.ic_mgtq.head==NULL);
		for (unsigned q=0;q<RTW88_NTXQ;q++)
			CHECK(state_current_net->core->sc_txq[q].queued==0);
	}
	return bus_master(cookie,enable);
}

static void
test_reconnect_coalesced_peer_barrier(void)
{
	struct state_fixture s;
	unsigned loads, generation;
	if (!state_fixture_start(&s,1,20)) return;
	reconnect_setup_peer(&s,0);
	CHECK(rtw88_net_newstate(&s.net.ic,IEEE80211_S_RUN,-1)==0);
	rtw88_net_state_work(&s.net.state_work,&s.net);
	CHECK(s.sc.sc_linked && s.net.policy.accepting);
	const unsigned queues[4]={RTW88_TXQ_BE,RTW88_TXQ_BK,RTW88_TXQ_VI,RTW88_TXQ_VO};
	const uint8_t tids[4]={0,1,5,6};
	uint8_t data[26];
	for (unsigned ac=0;ac<4;ac++) {
		memcpy(data,ra_frame,sizeof(data));data[0]=0x88;data[24]=tids[ac];
		CHECK(rtw88_net_tx_submit(&s.net,&s.ni,queues[ac],data,sizeof(data))==0);
		CHECK(s.sc.sc_txq[queues[ac]].queued==1);
	}
	s.sc.sc_rx_rp=s.sc.sc_rx_tag=7;f.rx_hw_idx=8;
	rx_put(&s.sc,7,26,1,data,0);
	s.bus.write_4=reconnect_write4;s.bus.bus_master=reconnect_bus_master;
	reconnect_reset_writes=0;
	s.net.tx_timer=5;s.net.tx_completed=3;
	/* Two queued old management contexts each own one node reference. */
	s.ni.references=3;
	struct mbuf *first=calloc(1,sizeof(*first)), *second=calloc(1,sizeof(*second));
	assert(first && second);
	first->ctx=second->ctx=&s.ni;first->next=second;
	s.net.ic.ic_mgtq.head=first;
	unsigned freed=state_mbuf_frees, unsafe=state_mbuf_free_while_master;
	loads=f.firmware_loads;generation=s.sc.sc_generation;
	/* Latest-request coalescing must not lose the old hardware peer. */
	CHECK(rtw88_net_newstate(&s.net.ic,IEEE80211_S_INIT,-1)==0);
	CHECK(rtw88_net_newstate(&s.net.ic,IEEE80211_S_SCAN,-1)==0);
	rtw88_net_state_work(&s.net.state_work,&s.net);
	CHECK(f.firmware_loads==loads+1 && s.sc.sc_generation>generation);
	CHECK(s.sc.sc_txq[RTW88_TXQ_BE].queued==0);
	CHECK(s.sc.sc_stats.tx_dropped==4 && s.sc.sc_stats.tx_done==0);
	CHECK(reconnect_reset_writes>=1 && s.sc.sc_rx_rp==0 && s.sc.sc_rx_tag==0);
	CHECK(f.rx_hw_idx==0 && host_rx_idx()==0);
	CHECK(s.net.tx_timer==0 && s.net.tx_completed==3);
	CHECK(s.net.ic.ic_mgtq.head==NULL && s.ni.references==1);
	CHECK(state_mbuf_frees==freed+2 && state_mbuf_free_while_master==unsafe);
	CHECK(s.net.ic.ic_state==IEEE80211_S_SCAN);
	CHECK((s.net.ec.ec_if.if_flags&IFF_RUNNING)!=0 && s.sc.sc_started);
	loads=f.firmware_loads;
	CHECK(rtw88_net_newstate(&s.net.ic,IEEE80211_S_SCAN,-1)==0);
	rtw88_net_state_work(&s.net.state_work,&s.net);
	CHECK(f.firmware_loads==loads); /* No full restart on every scan channel. */
	/* Clean up historical RED ownership too; no leaked test allocations. */
	ieee80211_drain_ifq(&s.net.ic.ic_mgtq);
	state_fixture_finish(&s);
}
static void
test_reconnect_init_ready_scan(void)
{
	for (unsigned band=0; band<2; band++)
	for (unsigned down=0; down<2; down++) {
		struct state_fixture s;
		unsigned loads;
		if (!state_fixture_start(&s,band?36:1,band?9:20)) continue;
		reconnect_setup_peer(&s,band);
		CHECK(rtw88_net_newstate(&s.net.ic,IEEE80211_S_RUN,-1)==0);
		rtw88_net_state_work(&s.net.state_work,&s.net);
		loads=f.firmware_loads;
		if (down) s.net.ec.ec_if.if_flags&=~IFF_UP;
		CHECK(rtw88_net_newstate(&s.net.ic,IEEE80211_S_INIT,-1)==0);
		rtw88_net_state_work(&s.net.state_work,&s.net);
		CHECK(s.net.ic.ic_state==IEEE80211_S_INIT && !s.net.policy.accepting);
		CHECK(!s.net.work_pending && !s.sc.sc_linked);
		CHECK(f.firmware_loads==loads+!down);
		CHECK(s.sc.sc_started==!down);
		CHECK(((s.net.ec.ec_if.if_flags&IFF_RUNNING)!=0)==!down);
		CHECK(reconnect_scan_request(&s.net.ic)==(down?EINVAL:0));
		if (!down) {
			rtw88_net_state_work(&s.net.state_work,&s.net);
			CHECK(s.net.ic.ic_state==IEEE80211_S_SCAN && s.net.policy.accepting);
			CHECK(f.firmware_loads==loads+1);
			/* Reassociation publishes a new RA capsule after the barrier. */
			reconnect_setup_peer(&s,band);
			CHECK(rtw88_net_newstate(&s.net.ic,IEEE80211_S_RUN,-1)==0);
			rtw88_net_state_work(&s.net.state_work,&s.net);
			CHECK(s.sc.sc_linked && s.sc.sc_ra.active && s.net.policy.accepting);
			CHECK(rtw88_net_tx_submit(&s.net,&s.ni,RTW88_TXQ_BE,
			    ra_frame,sizeof(ra_frame))==0);
		}
		state_fixture_finish(&s);
	}
}

static void
test_reconnect_core_failures(void)
{
	for (unsigned fault=0;fault<5;fault++) {
		struct state_fixture s;
		unsigned loads;
		if (!state_fixture_start(&s,1,20)) continue;
		reconnect_setup_peer(&s,0);
		CHECK(rtw88_net_newstate(&s.net.ic,IEEE80211_S_RUN,-1)==0);
		rtw88_net_state_work(&s.net.state_work,&s.net);
		loads=f.firmware_loads;
		struct mbuf *old=calloc(1,sizeof(*old));assert(old);
		s.ni.references=2;old->ctx=&s.ni;s.net.ic.ic_mgtq.head=old;
		unsigned freed=state_mbuf_frees;
		if (fault==0) f.off_error=1;
		if (fault==1) f.disable_error=EIO;
		if (fault==2) f.fw_error=ENOENT;
		if (fault==3) f.fail_alloc_at=f.alloc_calls+1;
		if (fault==4) f.fw_never_ready=1;
		CHECK(rtw88_net_newstate(&s.net.ic,IEEE80211_S_SCAN,-1)==0);
		rtw88_net_state_work(&s.net.state_work,&s.net);
		CHECK(!s.net.policy.accepting && !s.net.work_pending);
		CHECK((s.net.ec.ec_if.if_flags&(IFF_RUNNING|IFF_OACTIVE))==0);
		CHECK(!s.sc.sc_running && !s.sc.sc_started && s.net.reset_required);
		CHECK(strstr(state_diag,"stage 6 ")!=NULL);
		CHECK(s.net.ic.ic_mgtq.head==NULL && s.ni.references==1);
		CHECK(state_mbuf_frees==freed+1);
		CHECK(rtw88_tx_available(&s.sc,RTW88_TXQ_BE)==0);
		if (fault<2) {
			CHECK(f.firmware_loads==loads);
			CHECK(rtw88_start(&s.sc)==EBUSY);
		}
		f.off_error=f.disable_error=f.fw_error=f.fw_never_ready=0;
		f.fail_alloc_at=0;
		state_fixture_finish(&s);
	}
}

/* Inject a newer request at actual bus lifecycle boundaries, not a fake stop. */
static unsigned reconnect_event;
static void
reconnect_interrupt_barrier(void *cookie)
{
	intr_barrier(cookie);
	CHECK(!state_lock.held);
	if (reconnect_event==1) {
		reconnect_event=0;
		CHECK(rtw88_net_newstate(&state_current_net->ic,IEEE80211_S_INIT,-1)==0);
	}
}
static int
reconnect_firmware_load(void *cookie,const char *driver,const char *name,
    const uint8_t **data,size_t *len)
{
	int e=firmware_load(cookie,driver,name,data,len);
	CHECK(!state_lock.held);
	if (reconnect_event==2) {
		reconnect_event=0;
		CHECK(rtw88_net_newstate(&state_current_net->ic,IEEE80211_S_INIT,-1)==0);
	} else if (reconnect_event==4) {
		reconnect_event=0;
		state_current_net->ec.ec_if.if_flags&=~IFF_UP;
		CHECK(rtw88_net_newstate(&state_current_net->ic,IEEE80211_S_INIT,-1)==0);
	} else if (reconnect_event==3) {
		reconnect_event=0;
		state_current_net->policy.dying=1;
	}
	return e;
}
static void
test_reconnect_cancel_boundaries(void)
{
	for (unsigned event=1;event<=4;event++) {
		struct state_fixture s;
		unsigned loads,parents;
		if (!state_fixture_start(&s,1,20)) continue;
		reconnect_setup_peer(&s,0);
		CHECK(rtw88_net_newstate(&s.net.ic,IEEE80211_S_RUN,-1)==0);
		rtw88_net_state_work(&s.net.state_work,&s.net);
		loads=f.firmware_loads;parents=ra_parent_data;
		s.bus.intr_barrier=reconnect_interrupt_barrier;
		s.bus.firmware_load=reconnect_firmware_load;
		reconnect_event=event;
		CHECK(rtw88_net_newstate(&s.net.ic,IEEE80211_S_SCAN,-1)==0);
		rtw88_net_state_work(&s.net.state_work,&s.net);
		CHECK(reconnect_event==0 && ra_parent_data==parents);
		CHECK(!s.net.policy.accepting && !s.net.work_pending);
		CHECK(f.firmware_loads==loads+1);
		if (event>=3) {
			CHECK(!s.sc.sc_running && !s.sc.sc_started);
			CHECK((s.net.ec.ec_if.if_flags&IFF_RUNNING)==0);
		} else {
			CHECK(s.net.ic.ic_state==IEEE80211_S_INIT && s.sc.sc_started);
			CHECK((s.net.ec.ec_if.if_flags&IFF_RUNNING)!=0);
		}
		s.bus.intr_barrier=intr_barrier;s.bus.firmware_load=firmware_load;
		state_fixture_finish(&s);
	}
}

static void
test_reconnect_run_replacement(void)
{
	struct state_fixture s;
	unsigned loads,parents;
	if (!state_fixture_start(&s,1,20)) return;
	reconnect_setup_peer(&s,0);
	CHECK(rtw88_net_newstate(&s.net.ic,IEEE80211_S_RUN,-1)==0);
	rtw88_net_state_work(&s.net.state_work,&s.net);
	loads=f.firmware_loads;parents=ra_parent_data;
	/* Empty host rings still do not prove that old firmware TX is empty. */
	CHECK(rtw88_net_newstate(&s.net.ic,IEEE80211_S_RUN,-1)==0);
	rtw88_net_state_work(&s.net.state_work,&s.net);
	CHECK(f.firmware_loads==loads+1 && ra_parent_data==parents+1);
	CHECK(s.sc.sc_ra.active && s.net.policy.accepting && !s.net.reset_required);
	CHECK(rtw88_net_tx_submit(&s.net,&s.ni,RTW88_TXQ_BE,
	    ra_frame,sizeof(ra_frame))==0);
	state_fixture_finish(&s);
}

static void
test_reconnect_external_stop_owner(void)
{
	struct state_fixture s;
	unsigned loads;
	if (!state_fixture_start(&s,1,20)) return;
	reconnect_setup_peer(&s,0);
	CHECK(rtw88_net_newstate(&s.net.ic,IEEE80211_S_RUN,-1)==0);
	rtw88_net_state_work(&s.net.state_work,&s.net);
	loads=f.firmware_loads;
	/* net_stop's synchronous owner clears RUNNING before waiting for work. */
	CHECK(rtw88_net_newstate(&s.net.ic,IEEE80211_S_INIT,-1)==0);
	s.net.ec.ec_if.if_flags&=~IFF_RUNNING;
	rtw88_net_state_work(&s.net.state_work,&s.net);
	CHECK(f.firmware_loads==loads && !s.sc.sc_started && !s.sc.sc_running);
	CHECK(s.net.ic.ic_state==IEEE80211_S_INIT && !s.net.policy.accepting);
	CHECK((s.net.ec.ec_if.if_flags&IFF_RUNNING)==0);
	state_fixture_finish(&s);
}

int
main(void)
{
	if (legacy_ra_main()!=0) return 1;
	test_reconnect_coalesced_peer_barrier();
	test_reconnect_init_ready_scan();
	test_reconnect_core_failures();
	test_reconnect_cancel_boundaries();
	test_reconnect_run_replacement();
	test_reconnect_external_stop_owner();
	printf("rtw88 safe reconnect: %u assertions, %u failures\n",assertions,failures);
	return failures!=0;
}
