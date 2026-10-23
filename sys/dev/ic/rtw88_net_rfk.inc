/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: native RFK owner and current-peer withdrawal services, AI-assisted. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Internal default-OFF RFK worker services. Giant owns all shared-stack values.
 * No node, key, channel or provider pointer survives a sleep as an identity. */

static uint64_t rtw88_net_rfk_ticks(void *);

/* PCI-serialized observation before ordinary RX admission. Keep only
 * numeric peer observations; Giant owns every stack/state mutation.
 * A closed or full mbuf queue cannot hide an AP's withdrawal during RFK. */
static void
rtw88_net_rfk_rx_observe(struct rtw88_net *net, const uint8_t *frame, size_t len,
    const struct rtw88_rx_info *info)
{
	const struct ieee80211_frame *wh;
	const uint8_t *body;
	uint64_t observed;
	uint16_t interval;
	unsigned i;
	int notice;

	KASSERT(mutex_owned(net->lock));
	if (net->policy.dying || net->tx_quiet || !net->core->sc_running ||
	    net->rfk.control_epoch == 0 ||
	    net->rfk.control_epoch != net->policy.epoch ||
	    net->rfk.control_generation != net->core->sc_generation ||
	    net->rfk.control_channel != net->core->sc_channel ||
	    frame == NULL || info == NULL || len < sizeof(*wh) ||
	    len > RTW88_NET_MPDU_LIMIT || len != info->packet_size ||
	    (info->flags & 0x13) != 0 || info->security != 0 ||
	    !info->signal.valid ||
	    info->signal.generation != net->rfk.control_generation ||
	    info->signal.channel != net->rfk.control_channel)
		return;
	wh = (const void *)frame;
	if ((wh->i_fc[1] & (IEEE80211_FC1_WEP | IEEE80211_FC1_MORE_FRAG |
	    IEEE80211_FC1_ORDER | IEEE80211_FC1_DIR_MASK)) != 0 ||
	    (wh->i_seq[0] & IEEE80211_SEQ_FRAG_MASK) != 0 ||
	    (!IEEE80211_IS_MULTICAST(wh->i_addr1) &&
	     !IEEE80211_ADDR_EQ(wh->i_addr1, net->rfk.control_ra)) ||
	    !IEEE80211_ADDR_EQ(wh->i_addr2, net->rfk.control_ta) ||
	    !IEEE80211_ADDR_EQ(wh->i_addr3, net->rfk.control_bssid))
		return;
	/* Stamp beacon arrival before either body parser. Do not read a clock
	 * for other frame types or use a cached hardclock snapshot. */
	observed = 0;
	if (wh->i_fc[0] == (IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_BEACON))
		observed = rtw88_net_rfk_ticks(net);
	body = frame + sizeof(*wh);
	len -= sizeof(*wh);
	if (wh->i_fc[0] == (IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_BEACON) ||
	    wh->i_fc[0] == (IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_PROBE_RESP))
		notice = ieee80211_sta_beacon_announcement(body, len);
	else if (wh->i_fc[0] == (IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_ACTION))
		notice = ieee80211_sta_action_announcement(body, len);
	else
		return;
	if (notice) {
		/* Withdrawal wins over any earlier or later positive observation. */
		net->rfk.beacon_epoch = 0;
		net->rfk.notice_epoch = net->rfk.control_epoch;
		net->rfk.notice_generation = net->rfk.control_generation;
		net->rfk.notice_channel = net->rfk.control_channel;
		net->rfk.notice_pending = 1;
		softint_schedule(net->softint);
		return;
	}
	/* Only an exact broadcast beacon can record presence. Probe responses,
	 * local unicast and other multicast destinations cannot renew it. */
	if (net->rfk.notice_pending || !net->rfk.frontier_valid ||
	    net->rfk.frontier_generation != net->core->sc_generation ||
	    info->generation != net->rfk.frontier_generation ||
	    info->sequence == 0 || info->sequence <= net->rfk.frontier ||
	    wh->i_fc[0] != (IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_BEACON))
		return;
	for (i = 0; i < IEEE80211_ADDR_LEN; i++) {
		if (wh->i_addr1[i] != 0xff)
			return;
	}
	interval = ieee80211_sta_beacon_interval(body, len,
	    net->rfk.control_channel);
	if (interval < IEEE80211_BINTVAL_MIN || interval > IEEE80211_BINTVAL_MAX)
		return;
	net->rfk.beacon_us = observed;
	net->rfk.beacon_generation = net->rfk.control_generation;
	net->rfk.beacon_channel = net->rfk.control_channel;
	net->rfk.beacon_interval = interval;
	net->rfk.beacon_epoch = net->rfk.control_epoch;
}

static void
rtw88_net_rfk_rx_deliver(struct rtw88_net *net)
{
	int current;

	mutex_enter(net->lock);
	current = net->rfk.notice_pending && !net->policy.dying &&
	    net->rfk.notice_epoch == net->policy.epoch &&
	    net->rfk.notice_epoch == net->rfk.control_epoch &&
	    net->rfk.notice_generation == net->rfk.control_generation &&
	    net->rfk.notice_channel == net->core->sc_channel &&
	    net->rfk.notice_channel == net->rfk.control_channel;
	net->rfk.notice_pending = 0;
	mutex_exit(net->lock);
	if (current) {
		rtw88_net_sta_quiet(&net->ic);
		(void)ieee80211_ht20_state_request(&net->ic, IEEE80211_S_INIT);
		(void)rtw88_net_newstate(&net->ic, IEEE80211_S_INIT, -1);
	}
}

static void
rtw88_net_rfk_publish(struct rtw88_net *net)
{
	struct rtw88_rfk_diag diag;

	rtw8822c_rfk_progress(net->core, &diag);
	mutex_enter(net->lock);
	net->core->sc_stats.rfk = diag;
	mutex_exit(net->lock);
}

static int
rtw88_net_rfk_capture(struct rtw88_net *net, uint32_t values[192])
{
	struct ieee80211com *ic = &net->ic;
	const struct ieee80211_node *ni = ic->ic_bss;
	struct ieee80211_ht20_profile profile;
	struct rtw88_edca_params edca[RTW88_WME_NAC];
	const struct ieee80211_rateset *local;
	unsigned n = 0, i, slot = net->channel > 14 ? 9 : 20;
	int error;

	if (ni == NULL || (unsigned)ic->ic_curmode >= IEEE80211_MODE_MAX ||
	    net->requested != IEEE80211_S_AUTH ||
	    ni->ni_esslen > sizeof(ni->ni_essid))
		return EINVAL;
	local = &ic->ic_sup_rates[ic->ic_curmode];
	if (local->rs_nrates > sizeof(local->rs_rates) ||
	    ni->ni_rates.rs_nrates > sizeof(ni->ni_rates.rs_rates))
		return EINVAL;
	memset(values, 0, sizeof(uint32_t) * 192);
#define RFK_VALUE(v) do { if (n == 192) return EOVERFLOW; values[n++] = (uint32_t)(v); } while (0)
	RFK_VALUE(net->policy.epoch); RFK_VALUE(net->policy.epoch >> 32);
	RFK_VALUE(net->core->sc_generation); RFK_VALUE(net->requested);
	RFK_VALUE(net->channel); RFK_VALUE(net->state_arg);
	RFK_VALUE(net->configured_media); RFK_VALUE(ic->ic_flags);
	RFK_VALUE(ic->ic_flags_ext); RFK_VALUE(ic->ic_caps);
	RFK_VALUE(ic->ic_curmode); RFK_VALUE(ic->ic_fixed_rate);
	RFK_VALUE(ic->ic_opmode); RFK_VALUE(ic->ic_protmode);
	RFK_VALUE(ni->ni_flags); RFK_VALUE(ni->ni_authmode);
	RFK_VALUE(ni->ni_capinfo); RFK_VALUE(ni->ni_associd);
	RFK_VALUE(ieee80211_chan2ieee(ic, ic->ic_curchan));
	RFK_VALUE(ieee80211_chan2ieee(ic, ni->ni_chan));
	RFK_VALUE(ni->ni_rsn.rsn_mcastcipher); RFK_VALUE(ni->ni_rsn.rsn_mcastkeylen);
	RFK_VALUE(ni->ni_rsn.rsn_ucastcipherset); RFK_VALUE(ni->ni_rsn.rsn_ucastcipher);
	RFK_VALUE(ni->ni_rsn.rsn_ucastkeylen); RFK_VALUE(ni->ni_rsn.rsn_keymgmtset);
	RFK_VALUE(ni->ni_rsn.rsn_keymgmt); RFK_VALUE(ni->ni_rsn.rsn_caps);
	RFK_VALUE(ni->ni_esslen);
	for (i = 0; i < sizeof(ni->ni_bssid); i++) RFK_VALUE(ni->ni_bssid[i]);
	for (i = 0; i < ni->ni_esslen; i++) RFK_VALUE(ni->ni_essid[i]);
	RFK_VALUE(local->rs_nrates);
	for (i = 0; i < local->rs_nrates; i++) RFK_VALUE(local->rs_rates[i]);
	RFK_VALUE(ni->ni_rates.rs_nrates);
	for (i = 0; i < ni->ni_rates.rs_nrates; i++) RFK_VALUE(ni->ni_rates.rs_rates[i]);
	error = rtw88_net_edca_prepare(ic, net->configured_media,
	    IEEE80211_S_AUTH, net->channel, slot, edca);
	if (error != 0)
		return error;
	for (i = 0; i < RTW88_WME_NAC; i++) {
		RFK_VALUE(edca[i].aifsn); RFK_VALUE(edca[i].ecw_min);
		RFK_VALUE(edca[i].ecw_max); RFK_VALUE(edca[i].txop);
	}
	/* Provider snapshots are pure and Giant-owned. Also compare a provider
	 * value change without a generation change, including AMPDU spacing. */
	memset(&profile, 0, sizeof(profile));
	error = ic->ic_ht20_admit == NULL ? EOPNOTSUPP :
	    ic->ic_ht20_admit(ic, &profile);
	RFK_VALUE(error); RFK_VALUE(profile.generation);
	RFK_VALUE(profile.rx_mcs); RFK_VALUE(profile.tx_mcs);
	RFK_VALUE(profile.ready); RFK_VALUE(profile.ampdu);
	RFK_VALUE(ni->ni_ht20.attempt); RFK_VALUE(ni->ni_ht20.offer_attempt);
	RFK_VALUE(ni->ni_ht20.offered); RFK_VALUE(ni->ni_ht20.valid);
	RFK_VALUE(ni->ni_ht20.security); RFK_VALUE(ni->ni_ht20.subtype);
	RFK_VALUE(ni->ni_ht20.tx_mcs); RFK_VALUE(ni->ni_ht20.peer_rx_mcs);
	RFK_VALUE(ni->ni_ht20.basic_mcs); RFK_VALUE(ni->ni_ht20.peer_cap);
	RFK_VALUE(ni->ni_ht20.operation); RFK_VALUE(ni->ni_ht20.profile.generation);
	RFK_VALUE(ni->ni_ht20.profile.rx_mcs); RFK_VALUE(ni->ni_ht20.profile.tx_mcs);
	RFK_VALUE(ni->ni_ht20.profile.ready); RFK_VALUE(ni->ni_ht20.profile.ampdu);
	RFK_VALUE(ni->ni_ht20.channel == NULL ? 0 :
	    ieee80211_chan2ieee(ic, ni->ni_ht20.channel));
	for (i = 0; i < sizeof(ni->ni_ht20.bssid); i++) RFK_VALUE(ni->ni_ht20.bssid[i]);
#undef RFK_VALUE
	/* AUTH has no assigned AID or RA command. These captured rates/media/
	 * protection values are the future RA inputs; real RUN prepares RA. */
	return 0;
}

/* Giant owns provider calls. Never invoke this under the PCI lock. */
static int
rtw88_net_rfk_capture_current(struct rtw88_net *net)
{
	uint32_t values[192];
	int error;

	KASSERT(!mutex_owned(net->lock));
	error = rtw88_net_rfk_capture(net, values);
	if (error != 0)
		return error;
	return memcmp(values, net->rfk.values, sizeof(values)) != 0 ? ECANCELED : 0;
}

/* The native window starts before allocation. The core's later start must
 * not grant a second new-work budget after KM_SLEEP releases Giant. */
static int
rtw88_net_rfk_budget(const struct rtw88_net *net, uint64_t now)
{
	KASSERT(mutex_owned(net->lock));
	if (now < net->rfk.started_us ||
	    net->rfk.deadline_us < net->rfk.started_us)
		return ESTALE;
	return now >= net->rfk.deadline_us ? ETIMEDOUT : 0;
}

static int
rtw88_net_rfk_enter(void *cookie, int cleanup)
{
	struct rtw88_net *net = cookie;
	int error = 0;

	mutex_enter(net->lock);
	if (net->rfk.generation != net->core->sc_generation ||
	    net->core->sc_stop_requested || !net->core->sc_resources_ready)
		error = ESTALE;
	else if (net->rfk.notice_pending)
		error = ECANCELED;
	else if (!cleanup && (net->policy.dying ||
	    net->rfk.epoch != net->policy.epoch ||
	    net->requested != IEEE80211_S_AUTH))
		error = ECANCELED;
	if (error == 0 && !cleanup)
		error = rtw88_net_rfk_budget(net, rtw88_net_rfk_ticks(net));
	if (error != 0)
		mutex_exit(net->lock);
	return error;
}

static void
rtw88_net_rfk_leave(void *cookie)
{
	struct rtw88_net *net = cookie;

	mutex_exit(net->lock);
}

static uint64_t
rtw88_net_rfk_ticks(void *cookie)
{
	struct timespec ts;

	(void)cookie;
	/* Read the current timecounter, not the cached hardclock snapshot.
	 * Keep 64 bits so a long scheduling pause cannot alias a deadline. */
	nanouptime(&ts);
	return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* This checks an observation, not regulatory or transmission permission. */
static int
rtw88_net_rfk_beacon_current(const struct rtw88_net *net, uint64_t now)
{
	uint64_t span;

	KASSERT(mutex_owned(net->lock));
	if (net->policy.dying || net->tx_quiet || net->rfk.notice_pending ||
	    net->requested != IEEE80211_S_AUTH ||
	    !net->core->sc_started || !net->core->sc_running ||
	    !net->core->sc_resources_ready || net->core->sc_stop_requested ||
	    net->rfk.beacon_epoch == 0 ||
	    net->rfk.beacon_epoch != net->policy.epoch ||
	    net->rfk.beacon_epoch != net->rfk.epoch ||
	    net->rfk.beacon_epoch != net->rfk.control_epoch ||
	    net->rfk.beacon_generation != net->core->sc_generation ||
	    net->rfk.beacon_generation != net->rfk.generation ||
	    net->rfk.beacon_generation != net->rfk.control_generation ||
	    net->rfk.beacon_channel != net->core->sc_channel ||
	    net->rfk.beacon_channel != net->rfk.channel ||
	    net->rfk.beacon_channel != net->rfk.control_channel ||
	    net->rfk.beacon_interval < IEEE80211_BINTVAL_MIN ||
	    net->rfk.beacon_interval > IEEE80211_BINTVAL_MAX ||
	    now < net->rfk.beacon_us)
		return 0;
	/* Keep the existing seven-beacon liveness policy. Subtraction avoids
	 * adding a lifetime to an arrival close to the clock's upper bound. */
	span = (uint64_t)net->rfk.beacon_interval * 1024 * 7;
	return now - net->rfk.beacon_us < span;
}

static int
rtw88_net_rfk_beacon_check(struct rtw88_net *net)
{
	uint64_t now;
	int error;

	KASSERT(mutex_owned(net->lock));
	now = rtw88_net_rfk_ticks(net);
	error = rtw88_net_rfk_budget(net, now);
	if (error != 0)
		return error;
	return rtw88_net_rfk_beacon_current(net, now) ? 0 : ESTALE;
}

static int
rtw88_net_rfk_wait(void *cookie, unsigned us, int cleanup)
{
	struct rtw88_net *net = cookie;
	uint64_t count, now, previous, until;
	unsigned remaining = us;
	int error;

	KASSERT(!mutex_owned(net->lock));
	rtw88_net_rfk_publish(net);
	if (us == 0 || us > INT_MAX || hz <= 0)
		return EINVAL;
	previous = rtw88_net_rfk_ticks(cookie);
	if (previous > UINT64_MAX - us)
		return EOVERFLOW;
	until = previous + us;
	do {
		/* kpause rounds a sub-tick report interval up to a whole tick.
		 * At 100 Hz that consumes the complete coefficient budget.
		 * yield releases Giant and does not hold PCI. Preserve the
		 * minimum delay even when the scheduler returns immediately. */
		count = ((uint64_t)remaining * (unsigned)hz + 999999) / 1000000;
		if (count > INT_MAX)
			return EINVAL;
		if ((uint64_t)remaining * (unsigned)hz < 1000000) {
			yield();
		} else {
			error = kpause("rtwrfk", false, (int)count, NULL);
			if (error != 0 && error != EWOULDBLOCK)
				return error;
		}
		/* Giant is held again after the scheduler return. A pending AP
		 * event must close TX before any later RFK or cleanup I/O. */
		rtw88_net_rfk_rx_deliver(net);
		/* Cleanup retains the old owner, not new request values. The
		 * core rechecks its lease and deadline before the next IO. */
		if (!cleanup) {
			error = rtw88_net_rfk_capture_current(net);
			if (error != 0)
				return error;
		}
		now = rtw88_net_rfk_ticks(cookie);
		if (now < previous)
			return ESTALE;
		previous = now;
		if (now >= until)
			break;
		remaining = (unsigned)(until - now);
	} while (true);
	return 0;
}

/* Tune before listening. No ordinary RX admission is needed for this IRQ
 * record. The original AUTH budget includes allocation, drain and this wait. */
static int
rtw88_net_rfk_listen(struct rtw88_net *net)
{
	uint64_t now, previous = net->rfk.started_us, remaining;
	int current, error;

	KASSERT(!mutex_owned(net->lock));
	for (;;) {
		rtw88_net_rfk_rx_deliver(net);
		error = rtw88_net_rfk_capture_current(net);
		if (error != 0)
			return error;
		mutex_enter(net->lock);
		now = rtw88_net_rfk_ticks(net);
		error = now < previous ? ESTALE : rtw88_net_rfk_budget(net, now);
		current = error == 0 && rtw88_net_rfk_beacon_current(net, now);
		mutex_exit(net->lock);
		if (error != 0)
			return error;
		previous = now;
		if (current)
			return 0;
		remaining = net->rfk.deadline_us - now;
		error = rtw88_net_rfk_wait(net, (unsigned)MIN(remaining, 10000), 0);
		if (error != 0)
			return error;
	}
}

static int
rtw88_net_rfk_begin(struct rtw88_net *net)
{
	struct rtw8822c_rfk_services svc = {
	    .cookie = net, .enter = rtw88_net_rfk_enter,
	    .leave = rtw88_net_rfk_leave, .wait = rtw88_net_rfk_wait,
	    .clock_ticks = rtw88_net_rfk_ticks, .ticks_per_second = 1000000 };
	uint32_t values[192];
	int error;

	error = rtw88_net_rfk_capture(net, values);
	if (error != 0)
		return error;
	if (rtw8822c_rfk_active(net->core)) {
		if (memcmp(values, net->rfk.values, sizeof(values)) != 0)
			return ESTALE;
		return rtw8822c_rfk_begin(net->core, &svc);
	}
	net->rfk.started_us = rtw88_net_rfk_ticks(net);
	if (net->rfk.started_us > UINT64_MAX - 6000000)
		return EOVERFLOW;
	net->rfk.deadline_us = net->rfk.started_us + 6000000;
	memcpy(net->rfk.values, values, sizeof(values));
	net->rfk.epoch = net->policy.epoch;
	net->rfk.generation = net->core->sc_generation;
	net->rfk.channel = net->channel;
	net->rfk.drained = net->rfk.ran = 0;
	error = rtw8822c_rfk_begin(net->core, &svc);
	/* KM_SLEEP can release Giant. No pre-allocation borrowed value is trust. */
	if (error == 0) {
		error = rtw88_net_rfk_capture(net, values);
		if (error == 0 && memcmp(values, net->rfk.values, sizeof(values)) != 0)
			error = ESTALE;
		/* Allocation can also expose a global quarantine without a BSS
		 * change. Recheck the existing lease before channel/power IO. */
		if (error == 0)
			error = rtw8822c_rfk_begin(net->core, &svc);
	}
	return error;
}

/* Snapshot once at each phase boundary under the IRQ's PCI lock. Validate
 * only an existing lease: inactive begin may allocate and rebind its token. */
static int
rtw88_net_rfk_phase(struct rtw88_net *net)
{
	uint64_t frontier;
	int error;

	KASSERT(mutex_owned(net->lock));
	net->rfk.frontier_valid = 0;
	net->rfk.beacon_epoch = 0;
	if (net->policy.dying || net->rfk.notice_pending ||
	    net->requested != IEEE80211_S_AUTH ||
	    net->rfk.epoch != net->policy.epoch ||
	    net->rfk.control_epoch != net->rfk.epoch)
		return ECANCELED;
	if (net->rfk.generation != net->core->sc_generation ||
	    net->rfk.control_generation != net->rfk.generation ||
	    net->rfk.channel != net->core->sc_channel ||
	    net->rfk.control_channel != net->rfk.channel ||
	    !net->core->sc_resources_ready || !rtw8822c_rfk_active(net->core))
		return ESTALE;
	error = rtw88_net_rfk_budget(net, rtw88_net_rfk_ticks(net));
	if (error == 0)
		error = rtw8822c_rfk_validate(net->core);
	if (error == 0)
		error = rtw88_rx_frontier(net->core, net->rfk.generation, &frontier);
	if (error != 0)
		return error;
	net->rfk.frontier = frontier;
	net->rfk.frontier_generation = net->rfk.generation;
	net->rfk.frontier_valid = 1;
	return 0;
}
