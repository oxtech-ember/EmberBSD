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
 for(unsigned kind=0;kind<26;kind++) {
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
  }
  unsigned writes=f.writes,box=sc.sc_h2c_box;
  struct rtw88_ra_state saved=sc.sc_ra;
  int error=rtw88_ra_configure(&sc,&cfg);
  if(kind<6) {
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
	    EOPNOTSUPP);
	CHECK(rtw88_tx_fixed(sc, RTW88_TXQ_BE, ra_frame, sizeof(ra_frame),
	    &fixed) == EOPNOTSUPP);
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

static const struct rtw88_ra_config ra_vht80_cfg = {
	.epoch = 9, .channel = 36, .common_rates = 0xff0,
	.mode = RTW88_FIXED_11A, .phy = RTW88_RA_VHT80_1SS,
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
		CHECK(rtw88_channel(&sc, 36) == 0);
		CHECK(rtw88_station_config(&sc, bssid, 1, 0x150, 9, 1) == 0);
		if (variant != 1) {
			CHECK(ra_seed_tuple(&sc,
			    (struct rtw88_channel_tuple){ 36, 42, 80 }));
			CHECK(f.mmio[0x0483] == 0xa4);
		}
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
			CHECK(sc.sc_ra.phy == RTW88_RA_VHT80_1SS &&
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
			CHECK((desc[13] & 0x05) == 0x05);
			wp = sc.sc_txq[RTW88_TXQ_BE].wp;
			CHECK(rtw88_tx_fixed(&sc, RTW88_TXQ_BE, ra_frame,
			    sizeof(ra_frame), &fixed) == 0);
			desc = sc.sc_txq[RTW88_TXQ_BE].buf[wp].vaddr;
			CHECK((desc[16] & 0x7f) == 11 && ((desc[20] >> 5) & 3) == 0);
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
			sc.sc_phy.channel.center = 38;
			ra_wide_denied(&sc, 9, EOPNOTSUPP);
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

/* DFS primary64 tuples program in the model but never transmit. */
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
		cfg.channel = 64;
		CHECK(rtw88_station_config(&sc, bssid, 1, 0x150, 9, 1) == 0);
		writes = f.writes;
		CHECK(rtw88_ra_configure(&sc, &cfg) != 0 && f.writes == writes);
		ra_wide_denied(&sc, 9, EBUSY);
		/* Even an active VHT80 entry cannot open DFS primary64 data. */
		sc.sc_ra = (struct rtw88_ra_state){ .epoch = 9,
		    .generation = sc.sc_generation, .channel = 64,
		    .common_rates = 0xff0, .mask = 0x000ff010, .rate_id = 10,
		    .phy = RTW88_RA_VHT80_1SS,
		    .vht_tx_map = RTW88_VHT_MAP_NSS1_MCS7,
		    .profile_generation = 777, .ht = cfg.ht,
		    .configured = 1, .active = 1 };
		ra_wide_denied(&sc, 9, EOPNOTSUPP);
		CHECK(rtw88_stop(&sc) == 0);
		CHECK(rtw88_core_detach(&sc) == 0);
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
#ifdef RTW88_VHT80
	test_ra_vht80_formats();
	test_ra_vht80_channel64_closed();
	printf("rtw88 VHT80 RA and TX formats: enabled\n");
#else
	test_ra_vht80_disabled();
	printf("rtw88 VHT80 RA and TX formats: disabled\n");
#endif
	printf("rtw88 operational legacy RA: %u assertions, %u failures\n",
	    assertions, failures);
	return failures != 0;
}
