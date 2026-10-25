/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD native RTL8822CE RA and TX contracts, AI-assisted. */
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
/* Managed worker faults occur after the station commands, without changing
 * the reservation's box sequence. Unmanaged core timeout tests stay below. */
static int
ra_read1(void *cookie, uint32_t reg, uint8_t *value)
{
	int error = read_1(cookie, reg, value);
	if (error == 0 && reg == 0x1cc && ra_config_calls != 0) {
		if (ra_fault == 3)
			*value |= (uint8_t)(1U << f.sc->sc_h2c_box);
		if (ra_fault == 4)
			f.sc->sc_generation++;
	}
	return error;
}
static int
state_ra_configure(struct rtw88_softc *sc, const struct rtw88_ra_config *cfg)
{
	CHECK(state_lock.held && !state_current_net->policy.accepting);
	ra_config_calls++;
	if (ra_fault==1 || ra_fault==2)
		ra_fault_reg=(ra_fault==1?0x1f0:0x1d0)+4*sc->sc_h2c_box;
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
		if (variant == 1) p[2] = 20; /* MCS8 is outside the bounded 1SS parser. */
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
		int expected=fault<=2?EIO:(fault==3?EBUSY:ESTALE);
		if (!state_fixture_start(&s,1,20)) continue;
		ra_setup_peer(&s);
		s.net.parent_state=ra_parent;
		s.bus.write_4=ra_write4;
		s.bus.read_1=ra_read1;
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
	uint8_t report[9]={0x0c,0xff,19,0,0,0,0,0,0};
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
	CHECK(sc.sc_stats.ra_outside_mask==1 && sc.sc_stats.ra_report_rate==19);
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

/* Typed core boundary. Peer negotiation and precrypto ownership are exercised
 * by the actual association/worker producer in the enable contract. */
static void
test_ra_ht20_numeric(void)
{
	const uint8_t bssid[6]={2,0,0,0,0,1};
	for (unsigned variant=0; variant<17; variant++) {
		struct rtw88_softc sc;
		struct rtw88_ra_config cfg={.epoch=9,.channel=36,.common_rates=0xff0,
		    .mode=RTW88_FIXED_11A,.phy=RTW88_RA_HT20_1SS,
		    .ht_mcs=0xff,.profile_generation=777,
		    .ht={.peer_cap=0x0c,.mtu=1500,.max_mpdu=1550,.rts_threshold=2346,
		        .qualification=RTW88_HT20_PROT_MF20_NO_RTS}};
		if (!start_ok(&sc)) continue;
		CHECK(rtw88_channel(&sc,36)==0);
		CHECK(rtw88_station_config(&sc,bssid,1,0x150,9,1)==0);
		unsigned box=sc.sc_h2c_box,writes=f.writes;
		struct rtw88_ra_state saved=sc.sc_ra;
		switch(variant) {
		case 1: cfg.ht_mcs=0x25; break;
		case 2: cfg.common_rates=0x150; break;
		case 3: cfg.phy=3; break; /* Undefined in every build. */
		case 4: cfg.ht_mcs=0; break;
		case 5: cfg.ht_mcs=0x100; break;
		case 6: cfg.profile_generation=0; break;
		case 7: cfg.ht_operation=1; break;
		case 8: cfg.use_protection=1; break;
		case 9: cfg.mode=RTW88_FIXED_11G; break;
		case 10: cfg.phy=RTW88_RA_LEGACY; break;
		case 11: cfg.channel=1; break;
		case 12: cfg.epoch=0; break;
		case 13: cfg.common_rates=0; break;
		case 14: cfg.common_rates|=1; break;
		case 15: sc.sc_h2c_box=0; f.busy_box=1; break;
		case 16: sc.sc_edca_error=EIO; break;
		}
		int error=rtw88_ra_configure(&sc,&cfg);
		if (variant<3) {
			unsigned mask=(unsigned)cfg.ht_mcs<<12|(cfg.common_rates&0x30);
			CHECK(error==0 && sc.sc_ra.active && sc.sc_ra.rate_id==5);
			CHECK(sc.sc_ra.mask==mask && sc.sc_ra.common_rates==cfg.common_rates);
			CHECK(sc.sc_basic_rates==0x150); /* BASIC is not the MCS mask. */
			CHECK(mmio32(0x1d0+4*box)==UINT32_C(0x40250040));
			CHECK(mmio32(0x1f0+4*box)==mask);
			CHECK(sc.sc_ra.profile_generation==777 && sc.sc_ra.ht_mcs==cfg.ht_mcs);
			saved=sc.sc_ra;
			{
				struct rtw88_tx_fixed fixed = {
					.associated = 1, .channel = 36, .rate500 = 12,
					.peer_mask = 0xff0, .mode = RTW88_FIXED_11A
				};
				uint8_t mgmt[24] = { 0x80 };
				uint8_t qos[26] = { 0x88, 0x01 };
				uint8_t before[RTW8822C_TX_PACKET_DESC_SIZE];
				struct rtw88_stats stats_before = sc.sc_stats;
				unsigned wp = sc.sc_txq[RTW88_TXQ_BE].wp;
				unsigned queued = sc.sc_txq[RTW88_TXQ_BE].queued;
				unsigned syncs = f.syncs, writes = f.writes;
				void *slot = sc.sc_txq[RTW88_TXQ_BE].buf[wp].vaddr;

				memcpy(before, slot, sizeof(before));
				sc.sc_phy.channel.width_mhz = 40;
				CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT,
				    mgmt, sizeof(mgmt)) == EOPNOTSUPP);
				CHECK(rtw88_tx_fixed(&sc, RTW88_TXQ_BE, ra_frame,
				    sizeof(ra_frame), &fixed) == EOPNOTSUPP);
				CHECK(rtw88_tx_auto(&sc, RTW88_TXQ_BE, qos,
				    sizeof(qos), 9, 0) == EOPNOTSUPP);
				CHECK(sc.sc_txq[RTW88_TXQ_BE].wp == wp &&
				    sc.sc_txq[RTW88_TXQ_BE].queued == queued);
				CHECK(f.syncs == syncs && f.writes == writes);
				CHECK(memcmp(before, slot, sizeof(before)) == 0);
				CHECK(memcmp(&stats_before, &sc.sc_stats,
				    sizeof(stats_before)) == 0);
				sc.sc_phy.channel.width_mhz = 20;
			}
			unsigned wp=sc.sc_txq[RTW88_TXQ_BE].wp;
			CHECK(rtw88_tx_auto(&sc,RTW88_TXQ_BE,ra_frame,sizeof(ra_frame),9,1)==EOPNOTSUPP);
			CHECK(sc.sc_txq[RTW88_TXQ_BE].wp==wp && memcmp(&saved,&sc.sc_ra,sizeof(saved))==0);
			sc.sc_ra=saved; /* Keep later report cases independent of this check. */
			CHECK(rtw88_tx_auto(&sc,RTW88_TXQ_BE,ra_frame,sizeof(ra_frame),9,0)==0);
			for(unsigned rate=12;rate<=19;rate++) {
				uint8_t p[9]={0x0c,0x22,(uint8_t)rate,0,0,0,0,0,0};
				saved=sc.sc_ra;
				unsigned observed=sc.sc_stats.ra_reports;
				rx_put(&sc,rate-12,sizeof(p),1,p,(uint16_t)(rate-12));f.rx_hw_idx=rate-11;set_hisr0(1);
				CHECK(rtw88_intr(&sc)==1);
				CHECK(sc.sc_stats.ra_reports==observed+1 && sc.sc_stats.ra_report_rate==rate);
				CHECK(sc.sc_stats.ra_report_bw==0 && sc.sc_stats.ra_unattributed==rate-11);
				CHECK(memcmp(&saved,&sc.sc_ra,sizeof(saved))==0 && sc.sc_stats.tx_done==0);
			}
		} else if (variant==15) {
			CHECK(error==ETIMEDOUT && sc.sc_ra.error==ETIMEDOUT);
			CHECK(!sc.sc_ra.configured && !sc.sc_ra.active);
			writes=f.writes;
			CHECK(rtw88_tx_available(&sc,RTW88_TXQ_BE)==0);
			CHECK(rtw88_ra_configure(&sc,&cfg)==ETIMEDOUT && f.writes==writes);
			CHECK(rtw88_tx_auto(&sc,RTW88_TXQ_BE,ra_frame,sizeof(ra_frame),9,0)==ETIMEDOUT);
			CHECK(rtw88_stop(&sc)==0);f.busy_box=0;
			CHECK(rtw88_start(&sc)==0 && sc.sc_ra.error==0 && !sc.sc_ra.configured);
		} else {
			CHECK(error!=0 && f.writes==writes && sc.sc_h2c_box==box);
			CHECK(memcmp(&saved,&sc.sc_ra,sizeof(saved))==0);
		}
		CHECK(rtw88_stop(&sc)==0);
		CHECK(rtw88_core_detach(&sc)==0);
	}
}

static void
test_ra_ht20_protection_bounds(void)
{
 const uint8_t bssid[6]={2,0,0,0,0,1};
 for(unsigned kind=0;kind<28;kind++) {
  struct rtw88_softc sc;
  struct rtw88_ra_config cfg={.epoch=9,.channel=36,.common_rates=0xff0,
   .mode=RTW88_FIXED_11A,.phy=RTW88_RA_HT20_1SS,.ht_mcs=0xff,
   .ht_operation=6,.profile_generation=777,
   .ht={.peer_cap=0x0c,.mtu=1500,.max_mpdu=1550,.rts_threshold=2346,
    .secondary=5,.qualification=RTW88_HT20_PROT_MF20_NO_RTS}};
  if(!start_ok(&sc))continue;
  CHECK(rtw88_channel(&sc,36)==0);
  CHECK(rtw88_station_config(&sc,bssid,1,0x150,9,1)==0);
  switch(kind) {
  case 1: cfg.ht_operation=0;cfg.ht.secondary=0;break;
  case 2: cfg.ht.secondary=7;break;
  case 3: cfg.ht.mtu=32;cfg.ht.max_mpdu=82;cfg.ht.rts_threshold=86;break;
  case 4: cfg.ht.rts_threshold=1554;break;
  case 5: cfg.ht.peer_cap=0x09ef;break;
  case 6: cfg.ht.qualification=0;break;
  case 7: cfg.ht.peer_cap|=0x10;break;
  case 8: cfg.ht.peer_cap&=~0x0c;break;
  case 9: cfg.ht.peer_cap=(cfg.ht.peer_cap&~0x0c)|4;break;
  case 10: cfg.ht.peer_cap=(cfg.ht.peer_cap&~0x0c)|8;break;
  case 11: cfg.ht.secondary=2;break;
  case 12: cfg.ht.secondary=4;break;
  case 13: cfg.ht.secondary=1;break;
  case 14: cfg.ht.mtu=31;break;
  case 15: cfg.ht.mtu=1501;cfg.ht.max_mpdu=1551;break;
  case 16: cfg.ht.max_mpdu=1551;break;
  case 17: cfg.ht.rts_threshold=1553;break;
  case 18: cfg.ht.rts_threshold=2347;break;
  case 19: cfg.ht_operation=2;break;
  case 20: cfg.ht_operation=3;break;
  case 21: cfg.ht_operation=7;break;
  case 22: cfg.use_protection=1;break;
  case 23: cfg.ht.max_mpdu=4049;break;
  case 24: cfg.ht.qualification=2;break;
  case 25: cfg.phy=RTW88_RA_LEGACY;break;
  case 26: cfg.ht_operation=0x17;break; /* OBSS non-HT STAs present. */
  case 27: cfg.ht_operation=5;break; /* Non-member protection. */
  }
  unsigned writes=f.writes,box=sc.sc_h2c_box;
  struct rtw88_ra_state saved=sc.sc_ra;
  int error=rtw88_ra_configure(&sc,&cfg);
  /* Non-HT mixed mode 3 (and 7, with non-GF) is accepted like upstream. */
  if(kind<6 || kind==20 || kind==21) {
   CHECK(error==0 && sc.sc_ra.active && sc.sc_ra.ht_operation==cfg.ht_operation);
   CHECK(sc.sc_ra.ht.max_mpdu==cfg.ht.max_mpdu && sc.sc_ra.ht.mtu==cfg.ht.mtu);
   uint8_t packet[4096]={0};packet[0]=0x88;packet[1]=1;packet[4]=2;packet[16]=2;
   unsigned wp=sc.sc_txq[RTW88_TXQ_BE].wp;
   CHECK(rtw88_tx_auto(&sc,RTW88_TXQ_BE,packet,cfg.ht.max_mpdu,9,0)==0);
   const uint8_t *desc=sc.sc_txq[RTW88_TXQ_BE].buf[wp].vaddr;
   CHECK((desc[9]&0x10)==0 && (desc[13]&0x10)==0 && desc[20]==0 && desc[21]==0);
   wp=sc.sc_txq[RTW88_TXQ_BE].wp;writes=f.writes;
   CHECK(rtw88_tx_auto(&sc,RTW88_TXQ_BE,packet,cfg.ht.max_mpdu+1,9,0)==EMSGSIZE);
   CHECK(sc.sc_txq[RTW88_TXQ_BE].wp==wp && f.writes==writes);
   for(unsigned form=0;form<6;form++) {
    packet[1]=1;packet[22]=packet[24]=packet[25]=0;
    if(form==0)packet[1]|=4;
    if(form==1)packet[22]=1;
    if(form==2)packet[24]=0x20;
    if(form==3)packet[24]=0x80;
    if(form==4)packet[24]=0x10;
    if(form==5)packet[25]=1;
    CHECK(rtw88_tx_auto(&sc,RTW88_TXQ_BE,packet,cfg.ht.max_mpdu,9,0)==EOPNOTSUPP);
    CHECK(sc.sc_txq[RTW88_TXQ_BE].wp==wp && f.writes==writes);
   }
  } else {
   CHECK(error!=0 && f.writes==writes && sc.sc_h2c_box==box);
   CHECK(memcmp(&saved,&sc.sc_ra,sizeof(saved))==0);
  }
  CHECK(rtw88_stop(&sc)==0);CHECK(rtw88_core_detach(&sc)==0);
 }
}

/* Wide frames must be rejected before descriptor, DMA, ring or doorbell. */
static void
ra_wide_denied(struct rtw88_softc *sc, uint64_t epoch, int auto_error)
{
	/* A DFS channel is closed by the slave permit before any format. */
	unsigned ch = sc->sc_channel;
	int dfs = (ch >= 52 && ch <= 64) || (ch >= 100 && ch <= 144);
	int error = dfs && !(sc->sc_dfs.valid && sc->sc_dfs.channel == ch &&
	    sc->sc_dfs.generation == sc->sc_generation) ? EACCES : EOPNOTSUPP;
	struct rtw88_tx_fixed fixed = {
		.associated = 1, .channel = sc->sc_channel, .rate500 = 12,
		.peer_mask = 0xff0, .mode = RTW88_FIXED_11A
	};
	uint8_t mgmt[24] = { 0x80 };
	uint8_t qos[26] = { 0x88, 0x01, 0, 0, 2 };
	uint8_t before[RTW8822C_TX_PACKET_DESC_SIZE];
	struct rtw88_stats stats = sc->sc_stats;
	struct rtw88_ra_state ra = sc->sc_ra;
	unsigned wp = sc->sc_txq[RTW88_TXQ_BE].wp;
	unsigned mwp = sc->sc_txq[RTW88_TXQ_MGMT].wp;
	unsigned queued = sc->sc_txq[RTW88_TXQ_BE].queued;
	unsigned syncs = f.syncs, writes = f.writes;
	void *slot = sc->sc_txq[RTW88_TXQ_BE].buf[wp].vaddr;

	memcpy(before, slot, sizeof(before));
	CHECK(rtw88_tx_raw(sc, RTW88_TXQ_MGMT, mgmt, sizeof(mgmt)) ==
	    error);
	CHECK(rtw88_tx_fixed(sc, RTW88_TXQ_BE, ra_frame, sizeof(ra_frame),
	    &fixed) == error);
	CHECK(rtw88_tx_auto(sc, RTW88_TXQ_BE, qos, sizeof(qos), epoch, 0) ==
	    auto_error);
	CHECK(sc->sc_txq[RTW88_TXQ_BE].wp == wp &&
	    sc->sc_txq[RTW88_TXQ_MGMT].wp == mwp &&
	    sc->sc_txq[RTW88_TXQ_BE].queued == queued);
	CHECK(f.syncs == syncs && f.writes == writes);
	CHECK(memcmp(before, slot, sizeof(before)) == 0);
	CHECK(memcmp(&stats, &sc->sc_stats, sizeof(stats)) == 0);
	CHECK(memcmp(&ra, &sc->sc_ra, sizeof(ra)) == 0);
}

#if !defined(RTW88_VHT80) || !defined(RTW88_DFS64)
/* Commit the target through the actual PHY and power programmers, as the
 * stop/restart owner must do before it publishes the tuple. */
static int
ra_seed_tuple(struct rtw88_softc *sc, struct rtw88_channel_tuple target)
{
	struct rtw8822c_efuse efuse;

	if (rtw8822c_efuse_decode(sc->sc_efuse_phys,
	    sizeof(sc->sc_efuse_phys), &efuse) != 0 ||
	    rtw8822c_set_channel_tuple(sc, &target) != 0 ||
	    rtw8822c_set_tx_power_tuple(sc, &efuse, &target, 12) != 0)
		return 0;
	sc->sc_channel = target.primary;
	sc->sc_phy.channel = target;
	sc->sc_phy.channel_valid = 1;
	return 1;
}
#endif

static const struct rtw88_channel_tuple ra_vht80_target = { 36, 42, 80 };

/* A different RF width is entered only by a cold start after a real stop.
 * Every rejection precedes bus I/O; warm calls keep the committed width. */
static void
test_ra_start_tuple(void)
{
	static const struct rtw88_channel_tuple closed[] = {
#ifndef RTW88_DFS64
		{ 64, 64, 20 }, { 64, 62, 40 }, { 64, 58, 80 }, { 52, 58, 80 },
		{ 60, 62, 40 },
#endif
#ifndef RTW88_VHT80
		{ 64, 62, 40 }, { 64, 58, 80 }, { 36, 38, 40 }, { 40, 42, 80 },
		{ 149, 155, 80 }, { 100, 102, 40 },
#endif
		/* Misaligned 40 MHz pairs pass the PHY geometry alone. */
		{ 40, 42, 40 }, { 161, 163, 40 }, { 165, 163, 40 },
		{ 144, 146, 40 }, { 36, 34, 40 }, { 6, 8, 40 }, { 2, 6, 80 },
		{ 36, 42, 40 }, { 36, 36, 80 }, { 36, 40, 20 }, { 165, 155, 80 },
		{ 36, 42, 160 }, { 14, 14, 20 }
	};
	struct rtw88_softc sc;
	unsigned reads, writes;

	if (!start_ok(&sc))
		return;
	CHECK(rtw88_stop(&sc) == 0);
	for (unsigned i = 0; i < __arraycount(closed); i++) {
		reads = f.reads;
		writes = f.writes;
		CHECK(rtw88_start_tuple(&sc, &closed[i]) == EOPNOTSUPP);
		CHECK(f.reads == reads && f.writes == writes && !sc.sc_started);
	}
	reads = f.reads;
	writes = f.writes;
	CHECK(rtw88_start_tuple(&sc, NULL) == EINVAL);
	CHECK(f.reads == reads && f.writes == writes && !sc.sc_started);
#ifdef RTW88_VHT80
	CHECK(rtw88_start_tuple(&sc, &ra_vht80_target) == 0);
	CHECK(sc.sc_started && sc.sc_channel == 36 && sc.sc_phy.channel_valid);
	CHECK(sc.sc_phy.channel.primary == 36 && sc.sc_phy.channel.center == 42 &&
	    sc.sc_phy.channel.width_mhz == 80);
	CHECK(f.mmio[0x0483] == 0xa4);
	/* Same-width warm reprogramming remains available. */
	CHECK(rtw88_channel_apply(&sc, &ra_vht80_target) == 0);
	CHECK(sc.sc_phy.channel_valid && sc.sc_phy.channel.width_mhz == 80);
	for (unsigned i = 0; i < 3; i++) {
		static const struct rtw88_channel_tuple warm[] = {
			{ 36, 36, 20 }, { 40, 40, 20 }, { 36, 38, 80 }
		};
		reads = f.reads;
		writes = f.writes;
		/* Width changes, then a same-width tuple off every block. */
		CHECK(rtw88_channel_apply(&sc, &warm[i]) ==
		    (i < 2 ? EOPNOTSUPP : EINVAL));
		CHECK(f.reads == reads && f.writes == writes);
		CHECK(sc.sc_phy.channel_valid && sc.sc_channel == 36);
	}
	/* A same-width retune between admitted blocks stays warm. */
	CHECK(rtw88_channel_apply(&sc,
	    &(struct rtw88_channel_tuple){ 161, 155, 80 }) == 0);
	CHECK(sc.sc_channel == 161 && f.mmio[0x0483] == 0x93);
	CHECK(rtw88_channel_apply(&sc, &ra_vht80_target) == 0);
	CHECK(sc.sc_channel == 36 && f.mmio[0x0483] == 0xa4);
	CHECK(rtw88_stop(&sc) == 0);
#else
	reads = f.reads;
	writes = f.writes;
	CHECK(rtw88_start_tuple(&sc, &ra_vht80_target) == EOPNOTSUPP);
	CHECK(f.reads == reads && f.writes == writes && !sc.sc_started);
#endif
	/* The ordinary narrow cold start, then no warm entry to 80 MHz. */
	CHECK(rtw88_start_tuple(&sc,
	    &(struct rtw88_channel_tuple){ 36, 36, 20 }) == 0);
	CHECK(sc.sc_channel == 36 && sc.sc_phy.channel.width_mhz == 20);
	reads = f.reads;
	writes = f.writes;
	CHECK(rtw88_channel_apply(&sc, &ra_vht80_target) == EOPNOTSUPP);
	CHECK(f.reads == reads && f.writes == writes);
	CHECK(rtw88_stop(&sc) == 0);
	CHECK(rtw88_core_detach(&sc) == 0);
}

/*
 * A DFS slave transmits only under a permit for this channel and core
 * generation. Every frame class is denied before descriptor, DMA, ring or
 * doorbell; only 64/64/20 with RTW88_DFS64 can be opened at all.
 */
static void
ra_dfs_denied(struct rtw88_softc *sc)
{
	uint8_t mgmt[24] = { 0xb0 }; /* Authentication. */
	uint8_t mgmt_before[RTW88_TX_BUF_SIZE], be_before[RTW88_TX_BUF_SIZE];
	uint8_t mgmt_bd_before[RTW8822C_TX_BUFFER_DESC_SIZE];
	uint8_t be_bd_before[RTW8822C_TX_BUFFER_DESC_SIZE];
	struct rtw88_stats stats = sc->sc_stats;
	unsigned wp = sc->sc_txq[RTW88_TXQ_MGMT].wp;
	unsigned bwp = sc->sc_txq[RTW88_TXQ_BE].wp;
	unsigned mqueued = sc->sc_txq[RTW88_TXQ_MGMT].queued;
	unsigned bqueued = sc->sc_txq[RTW88_TXQ_BE].queued;
	unsigned syncs = f.syncs, writes = f.writes;
	uint8_t *mbuf, *bbuf, *mbd, *bbd;
	struct rtw88_tx_fixed fixed = {
		.associated = 1, .channel = sc->sc_channel, .rate500 = 12,
		.peer_mask = 0xff0, .mode = RTW88_FIXED_11A
	};

	CHECK(wp < RTW88_TX_SLOTS && bwp < RTW88_TX_SLOTS);
	if (wp >= RTW88_TX_SLOTS || bwp >= RTW88_TX_SLOTS)
		return;
	mbuf = sc->sc_txq[RTW88_TXQ_MGMT].buf[wp].vaddr;
	bbuf = sc->sc_txq[RTW88_TXQ_BE].buf[bwp].vaddr;
	CHECK(mbuf != NULL && bbuf != NULL);
	if (mbuf == NULL || bbuf == NULL)
		return;
	CHECK(sc->sc_txq[RTW88_TXQ_MGMT].buf[wp].size >= sizeof(mgmt_before));
	CHECK(sc->sc_txq[RTW88_TXQ_BE].buf[bwp].size >= sizeof(be_before));
	if (sc->sc_txq[RTW88_TXQ_MGMT].buf[wp].size < sizeof(mgmt_before) ||
	    sc->sc_txq[RTW88_TXQ_BE].buf[bwp].size < sizeof(be_before))
		return;
	CHECK(sc->sc_ring[RTW88_TX_RING_MGMT].vaddr != NULL &&
	    sc->sc_ring[RTW88_TX_RING_BE].vaddr != NULL);
	CHECK(sc->sc_ring[RTW88_TX_RING_MGMT].size >=
	    RTW88_TX_SLOTS * sizeof(mgmt_bd_before));
	CHECK(sc->sc_ring[RTW88_TX_RING_BE].size >=
	    RTW88_TX_SLOTS * sizeof(be_bd_before));
	if (sc->sc_ring[RTW88_TX_RING_MGMT].vaddr == NULL ||
	    sc->sc_ring[RTW88_TX_RING_BE].vaddr == NULL ||
	    sc->sc_ring[RTW88_TX_RING_MGMT].size <
	    RTW88_TX_SLOTS * sizeof(mgmt_bd_before) ||
	    sc->sc_ring[RTW88_TX_RING_BE].size <
	    RTW88_TX_SLOTS * sizeof(be_bd_before))
		return;
	mbd = (uint8_t *)sc->sc_ring[RTW88_TX_RING_MGMT].vaddr +
	    RTW8822C_TX_BUFFER_DESC_SIZE * wp;
	bbd = (uint8_t *)sc->sc_ring[RTW88_TX_RING_BE].vaddr +
	    RTW8822C_TX_BUFFER_DESC_SIZE * bwp;
	memcpy(mgmt_before, mbuf, sizeof(mgmt_before));
	memcpy(be_before, bbuf, sizeof(be_before));
	memcpy(mgmt_bd_before, mbd, sizeof(mgmt_bd_before));
	memcpy(be_bd_before, bbd, sizeof(be_bd_before));

	CHECK(rtw88_tx_raw(sc, RTW88_TXQ_MGMT, mgmt, sizeof(mgmt)) == EACCES);
	CHECK(rtw88_tx_raw(sc, RTW88_TXQ_BE, ra_frame, sizeof(ra_frame)) ==
	    EACCES);
	CHECK(rtw88_tx_fixed(sc, RTW88_TXQ_BE, ra_frame, sizeof(ra_frame),
	    &fixed) == EACCES);
	/* The barrier precedes every frame-format check. */
	CHECK(rtw88_tx_raw(sc, RTW88_TXQ_MGMT, mgmt, 10) == EACCES);
	CHECK(sc->sc_txq[RTW88_TXQ_MGMT].wp == wp &&
	    sc->sc_txq[RTW88_TXQ_BE].wp == bwp);
	CHECK(sc->sc_txq[RTW88_TXQ_MGMT].queued == mqueued &&
	    sc->sc_txq[RTW88_TXQ_BE].queued == bqueued);
	CHECK(memcmp(mgmt_before, mbuf, sizeof(mgmt_before)) == 0);
	CHECK(memcmp(be_before, bbuf, sizeof(be_before)) == 0);
	CHECK(memcmp(mgmt_bd_before, mbd, sizeof(mgmt_bd_before)) == 0);
	CHECK(memcmp(be_bd_before, bbd, sizeof(be_bd_before)) == 0);
	CHECK(f.syncs == syncs && f.writes == writes);
	CHECK(memcmp(&stats, &sc->sc_stats, sizeof(stats)) == 0);
}

static void
test_ra_dfs64(void)
{
	static const struct rtw88_channel_tuple dfs64 = { 64, 64, 20 };
	uint8_t mgmt[24] = { 0xb0 };
	struct rtw88_softc sc;
	uint32_t gen = 0;
	unsigned writes;

	if (!start_ok(&sc))
		return;
	/* Every core-admitted DFS channel denies host TX without a permit. */
	for (unsigned ch = 52; ch <= 144; ch += 4) {
		if (ch > 60 && ch < 100)
			continue;
		CHECK(rtw88_channel(&sc, ch) == 0);
		ra_dfs_denied(&sc);
		CHECK(rtw88_dfs_open(&sc, ch, sc.sc_generation) == EOPNOTSUPP);
		ra_dfs_denied(&sc);
	}
	/* Non-DFS channels keep transmitting; then no retune into DFS while
	 * those frames are posted. */
	for (unsigned i = 0; i < 12; i++) {
		static const unsigned open_ch[] = { 1, 6, 13, 36, 40, 44, 48,
		    149, 153, 157, 161, 165 };
		CHECK(rtw88_channel(&sc, open_ch[i]) == 0);
		CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, mgmt,
		    sizeof(mgmt)) == 0);
	}
	writes = f.writes;
	CHECK(rtw88_channel(&sc, 52) == EBUSY && f.writes == writes);
	CHECK(sc.sc_channel == 165 && sc.sc_phy.channel_valid);
	CHECK(rtw88_stop(&sc) == 0);
	/* A stopped core reports its lifecycle before the DFS barrier. */
	sc.sc_channel = 64;
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, mgmt, sizeof(mgmt)) == ENXIO);
#ifndef RTW88_DFS64
	writes = f.writes;
	CHECK(rtw88_start_tuple(&sc, &dfs64) == EOPNOTSUPP);
	CHECK(f.writes == writes && !sc.sc_started);
	CHECK(rtw88_start(&sc) == 0);
	CHECK(rtw88_dfs_open(&sc, 64, sc.sc_generation) == EOPNOTSUPP);
	/* Even a seeded 64/64/20 program cannot open without the option. */
	sc.sc_channel = 64;
	sc.sc_phy.channel = dfs64;
	sc.sc_phy.channel_valid = 1;
	CHECK(rtw88_dfs_open(&sc, 64, sc.sc_generation) == EOPNOTSUPP);
	CHECK(!sc.sc_dfs.valid);
	printf("rtw88 DFS64 slave permit: disabled\n");
#else
	CHECK(rtw88_start_tuple(&sc, &dfs64) == 0);
	CHECK(sc.sc_channel == 64 && sc.sc_phy.channel_valid);
	ra_dfs_denied(&sc);
	/* With empty rings a warm 64 -> 64 retune succeeds and withdraws. */
	CHECK(rtw88_dfs_open(&sc, 64, sc.sc_generation) == 0);
	CHECK(rtw88_channel(&sc, 64) == 0 && !sc.sc_dfs.valid);
	ra_dfs_denied(&sc);
	gen = sc.sc_generation;
	writes = f.writes;
	CHECK(rtw88_dfs_open(&sc, 64, gen + 1) == ESTALE);
	CHECK(rtw88_dfs_open(&sc, 60, gen) == EOPNOTSUPP);
	CHECK(!sc.sc_dfs.valid && f.writes == writes);
	ra_dfs_denied(&sc);
	CHECK(rtw88_dfs_open(&sc, 64, gen) == 0 && f.writes == writes);
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, mgmt, sizeof(mgmt)) == 0);
	/* A permit names one generation and one channel. */
	sc.sc_dfs.generation--;
	ra_dfs_denied(&sc);
	sc.sc_dfs.generation++;
	sc.sc_dfs.channel = 60;
	ra_dfs_denied(&sc);
	sc.sc_dfs.channel = 64;
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, mgmt, sizeof(mgmt)) == 0);
	rtw88_dfs_close(&sc);
	ra_dfs_denied(&sc);
	/* A warm retune or a lifecycle restart withdraws the permit. */
	CHECK(rtw88_dfs_open(&sc, 64, gen) == 0);
	CHECK(rtw88_channel(&sc, 36) == 0 && !sc.sc_dfs.valid);
	/* Frames still posted from 64 block a retune back into it. */
	writes = f.writes;
	CHECK(rtw88_channel(&sc, 64) == EBUSY && f.writes == writes);
	CHECK(rtw88_stop(&sc) == 0);
	CHECK(rtw88_start_tuple(&sc, &dfs64) == 0);
	CHECK(rtw88_dfs_open(&sc, 64, sc.sc_generation) == 0);
	CHECK(rtw88_stop(&sc) == 0 && !sc.sc_dfs.valid);
	CHECK(rtw88_start_tuple(&sc, &dfs64) == 0);
	ra_dfs_denied(&sc);
	/* A stop request or a missing channel program cannot open it. */
	sc.sc_phy.channel_valid = 0;
	CHECK(rtw88_dfs_open(&sc, 64, sc.sc_generation) == EOPNOTSUPP);
	sc.sc_phy.channel_valid = 1;
	sc.sc_phy.channel.width_mhz = 40; /* Seeded wide program at 64. */
	CHECK(rtw88_dfs_open(&sc, 64, sc.sc_generation) == EOPNOTSUPP);
	sc.sc_phy.channel.width_mhz = 20;
	sc.sc_stop_requested = 1;
	CHECK(rtw88_dfs_open(&sc, 64, sc.sc_generation) == ENXIO);
	sc.sc_stop_requested = 0;
	CHECK(!sc.sc_dfs.valid);
	CHECK(rtw88_dfs_open(&sc, 64, sc.sc_generation) == 0);
	CHECK(rtw88_tx_quiet(&sc) == 0);
	CHECK(!sc.sc_dfs.valid && sc.sc_stop_requested);
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, mgmt, sizeof(mgmt)) ==
	    ENXIO);
	printf("rtw88 DFS64 slave permit: enabled\n");
#endif
	(void)gen;
	(void)writes;
	CHECK(rtw88_stop(&sc) == 0);
	CHECK(rtw88_core_detach(&sc) == 0);
}

#ifdef RTW88_DFS64
/* Actual worker transitions on channel 64 attempt one AUTH frame each. */
static unsigned ra_dfs_parent_calls;
static int ra_dfs_parent_tx;
static int
ra_dfs_parent(struct ieee80211com *ic, enum ieee80211_state state, int arg)
{
	struct rtw88_net *net = ic->ic_ifp->if_softc;
	uint8_t auth[24] = { 0xb0 };

	(void)arg;
	CHECK(!state_lock.held);
	ic->ic_state = state;
	if (state == IEEE80211_S_INIT)
		return 0;
	ra_dfs_parent_calls++;
	ra_dfs_parent_tx = rtw88_tx_raw(net->core, RTW88_TXQ_MGMT, auth,
	    sizeof(auth));
	return 0;
}

#define RA_DFS_BEACON_LEN 44
/* A complete ESS beacon: interval 100 TU, SSID, one rate, DS channel 64. */
static void
ra_dfs_beacon(uint8_t frame[RA_DFS_BEACON_LEN], const struct ieee80211_node *ni)
{
	memset(frame, 0, RA_DFS_BEACON_LEN);
	frame[0] = 0x80;
	memset(frame + 4, 0xff, 6);
	memcpy(frame + 10, ni->ni_macaddr, 6);
	memcpy(frame + 16, ni->ni_bssid, 6);
	frame[32] = 100;
	frame[34] = 0x01;
	frame[38] = 1; frame[39] = 1; frame[40] = 12;
	frame[41] = 3; frame[42] = 1; frame[43] = 64;
}

static void
ra_dfs_peer(struct state_fixture *s)
{
	static const uint8_t master[6] = { 2, 0, 0, 0, 0, 9 };
	static const uint8_t bssid[6] = { 2, 0, 0, 0, 0, 8 };

	state_legacy_peer(&s->net, &s->ni, 64);
	s->ni.ni_associd = 0xc001;
	/* Distinct transmitter and BSSID so each address check is visible. */
	memcpy(s->ni.ni_macaddr, master, 6);
	memcpy(s->ni.ni_bssid, bssid, 6);
	s->net.parent_state = ra_dfs_parent;
	ra_dfs_parent_calls = 0;
	ra_dfs_parent_tx = 0;
}

/* Observe one beacon at tick with the current request and generation. */
static void
ra_dfs_observe_sequence(struct state_fixture *s, const uint8_t *frame,
	size_t len, uint64_t sequence, uint32_t tick)
{
	rtw88_net_dfs_beacon(&s->net, frame, len, sequence,
	    s->sc.sc_generation, tick, s->net.policy.epoch, tick, 100);
}

static void
ra_dfs_observe(struct state_fixture *s, const uint8_t *frame, size_t len,
	uint32_t tick)
{
	ra_dfs_observe_sequence(s, frame, len, s->net.dfs_frontier + 1, tick);
}

/*
 * Channel 64 slave through the actual newstate and state worker: the
 * worker's channel program leaves TX closed, so the one AUTH/ASSOC frame
 * net80211 sends waits (no capacity) until the selected master's current
 * beacon opens TX. A lost lease or CSA/Quiet withdraws through
 * rtw88_tx_quiet() before a clean restart.
 */
static void
test_ra_dfs64_net(void)
{
	uint8_t auth[24] = { 0xb0 }, frame[RA_DFS_BEACON_LEN];
	uint8_t bad[RA_DFS_BEACON_LEN], csa[RA_DFS_BEACON_LEN + 5];
	struct state_fixture s;
	uint32_t gen, timeout = 100, tick = 5000;
	uint64_t epoch;

	if (!state_fixture_start(&s, 64, 9))
		return;
	ra_dfs_peer(&s);
	/* One beacon completed before the request is still in the DMA ring. */
	f.rx_hw_idx = 1;
	CHECK(rtw88_net_newstate(&s.net.ic, IEEE80211_S_AUTH, 0) == 0);
	rtw88_net_state_work(&s.net.state_work, &s.net);
	CHECK(ra_dfs_parent_calls == 1 && ra_dfs_parent_tx == EACCES);
	CHECK(s.sc.sc_channel == 64 && !s.sc.sc_dfs.valid);
	CHECK(s.net.dfs_frontier_valid && s.net.dfs_frontier == 1 &&
	    s.net.dfs_frontier_generation == s.sc.sc_generation);
	/* No capacity: the queued AUTH stays queued for the first beacon. */
	CHECK(rtw88_tx_available(&s.sc, RTW88_TXQ_MGMT) == 0);
	gen = s.sc.sc_generation;
	epoch = s.net.policy.epoch;
	ra_dfs_beacon(frame, &s.ni);
	/* Foreign, malformed, stale or quiet observations open nothing. */
	for (unsigned v = 0; v < 13; v++) {
		size_t len = sizeof(bad);
		memcpy(bad, frame, sizeof(bad));
		switch (v) {
		case 0: bad[16] ^= 1; break;		/* BSSID. */
		case 1: bad[10] ^= 1; break;		/* Transmitter. */
		case 2: bad[4] = 2; break;		/* Unicast receiver. */
		case 3: bad[1] = 0x01; break;		/* ToDS is not a beacon. */
		case 4: bad[22] = 1; break;		/* Fragment. */
		case 5: bad[0] = 0x50; break;		/* Probe response. */
		case 6: len = 35; break;
		case 7: bad[34] = 0x02; break;		/* IBSS, not ESS. */
		case 8: bad[43] = 60; break;		/* DS names another channel. */
		case 9: bad[38] = 37; break;		/* CSA in place of rates. */
		case 10: bad[32] = 0; break;		/* Interval 0 TU. */
		case 11: len = 38; break;		/* No rates. */
		case 12: break;
		}
		if (v == 12)
			rtw88_net_dfs_beacon(&s.net, bad, len,
			    s.net.dfs_frontier + 1, gen, tick, epoch,
			    tick + timeout, timeout); /* Evidence already stale. */
		else
			rtw88_net_dfs_beacon(&s.net, bad, len,
			    s.net.dfs_frontier + 1, gen, tick, epoch,
			    tick, timeout);
		CHECK(!s.sc.sc_dfs.valid);
	}
	/* A complete beacon that announces a channel switch opens nothing. */
	memcpy(csa, frame, sizeof(frame));
	csa[44] = 37; csa[45] = 3; csa[46] = 1; csa[47] = 100; csa[48] = 1;
	rtw88_net_dfs_beacon(&s.net, csa, sizeof(csa), s.net.dfs_frontier + 1,
	    gen, tick, epoch, tick, timeout);
	CHECK(!s.sc.sc_dfs.valid);
	rtw88_net_dfs_beacon(&s.net, frame, sizeof(frame), s.net.dfs_frontier + 1,
	    gen - 1, tick, epoch, tick, timeout);
	rtw88_net_dfs_beacon(&s.net, frame, sizeof(frame), s.net.dfs_frontier + 1,
	    gen, tick, epoch + 1, tick, timeout);
	rtw88_net_dfs_beacon(&s.net, frame, sizeof(frame), s.net.dfs_frontier + 1,
	    gen, tick, epoch, tick, 0);
	/* The old completed descriptor lies at the inclusive frontier. */
	ra_dfs_observe_sequence(&s, frame, sizeof(frame), s.net.dfs_frontier, tick);
	CHECK(!s.sc.sc_dfs.valid);
	s.net.tx_quiet = 1;
	ra_dfs_observe(&s, frame, sizeof(frame), tick);
	s.net.tx_quiet = 0;
	s.net.policy.dying = 1;
	ra_dfs_observe(&s, frame, sizeof(frame), tick);
	s.net.policy.dying = 0;
	s.net.policy.accepting = 0; /* A request has withdrawn acceptance. */
	ra_dfs_observe(&s, frame, sizeof(frame), tick);
	s.net.policy.accepting = 1;
	s.net.ic.ic_state = IEEE80211_S_SCAN;
	ra_dfs_observe(&s, frame, sizeof(frame), tick);
	s.net.ic.ic_state = IEEE80211_S_INIT;
	ra_dfs_observe(&s, frame, sizeof(frame), tick);
	s.net.ic.ic_state = IEEE80211_S_AUTH;
	s.ni.ni_chan = NULL;
	ra_dfs_observe(&s, frame, sizeof(frame), tick);
	s.ni.ni_chan = s.net.ic.ic_curchan;
	CHECK(!s.sc.sc_dfs.valid);
	CHECK(rtw88_tx_raw(&s.sc, RTW88_TXQ_MGMT, auth, sizeof(auth)) == EACCES);
	/* The master's current beacon opens AUTH TX and arms the watchdog. */
	s.net.ec.ec_if.if_timer = 0;
	ra_dfs_observe(&s, frame, sizeof(frame), tick);
	CHECK(s.sc.sc_dfs.valid && s.net.dfs_tick == tick);
	CHECK(s.net.ec.ec_if.if_timer == 1);
	CHECK(rtw88_tx_available(&s.sc, RTW88_TXQ_MGMT) != 0);
	CHECK(rtw88_tx_raw(&s.sc, RTW88_TXQ_MGMT, auth, sizeof(auth)) == 0);
	/* A later beacon refreshes the lease. */
	ra_dfs_observe(&s, frame, sizeof(frame), tick + 50);
	CHECK(s.net.dfs_tick == tick + 50);
	CHECK(rtw88_net_dfs_expire(&s.net, tick + timeout, timeout) == 0);
	/* Every new request closes TX; the worker reprograms 64. */
	CHECK(rtw88_net_newstate(&s.net.ic, IEEE80211_S_ASSOC, 0) == 0);
	CHECK(!s.sc.sc_dfs.valid);
	rtw88_net_state_work(&s.net.state_work, &s.net);
	CHECK(ra_dfs_parent_calls == 2 && ra_dfs_parent_tx == EACCES);
	CHECK(!s.sc.sc_dfs.valid);
	CHECK(rtw88_net_newstate(&s.net.ic, IEEE80211_S_RUN, 0) == 0);
	rtw88_net_state_work(&s.net.state_work, &s.net);
	CHECK(ra_dfs_parent_calls == 3 && ra_dfs_parent_tx == EACCES);
	CHECK(s.net.ic.ic_state == IEEE80211_S_RUN && s.net.policy.accepting);
	ra_dfs_observe(&s, frame, sizeof(frame), tick);
	CHECK(s.sc.sc_dfs.valid);
	/* The lease ends one timeout after the beacon. A zero timeout ends
	 * nothing; a backward clock fails closed. Loss quiets host and MAC TX
	 * and requests a clean restart. */
	CHECK(rtw88_net_dfs_expire(&s.net, tick + timeout - 1, timeout) == 0);
	CHECK(rtw88_net_dfs_expire(&s.net, tick + timeout, 0) == 0);
	CHECK(s.sc.sc_dfs.valid && !s.net.reset_required);
	CHECK(rtw88_net_dfs_expire(&s.net, tick - 5, timeout) == 1);
	CHECK(!s.sc.sc_dfs.valid && s.sc.sc_stop_requested);
	CHECK(s.net.reset_required && s.net.quiet_error == 0);
	CHECK(f.mmio[0x0522] == 0xff);
	CHECK(rtw88_tx_raw(&s.sc, RTW88_TXQ_MGMT, auth, sizeof(auth)) == ENXIO);
	CHECK(rtw88_net_dfs_expire(&s.net, tick + timeout, timeout) == 0);
	gen = s.sc.sc_generation;
	CHECK(rtw88_net_newstate(&s.net.ic, IEEE80211_S_INIT, -1) == 0);
	rtw88_net_state_work(&s.net.state_work, &s.net);
	CHECK(s.sc.sc_started && !s.sc.sc_stop_requested);
	CHECK(!s.net.reset_required && s.sc.sc_generation != gen);
	CHECK(!s.sc.sc_dfs.valid);
	/* The forward wrap of the tick counter is an ordinary interval. */
	CHECK(rtw88_net_newstate(&s.net.ic, IEEE80211_S_AUTH, 0) == 0);
	rtw88_net_state_work(&s.net.state_work, &s.net);
	ra_dfs_observe(&s, frame, sizeof(frame), UINT32_MAX - 10);
	CHECK(s.sc.sc_dfs.valid);
	CHECK(rtw88_net_dfs_expire(&s.net, 20, timeout) == 0);
	/* CSA or Quiet withdraws through the shared quiet owner. */
	rtw88_net_sta_quiet(&s.net.ic);
	CHECK(s.net.tx_quiet && !s.sc.sc_dfs.valid);
	ra_dfs_observe(&s, frame, sizeof(frame), tick);
	CHECK(!s.sc.sc_dfs.valid);
	CHECK(rtw88_tx_raw(&s.sc, RTW88_TXQ_MGMT, auth, sizeof(auth)) == ENXIO);
	state_fixture_finish(&s);
}
#endif

static const struct rtw88_ra_config ra_vht80_cfg = {
	.epoch = 9, .channel = 36, .common_rates = 0xff0,
	.mode = RTW88_FIXED_11A, .phy = RTW88_RA_VHT_1SS,
	.vht_tx_map = RTW88_VHT_MAP_NSS1_MCS7, .profile_generation = 777,
	.ht = { .peer_cap = 0x0c, .mtu = 1500, .max_mpdu = 1550,
	    .rts_threshold = 2346, .qualification = RTW88_HT20_PROT_MF20_NO_RTS }
};

/* Legacy and HT20 entries require the exact narrow committed tuple, and
 * carry no VHT map; an undefined phy is rejected in every build. */
static void
test_ra_narrow_entries(void)
{
	static const struct rtw88_channel_tuple wrong[] = {
		{ 36, 38, 20 }, { 40, 40, 20 }
	};
	const uint8_t bssid[6] = {2, 0, 0, 0, 0, 1};
	const struct rtw88_ra_config legacy = { .epoch = 9, .channel = 36,
	    .common_rates = 0xff0, .mode = RTW88_FIXED_11A };
	struct rtw88_ra_config ht20 = ra_vht80_cfg;

	ht20.phy = RTW88_RA_HT20_1SS;
	ht20.ht_mcs = 0xff;
	ht20.vht_tx_map = 0;
	for (unsigned variant = 0; variant < 7; variant++) {
		struct rtw88_softc sc;
		struct rtw88_ra_config cfg = variant & 1 ? ht20 : legacy;
		struct rtw88_ra_state saved;
		unsigned box, writes;
		int error, expected = EOPNOTSUPP;

		if (!start_ok(&sc))
			continue;
		CHECK(rtw88_channel(&sc, 36) == 0);
		CHECK(rtw88_station_config(&sc, bssid, 1, 0x150, 9, 1) == 0);
		if (variant < 4)
			sc.sc_phy.channel = wrong[variant / 2];
		else if (variant < 6) {
			cfg.vht_tx_map = RTW88_VHT_MAP_NSS1_MCS7;
			if (!(variant & 1))
				expected = EINVAL;
		} else {
			cfg.phy = 3;
			expected = EINVAL;
		}
		box = sc.sc_h2c_box;
		writes = f.writes;
		saved = sc.sc_ra;
		error = rtw88_ra_configure(&sc, &cfg);
		CHECK(error == expected && f.writes == writes &&
		    sc.sc_h2c_box == box);
		CHECK(memcmp(&saved, &sc.sc_ra, sizeof(saved)) == 0);
		CHECK(rtw88_stop(&sc) == 0);
		CHECK(rtw88_core_detach(&sc) == 0);
	}
}

/* A negotiated profile is seeded as net80211 commits it; the WME contract
 * executes the actual association negotiation. */
static int
ra_vht_ht20_admit(struct ieee80211com *ic, struct ieee80211_ht20_profile *p)
{
	(void)ic;
	*p = (struct ieee80211_ht20_profile){ .generation = 777,
	    .rx_mcs = 0xff, .tx_mcs = 0xff, .ready = IEEE80211_HT20_READY_ALL,
	    .ampdu = 0x10 };
	return 0;
}
static int
ra_vht_wme_apply(struct ieee80211com *ic, const struct chanAccParams *p,
    unsigned slot)
{
	(void)ic; (void)p; (void)slot;
	return 0;
}
static uint64_t ra_vht_now(struct ieee80211com *ic) { (void)ic; return 1; }
static int
ra_vht_schedule(struct ieee80211com *ic, uint64_t a, uint64_t b)
{
	(void)ic; (void)a; (void)b;
	return 0;
}
static void ra_vht_cancel(struct ieee80211com *ic) { (void)ic; }
static const struct ieee80211_rxba_ops ra_vht_rxba = {
	ra_vht_now, ra_vht_schedule, ra_vht_cancel
};
static unsigned ra_vht_runs;
static int
ra_vht_parent(struct ieee80211com *ic, enum ieee80211_state state, int arg)
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
	if (ic->ic_bss->ni_ht20.vht.valid) {
		/* RA_INFO byte2 carries the negotiated width: BW80 2, BW40 1. */
		CHECK(mmio32(0x1d0 + 4 * box) ==
		    (ic->ic_bss->ni_ht20.vht.channel.width_mhz == 80 ?
		    UINT32_C(0x522a0040) : UINT32_C(0x512a0040)));
		CHECK(mmio32(0x1f0 + 4 * box) == UINT32_C(0x000ff010));
	} else {
		CHECK(mmio32(0x1d0 + 4 * box) == UINT32_C(0x40250040));
	}
	ra_vht_runs++;
	return 0;
}
static void
ra_vht_peer(struct state_fixture *s)
{
	struct ieee80211_ht20_node *h = &s->ni.ni_ht20;

	s->net.ic.ic_flags = IEEE80211_F_SHSLOT | IEEE80211_F_WME;
	s->net.ic.ic_caps = IEEE80211_C_WME;
	s->net.ic.ic_wme.wme_sta_apply = ra_vht_wme_apply;
	s->net.ic.ic_ht20_admit = ra_vht_ht20_admit;
	s->net.ic.ic_rxba_ops = &ra_vht_rxba;
	s->net.ic.ic_fragthreshold = RTW88_HT20_THRESHOLD_MAX;
	s->net.ic.ic_rtsthreshold = RTW88_HT20_THRESHOLD_MAX;
	s->net.ec.ec_if.if_mtu = 1500;
	s->ni.ni_associd = 0xc001;
	s->ni.ni_bssid[0] = 2;
	state_legacy_peer(&s->net, &s->ni, 36);
	s->ni.ni_flags = IEEE80211_NODE_QOS | IEEE80211_NODE_WME_VALID;
	s->ni.ni_wme_applied_chan = s->net.ic.ic_curchan;
	s->ni.ni_wme_applied_slot = 9;
	for (unsigned ac = 0; ac < 4; ac++) {
		struct wmeParams p = { 0, 2 + ac, 3, 5, 55 + ac, 0 };
		s->net.ic.ic_wme.wme_chanParams.cap_wmeParams[ac] = p;
	}
	state_negotiated = 1;
	memset(h, 0, sizeof(*h));
	h->attempt = h->offer_attempt = 1;
	h->profile = (struct ieee80211_ht20_profile){ .generation = 777,
	    .rx_mcs = 0xff, .tx_mcs = 0xff, .ready = IEEE80211_HT20_READY_ALL,
	    .ampdu = 0x10 };
	h->channel = s->net.ic.ic_curchan;
	memcpy(h->bssid, s->ni.ni_bssid, 6);
	h->valid = 1;
	h->tx_mcs = h->peer_rx_mcs = 0xff;
	h->peer_cap = 0x000c;
	h->peer_htcap[0] = 0x0c;
	h->peer_htop[0] = 36;
	h->peer_htop[1] = 5; /* Secondary above; the 80 MHz block is 36..48. */
	h->vht.valid = 1;
	h->vht.maps.tx_map = h->vht.maps.rx_map = RTW88_VHT_MAP_NSS1_MCS7;
	h->vht.channel = (struct ieee80211_vht_channel){ 36, 42, 80 };
	s->net.ic.ic_state = IEEE80211_S_ASSOC;
	s->net.parent_state = ra_vht_parent;
}

#ifdef RTW88_VHT80
/* The same negotiated peer on another primary and AP-derived tuple. */
static void
ra_vht_peer_at(struct state_fixture *s, struct rtw88_channel_tuple t)
{
	ra_vht_peer(s);
	state_legacy_peer(&s->net, &s->ni, t.primary);
	s->ni.ni_wme_applied_chan = s->net.ic.ic_curchan;
	s->ni.ni_ht20.channel = s->net.ic.ic_curchan;
	s->ni.ni_ht20.peer_htop[0] = (uint8_t)t.primary;
	/* Secondary above when the primary is the lower of its 40 MHz pair. */
	s->ni.ni_ht20.peer_htop[1] = (uint8_t)(4 |
	    ((t.primary - (t.primary > 144 ? 149 : 36)) % 8 == 0 ? 1 : 3));
	s->ni.ni_ht20.vht.channel = (struct ieee80211_vht_channel){
	    (uint8_t)t.primary, (uint8_t)t.center, (uint8_t)t.width_mhz };
}
#endif

/* Both builds: the RUN TX gate revalidates the committed HT20 RA entry. */
static void
test_ra_ht20_run(void)
{
	struct state_fixture s;
	struct rtw88_ra_config tx_policy;
	int error;

	if (!state_fixture_start(&s, 36, 9))
		return;
	ra_vht_peer(&s);
	s.ni.ni_ht20.vht.valid = 0;
	ra_vht_runs = 0;
	CHECK(rtw88_net_newstate(&s.net.ic, IEEE80211_S_RUN, 0) == 0);
	CHECK(!s.net.reset_required);
	rtw88_net_state_work(&s.net.state_work, &s.net);
	CHECK(ra_vht_runs == 1 && s.net.policy.accepting);
	CHECK(s.sc.sc_ra.phy == RTW88_RA_HT20_1SS);
	error = rtw88_net_tx_ht20_policy(&s.net.ic, &tx_policy);
	CHECK(error == 0);
	if (error != 0) {
		state_fixture_finish(&s);
		return;
	}
	mutex_enter(s.net.lock);
	CHECK(rtw88_net_tx_ra_validate(&s.net, &tx_policy,
	    s.net.policy.epoch) == 0);
	CHECK(rtw88_net_tx_ra_validate(&s.net, &tx_policy,
	    s.net.policy.epoch + 1) == EPROTO);
	tx_policy.ht.mtu--;
	CHECK(rtw88_net_tx_ra_validate(&s.net, &tx_policy,
	    s.net.policy.epoch) == EPROTO);
	tx_policy.ht.mtu++;
	s.sc.sc_ra.vht_tx_map = RTW88_VHT_MAP_NSS1_MCS7;
	CHECK(rtw88_net_tx_ra_validate(&s.net, &tx_policy,
	    s.net.policy.epoch) == EPROTO);
	s.sc.sc_ra.vht_tx_map = 0;
	/* A stale VHT80 entry for a peer that no longer negotiated VHT. */
	tx_policy.phy = s.sc.sc_ra.phy = RTW88_RA_VHT_1SS;
	s.sc.sc_ra.ht_mcs = tx_policy.ht_mcs = 0xff;
	CHECK(rtw88_net_tx_ra_validate(&s.net, &tx_policy,
	    s.net.policy.epoch) == EPROTO);
	tx_policy.phy = s.sc.sc_ra.phy = RTW88_RA_HT20_1SS;
	s.ni.ni_ht20.vht.valid = 1; /* Not an HT20 entry any more. */
	CHECK(rtw88_net_tx_ra_validate(&s.net, &tx_policy,
	    s.net.policy.epoch) == EPROTO);
	mutex_exit(s.net.lock);
	/* Without a committed 36/42/80 core a VHT profile has no RA entry. */
	CHECK(rtw88_net_tx_ht20_policy(&s.net.ic, &tx_policy) == EOPNOTSUPP);
	state_fixture_finish(&s);
}

#ifdef RTW88_VHT80
/* Pinned main.c/fw.c: rate ID10, BW80, VHT1, DIS_PT and NSS1 MCS0-7 plus
 * 6 Mbps. Data seeds VHT1SS MCS7 (0x33), never ctz(mask); every other
 * accepted frame is non-HT on primary20 through DATA_SC 0xa4. */
static void
test_ra_vht80_formats(void)
{
	const uint8_t bssid[6] = {2, 0, 0, 0, 0, 1};

	for (unsigned variant = 0; variant < 20; variant++) {
		struct rtw88_softc sc;
		struct rtw88_ra_config cfg = ra_vht80_cfg;
		struct rtw88_ra_state saved;
		unsigned box, writes;
		int error;

		if (!start_ok(&sc))
			continue;
		if (variant != 1) {
			/* The real width owner: stop, then cold start at 80. */
			CHECK(rtw88_stop(&sc) == 0);
			CHECK(rtw88_start_tuple(&sc, &ra_vht80_target) == 0);
			CHECK(f.mmio[0x0483] == 0xa4);
		} else
			CHECK(rtw88_channel(&sc, 36) == 0);
		CHECK(rtw88_station_config(&sc, bssid, 1, 0x150, 9, 1) == 0);
		switch (variant) {
		case 2: cfg.vht_tx_map = 0xfffd; break; /* MCS8 is not local. */
		case 3: cfg.vht_tx_map = 0xfffe; break;
		case 4: cfg.vht_tx_map = 0xffff; break;
		case 5: cfg.vht_tx_map = 0xfff0; break; /* NSS2 is outside 1SS. */
		case 6: cfg.common_rates = 0xfe0; break; /* No 6 Mbps fallback. */
		case 7: cfg.profile_generation = 0; break;
		case 8: cfg.use_protection = 1; break;
		case 9: cfg.ht_mcs = 0xff; break;
		case 10: cfg.channel = 40; break;
		case 11: sc.sc_phy.channel_valid = 0; break;
		case 12: sc.sc_phy.channel.center = 38; break;
		case 13: cfg.ht.qualification = 0; break;
		case 14: cfg.mode = RTW88_FIXED_11G; break;
		case 15: sc.sc_h2c_box = 0; f.busy_box = 1; break;
		case 16: sc.sc_edca_error = EIO; break;
		case 17: /* HT-mixed is not a qualified wide format. */
			cfg.phy = RTW88_RA_HT20_1SS; cfg.ht_mcs = 0xff;
			cfg.vht_tx_map = 0; break;
		case 18: /* Legacy firmware RA has no BW80 descriptor owner. */
			cfg = (struct rtw88_ra_config){ .epoch = 9,
			    .channel = 36, .common_rates = 0xff0,
			    .mode = RTW88_FIXED_11A };
			break;
		case 19: cfg.ht_operation = 1; break;
		}
		box = sc.sc_h2c_box;
		writes = f.writes;
		saved = sc.sc_ra;
		error = rtw88_ra_configure(&sc, &cfg);
		if (variant == 0) {
			struct rtw88_tx_fixed fixed = {
				.associated = 1, .channel = 36, .rate500 = 108,
				.peer_mask = 0xff0, .mode = RTW88_FIXED_11A
			};
			uint8_t mgmt[24] = { 0x80 };
			uint8_t packet[1551] = { 0x88, 0x01, 0, 0, 2 };
			const uint8_t *desc;
			unsigned wp;

			CHECK(error == 0 && sc.sc_ra.active);
			CHECK(sc.sc_ra.rate_id == 10 && sc.sc_ra.mask == 0x000ff010);
			CHECK(sc.sc_ra.phy == RTW88_RA_VHT_1SS &&
			    sc.sc_ra.vht_tx_map == RTW88_VHT_MAP_NSS1_MCS7);
			CHECK(sc.sc_basic_rates == 0x150);
			/* Seven-byte RA_INFO: 00 2a 52 10 f0 0f 00. */
			CHECK(mmio32(0x1d0 + 4 * box) == UINT32_C(0x522a0040));
			CHECK(mmio32(0x1f0 + 4 * box) == UINT32_C(0x000ff010));
			for (unsigned form = 0; form < 2; form++) {
				wp = sc.sc_txq[RTW88_TXQ_BE].wp;
				CHECK(rtw88_tx_auto(&sc, RTW88_TXQ_BE,
				    form == 0 ? ra_frame : packet,
				    form == 0 ? sizeof(ra_frame) : 1550, 9, 0) == 0);
				desc = sc.sc_txq[RTW88_TXQ_BE].buf[wp].vaddr;
				CHECK(desc[6] == 10 && (desc[16] & 0x7f) == 0x33);
				CHECK(((desc[20] >> 5) & 3) == 2);
				CHECK((desc[20] & 0x0f) ==
				    RTW88_TX_DATA_SC_DONT_CARE);
				CHECK((desc[13] & 0x05) == 0);
			}
			CHECK(sc.sc_stats.tx_ra_encoded == 2);
			wp = sc.sc_txq[RTW88_TXQ_BE].wp;
			writes = f.writes;
			CHECK(rtw88_tx_auto(&sc, RTW88_TXQ_BE, packet, 1551, 9, 0) ==
			    EMSGSIZE);
			packet[1] |= 0x80; /* HT Control is not qualified. */
			CHECK(rtw88_tx_auto(&sc, RTW88_TXQ_BE, packet, 1550, 9, 0) ==
			    EOPNOTSUPP);
			saved = sc.sc_ra;
			CHECK(rtw88_tx_auto(&sc, RTW88_TXQ_BE, ra_frame,
			    sizeof(ra_frame), 9, 1) == EOPNOTSUPP);
			CHECK(memcmp(&saved, &sc.sc_ra, sizeof(saved)) == 0);
			CHECK(sc.sc_txq[RTW88_TXQ_BE].wp == wp && f.writes == writes);
			/* A CCK basic set from a malformed 5 GHz peer reaches the
			 * protection-limited path; it must not become a wide frame. */
			sc.sc_basic_rates |= 1;
			sc.sc_ra.common_rates |= 1;
			sc.sc_ra.protection_limited = 1;
			CHECK(rtw88_tx_auto(&sc, RTW88_TXQ_BE, ra_frame,
			    sizeof(ra_frame), 9, 0) == EOPNOTSUPP);
			CHECK(sc.sc_txq[RTW88_TXQ_BE].wp == wp && f.writes == writes);
			sc.sc_ra = saved;
			sc.sc_basic_rates = 0x150;
			wp = sc.sc_txq[RTW88_TXQ_MGMT].wp;
			CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, mgmt,
			    sizeof(mgmt)) == 0);
			desc = sc.sc_txq[RTW88_TXQ_MGMT].buf[wp].vaddr;
			CHECK((desc[16] & 0x7f) == 4 && ((desc[20] >> 5) & 3) == 0);
			CHECK((desc[20] & 0x0f) ==
			    RTW88_TX_DATA_SC_20_LOWEST_OF_80);
			CHECK((desc[13] & 0x05) == 0x05);
			wp = sc.sc_txq[RTW88_TXQ_BE].wp;
			CHECK(rtw88_tx_fixed(&sc, RTW88_TXQ_BE, ra_frame,
			    sizeof(ra_frame), &fixed) == 0);
			desc = sc.sc_txq[RTW88_TXQ_BE].buf[wp].vaddr;
			CHECK((desc[16] & 0x7f) == 11 && ((desc[20] >> 5) & 3) == 0);
			CHECK((desc[20] & 0x0f) ==
			    RTW88_TX_DATA_SC_20_LOWEST_OF_80);
			CHECK(desc[6] == 7 && (desc[13] & 0x05) == 0x05); /* RATEID_G. */
			/* Legacy and HT20 firmware-RA entries own no BW80 format. */
			for (unsigned phy = 0; phy < 2; phy++) {
				saved = sc.sc_ra;
				sc.sc_ra.phy = phy == 0 ? RTW88_RA_HT20_1SS :
				    RTW88_RA_LEGACY;
				wp = sc.sc_txq[RTW88_TXQ_BE].wp;
				writes = f.writes;
				CHECK(rtw88_tx_auto(&sc, RTW88_TXQ_BE, ra_frame,
				    sizeof(ra_frame), 9, 0) == EOPNOTSUPP);
				CHECK(sc.sc_txq[RTW88_TXQ_BE].wp == wp &&
				    f.writes == writes);
				sc.sc_ra = saved;
			}
			/* Every committed-tuple clause is required. */
			{
				static const struct rtw88_channel_tuple wrong[] = {
					{ 36, 42, 40 }, { 36, 42, 20 }, { 40, 42, 80 },
					{ 36, 38, 80 }
				};
				for (unsigned i = 0; i < __arraycount(wrong); i++) {
					sc.sc_phy.channel = wrong[i];
					ra_wide_denied(&sc, 9, EOPNOTSUPP);
				}
				sc.sc_phy.channel =
				    (struct rtw88_channel_tuple){ 36, 42, 80 };
				sc.sc_channel = 40;
				ra_wide_denied(&sc, 9, EBUSY);
				sc.sc_channel = 36;
			}
			/* A stale generation or a changed tuple closes all formats. */
			sc.sc_generation++;
			CHECK(rtw88_tx_auto(&sc, RTW88_TXQ_BE, ra_frame,
			    sizeof(ra_frame), 9, 0) == EBUSY);
			sc.sc_generation--;
			sc.sc_phy.channel.width_mhz = 40;
			sc.sc_phy.channel.center = 42;
			ra_wide_denied(&sc, 9, EOPNOTSUPP);
			/* The entry is bound to the width it was configured for:
			 * a committed 36/38/40 still accepts non-HT, not BW80 RA. */
			sc.sc_phy.channel.center = 38;
			wp = sc.sc_txq[RTW88_TXQ_BE].wp;
			writes = f.writes;
			CHECK(rtw88_tx_auto(&sc, RTW88_TXQ_BE, ra_frame,
			    sizeof(ra_frame), 9, 0) == EOPNOTSUPP);
			CHECK(sc.sc_txq[RTW88_TXQ_BE].wp == wp && f.writes == writes);
			sc.sc_phy.channel = (struct rtw88_channel_tuple){ 36, 42, 80 };
			sc.sc_phy.channel_valid = 0;
			ra_wide_denied(&sc, 9, EOPNOTSUPP);
			sc.sc_phy.channel_valid = 1;
			/* A VHT RA entry cannot survive a narrow committed tuple. */
			sc.sc_phy.channel = (struct rtw88_channel_tuple){ 36, 36, 20 };
			wp = sc.sc_txq[RTW88_TXQ_BE].wp;
			writes = f.writes;
			CHECK(rtw88_tx_auto(&sc, RTW88_TXQ_BE, ra_frame,
			    sizeof(ra_frame), 9, 0) == EOPNOTSUPP);
			CHECK(sc.sc_txq[RTW88_TXQ_BE].wp == wp && f.writes == writes);
		} else if (variant == 15) {
			CHECK(error == ETIMEDOUT && sc.sc_ra.error == ETIMEDOUT);
			CHECK(!sc.sc_ra.configured && !sc.sc_ra.active);
			f.busy_box = 0;
		} else {
			CHECK(error != 0 && f.writes == writes && sc.sc_h2c_box == box);
			CHECK(memcmp(&saved, &sc.sc_ra, sizeof(saved)) == 0);
		}
		CHECK(rtw88_stop(&sc) == 0);
		CHECK(rtw88_core_detach(&sc) == 0);
	}
}

#ifndef RTW88_DFS64
/* Without RTW88_DFS64, primary64 tuples program in the model but never
 * transmit. */
static void
test_ra_vht80_channel64_closed(void)
{
	static const struct rtw88_channel_tuple tuples[] = {
		{ 64, 62, 40 }, { 64, 58, 80 }
	};
	const uint8_t bssid[6] = {2, 0, 0, 0, 0, 1};

	for (unsigned i = 0; i < __arraycount(tuples); i++) {
		struct rtw88_softc sc;
		struct rtw88_ra_config cfg = ra_vht80_cfg;
		unsigned writes;

		if (!start_ok(&sc))
			continue;
		CHECK(ra_seed_tuple(&sc, tuples[i]));
		/* A wide DFS tuple never receives a slave TX permit. */
		CHECK(rtw88_dfs_open(&sc, 64, sc.sc_generation) == EOPNOTSUPP);
		cfg.channel = 64;
		CHECK(rtw88_station_config(&sc, bssid, 1, 0x150, 9, 1) == 0);
		writes = f.writes;
		CHECK(rtw88_ra_configure(&sc, &cfg) != 0 && f.writes == writes);
		ra_wide_denied(&sc, 9, EBUSY);
		/* Even an active VHT80 entry cannot open DFS primary64 data. */
		sc.sc_ra = (struct rtw88_ra_state){ .epoch = 9,
		    .generation = sc.sc_generation, .channel = 64,
		    .common_rates = 0xff0, .mask = 0x000ff010, .rate_id = 10,
		    .phy = RTW88_RA_VHT_1SS,
		    .vht_tx_map = RTW88_VHT_MAP_NSS1_MCS7,
		    .profile_generation = 777, .ht = cfg.ht,
		    .configured = 1, .active = 1 };
		ra_wide_denied(&sc, 9, EACCES);
		/* With a forced permit the wide-format gate still refuses. */
		sc.sc_dfs.generation = sc.sc_generation;
		sc.sc_dfs.channel = 64;
		sc.sc_dfs.valid = 1;
		ra_wide_denied(&sc, 9, EOPNOTSUPP);
		rtw88_dfs_close(&sc);
		CHECK(rtw88_stop(&sc) == 0);
		CHECK(rtw88_core_detach(&sc) == 0);
	}
}
#endif

/*
 * Every admitted wide tuple, entered by the real cold start: pinned
 * DATA_SC register, RA_INFO width, VHT descriptor width and the primary20
 * subchannel of every non-HT frame. Primary 64 also needs the DFS slave
 * permit first. The pure tuple check agrees with start and does no I/O.
 */
static void
test_ra_vht_tuples(void)
{
	static const struct {
		struct rtw88_channel_tuple t;
		uint8_t reg, sc;
	} v[] = {
		{ { 36, 42, 80 }, 0xa4, 4 }, { { 40, 42, 80 }, 0xa2, 2 },
		{ { 44, 42, 80 }, 0x91, 1 }, { { 48, 42, 80 }, 0x93, 3 },
		{ { 36, 38, 40 }, 0x02, 2 }, { { 40, 38, 40 }, 0x01, 1 },
		{ { 44, 46, 40 }, 0x02, 2 }, { { 149, 155, 80 }, 0xa4, 4 },
		{ { 161, 155, 80 }, 0x93, 3 }, { { 157, 159, 40 }, 0x02, 2 },
#ifdef RTW88_DFS64
		{ { 64, 58, 80 }, 0x93, 3 }, { { 64, 62, 40 }, 0x01, 1 },
#endif
	};
	const uint8_t bssid[6] = {2, 0, 0, 0, 0, 1};

	for (unsigned i = 0; i < __arraycount(v); i++) {
		struct rtw88_softc sc;
		struct rtw88_ra_config cfg = ra_vht80_cfg;
		struct rtw88_tx_fixed fixed = {
			.associated = 1, .channel = v[i].t.primary,
			.rate500 = 108, .peer_mask = 0xff0,
			.mode = RTW88_FIXED_11A
		};
		uint8_t mgmt[24] = { 0x80 };
		const uint8_t *desc;
		unsigned box, reads, writes, wp;

		if (!start_ok(&sc))
			continue;
		reads = f.reads;
		writes = f.writes;
		CHECK(rtw88_tuple_check(&sc, &v[i].t) == 0);
		CHECK(f.reads == reads && f.writes == writes);
		CHECK(rtw88_stop(&sc) == 0);
		CHECK(rtw88_start_tuple(&sc, &v[i].t) == 0);
		CHECK(f.mmio[0x0483] == v[i].reg);
		CHECK(rtw88_vht_committed(&sc));
		CHECK(rtw88_station_config(&sc, bssid, 1, 0x150, 9, 1) == 0);
		cfg.channel = v[i].t.primary;
		/* The observed primary-64 AP runs non-HT mixed mode 3. */
		if (v[i].t.primary == 64)
			cfg.ht_operation = 7;
		box = sc.sc_h2c_box;
		CHECK(rtw88_ra_configure(&sc, &cfg) == 0 && sc.sc_ra.active);
		CHECK(sc.sc_ra.rate_id == 10 && sc.sc_ra.mask == 0x000ff010);
		CHECK(sc.sc_ra.tuple.center == v[i].t.center &&
		    sc.sc_ra.tuple.width_mhz == v[i].t.width_mhz);
		/* RA_INFO byte2: DIS_PT, VHT1 and BW40 1 or BW80 2. */
		CHECK(mmio32(0x1d0 + 4 * box) == (v[i].t.width_mhz == 80 ?
		    UINT32_C(0x522a0040) : UINT32_C(0x512a0040)));
		CHECK(mmio32(0x1f0 + 4 * box) == UINT32_C(0x000ff010));
		if (v[i].t.primary == 64) {
			ra_wide_denied(&sc, 9, EACCES);
			CHECK(rtw88_dfs_open(&sc, 64, sc.sc_generation + 1) ==
			    ESTALE);
			CHECK(rtw88_dfs_open(&sc, 64, sc.sc_generation) == 0);
		}
		wp = sc.sc_txq[RTW88_TXQ_BE].wp;
		CHECK(rtw88_tx_auto(&sc, RTW88_TXQ_BE, ra_frame,
		    sizeof(ra_frame), 9, 0) == 0);
		desc = sc.sc_txq[RTW88_TXQ_BE].buf[wp].vaddr;
		CHECK(desc[6] == 10 && (desc[16] & 0x7f) == 0x33);
		CHECK(((desc[20] >> 5) & 3) == (v[i].t.width_mhz == 80 ? 2 : 1));
		CHECK((desc[20] & 0x0f) == RTW88_TX_DATA_SC_DONT_CARE);
		/* Neither aggregation nor RTS protection in any mode. */
		CHECK((desc[9] & 0x10) == 0 && (desc[13] & 0x10) == 0);
		wp = sc.sc_txq[RTW88_TXQ_MGMT].wp;
		CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, mgmt, sizeof(mgmt)) == 0);
		desc = sc.sc_txq[RTW88_TXQ_MGMT].buf[wp].vaddr;
		CHECK((desc[16] & 0x7f) == 4 && ((desc[20] >> 5) & 3) == 0);
		CHECK((desc[20] & 0x0f) == v[i].sc);
		wp = sc.sc_txq[RTW88_TXQ_BE].wp;
		CHECK(rtw88_tx_fixed(&sc, RTW88_TXQ_BE, ra_frame,
		    sizeof(ra_frame), &fixed) == 0);
		desc = sc.sc_txq[RTW88_TXQ_BE].buf[wp].vaddr;
		CHECK((desc[16] & 0x7f) == 11 && ((desc[20] >> 5) & 3) == 0);
		CHECK((desc[20] & 0x0f) == v[i].sc);
		/* A warm retune keeps the width, within admitted tuples. */
		CHECK(rtw88_channel_apply(&sc, &(struct rtw88_channel_tuple){
		    v[i].t.primary, v[i].t.primary, 20 }) == EOPNOTSUPP);
		CHECK(rtw88_stop(&sc) == 0);
		CHECK(rtw88_core_detach(&sc) == 0);
	}
}

/*
 * Admitted DFS blocks other than primary 64 program but never transmit:
 * no permit can open, so every format is refused before descriptor and
 * DMA. A warm retune into a DFS block waits for posted host frames, and
 * leaving a permitted primary 64 withdraws the permit and the RA entry.
 */
static void
test_ra_vht_dfs_closed(void)
{
	static const struct rtw88_channel_tuple v[] = {
		{ 100, 106, 80 }, { 112, 106, 80 }, { 132, 134, 40 },
		{ 144, 142, 40 }, { 56, 54, 40 },
#ifdef RTW88_DFS64
		{ 52, 58, 80 }, { 60, 62, 40 },
#endif
	};
	const uint8_t bssid[6] = {2, 0, 0, 0, 0, 1};
	uint8_t mgmt[24] = { 0x80 };

	for (unsigned i = 0; i < __arraycount(v); i++) {
		struct rtw88_softc sc;
		struct rtw88_ra_config cfg = ra_vht80_cfg;

		if (!start_ok(&sc))
			continue;
		CHECK(rtw88_stop(&sc) == 0);
		CHECK(rtw88_start_tuple(&sc, &v[i]) == 0);
		CHECK(rtw88_vht_committed(&sc));
		CHECK(rtw88_station_config(&sc, bssid, 1, 0x150, 9, 1) == 0);
		cfg.channel = v[i].primary;
		CHECK(rtw88_ra_configure(&sc, &cfg) == 0 && sc.sc_ra.active);
		ra_wide_denied(&sc, 9, EACCES);
		CHECK(rtw88_dfs_open(&sc, v[i].primary, sc.sc_generation) ==
		    EOPNOTSUPP);
		CHECK(rtw88_dfs_open(&sc, 64, sc.sc_generation) == EOPNOTSUPP);
		ra_wide_denied(&sc, 9, EACCES);
		CHECK(rtw88_stop(&sc) == 0);
		CHECK(rtw88_core_detach(&sc) == 0);
	}
	/* A posted host frame keeps a warm retune out of a DFS block. */
	{
		struct rtw88_softc sc;
		unsigned reads, writes;

		if (start_ok(&sc)) {
			CHECK(rtw88_stop(&sc) == 0);
			CHECK(rtw88_start_tuple(&sc, &ra_vht80_target) == 0);
			CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, mgmt,
			    sizeof(mgmt)) == 0);
			reads = f.reads;
			writes = f.writes;
			CHECK(rtw88_channel_apply(&sc, &(struct rtw88_channel_tuple){
			    100, 106, 80 }) == EBUSY);
			CHECK(f.reads == reads && f.writes == writes);
			CHECK(sc.sc_channel == 36 && rtw88_vht_committed(&sc));
			CHECK(rtw88_stop(&sc) == 0);
			CHECK(rtw88_core_detach(&sc) == 0);
		}
	}
#ifdef RTW88_DFS64
	/* Leaving a permitted primary 64 closes the permit and the RA entry. */
	{
		struct rtw88_softc sc;
		struct rtw88_ra_config cfg = ra_vht80_cfg;
		const struct rtw88_channel_tuple from = { 64, 58, 80 };
		const struct rtw88_channel_tuple to = { 149, 155, 80 };

		if (start_ok(&sc)) {
			CHECK(rtw88_stop(&sc) == 0);
			CHECK(rtw88_start_tuple(&sc, &from) == 0);
			CHECK(rtw88_station_config(&sc, bssid, 1, 0x150, 9, 1) == 0);
			cfg.channel = 64;
			CHECK(rtw88_ra_configure(&sc, &cfg) == 0);
			CHECK(rtw88_dfs_open(&sc, 64, sc.sc_generation) == 0);
			CHECK(rtw88_tx_auto(&sc, RTW88_TXQ_BE, ra_frame,
			    sizeof(ra_frame), 9, 0) == 0);
			CHECK(rtw88_channel_apply(&sc, &to) == 0);
			CHECK(!sc.sc_dfs.valid && !sc.sc_ra.active);
			CHECK(rtw88_tx_auto(&sc, RTW88_TXQ_BE, ra_frame,
			    sizeof(ra_frame), 9, 0) == EBUSY);
			cfg.channel = 149;
			CHECK(rtw88_ra_configure(&sc, &cfg) == 0);
			CHECK(sc.sc_ra.tuple.primary == 149 &&
			    sc.sc_ra.tuple.center == 155 &&
			    sc.sc_ra.tuple.width_mhz == 80);
			CHECK(rtw88_stop(&sc) == 0);
			CHECK(rtw88_core_detach(&sc) == 0);
		}
	}
#endif
}

/* The pure check refuses exactly what start refuses, before any I/O. A
 * wide block is DFS exactly when its primary is, so the primary permit
 * covers the span. */
static void
test_ra_vht_tuple_model(void)
{
	struct rtw88_softc sc;
	unsigned reads, writes, admitted = 0;

	if (!start_ok(&sc))
		return;
	for (unsigned width = 40; width <= 80; width += 40) {
		for (unsigned center = 30; center <= 170; center++) {
			for (int offset = -6; offset <= 6; offset += 4) {
				struct rtw88_channel_tuple t = {
					(unsigned)((int)center + offset), center, width
				};
				unsigned span = width / 20, dfs = 0;
				int error;

				if (width == 40 && (offset < -2 || offset > 2))
					continue;
				reads = f.reads;
				writes = f.writes;
				error = rtw88_tuple_check(&sc, &t);
				CHECK(f.reads == reads && f.writes == writes);
				if (error != 0)
					continue;
				admitted++;
				for (unsigned k = 0; k < span; k++) {
					unsigned ch = center - (span - 1) * 2 + 4 * k;

					dfs += (ch >= 52 && ch <= 64) ||
					    (ch >= 100 && ch <= 144);
				}
				CHECK(dfs == 0 || dfs == span);
				CHECK((dfs != 0) == ((t.primary >= 52 &&
				    t.primary <= 64) || (t.primary >= 100 &&
				    t.primary <= 144)));
			}
		}
	}
	/* 12 40 MHz pairs and 6 80 MHz blocks, minus 52..64 without DFS64. */
#ifdef RTW88_DFS64
	CHECK(admitted == 24 + 24);
#else
	CHECK(admitted == 22 + 20);
#endif
	CHECK(rtw88_tuple_check(&sc, NULL) == EINVAL);
	CHECK(rtw88_tuple_check(&sc, &(struct rtw88_channel_tuple){
	    40, 42, 40 }) == EOPNOTSUPP);
	/* A power program that cannot be built closes the tuple, without I/O. */
	sc.sc_rfe ^= 1;
	reads = f.reads;
	writes = f.writes;
	CHECK(rtw88_tuple_check(&sc, &ra_vht80_target) == EINVAL);
	CHECK(f.reads == reads && f.writes == writes);
	sc.sc_rfe ^= 1;
	CHECK(rtw88_tuple_check(&sc, &ra_vht80_target) == 0);
	CHECK(rtw88_stop(&sc) == 0);
	CHECK(rtw88_core_detach(&sc) == 0);
}
/* The AP channel net80211 derives for the 36/42/80 peer. */
static const struct ieee80211_vht_channel ra_vht_chan = { 36, 42, 80 };
/*
 * Every negotiated tuple through the actual chain: provider admission ->
 * RUN producer -> width stop/restart owner -> cold start -> VHT RA at the
 * committed width -> native TX. Primary 64 transmits nothing until the
 * DFS slave permit opens, and a node bound to another tuple cannot use
 * the entry.
 */
static void
test_ra_vht_run_tuples(void)
{
	static const struct rtw88_channel_tuple v[] = {
		{ 36, 42, 80 }, { 36, 38, 40 }, { 44, 42, 80 }, { 149, 155, 80 },
		{ 157, 159, 40 },
#ifdef RTW88_DFS64
		{ 64, 58, 80 }, { 64, 62, 40 },
#endif
	};

	for (unsigned i = 0; i < __arraycount(v); i++) {
		struct state_fixture s;
		struct rtw88_ra_config tx_policy;
		struct ieee80211_vht_cap cap;
		struct ieee80211_vht_channel chan = { (uint8_t)v[i].primary,
		    (uint8_t)v[i].center, (uint8_t)v[i].width_mhz };
		uint8_t frame[26] = { 0x88, 0x01, 0, 0, 2, 0, 0, 0, 0, 1, 2,
		    0, 0, 0, 0, 2, 2, 0, 0, 0, 0, 3, 0x10, 0 };
		const uint8_t *desc;
		unsigned slot;
		int error;

		if (!state_fixture_start(&s, v[i].primary, 9))
			continue;
		ra_vht_peer_at(&s, v[i]);
		ra_vht_runs = 0;
		s.net.ic.ic_ht20_diag.offers = 5; /* Mirrored by the worker. */
		s.net.ic.ic_ht20_diag.vht = 3;
		CHECK(rtw88_net_vht_admit(&s.net.ic, &chan, &cap) == 0);
		/* The association timer would abandon a long cold start. */
		s.net.ic.ic_mgt_timer = 5;
		CHECK(rtw88_net_newstate(&s.net.ic, IEEE80211_S_RUN, 0) == 0);
		CHECK(s.net.ic.ic_mgt_timer == 0);
		CHECK(s.net.reset_required);
		CHECK(s.net.pending_channel.primary == v[i].primary &&
		    s.net.pending_channel.center == v[i].center &&
		    s.net.pending_channel.width_mhz == v[i].width_mhz);
		rtw88_net_state_work(&s.net.state_work, &s.net);
		CHECK(ra_vht_runs == 1 && rtw88_vht_committed(&s.sc));
		CHECK(s.sc.sc_phy.channel.center == v[i].center &&
		    s.sc.sc_phy.channel.width_mhz == v[i].width_mhz);
		{
			/* The diagnostic summary records admission and RUN. */
			struct rtw88_net_neg ns;

			mutex_enter(s.net.lock);
			ns = s.net.neg;
			mutex_exit(s.net.lock);
			CHECK(ns.vht_admit == 1 && ns.vht_refuse == 0 &&
			    ns.vht_error == 0);
			CHECK(ns.vht_chan[0] == v[i].primary &&
			    ns.vht_chan[1] == v[i].center &&
			    ns.vht_chan[2] == v[i].width_mhz);
			CHECK(ns.ht20_offers == 5 && ns.ht20_vht == 3);
			/* One RUN pass restarted the width and published RUN. */
			CHECK(ns.run_passes == 1 && ns.run_restart_fail == 0 &&
			    ns.run_cancels == 0 && ns.run_fails == 0 &&
			    ns.run_published == 1);
			CHECK(ns.run_requests == 1 && ns.run_to_init == 0 &&
			    ns.run_queued == 1 && ns.run_replaced == 0);
			CHECK(ns.run_ht == 1 && ns.run_vht == 1 &&
			    ns.run_chan[0] == v[i].primary &&
			    ns.run_chan[1] == v[i].center &&
			    ns.run_chan[2] == v[i].width_mhz);
		}
		CHECK(s.sc.sc_ra.active && s.sc.sc_ra.phy == RTW88_RA_VHT_1SS);
		error = rtw88_net_tx_ht20_policy(&s.net.ic, &tx_policy);
		CHECK(error == 0);
		if (error != 0) {
			state_fixture_finish(&s);
			continue;
		}
		mutex_enter(s.net.lock);
		CHECK(rtw88_net_tx_ra_validate(&s.net, &tx_policy,
		    s.net.policy.epoch) == 0);
		/* The node must have negotiated exactly the committed tuple. */
		s.ni.ni_ht20.vht.channel.width_mhz =
		    v[i].width_mhz == 80 ? 40 : 80;
		CHECK(rtw88_net_tx_ra_validate(&s.net, &tx_policy,
		    s.net.policy.epoch) == EPROTO);
		mutex_exit(s.net.lock);
		CHECK(rtw88_net_tx_ht20_policy(&s.net.ic, &tx_policy) ==
		    EOPNOTSUPP);
		slot = s.sc.sc_txq[RTW88_TXQ_BE].wp;
		CHECK(rtw88_net_tx_submit(&s.net, &s.ni, RTW88_TXQ_BE, frame,
		    sizeof(frame)) == EBUSY);
		CHECK(s.sc.sc_txq[RTW88_TXQ_BE].wp == slot);
		s.ni.ni_ht20.vht.channel.width_mhz = (uint8_t)v[i].width_mhz;
		if (v[i].primary == 64) {
			CHECK(rtw88_net_tx_submit(&s.net, &s.ni, RTW88_TXQ_BE,
			    frame, sizeof(frame)) == EACCES);
			CHECK(s.sc.sc_txq[RTW88_TXQ_BE].wp == slot);
			/* The beacon owner opens it for this generation. */
			CHECK(rtw88_dfs_open(&s.sc, 64, s.sc.sc_generation) == 0);
		}
		CHECK(rtw88_net_tx_submit(&s.net, &s.ni, RTW88_TXQ_BE, frame,
		    sizeof(frame)) == 0);
		desc = s.sc.sc_txq[RTW88_TXQ_BE].buf[slot].vaddr;
		CHECK(desc[6] == 10 && (desc[16] & 0x7f) == 0x33);
		CHECK(((desc[20] >> 5) & 3) == (v[i].width_mhz == 80 ? 2 : 1));
		state_fixture_finish(&s);
	}
	/* A queued RUN replaced before the worker takes it is counted. */
	{
		struct state_fixture s;

		if (state_fixture_start(&s, 36, 9)) {
			ra_vht_peer(&s);
			CHECK(rtw88_net_newstate(&s.net.ic, IEEE80211_S_RUN, 0) == 0);
			CHECK(rtw88_net_newstate(&s.net.ic, IEEE80211_S_SCAN, 0) == 0);
			CHECK(s.net.neg.run_queued == 1 && s.net.neg.run_replaced == 1 &&
			    s.net.neg.run_replaced_by == IEEE80211_S_SCAN);
			rtw88_net_state_work(&s.net.state_work, &s.net);
			CHECK(s.net.neg.run_passes == 0);
			state_fixture_finish(&s);
		}
	}
	/* A stale committed node turns RUN into INIT; the first clause shows. */
	{
		struct state_fixture s;

		if (state_fixture_start(&s, 36, 9)) {
			ra_vht_peer(&s);
			s.ni.ni_ht20.attempt++; /* Offer attempt no longer current. */
			(void)rtw88_net_newstate(&s.net.ic, IEEE80211_S_RUN, 0);
			CHECK(s.net.neg.run_requests == 1 && s.net.neg.run_to_init == 1 &&
			    s.net.neg.run_queued == 0);
			CHECK(s.net.ic.ic_ht20_diag.state_fail == 1 &&
			    s.net.ic.ic_ht20_diag.state_reason == 8);
			CHECK(s.net.requested == IEEE80211_S_INIT);
			state_fixture_finish(&s);
		}
	}
	/* The provider admits only a tuple the core can enter on this channel. */
	for (unsigned variant = 0; variant < 5; variant++) {
		static const struct {
			unsigned channel;
			struct ieee80211_vht_channel chan;
		} closed[] = {
			{ 40, { 40, 42, 40 } },		/* Misaligned pair. */
			{ 44, { 36, 42, 80 } },		/* Not the current primary. */
			{ 36, { 36, 42, 160 } },	/* No such width. */
			{ 64, { 64, 58, 80 } },		/* DFS needs RTW88_DFS64. */
			{ 36, { 36, 36, 20 } }		/* VHT20 has no consumer. */
		};
		struct state_fixture s;
		struct ieee80211_vht_cap cap;
		int want = EOPNOTSUPP;

		if (!state_fixture_start(&s, closed[variant].channel, 9))
			continue;
		ra_vht_peer(&s);
#ifdef RTW88_DFS64
		if (variant == 3)
			want = 0;
#endif
		memset(&cap, 0xa5, sizeof(cap));
		CHECK(rtw88_net_vht_admit(&s.net.ic, &closed[variant].chan,
		    &cap) == want);
		CHECK(want == 0 ? cap.rx_map == 0xfffc :
		    cap.rx_map == 0 && cap.tx_map == 0 && cap.info == 0);
		{
			struct rtw88_net_neg ns;

			mutex_enter(s.net.lock);
			ns = s.net.neg;
			mutex_exit(s.net.lock);
			CHECK(ns.vht_admit == (want == 0) &&
			    ns.vht_refuse == (want != 0) && ns.vht_error == want);
			CHECK(ns.vht_chan[0] == closed[variant].chan.primary &&
			    ns.vht_chan[1] == closed[variant].chan.center &&
			    ns.vht_chan[2] == closed[variant].chan.width_mhz);
		}
		state_fixture_finish(&s);
	}
}
/* The ASSOC worker records the HT profile provider result and its mask. */
static void
test_ra_ht_prepare_diag(void)
{
	for (unsigned variant = 0; variant < 2; variant++) {
		struct state_fixture s;
		struct rtw88_net_neg ns;

		if (!state_fixture_start(&s, 36, 9))
			continue;
		ra_vht_peer(&s);
		s.net.ic.ic_state = IEEE80211_S_AUTH;
		if (variant == 1)
			s.net.ic.ic_wme.wme_sta_apply = NULL; /* No AMSDU owner. */
		CHECK(rtw88_net_newstate(&s.net.ic, IEEE80211_S_ASSOC, 0) == 0);
		rtw88_net_state_work(&s.net.state_work, &s.net);
		mutex_enter(s.net.lock);
		ns = s.net.neg;
		mutex_exit(s.net.lock);
		CHECK(ns.prepare_ok + ns.prepare_fail == 1);
		CHECK((ns.prepare_ok == 1) == (ns.prepare_error == 0));
		CHECK(ns.prepare_ok == 0 ||
		    ns.prepare_ready == RTW88_NET_HT20_READY_ALL);
		/* The mask names the missing readiness, also on failure. */
		CHECK(((ns.prepare_ready & RTW88_NET_HT20_READY_AMSDU) != 0) ==
		    (variant == 0));
		if (variant == 1)
			CHECK(ns.prepare_fail == 1 && ns.prepare_error == EOPNOTSUPP);
		state_fixture_finish(&s);
	}
}
/* Inject a newer state request while the cold start owns the core. */
static unsigned ra_vht_inject;
static int (*ra_vht_read1_next)(void *, uint32_t, uint8_t *);
static int
ra_vht_read1(void *cookie, uint32_t reg, uint8_t *value)
{
	if (ra_vht_inject && state_current_net->core->sc_bootstrapping) {
		ra_vht_inject = 0;
		CHECK(rtw88_net_newstate(&state_current_net->ic,
		    IEEE80211_S_SCAN, 0) == 0);
	}
	return ra_vht_read1_next(cookie, reg, value);
}

/*
 * Actual RUN producer -> width stop/restart owner -> cold start at 36/42/80
 * -> VHT RA -> native TX submit. A fallback or a later SCAN returns to 20.
 */
static void
test_ra_vht80_run(void)
{
	for (unsigned variant = 0; variant < 4; variant++) {
		struct state_fixture s;
		struct rtw88_ra_config tx_policy;
		struct ieee80211_vht_cap cap;
		uint8_t frame[26] = { 0x88, 0x01, 0, 0, 2, 0, 0, 0, 0, 1, 2,
		    0, 0, 0, 0, 2, 2, 0, 0, 0, 0, 3, 0x10, 0 };
		const uint8_t *desc;
		unsigned slot, gen;
		int error;

		if (!state_fixture_start(&s, 36, 9))
			continue;
		ra_vht_peer(&s);
		ra_vht_runs = 0;
		error = rtw88_net_vht_admit(&s.net.ic, &ra_vht_chan, &cap);
		CHECK(error == 0);
		if (error != 0) {
			state_fixture_finish(&s);
			continue;
		}
		CHECK(cap.info == 0 && cap.rx_highest == 0 && cap.tx_highest == 0);
		CHECK(cap.rx_map == 0xfffc && cap.tx_map == 0xfffc);
		if (variant == 1)
			s.ni.ni_ht20.vht.valid = 0; /* HT20 fallback response. */
		gen = s.sc.sc_generation;
		CHECK(rtw88_net_newstate(&s.net.ic, IEEE80211_S_RUN, 0) == 0);
		CHECK(s.net.reset_required == (variant != 1));
		if (variant == 3) {
			/* SCAN arrives while the 80 MHz cold start sleeps. */
			ra_vht_read1_next = s.bus.read_1;
			s.bus.read_1 = ra_vht_read1;
			ra_vht_inject = 1;
			rtw88_net_state_work(&s.net.state_work, &s.net);
			CHECK(!ra_vht_inject && ra_vht_runs == 0);
			/* The superseded RUN pass is counted as cancelled. */
			CHECK(s.net.neg.run_passes == 1 &&
			    s.net.neg.run_cancels == 1 &&
			    s.net.neg.run_published == 0);
			CHECK(s.sc.sc_started && !s.net.reset_required);
			CHECK(!rtw88_vht_committed(&s.sc));
			CHECK(s.sc.sc_phy.channel.width_mhz == 20 &&
			    s.sc.sc_channel == 36);
			s.bus.read_1 = ra_vht_read1_next;
			state_fixture_finish(&s);
			continue;
		}
		rtw88_net_state_work(&s.net.state_work, &s.net);
		CHECK(ra_vht_runs == 1 && s.net.policy.accepting);
		error = rtw88_net_tx_ht20_policy(&s.net.ic, &tx_policy);
		CHECK(error == 0);
		if (error != 0) {
			state_fixture_finish(&s);
			continue;
		}
		mutex_enter(s.net.lock);
		CHECK(rtw88_net_tx_ra_validate(&s.net, &tx_policy,
		    s.net.policy.epoch) == 0);
		mutex_exit(s.net.lock);
		CHECK((s.sc.sc_generation != gen) == (variant != 1));
		if (variant == 1) {
			CHECK(s.sc.sc_phy.channel.width_mhz == 20);
			CHECK(s.sc.sc_ra.phy == RTW88_RA_HT20_1SS);
			state_fixture_finish(&s);
			continue;
		}
		CHECK(rtw88_vht_committed(&s.sc));
		CHECK(f.mmio[0x0483] == 0xa4);
		CHECK(s.sc.sc_ra.active && s.sc.sc_ra.phy == RTW88_RA_VHT_1SS);
		mutex_enter(s.net.lock);
		s.sc.sc_ra.vht_tx_map = 0xfffe;
		CHECK(rtw88_net_tx_ra_validate(&s.net, &tx_policy,
		    s.net.policy.epoch) == EPROTO);
		s.sc.sc_ra.vht_tx_map = RTW88_VHT_MAP_NSS1_MCS7;
		s.ni.ni_ht20.vht.maps.tx_map = 0xfffe;
		CHECK(rtw88_net_tx_ra_validate(&s.net, &tx_policy,
		    s.net.policy.epoch) == EPROTO);
		s.ni.ni_ht20.vht.maps.tx_map = RTW88_VHT_MAP_NSS1_MCS7;
		s.sc.sc_phy.channel_valid = 0; /* No committed 80 MHz program. */
		CHECK(rtw88_net_tx_ra_validate(&s.net, &tx_policy,
		    s.net.policy.epoch) == EPROTO);
		s.sc.sc_phy.channel_valid = 1;
		mutex_exit(s.net.lock);
		slot = s.sc.sc_txq[RTW88_TXQ_BE].wp;
		CHECK(rtw88_net_tx_submit(&s.net, &s.ni, RTW88_TXQ_BE, frame,
		    sizeof(frame)) == 0);
		desc = s.sc.sc_txq[RTW88_TXQ_BE].buf[slot].vaddr;
		CHECK(desc[6] == 10 && (desc[16] & 0x7f) == 0x33);
		CHECK(((desc[20] >> 5) & 3) == 2);
		/* A node that no longer negotiated VHT cannot use the entry. */
		s.ni.ni_ht20.vht.valid = 0;
		slot = s.sc.sc_txq[RTW88_TXQ_BE].wp;
		CHECK(rtw88_net_tx_submit(&s.net, &s.ni, RTW88_TXQ_BE, frame,
		    sizeof(frame)) == EBUSY);
		CHECK(s.sc.sc_txq[RTW88_TXQ_BE].wp == slot);
		s.ni.ni_ht20.vht.valid = 1;
		/* A changed negotiated map no longer matches the RA entry. */
		s.ni.ni_ht20.vht.maps.tx_map = 0xfffe;
		slot = s.sc.sc_txq[RTW88_TXQ_BE].wp;
		CHECK(rtw88_net_tx_submit(&s.net, &s.ni, RTW88_TXQ_BE, frame,
		    sizeof(frame)) == EBUSY);
		CHECK(s.sc.sc_txq[RTW88_TXQ_BE].wp == slot);
		s.ni.ni_ht20.vht.maps.tx_map = RTW88_VHT_MAP_NSS1_MCS7;
		if (variant == 2) {
			/* Leaving RUN returns to the narrow scanning core. */
			gen = s.sc.sc_generation;
			CHECK(rtw88_net_newstate(&s.net.ic, IEEE80211_S_SCAN, 0) == 0);
			CHECK(s.net.reset_required);
			rtw88_net_state_work(&s.net.state_work, &s.net);
			CHECK(s.sc.sc_generation != gen && !rtw88_vht_committed(&s.sc));
			CHECK(s.sc.sc_phy.channel.width_mhz == 20 &&
			    s.sc.sc_channel == 36);
		}
		state_fixture_finish(&s);
	}
	/* A VHT profile off primary 36 never targets the wide tuple. */
	{
		struct state_fixture s;

		if (state_fixture_start(&s, 40, 9)) {
			ra_vht_peer(&s);
			CHECK(rtw88_net_newstate(&s.net.ic, IEEE80211_S_RUN, 0) == 0);
			CHECK(!s.net.reset_required &&
			    s.net.pending_channel.width_mhz == 20 &&
			    s.net.pending_channel.primary == 40);
			state_fixture_finish(&s);
		}
	}
	/* Admission is a fail-closed local snapshot. */
	for (unsigned variant = 0; variant < 4; variant++) {
		struct state_fixture s;
		struct ieee80211_vht_cap cap;

		if (!state_fixture_start(&s, variant == 0 ? 40 : 36, 9))
			continue;
		ra_vht_peer(&s);
		if (variant == 1) s.net.tx_quiet = 1;
		if (variant == 2) s.net.ic.ic_fixed_rate = 0;
		if (variant == 3) s.net.ic.ic_opmode = IEEE80211_M_HOSTAP;
		memset(&cap, 0xa5, sizeof(cap));
		CHECK(rtw88_net_vht_admit(&s.net.ic, &ra_vht_chan, &cap) == EOPNOTSUPP);
		CHECK(cap.rx_map == 0 && cap.tx_map == 0 && cap.info == 0);
		s.net.tx_quiet = 0;
		s.net.ic.ic_opmode = IEEE80211_M_STA;
		state_fixture_finish(&s);
	}
}
#else
/* Default build: no VHT RA entry and no wide frame of any format. */
static void
test_ra_vht80_disabled(void)
{
	struct rtw88_softc sc;
	struct rtw88_ra_config cfg = ra_vht80_cfg;
	unsigned writes, box;

	if (!start_ok(&sc))
		return;
	CHECK(rtw88_channel(&sc, 36) == 0);
	CHECK(ra_seed_tuple(&sc, (struct rtw88_channel_tuple){ 36, 42, 80 }));
	box = sc.sc_h2c_box;
	writes = f.writes;
	CHECK(rtw88_ra_configure(&sc, &cfg) == EINVAL);
	CHECK(f.writes == writes && sc.sc_h2c_box == box && !sc.sc_ra.configured);
	ra_wide_denied(&sc, 9, EBUSY);
	CHECK(rtw88_stop(&sc) == 0);
	CHECK(rtw88_core_detach(&sc) == 0);
}
#endif

#ifndef RTW88_RA_MAIN
#define RTW88_RA_MAIN main
#endif
int
RTW88_RA_MAIN(void)
{
	if (native_start_main() != 0)
		return 1;
	test_ra_ht20_numeric();
	test_ra_ht20_protection_bounds();
	test_ra_run_to_first_data();
	test_ra_c2h_bounds_and_telemetry();
	test_ra_masks_and_quarantine();
	test_ra_erp_gate();
	test_ra_worker_failures();
	test_ra_peer_numeric_masks();
	test_ra_reports_never_control_peer();
	test_ra_narrow_entries();
	test_ra_start_tuple();
	test_ra_ht20_run();
	test_ra_dfs64();
#ifdef RTW88_DFS64
	test_ra_dfs64_net();
#endif
#ifdef RTW88_VHT80
	test_ra_vht80_formats();
#ifndef RTW88_DFS64
	test_ra_vht80_channel64_closed();
#endif
	test_ra_vht_tuples();
	test_ra_vht_tuple_model();
	test_ra_vht_dfs_closed();
	test_ra_vht80_run();
	test_ra_vht_run_tuples();
	test_ra_ht_prepare_diag();
	printf("rtw88 VHT80 RA and TX formats: enabled\n");
#else
	test_ra_vht80_disabled();
	printf("rtw88 VHT80 RA and TX formats: disabled\n");
#endif
	printf("rtw88 operational legacy RA: %u assertions, %u failures\n",
	    assertions, failures);
	return failures != 0;
}
