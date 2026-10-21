/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual RUN worker, caller and core. Kernel services use the startup model. */
#define RTW88_STATE_TX_CALLER 1
#define RTW88_STATE_RA_OBSERVE 1
#define main native_start_main
#include "rtw88-start-contract.c"
#undef main

/* Boundary spy arms bus faults only; the worker and core stay actual. */
static unsigned ra_fault, ra_after, ra_fault_reg, ra_config_calls;
static int
ra_write4(void *cookie, uint32_t reg, uint32_t value)
{
	int e;
	if (ra_fault_reg == reg && !ra_after) return EIO;
	e=state_write4(cookie,reg,value);
	return e==0 && ra_fault_reg==reg ? EIO:e;
}
static int
state_ra_configure(struct rtw88_softc *sc, const struct rtw88_ra_config *cfg)
{
	CHECK(state_lock.held && !state_current_net->policy.accepting);
	ra_config_calls++;
	if (ra_fault==1 || ra_fault==2)
		ra_fault_reg=(ra_fault==1?0x1f0:0x1d0)+4*sc->sc_h2c_box;
	if (ra_fault==3 || ra_fault==4) {
		sc->sc_h2c_box=0; /* Busy-bit service model exposes box0 only. */
		f.busy_box=1;
		if (ra_fault==4) f.stop_at_delay=(int)f.delays+1;
	}
	return rtw88_ra_configure(sc,cfg);
}

static const uint8_t ra_legacy_rates[] = {
	2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108
};
static uint8_t ra_frame[26] = { 0x08, 0x01, 0, 0, 2, 0, 0, 0,
    0, 1, 2, 0, 0, 0, 0, 2, 2, 0, 0, 0, 0, 3, 0x10, 0, 0xaa, 0xbb };
static unsigned ra_parent_data;

static int
ra_parent(struct ieee80211com *ic, enum ieee80211_state state, int arg)
{
	struct rtw88_net *net = ic->ic_ifp->if_softc;
	struct rtw88_softc *sc = net->core;
	unsigned box;

	(void)arg;
	CHECK(!state_lock.held);
	ic->ic_state = state;
	if (state != IEEE80211_S_RUN)
		return 0;
	CHECK(net->policy.accepting && !net->policy.dying);
	box = (sc->sc_h2c_box - 1) & 3;
	CHECK(mmio32(0x1d0 + 4 * box) == UINT32_C(0x40260040));
	CHECK(mmio32(0x1f0 + 4 * box) == UINT32_C(0x00000ff5));
	ra_parent_data++;
	return 0;
}

static void
ra_setup_peer(struct state_fixture *s)
{
	s->net.ic.ic_curmode = IEEE80211_MODE_11G;
	s->net.ic.ic_fixed_rate = IEEE80211_FIXED_RATE_NONE;
	s->net.ic.ic_sup_rates[IEEE80211_MODE_11G].rs_nrates = 12;
	memcpy(s->net.ic.ic_sup_rates[IEEE80211_MODE_11G].rs_rates,
	    ra_legacy_rates, sizeof(ra_legacy_rates));
	s->ni.ni_rates = s->net.ic.ic_sup_rates[IEEE80211_MODE_11G];
	for (unsigned i = 0; i < 4; i++)
		s->ni.ni_rates.rs_rates[i] |= IEEE80211_RATE_BASIC;
	s->ni.ni_associd = 0xc001;
	s->ni.ni_bssid[0] = 2;
	s->ni.ni_chan = s->net.ic.ic_curchan;
}

static void
test_ra_run_to_first_data(void)
{
	struct state_fixture s;
	struct rtw88_softc *sc = &s.sc;
	unsigned slot;
	uint8_t *desc;

	if (!state_fixture_start(&s, 1, 20))
		return;
	ra_setup_peer(&s);
	s.net.parent_state = ra_parent;
	ra_parent_data = 0;
	CHECK(rtw88_net_newstate(&s.net.ic, IEEE80211_S_RUN, -1) == 0);
	rtw88_net_state_work(&s.net.state_work, &s.net);
	CHECK(ra_parent_data == 1 && s.net.policy.accepting);
	slot = sc->sc_txq[RTW88_TXQ_BE].wp;
	CHECK(rtw88_net_tx_submit(&s.net, &s.ni, RTW88_TXQ_BE,
	    ra_frame, sizeof(ra_frame)) == 0);
	desc = sc->sc_txq[RTW88_TXQ_BE].buf[slot].vaddr;
	/* w3 USE_RATE/DISDATAFB are clear only in firmware-RA DATA mode. */
	CHECK((desc[13] & 0x05) == 0);
	CHECK(desc[4] == 0 && desc[6] == 6);

	state_fixture_finish(&s);
}

static void
test_ra_c2h_bounds_and_telemetry(void)
{
	uint8_t report[10] = { 0x0c, 0x44, 0x0b, 0, 0xaa, 0xbb,
	    0xcc, 0xdd, 0, 0xee };

	for (unsigned len = 0; len <= sizeof(report); len++) {
		struct rtw88_softc sc;
		struct rtw88_ra_state saved;
		if (!start_ok(&sc))
			continue;
		saved = sc.sc_ra;
		rx_put(&sc, 0, len, 1, report, 0);
		f.rx_hw_idx = 1;
		set_hisr0(0x01);
		CHECK(rtw88_intr(&sc) == 1);
		CHECK(memcmp(&saved, &sc.sc_ra, sizeof(saved)) == 0);
		CHECK(sc.sc_stats.ra_reports == (len >= 9));
		CHECK(sc.sc_stats.ra_malformed == (len >= 2 && len < 9));
		CHECK(sc.sc_stats.ra_unattributed == (len >= 9));
		CHECK(sc.sc_stats.ra_inactive == (len >= 9));
		CHECK(sc.sc_stats.ra_trailing == (len > 9));
		if (len >= 9) {
			CHECK(sc.sc_stats.ra_report_valid == 1);
			CHECK(sc.sc_stats.ra_report_rate == 11 &&
			    sc.sc_stats.ra_report_macid == 0 &&
			    sc.sc_stats.ra_report_bw == 0);
			CHECK(sc.sc_stats.ra_observed_generation == sc.sc_generation);
		}
		CHECK(rtw88_stop(&sc) == 0);
		CHECK(rtw88_core_detach(&sc) == 0);
	}
	for (unsigned variant = 0; variant < 4; variant++) {
		struct rtw88_softc sc;
		uint8_t p[9] = { 0x0c, 0x22, 11, 0, 0, 0, 0, 0, 0 };
		if (!start_ok(&sc))
			continue;
		if (variant == 0) p[3] = 1; /* Foreign MACID. */
		if (variant == 1) p[2] = 12; /* HT is not configured. */
		if (variant == 2) p[2] |= 0x80; /* SGI is not configured. */
		if (variant == 3) p[8] = 1; /* BW40 is not configured. */
		rx_put(&sc, 0, sizeof(p), 1, p, 0);
		f.rx_hw_idx = 1;
		set_hisr0(1);
		CHECK(rtw88_intr(&sc) == 1);
		CHECK(sc.sc_stats.ra_reports == 0 &&
		    sc.sc_stats.ra_report_valid == 0);
		CHECK(sc.sc_stats.ra_foreign == (variant == 0));
		CHECK(sc.sc_stats.ra_unsupported == (variant != 0));
		CHECK(rtw88_stop(&sc) == 0);
		CHECK(rtw88_core_detach(&sc) == 0);
	}
}

static void
test_ra_masks_and_quarantine(void)
{
	static const struct { unsigned channel, mask, mode, id, wire; } cases[] = {
		{1, 0xf, RTW88_FIXED_11B, 8, 0xf},
		{1, 0xff0, RTW88_FIXED_11G, 7, 0xff0},
		{1, 0xfff, RTW88_FIXED_11G, 6, 0xff5},
		{36, 0xff0, RTW88_FIXED_11A, 7, 0xff0}
	};
	const uint8_t bssid[6] = {2, 0, 0, 0, 0, 1};
	for (unsigned i=0; i<sizeof(cases)/sizeof(cases[0]); i++) {
		struct rtw88_softc sc;
		struct rtw88_ra_config cfg = { .epoch=8, .channel=cases[i].channel,
		    .common_rates=cases[i].mask, .mode=cases[i].mode };
		unsigned box, writes, wp;
		if (!start_ok(&sc)) continue;
		CHECK(rtw88_channel(&sc,cfg.channel)==0);
		CHECK(rtw88_station_config(&sc,bssid,1,cfg.common_rates,
		    cfg.channel>14?9:20,1)==0);
		box=sc.sc_h2c_box;
		sc.sc_edca_error=EIO; writes=f.writes;
		CHECK(rtw88_ra_configure(&sc,&cfg)==EIO);
		CHECK(f.writes==writes && !sc.sc_ra.configured);
		sc.sc_edca_error=0;
		CHECK(rtw88_ra_configure(&sc,&cfg)==0);
		CHECK(mmio32(0x1d0+4*box)==(UINT32_C(0x40000040)|
		    ((cases[i].id|0x20)<<16)));
		CHECK(mmio32(0x1f0+4*box)==cases[i].wire);
		CHECK(sc.sc_ra.mask==cases[i].wire && sc.sc_ra.active);
		wp=sc.sc_txq[RTW88_TXQ_BE].wp;
		CHECK(rtw88_tx_auto(&sc,RTW88_TXQ_BE,ra_frame,sizeof(ra_frame),9,0)==EBUSY);
		sc.sc_generation++;
		CHECK(rtw88_tx_auto(&sc,RTW88_TXQ_BE,ra_frame,sizeof(ra_frame),8,0)==EBUSY);
		sc.sc_generation--;
		CHECK(sc.sc_txq[RTW88_TXQ_BE].wp==wp);
		CHECK(rtw88_tx_auto(&sc,RTW88_TXQ_BE,ra_frame,sizeof(ra_frame),8,0)==0);
		CHECK(sc.sc_stats.tx_ra_encoded==1);
		/* A busy mailbox is uncertain ownership, never a basic fallback. */
		sc.sc_h2c_box=0; /* Busy-bit service model exposes box0 only. */
		f.busy_box=1;
		CHECK(rtw88_ra_configure(&sc,&cfg)==ETIMEDOUT);
		CHECK(!sc.sc_ra.configured && sc.sc_ra.error==ETIMEDOUT);
		CHECK(sc.sc_stats.ra_command_errors==1);
		CHECK(rtw88_tx_available(&sc,RTW88_TXQ_BE)==0);
		writes=f.writes;
		CHECK(rtw88_station_config(&sc,bssid,1,cfg.common_rates,
		    cfg.channel>14?9:20,1)==ETIMEDOUT);
		CHECK(rtw88_ra_configure(&sc,&cfg)==ETIMEDOUT);
		CHECK(rtw88_tx_raw(&sc,RTW88_TXQ_BE,ra_frame,sizeof(ra_frame))==ETIMEDOUT);
		CHECK(rtw88_tx_auto(&sc,RTW88_TXQ_BE,ra_frame,sizeof(ra_frame),8,0)==ETIMEDOUT);
		CHECK(f.writes==writes);
		CHECK(rtw88_stop(&sc)==0);
		CHECK(sc.sc_ra.error==ETIMEDOUT);
		f.busy_box=0;
		CHECK(rtw88_start(&sc)==0 && sc.sc_ra.error==0);
		CHECK(!sc.sc_ra.configured && !sc.sc_stats.ra_report_valid);
		CHECK(rtw88_stop(&sc)==0);
		CHECK(rtw88_core_detach(&sc)==0);
	}
}

static void
test_ra_erp_gate(void)
{
	const uint8_t bssid[6] = {2, 0, 0, 0, 0, 1};
	for (unsigned initial=0; initial<2; initial++) {
		struct rtw88_softc sc;
		struct rtw88_ra_config cfg={.epoch=1,.channel=1,
		    .common_rates=0xfff,.mode=RTW88_FIXED_11G,.use_protection=initial};
		unsigned cmds, slot;
		uint8_t *desc;
		if (!start_ok(&sc)) continue;
		CHECK(rtw88_station_config(&sc,bssid,1,0xf,20,1)==0);
		cmds=sc.sc_h2c_box;
		CHECK(rtw88_ra_configure(&sc,&cfg)==0);
		CHECK(sc.sc_h2c_box==((cmds+!initial)&3));
		slot=sc.sc_txq[RTW88_TXQ_BE].wp;
		CHECK(rtw88_tx_auto(&sc,RTW88_TXQ_BE,ra_frame,sizeof(ra_frame),1,1)==0);
		desc=sc.sc_txq[RTW88_TXQ_BE].buf[slot].vaddr;
		CHECK((desc[13]&5)==5 && (desc[16]&0x7f)==0);
		CHECK(!sc.sc_ra.active && sc.sc_ra.protection_limited);
		CHECK(sc.sc_stats.tx_ra_protection_limited==1);
		CHECK(rtw88_tx_auto(&sc,RTW88_TXQ_BE,ra_frame,sizeof(ra_frame),1,0)==0);
		CHECK(sc.sc_stats.tx_ra_encoded==0 && sc.sc_stats.tx_ra_protection_limited==2);
		sc.sc_basic_rates=0x10;
		slot=sc.sc_txq[RTW88_TXQ_BE].wp;
		CHECK(rtw88_tx_auto(&sc,RTW88_TXQ_BE,ra_frame,sizeof(ra_frame),1,1)==EOPNOTSUPP);
		CHECK(sc.sc_txq[RTW88_TXQ_BE].wp==slot);
		CHECK(rtw88_stop(&sc)==0);
		CHECK(rtw88_core_detach(&sc)==0);
	}
}

static void
test_ra_worker_failures(void)
{
	for (unsigned fault=1; fault<=4; fault++)
	for (unsigned after=0; after<(fault<=2?2U:1U); after++) {
		struct state_fixture s;
		int expected=fault<=2?EIO:(fault==3?ETIMEDOUT:EINTR);
		if (!state_fixture_start(&s,1,20)) continue;
		ra_setup_peer(&s);
		s.net.parent_state=ra_parent;
		s.bus.write_4=ra_write4;
		ra_fault=fault; ra_after=after; ra_fault_reg=0; ra_config_calls=0;
		ra_parent_data=0;
		CHECK(rtw88_net_newstate(&s.net.ic,IEEE80211_S_RUN,-1)==0);
		rtw88_net_state_work(&s.net.state_work,&s.net);
		CHECK(ra_config_calls==1 && ra_parent_data==0);
		CHECK(!s.net.policy.accepting && !s.net.work_pending);
		CHECK(s.sc.sc_ra.error==expected && !s.sc.sc_running);
		CHECK(!s.sc.sc_ra.configured && s.sc.sc_stats.ra_command_errors==1);
		CHECK(strstr(state_diag,"stage 5 ")!=NULL);
		CHECK(rtw88_tx_available(&s.sc,RTW88_TXQ_BE)==0);
		ra_fault=ra_fault_reg=0; f.busy_box=0; f.stop_at_delay=0;
		state_fixture_finish(&s);
	}
}

static void
test_ra_peer_numeric_masks(void)
{
	struct state_fixture s;
	struct rtw88_ra_config cfg;
	if (!state_fixture_start(&s,1,20)) return;
	ra_setup_peer(&s);
	/* Reordered peer subset, BASIC flags and duplicate numeric rate. */
	s.ni.ni_rates.rs_nrates=4;
	s.ni.ni_rates.rs_rates[0]=108;
	s.ni.ni_rates.rs_rates[1]=2|IEEE80211_RATE_BASIC;
	s.ni.ni_rates.rs_rates[2]=24;
	s.ni.ni_rates.rs_rates[3]=24;
	CHECK(rtw88_net_ra_prepare(&s.net.ic,1,1,99,&cfg)==0);
	CHECK(cfg.common_rates==0x841 && cfg.epoch==99);
	CHECK(rtw88_net_ra_prepare(&s.net.ic,1,2,99,&cfg)==EINVAL);
	s.ni.ni_chan=NULL;
	CHECK(rtw88_net_ra_prepare(&s.net.ic,1,1,99,&cfg)==EINVAL);
	s.ni.ni_chan=s.net.ic.ic_curchan;
	s.ni.ni_associd=0;
	CHECK(rtw88_net_ra_prepare(&s.net.ic,1,1,99,&cfg)==EINVAL);
	s.ni.ni_associd=2008;
	CHECK(rtw88_net_ra_prepare(&s.net.ic,1,1,99,&cfg)==EINVAL);
	s.ni.ni_associd=1;
	CHECK(rtw88_net_ra_prepare(&s.net.ic,1,1,0,&cfg)==EINVAL);
	s.ni.ni_rates.rs_nrates=255;
	CHECK(rtw88_net_ra_prepare(&s.net.ic,1,1,99,&cfg)==EINVAL);
	state_fixture_finish(&s);
}

static void
test_ra_reports_never_control_peer(void)
{
	struct rtw88_softc sc;
	const uint8_t bssid[6]={2,0,0,0,0,1};
	struct rtw88_ra_config cfg={.epoch=1,.channel=1,.common_rates=0xfff,
	    .mode=RTW88_FIXED_11G};
	struct rtw88_ra_state saved;
	uint8_t report[9]={0x0c,0xff,11,0,0,0,0,0,0};
	uint8_t packet[26];
	unsigned wp;
	if (!start_ok(&sc)) return;
	CHECK(rtw88_station_config(&sc,bssid,1,0xf,20,1)==0);
	CHECK(rtw88_ra_configure(&sc,&cfg)==0);
	/* Reuse MACID0 for a narrower peer, without inventing a wire token. */
	CHECK(rtw88_station_config(&sc,bssid,2,0xf,20,1)==0);
	cfg.epoch=2; cfg.mode=RTW88_FIXED_11B; cfg.common_rates=0xf;
	CHECK(rtw88_ra_configure(&sc,&cfg)==0);
	saved=sc.sc_ra;
	rx_put(&sc,0,sizeof(report),1,report,0);f.rx_hw_idx=1;set_hisr0(1);
	CHECK(rtw88_intr(&sc)==1);
	CHECK(memcmp(&saved,&sc.sc_ra,sizeof(saved))==0);
	CHECK(sc.sc_stats.ra_reports==1 && sc.sc_stats.ra_unattributed==1);
	CHECK(sc.sc_stats.ra_outside_mask==1 && sc.sc_stats.ra_report_rate==11);
	CHECK(sc.sc_stats.tx_done==0); /* A firmware report is not an ACK. */
	CHECK(rtw88_tx_auto(&sc,RTW88_TXQ_BE,ra_frame,sizeof(ra_frame),2,0)==0);
	wp=sc.sc_txq[RTW88_TXQ_BE].wp;
	for (unsigned variant=0; variant<5; variant++) {
		memcpy(packet,ra_frame,sizeof(packet));
		if (variant==0) packet[0]=0x48; /* Null. */
		if (variant==1) packet[4]|=1; /* Group receiver. */
		if (variant==2) packet[16]|=1; /* Group Ethernet DA. */
		if (variant==3) packet[1]=3; /* Four addresses. */
		if (variant==4) packet[1]|=0x80; /* HT control. */
		CHECK(rtw88_tx_auto(&sc,RTW88_TXQ_BE,packet,sizeof(packet),2,0)==EOPNOTSUPP);
		CHECK(sc.sc_txq[RTW88_TXQ_BE].wp==wp);
	}
	CHECK(rtw88_stop(&sc)==0);
	CHECK(rtw88_core_detach(&sc)==0);
}

int
main(void)
{
	if (native_start_main() != 0)
		return 1;
	test_ra_run_to_first_data();
	test_ra_c2h_bounds_and_telemetry();
	test_ra_masks_and_quarantine();
	test_ra_erp_gate();
	test_ra_worker_failures();
	test_ra_peer_numeric_masks();
	test_ra_reports_never_control_peer();
	printf("rtw88 operational legacy RA: %u assertions, %u failures\n",
	    assertions, failures);
	return failures != 0;
}
