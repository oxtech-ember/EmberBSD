/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Internal default-OFF RFK worker services. Giant owns all shared-stack values.
 * No node, key, channel or provider pointer survives a sleep as an identity. */
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

static int
rtw88_net_rfk_enter(void *cookie, int cleanup)
{
	struct rtw88_net *net = cookie;
	int error = 0;

	mutex_enter(net->lock);
	if (net->rfk.generation != net->core->sc_generation ||
	    net->core->sc_stop_requested || !net->core->sc_resources_ready)
		error = ESTALE;
	else if (!cleanup && (net->policy.dying ||
	    net->rfk.epoch != net->policy.epoch ||
	    net->requested != IEEE80211_S_AUTH))
		error = ECANCELED;
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

static unsigned
rtw88_net_rfk_ticks(void *cookie)
{

	(void)cookie;
	return (unsigned)getticks();
}

static int
rtw88_net_rfk_wait(void *cookie, unsigned us, int cleanup)
{
	struct rtw88_net *net = cookie;
	uint32_t values[192];
	uint64_t count = ((uint64_t)us * (unsigned)hz + 999999) / 1000000;
	int error;

	KASSERT(!mutex_owned(net->lock));
	rtw88_net_rfk_publish(net);
	if (count > INT_MAX)
		return EINVAL;
	error = kpause("rtwrfk", false, (int)MAX(count, 1), NULL);
	if (error != 0 && error != EWOULDBLOCK)
		return error;
	/* Cleanup never relabels the old owner with new request values. The
	 * core rechecks global owner, resources, firmware and deadline per IO. */
	if (cleanup)
		return 0;
	error = rtw88_net_rfk_capture(net, values);
	if (error != 0)
		return error;
	return memcmp(values, net->rfk.values, sizeof(values)) == 0 ? 0 : ECANCELED;
}

static int
rtw88_net_rfk_begin(struct rtw88_net *net)
{
	struct rtw8822c_rfk_services svc = {
	    .cookie = net, .enter = rtw88_net_rfk_enter,
	    .leave = rtw88_net_rfk_leave, .wait = rtw88_net_rfk_wait,
	    .clock_ticks = rtw88_net_rfk_ticks, .ticks_per_second = (unsigned)hz };
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
	}
	return error;
}
