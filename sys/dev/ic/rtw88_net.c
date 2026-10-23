/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original native net80211 integration, AI-assisted. */
/*
 * One always-awake station, legacy 20 MHz rates and software crypto.
 * DMA completion is not a radio ACK. RF calibration and power policy
 * belong to the core. This wrapper does not establish hardware acceptance.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/device.h>
#include <sys/kmem.h>
#include <sys/kernel.h>
#include <sys/atomic.h>
#include <sys/mbuf.h>
#include <sys/callout.h>
#include <sys/timevar.h>
#include <sys/workqueue.h>
#include <sys/intr.h>
#include <net/if.h>
#include <net/if_ether.h>
#include <net/if_media.h>
#include <net/bpf.h>
#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_crypto.h>
#include <net80211/ieee80211_beacon_ies.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw88_net.h>
#include <dev/ic/rtw88_net_policy.h>
#include <dev/ic/rtw88_net_mbuf.h>
#ifdef RTW88_RFK_EXPERIMENT
#include <dev/ic/rtw8822c_rfk.h>
struct rtw88_net_rfk {
	uint64_t epoch;
	uint32_t generation;
	unsigned channel, drained, ran;
	uint32_t values[192];
	/* Numeric current-peer watch and IRQ-to-Giant withdrawal notification. */
	uint64_t control_epoch, notice_epoch;
	uint32_t control_generation, notice_generation;
	unsigned control_channel, notice_channel, notice_pending;
	uint8_t control_ra[6], control_ta[6], control_bssid[6];
};
#endif

struct rtw88_net_rx {
	struct mbuf *m;
	uint64_t epoch, arrived;
	uint32_t tsf, generation, tick;
	uint8_t quality;
};

struct rtw88_net {
	struct ethercom ec;
	struct ieee80211com ic;
	struct rtw88_softc *core;
	device_t dev;
	kmutex_t *lock;
	struct workqueue *wq;
	struct work state_work;
	void *softint;
	callout_t scan, rxba_callout;
	uint64_t rxba_token, rxba_deadline, rxba_due;
	/* Monotonic generation and profile word are published with atomic CAS. */
	volatile uint64_t ht20_snapshot;
	volatile uint32_t ht20_generation;
	struct rtw88_net_policy policy;
	struct rtw88_net_rx rx[RTW88_NET_RX_LIMIT];
	unsigned rx_head, rx_tail;
	int work_pending;
	int reset_required;	/* Linked-peer quiescence survives request coalescing. */
	int tx_quiet, quiet_error; /* Sticky until detach; state requests cannot clear it. */
	enum ieee80211_state requested;
	unsigned channel;
	unsigned configured_media; /* User configuration, not peer-selected mode. */
	int state_arg;
	int (*parent_state)(struct ieee80211com *, enum ieee80211_state, int);
	uint8_t txbuf[RTW88_TX_BUF_SIZE];
	uint64_t rx_dropped;
	struct rtw88_net_stats stats;
	unsigned tx_timer;
	unsigned tx_completed;
#ifdef RTW88_RFK_EXPERIMENT
	struct rtw88_net_rfk rfk;
#endif
};

static void rtw88_net_start(struct ifnet *);
static void rtw88_net_stop(struct ifnet *, int);
static int rtw88_net_init(struct ifnet *);
static int rtw88_net_newstate(struct ieee80211com *, enum ieee80211_state, int);
static void rtw88_net_ht20_publish(struct rtw88_net *,
    const struct ieee80211_ht20_profile *);
static int rtw88_net_ht20_admit(struct ieee80211com *,
    struct ieee80211_ht20_profile *);
static int rtw88_net_ht20_local_policy(struct ieee80211com *);
static int rtw88_net_ht20_profile_prepare(struct rtw88_net *,
    struct ieee80211_ht20_profile *);

/* Giant owns the spectrum event. Close admission before serialized device I/O.
 * Shared net80211 purges RX BA and requests INIT after this callback returns.
 * The existing worker owns sleeping stop and DMA release. */
static void
rtw88_net_sta_quiet(struct ieee80211com *ic)
{
	struct rtw88_net *net = ic->ic_ifp->if_softc;
	struct ifnet *ifp = &net->ec.ec_if;

	if (net->tx_quiet)
		return;
	rtw88_net_ht20_publish(net, NULL);
	callout_stop(&net->scan);
	mutex_enter(net->lock);
	net->tx_quiet = 1;
	(void)rtw88_net_request(&net->policy);
	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
	ifp->if_timer = 0;
	net->tx_timer = 0;
	net->quiet_error = rtw88_tx_quiet(net->core);
	mutex_exit(net->lock);
}

/* A beacon does not authorize active probes on the driver's DFS channels.
 * The scan channel can differ from the BSS node's previous channel. */
static int
rtw88_net_scan_probe_admit(struct ieee80211com *ic)
{
	unsigned channel;

	if (ic->ic_curchan == NULL || ic->ic_curchan == IEEE80211_CHAN_ANYC)
		return EACCES;
	channel = ieee80211_chan2ieee(ic, ic->ic_curchan);
	if (channel == 0 || channel > IEEE80211_CHAN_MAX ||
	    (channel >= 52 && channel <= 144))
		return EACCES;
	return 0;
}

/* This MPSAFE callout retains only interface-owned numeric tokens. The shared
 * stack and all node/key lifetime work run in the existing Giant softintr. */
static uint64_t
rtw88_net_rxba_now(struct ieee80211com *ic)
{
	struct timespec ts;
	uint64_t ms, fraction;

	(void)ic;
	getnanouptime(&ts);
	if (ts.tv_sec < 0 || (uint64_t)ts.tv_sec > UINT64_MAX / 1000)
		return UINT64_MAX;
	ms = (uint64_t)ts.tv_sec * 1000;
	fraction = (uint64_t)ts.tv_nsec / 1000000;
	return fraction > UINT64_MAX - ms ? UINT64_MAX : ms + fraction;
}

static int
rtw88_net_rxba_ticks(uint64_t now, uint64_t deadline)
{
	uint64_t span = deadline > now ? deadline - now : 0, count;

	if (now == UINT64_MAX || span > INT_MAX || hz <= 0)
		return 0;
	count = (span * (unsigned)hz + 999) / 1000;
	return count > INT_MAX ? 0 : (int)MAX(count, 1);
}

static void
rtw88_net_rxba_timeout(void *cookie)
{
	struct rtw88_net *net = cookie;
	uint64_t now = rtw88_net_rxba_now(&net->ic);
	int delay;

	mutex_enter(net->lock);
	if (!net->policy.dying && net->rxba_token != 0) {
		if (now < net->rxba_deadline) {
			delay = rtw88_net_rxba_ticks(now, net->rxba_deadline);
			/* The original validated span bounds this early callback. */
			if (delay != 0)
				callout_schedule(&net->rxba_callout, delay);
		} else {
			net->rxba_due = net->rxba_token;
			net->rxba_token = 0;
			softint_schedule(net->softint);
		}
	}
	mutex_exit(net->lock);
}

static int
rtw88_net_rxba_schedule(struct ieee80211com *ic, uint64_t deadline,
    uint64_t token)
{
	struct rtw88_net *net = ic->ic_ifp->if_softc;
	int error = 0, delay;

	KASSERT(!mutex_owned(net->lock));
	delay = rtw88_net_rxba_ticks(rtw88_net_rxba_now(ic), deadline);
	if (token == 0 || delay == 0)
		return EOVERFLOW;
	mutex_enter(net->lock);
	if (net->policy.dying)
		error = ENXIO;
	else {
		net->rxba_deadline = deadline;
		net->rxba_token = token;
		net->rxba_due = 0;
		callout_schedule(&net->rxba_callout, delay);
	}
	mutex_exit(net->lock);
	return error;
}

static void
rtw88_net_rxba_cancel(struct ieee80211com *ic)
{
	struct rtw88_net *net = ic->ic_ifp->if_softc;

	KASSERT(!mutex_owned(net->lock));
	mutex_enter(net->lock);
	net->rxba_token = net->rxba_due = 0;
	net->rxba_deadline = 0;
	callout_stop(&net->rxba_callout);
	mutex_exit(net->lock);
}

static void
rtw88_net_rxba_publish(struct rtw88_net *net)
{
	struct ieee80211com *ic = &net->ic;
	struct ieee80211_node *ni = ic->ic_bss;
	unsigned mask = ni != NULL ? ieee80211_rxba_mask(ic, ni) : 0;

	/* Giant protects the node. PCI receives no borrowed node/key pointers. */
	mutex_enter(net->lock);
	net->policy.rxba_mask = 0;
	if (mask != 0 && net->policy.accepting && !net->policy.dying &&
	    (net->ec.ec_if.if_flags & IFF_RUNNING) != 0) {
		memcpy(net->policy.rxba_ra, ic->ic_myaddr, 6);
		memcpy(net->policy.rxba_ta, ni->ni_bssid, 6);
		net->policy.rxba_mask = mask;
	}
	mutex_exit(net->lock);
}

static void
rtw88_net_rxba_softintr(struct rtw88_net *net)
{
	struct ieee80211com *ic = &net->ic;
	struct ieee80211_node *ni;
	uint64_t token, now = rtw88_net_rxba_now(ic);
	int valid;

	mutex_enter(net->lock);
	token = net->rxba_due;
	/* A frame can rearm an already expired absolute deadline before the
	 * callout executes. Do not let repeated RX postpone that hole forever. */
	if (token == 0 && net->rxba_token != 0 && now >= net->rxba_deadline) {
		token = net->rxba_token;
		net->rxba_token = 0;
		callout_stop(&net->rxba_callout);
	}
	net->rxba_due = 0;
	valid = !net->policy.dying && net->policy.accepting &&
	    (net->ec.ec_if.if_flags & IFF_RUNNING) != 0;
	mutex_exit(net->lock);
	/* Acquire the current BSS only after entering Giant. A late callback never
	 * retains a freed node or key and cannot pass the shared token fence. */
	if (token != 0 && valid && ic->ic_bss != NULL) {
		ni = ieee80211_ref_node(ic->ic_bss);
		ieee80211_rxba_age(ic, ni, token);
		ieee80211_free_node(ni);
	}
	rtw88_net_rxba_publish(net);
}

static const struct ieee80211_rxba_ops rtw88_net_rxba_ops = {
	rtw88_net_rxba_now, rtw88_net_rxba_schedule, rtw88_net_rxba_cancel
};

static void
rtw88_net_rxba_quiesce(struct rtw88_net *net)
{

	rtw88_net_rxba_cancel(&net->ic);
	/* The MPSAFE callback never waits for Giant. No PCI lock is held here. */
	callout_halt(&net->rxba_callout, NULL);
}

static void
rtw88_net_rxba_attach(struct rtw88_net *net)
{

	callout_init(&net->rxba_callout, CALLOUT_MPSAFE);
	callout_setfunc(&net->rxba_callout, rtw88_net_rxba_timeout, net);
	net->ic.ic_rxba_ops = &rtw88_net_rxba_ops;
}

static void
rtw88_net_rxba_detach(struct rtw88_net *net)
{

	rtw88_net_rxba_quiesce(net);
	net->ic.ic_rxba_ops = NULL;
	callout_destroy(&net->rxba_callout);
}

/*
 * Every update gets a monotonic generation. The payload and generation are
 * then published together in one 64-bit compare-and-swap; a late writer can
 * never replace a newer readiness decision with an older one.
 */
static void
rtw88_net_ht20_publish(struct rtw88_net *net,
    const struct ieee80211_ht20_profile *profile)
{
	struct rtw88_net_ht20_snapshot snapshot;
	uint64_t word, current;
	uint32_t generation, previous;

	for (;;) {
		previous = atomic_load_relaxed(&net->ht20_generation);
		if (previous == UINT32_MAX) {
			generation = UINT32_MAX;
			break;
		}
		generation = previous + 1;
		if (atomic_cas_32(&net->ht20_generation, previous,
		    generation) == previous)
			break;
	}
	snapshot.generation = generation;
	snapshot.rx_mcs = snapshot.tx_mcs = 0;
	snapshot.ready = snapshot.ampdu = 0;
	if (profile != NULL && generation != UINT32_MAX) {
		snapshot.rx_mcs = profile->rx_mcs;
		snapshot.tx_mcs = profile->tx_mcs;
		snapshot.ready = profile->ready;
		snapshot.ampdu = profile->ampdu;
	}
	if (rtw88_net_ht20_snapshot_encode(&snapshot, &word) != 0) {
		snapshot.rx_mcs = snapshot.tx_mcs = 0;
		snapshot.ready = snapshot.ampdu = 0;
		(void)rtw88_net_ht20_snapshot_encode(&snapshot, &word);
	}
	for (;;) {
		current = atomic_load_acquire(&net->ht20_snapshot);
		if ((uint32_t)current >= generation)
			return;
		if (atomic_cas_64(&net->ht20_snapshot, current, word) == current)
			return;
	}
}

static int
rtw88_net_ht20_admit(struct ieee80211com *ic,
    struct ieee80211_ht20_profile *profile)
{
	struct rtw88_net_ht20_snapshot snapshot;
	struct rtw88_net *net;
	uint64_t word;
	int error;

	if (ic == NULL || profile == NULL || ic->ic_ifp == NULL)
		return EINVAL;
	net = ic->ic_ifp->if_softc;
	if (net == NULL)
		return ENXIO;
#ifdef RTW88_RFK_EXPERIMENT
	/* A published snapshot cannot outlive the current RFK proof. */
	if (net->core == NULL || !net->core->sc_operating.sealed ||
	    rtw8822c_rfk_seal(net->core) != 0)
		return EOPNOTSUPP;
#endif
	word = atomic_load_acquire(&net->ht20_snapshot);
	error = rtw88_net_ht20_snapshot_admit(word, &snapshot);
	if (error != 0)
		return error;
	profile->generation = snapshot.generation;
	profile->rx_mcs = snapshot.rx_mcs;
	profile->tx_mcs = snapshot.tx_mcs;
	profile->ready = snapshot.ready;
	profile->ampdu = snapshot.ampdu;
	return 0;
}

static unsigned
rtw88_net_basic_rates(struct ieee80211com *ic)
{
	static const uint8_t rates[] = { 2, 4, 11, 22, 12, 18, 24, 36,
	    48, 72, 96, 108 };
	const struct ieee80211_rateset *rs = &ic->ic_bss->ni_rates;
	unsigned mask = 0, i, j;

	for (i = 0; i < rs->rs_nrates; i++) {
		if ((rs->rs_rates[i] & IEEE80211_RATE_BASIC) == 0)
			continue;
		for (j = 0; j < __arraycount(rates); j++) {
			if ((rs->rs_rates[i] & IEEE80211_RATE_VAL) == rates[j])
				mask |= 1U << j;
		}
	}
	return mask;
}

/*
 * Strict STA callback: caller holds Giant and the beacon lock. This PCI
 * operation is bounded and does not sleep. Core I/O failures latch TX and
 * station setup until full successful restart; software rollback is not HW
 * rollback. No PCI lock holder may enter net80211 with that lock held.
 */
static int
rtw88_net_wme_apply(struct ieee80211com *ic,
    const struct chanAccParams *params, unsigned slot)
{
	struct rtw88_net *net = ic->ic_ifp->if_softc;
	struct rtw88_edca_params edca[RTW88_WME_NAC];
	unsigned ac, channel;
	int error;

	if (params == NULL)
		return EINVAL;
	channel = ieee80211_chan2ieee(ic, ic->ic_curchan);
	for (ac = 0; ac < RTW88_WME_NAC; ac++) {
		const struct wmeParams *p = &params->cap_wmeParams[ac];
		edca[ac].aifsn = p->wmep_aifsn;
		edca[ac].ecw_min = p->wmep_logcwmin;
		edca[ac].ecw_max = p->wmep_logcwmax;
		edca[ac].txop = p->wmep_txopLimit;
	}
	mutex_enter(net->lock);
	if (net->policy.dying)
		error = ENXIO;
	else if (net->work_pending || net->core->sc_channel != channel)
		error = EBUSY;
	else
		error = rtw88_edca_apply(net->core, edca, slot);
	mutex_exit(net->lock);
	return error;
}

/* Prepare outside the PCI lock. No media lock is acquired by the worker. */
static int
rtw88_net_edca_prepare(struct ieee80211com *ic, unsigned media,
    enum ieee80211_state state, unsigned channel, unsigned slot,
    struct rtw88_edca_params edca[RTW88_WME_NAC])
{
	struct ieee80211_node *ni = ic->ic_bss;
	enum ieee80211_phymode mode;
	unsigned flags, ac, cwmin;

	if (channel == 0 || channel > IEEE80211_CHAN_MAX ||
	    ic->ic_curchan != &ic->ic_channels[channel] || ni == NULL ||
	    (slot != 9 && slot != 20) || (channel > 14 && slot != 9) ||
	    IFM_TYPE(media) != IFM_IEEE80211)
		return EINVAL;
	flags = ic->ic_channels[channel].ic_flags;
	if ((flags & (IEEE80211_CHAN_TURBO | IEEE80211_CHAN_HALF |
	    IEEE80211_CHAN_QUARTER | IEEE80211_CHAN_GFSK |
	    IEEE80211_CHAN_GSM)) != 0 || (media & IFM_IEEE80211_TURBO) != 0)
		return EOPNOTSUPP;
	if ((flags & (IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_5GHZ)) !=
	    (channel > 14 ? IEEE80211_CHAN_5GHZ : IEEE80211_CHAN_2GHZ))
		return EINVAL;
	switch (IFM_MODE(media)) {
	case IFM_AUTO:
		/* Fixed rate locks mode in media_change or legacy cfgset. */
		if (ic->ic_fixed_rate != IEEE80211_FIXED_RATE_NONE)
			mode = ic->ic_curmode;
		else if (channel > 14)
			mode = IEEE80211_MODE_11A;
		else if ((flags & (IEEE80211_CHAN_OFDM | IEEE80211_CHAN_DYN)) != 0)
			mode = IEEE80211_MODE_11G;
		else
			mode = IEEE80211_MODE_11B;
		break;
	case IFM_IEEE80211_11A:
		mode = IEEE80211_MODE_11A;
		break;
	case IFM_IEEE80211_11B:
		mode = IEEE80211_MODE_11B;
		break;
	case IFM_IEEE80211_11G:
		mode = IEEE80211_MODE_11G;
		break;
	default:
		return EOPNOTSUPP;
	}
	if (mode == IEEE80211_MODE_11A) {
		if (channel <= 14 || (flags & IEEE80211_CHAN_OFDM) == 0)
			return EINVAL;
	} else if (mode == IEEE80211_MODE_11B) {
		if (channel > 14 || slot != 20 ||
		    (flags & IEEE80211_CHAN_CCK) == 0)
			return EINVAL;
	} else if (mode == IEEE80211_MODE_11G) {
		if (channel > 14 ||
		    (flags & (IEEE80211_CHAN_OFDM | IEEE80211_CHAN_DYN)) == 0)
			return EINVAL;
	} else {
		return EOPNOTSUPP;
	}
	if (state == IEEE80211_S_RUN &&
	    (ni->ni_flags & (IEEE80211_NODE_QOS | IEEE80211_NODE_WME_VALID))) {
		if ((ni->ni_flags & (IEEE80211_NODE_QOS | IEEE80211_NODE_WME_VALID)) !=
		    (IEEE80211_NODE_QOS | IEEE80211_NODE_WME_VALID) ||
		    (ic->ic_caps & IEEE80211_C_WME) == 0 ||
		    (ic->ic_flags & IEEE80211_F_WME) == 0 ||
		    ni->ni_wme_applied_chan != ic->ic_curchan ||
		    ni->ni_wme_applied_slot != slot)
			return EPROTO;
		IEEE80211_BEACON_LOCK(ic);
		for (ac = 0; ac < RTW88_WME_NAC; ac++) {
			const struct wmeParams *p =
			    &ic->ic_wme.wme_chanParams.cap_wmeParams[ac];
			edca[ac].aifsn = p->wmep_aifsn;
			edca[ac].ecw_min = p->wmep_logcwmin;
			edca[ac].ecw_max = p->wmep_logcwmax;
			edca[ac].txop = p->wmep_txopLimit;
		}
		IEEE80211_BEACON_UNLOCK(ic);
	} else {
		/* Legacy DCF on every AC; no WME policy or advertisement. */
		cwmin = mode == IEEE80211_MODE_11B ? 5 : 4;
		for (ac = 0; ac < RTW88_WME_NAC; ac++)
			edca[ac] = (struct rtw88_edca_params){ 2, cwmin, 10, 0 };
	}
	return 0;
}

/* The worker and network state machine share Giant; no PCI lock is held. */
static int
rtw88_net_ht20_local_policy(struct ieee80211com *ic)
{
	unsigned mtu;

	if (ic == NULL || ic->ic_ifp == NULL)
		return EINVAL;
	mtu = ic->ic_ifp->if_mtu;
	if (ic->ic_curchan == NULL || ic->ic_curmode != IEEE80211_MODE_11A ||
	    ieee80211_chan2ieee(ic, ic->ic_curchan) <= 14 ||
	    ic->ic_fixed_rate != IEEE80211_FIXED_RATE_NONE ||
	    (ic->ic_flags & IEEE80211_F_DATAPAD) != 0 ||
	    ic->ic_fragthreshold != RTW88_HT20_THRESHOLD_MAX ||
	    mtu < RTW88_HT20_MTU_MIN || mtu > RTW88_HT20_MTU_MAX ||
	    ic->ic_rtsthreshold < mtu + RTW88_HT20_TX_OVERHEAD +
	    RTW88_HT20_FCS_SIZE ||
	    ic->ic_rtsthreshold > RTW88_HT20_THRESHOLD_MAX)
		return EOPNOTSUPP;
	return 0;
}

/* Publish only a proven local 5 GHz HT20 envelope. Giant serializes core
 * start/stop, protocol state and the non-MPSAFE state worker; the atomic
 * snapshot then fences any delayed publication against a newer withdrawal. */
static int
rtw88_net_ht20_profile_prepare(struct rtw88_net *net,
    struct ieee80211_ht20_profile *profile)
{
	struct ieee80211com *ic;
	struct rtw88_softc *sc;
	unsigned ready = 0;

	if (net == NULL || profile == NULL || net->core == NULL)
		return EINVAL;
	memset(profile, 0, sizeof(*profile));
	ic = &net->ic;
	sc = net->core;
#ifdef RTW88_RFK_EXPERIMENT
	/* Revalidate an existing RFK proof. Admission cannot create one. */
	if (!sc->sc_operating.sealed || rtw8822c_rfk_seal(sc) != 0)
		return EOPNOTSUPP;
#endif
	if (ic->ic_opmode != IEEE80211_M_STA ||
	    rtw88_net_ht20_local_policy(ic) != 0)
		return EOPNOTSUPP;
	if (sc->sc_resources_ready && sc->sc_started && sc->sc_running &&
	    sc->sc_rf_enabled && !sc->sc_stop_requested)
		ready |= RTW88_NET_HT20_READY_RADIO;
	/* power_dirty means shutdown still owes an off sequence, even while the
	 * active power state is confirmed and healthy. */
	if (sc->sc_resources_ready && sc->sc_powered &&
	    sc->sc_power_error == 0 && sc->sc_dma_master && sc->sc_dma_error == 0)
		ready |= RTW88_NET_HT20_READY_POWER;
	/* This same bounded mixed-format/no-RTS policy is rechecked with the AP. */
	ready |= RTW88_NET_HT20_READY_PROTECTION;
	if (ieee80211_rxba_ready(ic))
		ready |= RTW88_NET_HT20_READY_RXBA;
	if (net->wq != NULL && net->softint != NULL)
		ready |= RTW88_NET_HT20_READY_SCHEDULER;
	if ((ic->ic_caps & IEEE80211_C_WME) != 0 &&
	    (ic->ic_flags & IEEE80211_F_WME) != 0 &&
	    ic->ic_wme.wme_sta_apply != NULL)
		ready |= RTW88_NET_HT20_READY_AMSDU;
	if (ready != RTW88_NET_HT20_READY_ALL)
		return EOPNOTSUPP;
	profile->rx_mcs = 0xff;
	profile->tx_mcs = 0xff;
	profile->ready = ready;
	profile->ampdu = 0x10; /* Factor 0; conservative spacing 4. */
	return 0;
}

/* Giant owns peer/policy fields. The provider never runs under PCI. */
static int
rtw88_net_tx_ht20_policy(struct ieee80211com *ic, struct rtw88_ra_config *cfg)
{
	struct ieee80211_ht20_protection protection;
	struct ieee80211_node *ni = ic->ic_bss;
	unsigned mtu, maximum;
	int error;

	error = ieee80211_ht20_tx_protection(ic, ni, &protection);
	if (error != 0)
		return error;
	if (ic->ic_ifp == NULL)
		return EINVAL;
	mtu = ic->ic_ifp->if_mtu;
	if (rtw88_net_ht20_local_policy(ic) != 0 ||
	    protection.qualification != IEEE80211_HT20_PROT_MF20_NO_RTS ||
	    ni->ni_ht20.profile.rx_mcs != 0xff ||
	    ni->ni_ht20.profile.tx_mcs != 0xff || ni->ni_ht20.tx_mcs == 0 ||
	    (ni->ni_ht20.tx_mcs & ~0xffU) != 0)
		return EOPNOTSUPP;
	/* Validate the MTU before adding the bounded encapsulation overhead. */
	maximum = mtu + RTW88_HT20_TX_OVERHEAD;
	cfg->phy = RTW88_RA_HT20_1SS;
	cfg->ht_mcs = ni->ni_ht20.tx_mcs;
	cfg->ht_operation = protection.operation;
	cfg->profile_generation = protection.generation;
	cfg->ht.qualification = RTW88_HT20_PROT_MF20_NO_RTS;
	cfg->ht.peer_cap = protection.peer_cap;
	cfg->ht.secondary = protection.secondary;
	cfg->ht.mtu = mtu;
	cfg->ht.max_mpdu = maximum;
	cfg->ht.rts_threshold = ic->ic_rtsthreshold;
	return 0;
}

static int
rtw88_net_ra_prepare(struct ieee80211com *ic, unsigned channel,
    unsigned basic, uint64_t epoch, struct rtw88_ra_config *cfg)
{
	static const uint8_t rates[] = { 2, 4, 11, 22, 12, 18, 24, 36,
	    48, 72, 96, 108 };
	const struct ieee80211_rateset *local, *peer;
	struct ieee80211_node *ni = ic->ic_bss;
	unsigned i, j, k, local_mask = 0, peer_mask = 0;

	memset(cfg, 0, sizeof(*cfg));
	if (ni == NULL || ni->ni_chan != ic->ic_curchan ||
	    ic->ic_opmode != IEEE80211_M_STA ||
	    (ni->ni_associd & 0x3fff) == 0 ||
	    (ni->ni_associd & 0x3fff) > 2007 || epoch == 0)
		return EINVAL;
	switch (ic->ic_curmode) {
	case IEEE80211_MODE_11A: cfg->mode = RTW88_FIXED_11A; break;
	case IEEE80211_MODE_11B: cfg->mode = RTW88_FIXED_11B; break;
	case IEEE80211_MODE_11G: cfg->mode = RTW88_FIXED_11G; break;
	default: return EOPNOTSUPP;
	}
	if ((cfg->mode == RTW88_FIXED_11A) != (channel > 14))
		return EINVAL;
	local = &ic->ic_sup_rates[ic->ic_curmode];
	peer = &ni->ni_rates;
	if (local->rs_nrates > __arraycount(local->rs_rates) ||
	    peer->rs_nrates > __arraycount(peer->rs_rates))
		return EINVAL;
	for (i = 0; i < local->rs_nrates; i++)
		for (j = 0; j < __arraycount(rates); j++)
			if ((local->rs_rates[i] & IEEE80211_RATE_VAL) == rates[j])
				local_mask |= 1U << j;
	for (i = 0; i < peer->rs_nrates; i++)
		for (k = 0; k < __arraycount(rates); k++)
			if ((peer->rs_rates[i] & IEEE80211_RATE_VAL) == rates[k])
				peer_mask |= 1U << k;
	cfg->common_rates = local_mask & peer_mask;
	if (cfg->mode == RTW88_FIXED_11A)
		cfg->common_rates &= 0xff0;
	else if (cfg->mode == RTW88_FIXED_11B)
		cfg->common_rates &= 0xf;
	if (cfg->common_rates == 0 || basic == 0 ||
	    (basic & ~cfg->common_rates) != 0)
		return EINVAL;
	cfg->channel = channel;
	cfg->epoch = epoch;
	cfg->use_protection = (ic->ic_flags & IEEE80211_F_USEPROT) != 0;
	if (ni->ni_ht20.valid || ni->ni_ht20.offered ||
	    ni->ni_ht20.profile.generation != 0) {
		if (!ni->ni_ht20.valid ||
		    ieee80211_ht20_state_request(ic, IEEE80211_S_RUN) != 0)
			return EINVAL;
		return rtw88_net_tx_ht20_policy(ic, cfg);
	}
	return 0;
}

#ifdef RTW88_RFK_EXPERIMENT
#include <dev/ic/rtw88_net_rfk.inc>
#endif

/* Giant serializes state and queues. Never wait for this worker from itself. */
static int
rtw88_net_restart_peer(struct rtw88_net *net)
{
	struct ifnet *ifp = &net->ec.ec_if;
	int error, start;

	mutex_enter(net->lock);
#ifdef RTW88_RFK_EXPERIMENT
	if (net->rfk.notice_pending) {
		mutex_exit(net->lock);
		return ECANCELED;
	}
#endif
	if (!net->reset_required || net->tx_quiet) {
		mutex_exit(net->lock);
		return 0;
	}
	/* A synchronous stop owner may already have withdrawn RUNNING. */
	start = (ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
	    (IFF_UP | IFF_RUNNING);
	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
	ifp->if_timer = 0;
	net->tx_timer = 0;
	mutex_exit(net->lock);
	/* Stop drains interrupt ownership, powers down and verifies bus master. */
	error = rtw88_stop(net->core);
#ifdef RTW88_RFK_EXPERIMENT
	/* Keep an accepted withdrawal even if power-off or DMA stop failed. */
	rtw88_net_rfk_rx_deliver(net);
#endif
	if (error != 0)
		return error;
	/* Coalescing can bypass shared INIT's management-queue drain. */
	ieee80211_drain_ifq(&net->ic.ic_mgtq);
	mutex_enter(net->lock);
	start = start && (ifp->if_flags & IFF_UP) != 0 &&
	    !net->policy.dying && !net->tx_quiet;
	mutex_exit(net->lock);
	/* INIT must retain a real READY core for the next WPA SCAN_REQ. */
	if (start) {
		error = rtw88_start(net->core);
		if (error != 0)
			return error;
	}
	mutex_enter(net->lock);
	if (start && (net->policy.dying || net->tx_quiet ||
	    (ifp->if_flags & IFF_UP) == 0)) {
		mutex_exit(net->lock);
		error = rtw88_stop(net->core);
		mutex_enter(net->lock);
		if (error == 0)
			net->reset_required = 0;
		mutex_exit(net->lock);
		return error != 0 ? error : ECANCELED;
	}
	net->reset_required = 0;
	if (start)
		ifp->if_flags |= IFF_RUNNING;
	mutex_exit(net->lock);
	return 0;
}

/* The existing capacity fence closes all producers before this checkpoint.
 * Only numeric identity survives a sleep; outer work re-prepares the peer. */
static int
rtw88_net_drain_wait(struct rtw88_net *net, uint64_t epoch, uint32_t gen,
    unsigned deadline, int init, int *retry)
{
	unsigned pending = 0;
	int error;

	*retry = 0;
	mutex_enter(net->lock);
	if (epoch != net->policy.epoch || net->policy.dying)
		error = ECANCELED;
	else if (init && !net->core->sc_started)
		error = 0;
	else if (!net->core->sc_started || !net->core->sc_running)
		error = ENXIO;
	else if (gen != net->core->sc_generation || net->core->sc_stop_requested)
		error = ESTALE;
	else if ((int)(getticks() - deadline) >= 0)
		error = ETIMEDOUT;
	else
		error = rtw88_tx_drain_snapshot(net->core, &pending);
	mutex_exit(net->lock);
	if (error != 0 || pending == 0)
		return error;
	if ((int)(getticks() - deadline) >= 0)
		return ETIMEDOUT;
	error = kpause("rtwdrn", false, 1, NULL);
	if (error == 0 || error == EWOULDBLOCK) {
		*retry = 1;
		return 0;
	}
	return error;
}

/* No mutation before reservation. A wake always returns to the outer worker
 * to recapture HT/BSS/EDCA/RA under Giant, including same-epoch profile changes. */
static int
rtw88_net_mailbox_wait(struct rtw88_net *net, uint64_t epoch, uint32_t gen,
    unsigned box, unsigned count, unsigned deadline, int *retry)
{
	int error, ready = 0;

	*retry = 0;
	mutex_enter(net->lock);
	if (epoch != net->policy.epoch || net->policy.dying)
		error = ECANCELED;
	else if (count != 0 && (gen != net->core->sc_generation ||
	    box != (net->core->sc_h2c_box & 3)))
		error = ESTALE;
	else if (count != 0 && (int)(getticks() - deadline) >= 0)
		error = ETIMEDOUT;
	else
		error = rtw88_fw_mailbox_begin(net->core, count, &ready);
	mutex_exit(net->lock);
	if (error != 0 || ready)
		return error;
	if ((int)(getticks() - deadline) >= 0)
		return ETIMEDOUT;
	/* kpause releases Giant. PCI is never held while sleeping. */
	error = kpause("rtwmbx", false, 1, NULL);
	if (error == 0 || error == EWOULDBLOCK) {
		*retry = 1;
		return 0;
	}
	return error;
}

/* The workqueue has the kernel lock. Do not hold the PCI lock in net80211. */
static void
rtw88_net_state_work(struct work *work, void *cookie)
{
	struct rtw88_net *net = cookie;
	struct ieee80211com *ic = &net->ic;
	struct ifnet *ifp = &net->ec.ec_if;
	struct rtw88_net_state_diag diag;
	struct rtw88_edca_params edca[RTW88_WME_NAC];
	struct rtw88_ra_config ra;
	enum ieee80211_state state;
	uint64_t epoch, mailbox_epoch = 0, drain_epoch = 0;
	uint32_t gen, mailbox_gen = 0, drain_gen = 0;
	unsigned mailbox_box = 0, mailbox_count, mailbox_deadline = 0;
	unsigned drain_deadline = 0;
	unsigned channel, basic, slot, aid, i;
	unsigned media, prepare_stage;
	int use_ra, mailbox_retry, mailbox_reserved;
	struct ieee80211_ht20_profile ht20_profile;
	const uint8_t *bssid;
	char failure[384];
	uint8_t any;
	int arg, error, prepare_error, restart_error, current;

	(void)work;
	for (;;) {
#ifdef RTW88_RFK_EXPERIMENT
		rtw88_net_rfk_rx_deliver(net);
		/* Retire the OLD numeric owner before any restart or early dying
		 * return. Detach keeps resources and net allocated through wait. */
		if (rtw8822c_rfk_active(net->core) &&
		    (net->policy.dying || net->reset_required ||
		     net->rfk.epoch != net->policy.epoch ||
		     net->requested != IEEE80211_S_AUTH))
			(void)rtw8822c_rfk_retire(net->core);
		rtw88_net_rfk_publish(net);
#endif
		mutex_enter(net->lock);
		if (net->policy.dying) {
			net->work_pending = 0;
			mutex_exit(net->lock);
			return;
		}
		epoch = net->policy.epoch;
		state = net->requested;
		channel = net->channel;
		media = net->configured_media;
		arg = net->state_arg;
		mutex_exit(net->lock);
		callout_stop(&net->scan);
		restart_error = rtw88_net_restart_peer(net);
#ifdef RTW88_RFK_EXPERIMENT
		mutex_enter(net->lock);
		if (epoch == net->policy.epoch && net->rfk.control_epoch == epoch)
			net->rfk.control_generation = net->core->sc_generation;
		mutex_exit(net->lock);
#endif
		if ((ifp->if_flags & IFF_UP) == 0)
			state = IEEE80211_S_INIT;
		bssid = NULL;
		aid = 0;
		basic = channel > 14 ? 0x150 : 0x0f;
		slot = channel > 14 ? 9 : 20;
		if ((state == IEEE80211_S_AUTH || state == IEEE80211_S_ASSOC ||
		    state == IEEE80211_S_RUN) &&
		    ic->ic_bss->ni_rates.rs_nrates != 0)
			basic = rtw88_net_basic_rates(ic);
		if (state == IEEE80211_S_RUN) {
			bssid = ic->ic_bss->ni_bssid;
			aid = ic->ic_bss->ni_associd & 0x3fff;
			if ((ic->ic_flags & IEEE80211_F_SHSLOT) != 0)
				slot = 9;
		}
		/* Profile providers and shared state mutation require Giant. The
		 * workqueue holds Giant through this check and parent publication. */
		prepare_error = net->tx_quiet ? EBUSY :
		    state == IEEE80211_S_RUN ?
		    ieee80211_ht20_state_request(ic, state) : 0;
		if (state == IEEE80211_S_RUN && ic->ic_bss != NULL &&
		    ic->ic_bss->ni_ht20.valid &&
		    ic->ic_fixed_rate != IEEE80211_FIXED_RATE_NONE)
			prepare_error = EOPNOTSUPP;
		prepare_stage = RTW88_NET_STAGE_CORE;
		if (prepare_error == 0) {
			prepare_error = state == IEEE80211_S_INIT ? 0 :
			    rtw88_net_edca_prepare(ic, media, state, channel, slot, edca);
			prepare_stage = RTW88_NET_STAGE_EDCA;
		}
		if (restart_error != 0) {
			prepare_error = restart_error;
			prepare_stage = RTW88_NET_STAGE_RESTART;
		}
		use_ra = state == IEEE80211_S_RUN &&
		    ic->ic_fixed_rate == IEEE80211_FIXED_RATE_NONE;
		if (prepare_error == 0 && use_ra) {
			prepare_stage = RTW88_NET_STAGE_RA;
			prepare_error = rtw88_net_ra_prepare(ic, channel, basic,
			    epoch, &ra);
		}
#ifdef RTW88_RFK_EXPERIMENT
		/* A retired uncertain owner also blocks an already-started core.
		 * Ordinary SCAN/INIT coalescing cannot bypass it without start(). */
		if (prepare_error == 0 && rtw8822c_rfk_blocked() &&
		    !rtw8822c_rfk_active(net->core))
			prepare_error = EBUSY;
		/* Acquire before C1. No IO is added to an unstarted INIT. */
		if (prepare_error == 0 && state == IEEE80211_S_AUTH)
			prepare_error = rtw88_net_rfk_begin(net);
		rtw88_net_rfk_rx_deliver(net);
#endif
		if (net->core->sc_started && drain_epoch != epoch) {
			drain_epoch = epoch;
			drain_gen = net->core->sc_generation;
			drain_deadline = (unsigned)getticks() + MAX(1, mstohz(1000));
		}
		if (prepare_error == 0
#ifdef RTW88_RFK_EXPERIMENT
		    && !(state == IEEE80211_S_AUTH && net->rfk.drained)
#endif
		    ) {
			prepare_stage = RTW88_NET_STAGE_CORE;
			prepare_error = rtw88_net_drain_wait(net, epoch, drain_gen,
			    drain_deadline, state == IEEE80211_S_INIT, &mailbox_retry);
			if (mailbox_retry || prepare_error == ECANCELED)
				continue;
		}
#ifdef RTW88_RFK_EXPERIMENT
		if (prepare_error == 0 && state == IEEE80211_S_AUTH) {
			/* No producer can add a descriptor while this same numeric
			 * request owns the existing all-five-queue admission fence. */
			net->rfk.drained = 1;
			if (!net->rfk.ran) {
				mutex_enter(net->lock);
				prepare_error = epoch != net->policy.epoch || net->policy.dying ||
				    net->rfk.notice_pending ?
				    ECANCELED : rtw88_channel(net->core, channel);
				mutex_exit(net->lock);
				if (prepare_error == 0)
					prepare_error = rtw8822c_rfk_run(net->core);
				rtw88_net_rfk_rx_deliver(net);
				/* A same-epoch value/provider change has no successor
				 * request. Send it through the existing INIT/stop owner. */
				if (prepare_error == ECANCELED && epoch == net->policy.epoch &&
				    !net->policy.dying)
					prepare_error = ESTALE;
				if (prepare_error == 0)
					net->rfk.ran = 1;
			}
		}
#endif
		/* Current core is stable under Giant; no pointers survive a sleep. */
		mailbox_count = prepare_error != 0 || !net->core->sc_started ? 0 :
		    state == IEEE80211_S_RUN ? 2 +
		    (use_ra && !ra.use_protection) : 1;
		gen = net->core->sc_generation;
		if (mailbox_epoch != epoch) {
			mailbox_epoch = epoch;
			mailbox_gen = gen;
			mailbox_box = net->core->sc_h2c_box & 3;
			mailbox_deadline = (unsigned)getticks() +
			    MAX(1, mstohz(RTW88_COMMAND_US / 1000));
		}
		mailbox_reserved = 0;
		if (prepare_error == 0) {
			prepare_stage = RTW88_NET_STAGE_CORE;
			prepare_error = rtw88_net_mailbox_wait(net, epoch,
			    mailbox_gen, mailbox_box, mailbox_count, mailbox_deadline,
			    &mailbox_retry);
			if (mailbox_retry || prepare_error == ECANCELED)
				continue;
			mailbox_reserved = prepare_error == 0 && mailbox_count != 0;
		}
		mutex_enter(net->lock);
		if (epoch != net->policy.epoch || net->policy.dying
#ifdef RTW88_RFK_EXPERIMENT
		    || net->rfk.notice_pending
#endif
		    ) {
			if (mailbox_reserved)
				(void)rtw88_fw_mailbox_end(net->core, ECANCELED);
			mutex_exit(net->lock);
			continue;
		}
		error = prepare_error;
		memset(&diag, 0, sizeof(diag));
		diag.state = state;
		diag.channel = channel;
		diag.basic = basic;
		diag.slot = slot;
		diag.aid = aid;
		diag.raw_aid = ic->ic_bss->ni_associd;
		diag.nrates = ic->ic_bss->ni_rates.rs_nrates;
		diag.flags = ic->ic_flags;
		diag.started = net->core->sc_started;
		diag.running = net->core->sc_running;
		/* Bits: present, group address, all zero. Do not retain the address. */
		if (bssid != NULL) {
			diag.bssid_flags = 1 | ((bssid[0] & 1) << 1);
			any = 0;
			for (i = 0; i < 6; i++)
				any |= bssid[i];
			if (any == 0)
				diag.bssid_flags |= 4;
		}
		if (error != 0) {
			diag.stage = prepare_stage;
		} else if (net->core->sc_started) {
			if (state != IEEE80211_S_INIT
#ifdef RTW88_RFK_EXPERIMENT
			    && state != IEEE80211_S_AUTH
#endif
			    ) {
				diag.stage = RTW88_NET_STAGE_CHANNEL;

#ifdef RTW88_RFK_EXPERIMENT
				/* ASSOC/RUN may retain the same proven channel.
				 * A retune withdraws RFK and power provenance. Never
				 * retain an unsealed, stale or different envelope. */
				if ((state != IEEE80211_S_ASSOC &&
				    state != IEEE80211_S_RUN) ||
				    channel != net->core->sc_channel ||
				    !net->core->sc_operating.sealed ||
				    rtw8822c_rfk_seal(net->core) != 0)
#endif
				error = rtw88_channel(net->core, channel);
			}
			if (error == 0) {
				diag.stage = RTW88_NET_STAGE_STATION;
				error = rtw88_station_config(net->core, bssid, aid,
				    basic, slot, state == IEEE80211_S_RUN);
			}
			if (error == 0 && state != IEEE80211_S_INIT) {
				diag.stage = RTW88_NET_STAGE_EDCA;
				error = rtw88_edca_apply(net->core, edca, slot);
			}
			if (error == 0 && use_ra) {
				diag.stage = RTW88_NET_STAGE_RA;
				error = rtw88_ra_configure(net->core, &ra);
			}
		} else if (state != IEEE80211_S_INIT) {
			diag.stage = RTW88_NET_STAGE_CORE;
			error = ENXIO;
		}
		if (mailbox_reserved)
			error = rtw88_fw_mailbox_end(net->core, error);
		diag.core_channel = net->core->sc_channel;
		diag.error = error;
		error = rtw88_net_commit(&net->policy, epoch, error,
		    state != IEEE80211_S_INIT);
		if (error != 0)
			rtw88_ra_invalidate(net->core);
		mutex_exit(net->lock);
#ifdef RTW88_RFK_EXPERIMENT
		/* Completion has restored the outer originals. On any failure this
		 * retires the old engine before the existing INIT/stop owner. */
		if (rtw8822c_rfk_active(net->core)) {
			int retire_error = rtw8822c_rfk_retire(net->core);
			rtw88_net_rfk_rx_deliver(net);
			if (error == 0 && retire_error != 0)
				error = retire_error;
		}
		/* Retirement can yield during old-owner cleanup. Never publish an
		 * old AUTH or abort a successor that requested SCAN meanwhile. */
		mutex_enter(net->lock);
		current = epoch == net->policy.epoch && !net->policy.dying;
		mutex_exit(net->lock);
		if (!current)
			error = ECANCELED;
		if (error == 0 && state == IEEE80211_S_AUTH) {
			error = rtw8822c_rfk_seal(net->core);
			if (error != 0) {
				diag.stage = RTW88_NET_STAGE_CORE;
				diag.error = error;
			}
		}
		if (error != 0) {
			mutex_enter(net->lock);
			current = epoch == net->policy.epoch && !net->policy.dying;
			if (current) {
				/* A failed post-commit proof must withdraw acceptance.
				 * commit(nonzero error) deliberately changes no policy. */
				(void)rtw88_net_commit(&net->policy, epoch, 0, 0);
				rtw88_ra_invalidate(net->core);
			} else {
				error = ECANCELED;
			}
			mutex_exit(net->lock);
			net->core->sc_operating.prepared =
			    net->core->sc_operating.sealed = 0;
		}
		rtw88_net_rfk_publish(net);
#endif
		if (error != ECANCELED) {
			if (error != 0) {
				rtw88_net_ht20_publish(net, NULL);
				(void)ieee80211_ht20_state_request(ic, IEEE80211_S_INIT);
				(void)rtw88_net_state_format(failure,
				    sizeof(failure), &diag);
				aprint_error_dev(net->dev, "%s", failure);
				ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
				(void)net->parent_state(ic, IEEE80211_S_INIT, -1);
				if (diag.stage != RTW88_NET_STAGE_RESTART)
					rtw88_stop(net->core);
			} else {
				if (state == IEEE80211_S_ASSOC) {
					if (rtw88_net_ht20_profile_prepare(net,
					    &ht20_profile) == 0)
						rtw88_net_ht20_publish(net, &ht20_profile);
					else
						rtw88_net_ht20_publish(net, NULL);
				}
				error = net->parent_state(ic, state, arg);
				if (error != 0) {
					diag.stage = RTW88_NET_STAGE_CORE;
					diag.error = error;
					(void)rtw88_net_state_format(failure,
					    sizeof(failure), &diag);
					aprint_error_dev(net->dev, "%s", failure);
					mutex_enter(net->lock);
					current = epoch == net->policy.epoch &&
					    !net->policy.dying;
					if (current && state == IEEE80211_S_INIT) {
						(void)rtw88_net_commit(&net->policy, epoch,
						    error, 0);
						rtw88_ra_invalidate(net->core);
					}
					mutex_exit(net->lock);
					if (current && state != IEEE80211_S_INIT) {
						/* Existing request owner, no self-wait. */
						(void)rtw88_net_newstate(ic,
						    IEEE80211_S_INIT, -1);
					} else if (current) {
						/* An INIT rejection must not retry forever. */
						ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
						(void)ieee80211_ht20_state_request(ic,
						    IEEE80211_S_INIT);
						(void)rtw88_stop(net->core);
					}
				} else {
					if (state == IEEE80211_S_RUN) {
						mutex_enter(net->lock);
						if (rtw88_net_beacon_start(&net->policy,
						    epoch, (uint32_t)getticks()) == 0)
							ifp->if_timer = 1;
						mutex_exit(net->lock);
					}
					if (state == IEEE80211_S_SCAN)
						callout_schedule(&net->scan, mstohz(200));
					softint_schedule(net->softint);
				}
			}
		}
		mutex_enter(net->lock);
		if (epoch == net->policy.epoch || net->policy.dying) {
			net->work_pending = 0;
			mutex_exit(net->lock);
			return;
		}
		mutex_exit(net->lock);
	}
}

static int
rtw88_net_newstate(struct ieee80211com *ic, enum ieee80211_state state,
    int arg)
{
	struct rtw88_net *net = ic->ic_ifp->if_softc;
	int enqueue = 0, ht20_error;
	int quiet_error = net->tx_quiet && state != IEEE80211_S_INIT ? EBUSY : 0;

	if (quiet_error != 0) {
		state = IEEE80211_S_INIT;
		arg = -1;
	}

	/* Drop stale readiness before leaving the association/RUN lifecycle. */
	if (state != IEEE80211_S_ASSOC && state != IEEE80211_S_RUN)
		rtw88_net_ht20_publish(net, NULL);
	/* Shared capsule ownership is Giant-only, before taking the PCI lock. */
	if (state == IEEE80211_S_RUN && ic->ic_state == IEEE80211_S_RUN &&
	    ic->ic_bss != NULL)
		ieee80211_rxba_reset(ic, ic->ic_bss);
	ht20_error = ieee80211_ht20_state_request(ic, state);
	if (ht20_error != 0) {
		/* Withdraw an already accepted peer even for a direct RUN caller.
		 * Use the existing coalesced INIT owner; never leave TX accepting. */
		state = IEEE80211_S_INIT;
		arg = -1;
	}
	callout_stop(&net->scan);
#ifdef RTW88_RFK_EXPERIMENT
lock_request:
#endif
	mutex_enter(net->lock);
	if (net->policy.dying) {
		mutex_exit(net->lock);
		(void)ieee80211_ht20_state_request(ic, IEEE80211_S_INIT);
		return ENXIO;
	}
#ifdef RTW88_RFK_EXPERIMENT
	if (net->rfk.notice_pending) {
		mutex_exit(net->lock);
		rtw88_net_rfk_rx_deliver(net);
		quiet_error = net->tx_quiet && state != IEEE80211_S_INIT ? EBUSY : 0;
		if (quiet_error != 0) {
			state = IEEE80211_S_INIT;
			arg = -1;
		}
		goto lock_request;
	}
#endif
	if (net->core->sc_linked)
		net->reset_required = 1;
	rtw88_ra_invalidate(net->core);
	(void)rtw88_net_request(&net->policy);
	net->requested = state;
	net->state_arg = arg;
	net->channel = ieee80211_chan2ieee(ic, ic->ic_curchan);
#ifdef RTW88_RFK_EXPERIMENT
	net->rfk.control_epoch = 0;
	net->rfk.notice_pending = 0;
	if (!net->tx_quiet && ic->ic_opmode == IEEE80211_M_STA &&
	    (state == IEEE80211_S_AUTH || state == IEEE80211_S_ASSOC ||
	     state == IEEE80211_S_RUN) && ic->ic_bss != NULL &&
	    ic->ic_bss->ni_chan == ic->ic_curchan && net->channel != 0 &&
	    net->channel <= IEEE80211_CHAN_MAX) {
		net->rfk.control_epoch = net->policy.epoch;
		net->rfk.control_generation = net->core->sc_generation;
		net->rfk.control_channel = net->channel;
		memcpy(net->rfk.control_ra, ic->ic_myaddr, 6);
		memcpy(net->rfk.control_ta, ic->ic_bss->ni_macaddr, 6);
		memcpy(net->rfk.control_bssid, ic->ic_bss->ni_bssid, 6);
	}
#endif
	if (!net->work_pending) {
		net->work_pending = 1;
		enqueue = 1;
	}
	mutex_exit(net->lock);
	if (enqueue)
		workqueue_enqueue(net->wq, &net->state_work, NULL);
	return quiet_error != 0 ? quiet_error : ht20_error;
}

static void
rtw88_net_scan(void *cookie)
{
	struct rtw88_net *net = cookie;
	int s = splnet();

	if (!net->policy.dying && net->ic.ic_state == IEEE80211_S_SCAN)
		(void)ieee80211_next_scan(&net->ic);
	splx(s);
}

/* Invoked under the PCI interrupt lock; all borrowed bytes are copied. */
static void
rtw88_net_rx_frame(void *cookie, const uint8_t *frame, size_t len,
    const struct rtw88_rx_info *info)
{
	struct rtw88_net *net = cookie;
	struct rtw88_net_rx *rx;
	struct mbuf *m;
	uint32_t arrival_tick;
	int error;

	KASSERT(mutex_owned(net->lock));
	arrival_tick = (uint32_t)getticks();
#ifdef RTW88_RFK_EXPERIMENT
	/* Withdrawal must remain observable with closed/full ordinary admission. */
	rtw88_net_rfk_rx_observe(net, frame, len, info);
#endif
	error = rtw88_net_rx_admit(&net->policy, net->policy.epoch, frame,
	    len, info->flags, info->security);
	if (error != 0) {
		net->rx_dropped++;
		return;
	}
	m = rtw88_net_rx_mbuf(frame, len, &net->ec.ec_if);
	if (m == NULL) {
		rtw88_net_rx_release(&net->policy);
		net->rx_dropped++;
		net->stats.nomem++;
		if_statinc(&net->ec.ec_if, if_ierrors);
		return;
	}
	rx = &net->rx[net->rx_tail];
	rx->m = m;
	rx->epoch = net->policy.epoch;
	rx->arrived = rtw88_net_rxba_now(&net->ic);
	rx->tsf = info->tsf_low;
	rx->tick = arrival_tick;
	rx->generation = net->core->sc_generation;
	rx->quality = info->signal.valid && info->signal.quality <= 100 &&
	    info->signal.generation == rx->generation ? info->signal.quality : 0;
	net->rx_tail = (net->rx_tail + 1) % RTW88_NET_RX_LIMIT;
	softint_schedule(net->softint);
}

static void
rtw88_net_tx_complete(void *cookie, unsigned count)
{
	struct rtw88_net *net = cookie;

	KASSERT(mutex_owned(net->lock));
	if (count != 0 && !net->policy.dying)
		net->tx_completed += count;
	if (count != 0 && !net->policy.dying)
		softint_schedule(net->softint);
}

static void
rtw88_net_softintr(void *cookie)
{
	struct rtw88_net *net = cookie;
	struct ieee80211com *ic = &net->ic;
	struct ifnet *ifp = &net->ec.ec_if;
	struct rtw88_net_rx rx;
	struct ieee80211_node *ni;
	unsigned n, batch, completed, outstanding;
	int valid, expire;

#ifdef RTW88_RFK_EXPERIMENT
	/* Giant owns node/capsule cleanup; the IRQ kept only numeric identity. */
	rtw88_net_rfk_rx_deliver(net);
#endif
	mutex_enter(net->lock);
	batch = MIN(net->policy.queued, RTW88_NET_RX_LIMIT);
	mutex_exit(net->lock);
	for (n = 0; n < batch; n++) {
		mutex_enter(net->lock);
		if (net->policy.queued == 0) {
			mutex_exit(net->lock);
			break;
		}
		rx = net->rx[net->rx_head];
		net->rx[net->rx_head].m = NULL;
		net->rx_head = (net->rx_head + 1) % RTW88_NET_RX_LIMIT;
		rtw88_net_rx_release(&net->policy);
		expire = (net->rxba_due != 0 || net->rxba_token != 0) &&
		    rx.arrived >= net->rxba_deadline;
		mutex_exit(net->lock);
		/* Only frames already received before the deadline can fill its hole.
		 * A later frame cannot revive that expired window. No node is held. */
		if (expire)
			rtw88_net_rxba_softintr(net);
		mutex_enter(net->lock);
		if (rx.generation != net->core->sc_generation) {
			net->stats.fw_stale++;
			valid = 0;
		} else {
			valid = rtw88_net_rx_ready(&net->policy, rx.epoch);
		}
		if (valid && rx.m->m_next != NULL)
			net->stats.chained++;
		if (valid && rx.m->m_len < rx.m->m_pkthdr.len)
			net->stats.first_short++;
		mutex_exit(net->lock);
		if (!valid) {
			m_freem(rx.m);
			continue;
		}
		if (ic->ic_state == IEEE80211_S_RUN) {
			mutex_enter(net->lock);
			(void)rtw88_net_beacon_note(&net->policy, rx.epoch,
			    rx.tick, mtod(rx.m, const uint8_t *),
			    rx.m->m_len, ic->ic_bss->ni_bssid);
			mutex_exit(net->lock);
		}
		ni = ieee80211_find_rxnode(ic,
		    mtod(rx.m, struct ieee80211_frame_min *));
		/* Relative unsigned index only. No signed dBm is passed through this ABI. */
		ieee80211_input(ic, rx.m, ni, rx.quality, rx.tsf);
		ieee80211_free_node(ni);
		rtw88_net_rxba_publish(net);
		/*
		 * The stack holds Giant here. Publish a numeric snapshot under
		 * the PCI lock; sysctl must not acquire Giant under its gate.
		 * Native counts reflect the most recent input, not live stack state.
		 */
		mutex_enter(net->lock);
		net->stats.beacon = ic->ic_stats.is_rx_beacon;
		net->stats.chanmismatch = ic->ic_stats.is_rx_chanmismatch;
		net->stats.badchan = ic->ic_stats.is_rx_badchan;
		net->stats.elem_missing = ic->ic_stats.is_rx_elem_missing;
		net->stats.elem_toobig = ic->ic_stats.is_rx_elem_toobig;
		net->stats.elem_toosmall = ic->ic_stats.is_rx_elem_toosmall;
		net->stats.nodealloc = ic->ic_stats.is_rx_nodealloc;
		net->stats.mgtdiscard = ic->ic_stats.is_rx_mgtdiscard;
		mutex_exit(net->lock);
	}
	/* Let already queued gap frames complete the window before expiring a
	 * hole. The shared token check discards an event superseded by input. */
	rtw88_net_rxba_softintr(net);
	mutex_enter(net->lock);
	completed = net->tx_completed;
	net->tx_completed = 0;
	outstanding = 0;
	for (n = 0; n < RTW88_NTXQ; n++)
		outstanding += net->core->sc_txq[n].queued;
	if (net->policy.queued != 0 && !net->policy.dying)
		softint_schedule(net->softint);
	mutex_exit(net->lock);
	if (completed != 0) {
		if_statadd(ifp, if_opackets, completed);
		net->tx_timer = outstanding != 0 ? 5 : 0;
	}
	if ((ifp->if_flags & IFF_RUNNING) != 0 && !net->policy.dying) {
		ifp->if_flags &= ~IFF_OACTIVE;
		rtw88_net_start(ifp);
	}
}

void
rtw88_net_get_stats(struct rtw88_net *net, struct rtw88_net_stats *stats)
{

	KASSERT(mutex_owned(net->lock));
	*stats = net->stats;
	stats->rx = net->policy.rx;
}

/* Caller holds Giant and the bus lock; copy no node lifetime into the core. */
static int
rtw88_net_tx_submit(struct rtw88_net *net, struct ieee80211_node *ni,
    unsigned q, const void *frame, size_t len)
{
	static const uint8_t rates[] = { 2, 4, 11, 22, 12, 18, 24, 36,
	    48, 72, 96, 108 };
	struct ieee80211com *ic = &net->ic;
	const struct ieee80211_rateset *local, *peer;
	const uint8_t *bytes = frame;
	struct rtw88_tx_fixed fixed;
	unsigned i, j, aid;
	int index = ic->ic_fixed_rate;

#ifdef RTW88_RFK_EXPERIMENT
	/* Recheck after dequeue/crypto: IRQ can close the earlier capacity gate. */
	if (net->rfk.notice_pending)
		return EBUSY;
#endif
	/* Keep non-payload/group frames on the existing raw path. */
	if (q == RTW88_TXQ_MGMT ||
	    frame == NULL || len < 24 ||
	    (bytes[0] & 0x0c) != 0x08 || (bytes[0] & 0x40) != 0 ||
	    (bytes[4] & 1) != 0 ||
	    ((bytes[1] & 3) == 1 && (bytes[16] & 1) != 0))
		return rtw88_tx_raw(net->core, q, frame, len);
	/* Parent RUN may start TX before this committed worker releases ownership. */
	if (!net->policy.accepting || net->policy.dying)
		return EBUSY;
	if ((index < 0 && index != IEEE80211_FIXED_RATE_NONE) ||
	    ic->ic_state != IEEE80211_S_RUN ||
	    ic->ic_opmode != IEEE80211_M_STA || ni == NULL || ni != ic->ic_bss ||
	    ni->ni_chan == NULL || ni->ni_chan != ic->ic_curchan)
		return EINVAL;
	aid = ni->ni_associd & 0x3fff;
	if (aid == 0 || aid > 2007)
		return EINVAL;
	if (net->core->sc_ra.configured &&
	    net->core->sc_ra.phy == RTW88_RA_HT20_1SS) {
		if (!ni->ni_ht20.valid || index != IEEE80211_FIXED_RATE_NONE ||
		    ic->ic_curmode != IEEE80211_MODE_11A ||
		    (ic->ic_flags & IEEE80211_F_USEPROT) != 0 ||
		    ni->ni_ht20.profile.rx_mcs != 0xff ||
		    ni->ni_ht20.profile.tx_mcs != 0xff ||
		    net->core->sc_ra.channel != ieee80211_chan2ieee(ic, ni->ni_chan) ||
		    net->core->sc_ra.profile_generation != ni->ni_ht20.profile.generation ||
		    net->core->sc_ra.ht_mcs != ni->ni_ht20.tx_mcs ||
		    ni->ni_ht20.operation != net->core->sc_ra.ht_operation ||
		    ni->ni_ht20.peer_cap != net->core->sc_ra.ht.peer_cap ||
		    ni->ni_ht20.peer_htop[1] != net->core->sc_ra.ht.secondary ||
		    ni->ni_ht20.peer_htop[4] != 0 || ni->ni_ht20.peer_htop[5] != 0 ||
		    ic->ic_ifp->if_mtu != net->core->sc_ra.ht.mtu ||
		    ic->ic_rtsthreshold != net->core->sc_ra.ht.rts_threshold ||
		    ic->ic_fragthreshold != RTW88_HT20_THRESHOLD_MAX ||
		    (ic->ic_flags & IEEE80211_F_DATAPAD) != 0)
			return EBUSY;
		if (len > net->core->sc_ra.ht.max_mpdu)
			return EMSGSIZE;
	} else if (ni->ni_ht20.valid) {
		return EBUSY;
	}
	if (index == IEEE80211_FIXED_RATE_NONE)
		return rtw88_tx_auto(net->core, q, frame, len, net->policy.epoch,
		    (ic->ic_flags & IEEE80211_F_USEPROT) != 0);
	memset(&fixed, 0, sizeof(fixed));
	switch (ic->ic_curmode) {
	case IEEE80211_MODE_11A:
		fixed.mode = RTW88_FIXED_11A;
		break;
	case IEEE80211_MODE_11B:
		fixed.mode = RTW88_FIXED_11B;
		break;
	case IEEE80211_MODE_11G:
		fixed.mode = RTW88_FIXED_11G;
		break;
	default:
		return EOPNOTSUPP;
	}
	local = &ic->ic_sup_rates[ic->ic_curmode];
	peer = &ni->ni_rates;
	if (local->rs_nrates > __arraycount(local->rs_rates) ||
	    (unsigned)index >= local->rs_nrates ||
	    peer->rs_nrates > __arraycount(peer->rs_rates))
		return EINVAL;
	fixed.rate500 = local->rs_rates[index] & IEEE80211_RATE_VAL;
	for (i = 0; i < peer->rs_nrates; i++)
		for (j = 0; j < __arraycount(rates); j++)
			if ((peer->rs_rates[i] & IEEE80211_RATE_VAL) == rates[j])
				fixed.peer_mask |= 1U << j;
	fixed.channel = ieee80211_chan2ieee(ic, ni->ni_chan);
	fixed.associated = 1;
	fixed.use_protection = (ic->ic_flags & IEEE80211_F_USEPROT) != 0;
	return rtw88_tx_fixed(net->core, q, frame, len, &fixed);
}

/* Giant serializes discipline changes and the single native TX producer. */
static int
rtw88_net_tx_dynamic(struct ifnet *ifp)
{
#ifdef ALTQ
	int dynamic;

	IFQ_LOCK(&ifp->if_snd);
	dynamic = ALTQ_IS_ENABLED(&ifp->if_snd) ||
	    TBR_IS_ENABLED(&ifp->if_snd);
	IFQ_UNLOCK(&ifp->if_snd);
	return dynamic;
#else
	(void)ifp;
	return 0;
#endif
}

/* Never retain a FIFO peek's node/AC for a different dequeued packet. */
static struct mbuf *
rtw88_net_tx_dequeue_same(struct ifnet *ifp, struct mbuf *expected)
{
	struct mbuf *m;

	IFQ_LOCK(&ifp->if_snd);
	IF_POLL(&ifp->if_snd, m);
	if (m == expected)
		IF_DEQUEUE(&ifp->if_snd, m);
	else
		m = NULL;
	IFQ_UNLOCK(&ifp->if_snd);
	return m;
}

static int
rtw88_net_tx_queue(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni, unsigned *queue, int commit)
{
	static const unsigned queues[WME_NUM_AC] = { RTW88_TXQ_BE,
	    RTW88_TXQ_BK, RTW88_TXQ_VI, RTW88_TXQ_VO };
	struct ether_header eh;
	struct ieee80211_key *key;
	unsigned slot;
	int ac, error, flags;

	if (m->m_pkthdr.len < sizeof(eh))
		return EINVAL;
	if (ni->ni_ht20.valid && ic->ic_ifp != NULL &&
	    m->m_pkthdr.len > ic->ic_ifp->if_mtu + sizeof(eh))
		return EMSGSIZE;
	m_copydata(m, 0, sizeof(eh), &eh);
	if (ni->ni_ht20.valid && !IEEE80211_IS_MULTICAST(eh.ether_dhost) &&
	    (ic->ic_flags & IEEE80211_F_PRIVACY) != 0) {
		/*
		 * Encap uses the same pure selector. Giant prevents key changes
		 * before crypto. Neither selector uses PN.
		 */
		if (IEEE80211_KEY_UNDEFINED(ni->ni_ucastkey) &&
		    ic->ic_def_txkey != IEEE80211_KEYIX_NONE &&
		    ic->ic_def_txkey >= IEEE80211_WEP_NKID)
			return EINVAL;
		key = ieee80211_crypto_getucastkey(ic, ni);
		if (key != NULL && key->wk_cipher == NULL)
			return EOPNOTSUPP;
		if (key == NULL) {
			if (eh.ether_type != htons(ETHERTYPE_PAE))
				return EACCES;
		} else if (eh.ether_type != htons(ETHERTYPE_PAE) ||
		    ((ic->ic_flags & IEEE80211_F_WPA) != 0 &&
		     (ic->ic_opmode == IEEE80211_M_STA ?
		      !IEEE80211_KEY_UNDEFINED(*key) :
		      !IEEE80211_KEY_UNDEFINED(ni->ni_ucastkey)))) {
			if (key->wk_cipher->ic_cipher != IEEE80211_CIPHER_AES_CCM ||
			    key->wk_cipher->ic_header != 8 ||
			    key->wk_cipher->ic_trailer != 8 ||
			    key->wk_cipher->ic_miclen != 0 ||
			    (key->wk_flags & IEEE80211_KEY_SWCRYPT) == 0)
				return EOPNOTSUPP;
		}
	}
	if ((ni->ni_flags & (IEEE80211_NODE_QOS | IEEE80211_NODE_WME_VALID)) != 0) {
		slot = ieee80211_chan2ieee(ic, ic->ic_curchan) > 14 ||
		    (ic->ic_flags & IEEE80211_F_SHSLOT) != 0 ? 9 : 20;
		if ((ni->ni_flags & (IEEE80211_NODE_QOS | IEEE80211_NODE_WME_VALID)) !=
		    (IEEE80211_NODE_QOS | IEEE80211_NODE_WME_VALID) ||
		    (ic->ic_caps & IEEE80211_C_WME) == 0 ||
		    (ic->ic_flags & IEEE80211_F_WME) == 0 || ni != ic->ic_bss ||
		    ni->ni_wme_applied_chan != ic->ic_curchan ||
		    ni->ni_wme_applied_slot != slot)
			return EPROTO;
	}
	/* A capacity probe must retain BPF's original M_PROMISC marker. */
	flags = m->m_flags;
	error = ieee80211_classify(ic, m, ni);
	ac = eh.ether_type == htons(ETHERTYPE_PAE) ? WME_AC_BE : M_WME_GETAC(m);
	if (!commit)
		m->m_flags = flags;
	if (error != 0)
		return EINVAL;
	/* The stack intentionally sends EAPOL without a QoS header. */
	if (ac < 0 || ac >= WME_NUM_AC)
		return EINVAL;
	*queue = queues[ac];
	return 0;
}

static int
rtw88_net_tx_capacity(struct rtw88_net *net, unsigned queue, int all)
{
	unsigned q, last;

	KASSERT(mutex_owned(net->lock));
	if (!net->policy.accepting || net->policy.dying)
		return 0;
#ifdef RTW88_RFK_EXPERIMENT
	if (net->rfk.notice_pending)
		return 0;
#endif
	q = all ? RTW88_TXQ_BE : queue;
	last = all ? RTW88_TXQ_VO : queue;
	for (; q <= last; q++)
		if (rtw88_tx_available(net->core, q) == 0)
			return 0;
	return 1;
}

static void
rtw88_net_start(struct ifnet *ifp)
{
	struct rtw88_ra_config ht_policy;
	struct rtw88_net *net = ifp->if_softc;
	struct ieee80211com *ic = &net->ic;
	struct ieee80211_node *ni;
	const struct ieee80211_frame *wh;
	struct ether_header eh;
	struct mbuf *m, *head;
	unsigned q, n, actual;
	uint64_t ht_epoch;
	int available, error, dynamic, all, ht_expected;

	if ((ifp->if_flags & (IFF_RUNNING | IFF_OACTIVE)) != IFF_RUNNING)
		return;
	for (n = 0; n < RTW88_TX_SLOTS; n++) {
		ni = NULL;
		dynamic = all = error = 0;
		IF_POLL(&ic->ic_mgtq, m);
		q = m != NULL ? RTW88_TXQ_MGMT : RTW88_TXQ_BE;
		if (m == NULL) {
			if (ic->ic_state != IEEE80211_S_RUN)
				return;
			/* Giant owns the capsule. The provider never runs under PCI. */
			mutex_enter(net->lock);
			ht_epoch = net->policy.epoch;
			available = net->policy.accepting && !net->policy.dying;
			ht_expected = net->core->sc_ra.configured &&
			    net->core->sc_ra.phy == RTW88_RA_HT20_1SS;
			mutex_exit(net->lock);
			if (ht_expected || (ic->ic_bss != NULL &&
			    (ic->ic_bss->ni_ht20.valid || ic->ic_bss->ni_ht20.offered ||
			    ic->ic_bss->ni_ht20.profile.generation != 0))) {
				if (!available)
					return;
				error = ic->ic_bss == NULL || !ic->ic_bss->ni_ht20.valid ||
				    ieee80211_ht20_state_request(ic, IEEE80211_S_RUN) != 0;
				memset(&ht_policy, 0, sizeof(ht_policy));
				if (!error)
					error = rtw88_net_tx_ht20_policy(ic, &ht_policy);
				mutex_enter(net->lock);
				available = net->policy.accepting && !net->policy.dying &&
				    net->policy.epoch == ht_epoch;
				if (available && !error) {
					const struct ieee80211_ht20_node *h = &ic->ic_bss->ni_ht20;
					const struct rtw88_ra_state *ra = &net->core->sc_ra;
					error = ra->phy != RTW88_RA_HT20_1SS || !ra->configured ||
					    !ra->active || ra->error != 0 || ra->epoch != ht_epoch ||
					    ra->generation != net->core->sc_generation ||
					    ra->channel != net->core->sc_channel ||
					    ra->channel != ieee80211_chan2ieee(ic, ic->ic_curchan) ||
					    (ic->ic_bss->ni_associd & 0x3fff) == 0 ||
					    (ic->ic_bss->ni_associd & 0x3fff) > 2007 ||
					    ra->channel <= 14 || ic->ic_curmode != IEEE80211_MODE_11A ||
					    ic->ic_fixed_rate != IEEE80211_FIXED_RATE_NONE ||
					    (ic->ic_flags & IEEE80211_F_USEPROT) != 0 ||
					    ra->ht.qualification != ht_policy.ht.qualification ||
					    ra->ht.peer_cap != ht_policy.ht.peer_cap ||
					    ra->ht.secondary != ht_policy.ht.secondary ||
					    ra->ht.mtu != ht_policy.ht.mtu ||
					    ra->ht.max_mpdu != ht_policy.ht.max_mpdu ||
					    ra->ht.rts_threshold != ht_policy.ht.rts_threshold ||
					    h->profile.rx_mcs != 0xff ||
					    h->profile.tx_mcs != 0xff || h->tx_mcs == 0 ||
					    (h->tx_mcs & ~0xffU) != 0 ||
					    ra->profile_generation != h->profile.generation ||
					    ra->ht_mcs != h->tx_mcs || ra->ht_operation != h->operation;
				}
				mutex_exit(net->lock);
				if (!available)
					return; /* A newer request owns withdrawal. */
				if (error) {
					ieee80211_new_state(ic, IEEE80211_S_INIT, EPROTO);
					return;
				}
			}
			dynamic = rtw88_net_tx_dynamic(ifp);
			if (dynamic) {
				/* The scheduler may replace its polled head. */
				all = (ic->ic_caps & IEEE80211_C_WME) != 0 &&
				    (ic->ic_flags & IEEE80211_F_WME) != 0;
			} else {
				IFQ_POLL(&ifp->if_snd, m);
				if (m == NULL)
					return;
				/* Do not classify a peer while a state request owns it. */
				mutex_enter(net->lock);
				available = net->policy.accepting && !net->policy.dying;
				mutex_exit(net->lock);
				if (!available) {
					ifp->if_flags |= IFF_OACTIVE;
					return;
				}
				if (m->m_pkthdr.len < sizeof(eh)) {
					error = EINVAL;
				} else {
					m_copydata(m, 0, sizeof(eh), &eh);
					ni = ieee80211_find_txnode(ic, eh.ether_dhost);
					error = ni == NULL ? ENOENT :
					    rtw88_net_tx_queue(ic, m, ni, &q, 0);
				}
				if (error == EPROTO) {
					ieee80211_free_node(ni);
					if_statinc(ifp, if_oerrors);
					/* No queue or PCI lock across the state request. */
					ieee80211_new_state(ic, IEEE80211_S_INIT, EPROTO);
					return;
				}
			}
		}
		mutex_enter(net->lock);
		available = rtw88_net_tx_capacity(net, q, all);
		mutex_exit(net->lock);
		if (error == 0 && !available) {
			if (ni != NULL)
				ieee80211_free_node(ni);
			ifp->if_flags |= IFF_OACTIVE;
			return;
		}
		if (q == RTW88_TXQ_MGMT) {
			IF_DEQUEUE(&ic->ic_mgtq, m);
			if (m == NULL)
				return;
			ni = M_GETCTX(m, struct ieee80211_node *);
			M_CLEARCTX(m);
		} else {
			if (dynamic) {
				IFQ_DEQUEUE(&ifp->if_snd, m);
				if (m == NULL)
					return;
				if (m->m_pkthdr.len < sizeof(eh)) {
					error = EINVAL;
				} else {
					m_copydata(m, 0, sizeof(eh), &eh);
					ni = ieee80211_find_txnode(ic, eh.ether_dhost);
					error = ni == NULL ? ENOENT :
					    rtw88_net_tx_queue(ic, m, ni, &q, 0);
				}
				if (error == EPROTO) {
					ieee80211_free_node(ni);
					m_freem(m);
					if_statinc(ifp, if_oerrors);
					ieee80211_new_state(ic, IEEE80211_S_INIT, EPROTO);
					return;
				}
			} else {
				head = m;
				m = rtw88_net_tx_dequeue_same(ifp, head);
				if (m == NULL) {
					if (ni != NULL)
						ieee80211_free_node(ni);
					continue;
				}
			}
			if (error != 0) {
				m_freem(m);
				if (ni != NULL)
					ieee80211_free_node(ni);
				if_statinc(ifp, if_oerrors);
				continue;
			}
			if ((m = m_pullup(m, sizeof(eh))) == NULL)
				goto tx_error;
			/* BPF consumes M_PROMISC, which shares a WME metadata bit. */
			bpf_mtap(ifp, m, BPF_D_OUT);
			error = rtw88_net_tx_queue(ic, m, ni, &actual, 1);
			if (error == 0 && actual != q)
				error = EPROTO;
			if (error != 0)
				goto tx_error;
			m = ieee80211_encap(ic, m, ni);
			if (m == NULL)
				goto tx_error;
		}
		if (m->m_len < 24 && (m = m_pullup(m, 24)) == NULL)
			goto tx_error;
		wh = mtod(m, struct ieee80211_frame *);
		if ((wh->i_fc[1] & IEEE80211_FC1_WEP) != 0 &&
		    ieee80211_crypto_encap(ic, ni, m) == NULL)
			goto tx_error;
		if (m->m_pkthdr.len > RTW88_TX_BUF_SIZE -
		    RTW8822C_TX_PACKET_DESC_SIZE)
			goto tx_error;
		/* AC is now in the wire header, not BPF's duplicate-marker bit. */
		if (q != RTW88_TXQ_MGMT)
			m->m_flags &= ~M_WME_AC_MASK;
		bpf_mtap3(ic->ic_rawbpf, m, BPF_D_OUT);
		m_copydata(m, 0, m->m_pkthdr.len, net->txbuf);
		mutex_enter(net->lock);
		error = rtw88_net_tx_submit(net, ni, q, net->txbuf,
		    m->m_pkthdr.len);
		mutex_exit(net->lock);
		m_freem(m);
		ieee80211_free_node(ni);
		if (error != 0) {
			if_statinc(ifp, if_oerrors);
			continue;
		}
		net->tx_timer = 5;
		ifp->if_timer = 1;
		continue;
	tx_error:
		m_freem(m);
		ieee80211_free_node(ni);
		if_statinc(ifp, if_oerrors);
		if (error == EPROTO) {
			ieee80211_new_state(ic, IEEE80211_S_INIT, EPROTO);
			return;
		}
	}
	softint_schedule(net->softint);
}

static void
rtw88_net_stop(struct ifnet *ifp, int disable)
{
	struct rtw88_net *net = ifp->if_softc;

	rtw88_net_ht20_publish(net, NULL);
	rtw88_net_rxba_quiesce(net);
	if (net->ic.ic_bss != NULL)
		ieee80211_rxba_reset(&net->ic, net->ic.ic_bss);
	(void)disable;
	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
	ifp->if_timer = 0;
	callout_halt(&net->scan, NULL);
	mutex_enter(net->lock);
	(void)rtw88_net_request(&net->policy);
	net->requested = IEEE80211_S_INIT;
	net->state_arg = -1;
	mutex_exit(net->lock);
	workqueue_wait(net->wq, &net->state_work);
	(void)net->parent_state(&net->ic, IEEE80211_S_INIT, -1);
	rtw88_stop(net->core);
}

static int
rtw88_net_init(struct ifnet *ifp)
{
	struct rtw88_net *net = ifp->if_softc;
	struct ieee80211com *ic = &net->ic;
	unsigned channel;
	int error;

	if (net->tx_quiet)
		return EBUSY;
	if (net->policy.dying || net->ic.ic_opmode != IEEE80211_M_STA)
		return EOPNOTSUPP;
	if ((ifp->if_flags & IFF_RUNNING) != 0)
		rtw88_net_stop(ifp, 0);
	/*
	 * setmode narrows active channels without changing the old curchan.
	 * Prime only the initial SCAN request; shared next_scan owns hopping.
	 */
	channel = ic->ic_curchan == NULL || ic->ic_curchan == IEEE80211_CHAN_ANYC ?
	    0 : ieee80211_chan2ieee(ic, ic->ic_curchan);
	if (channel == 0 || channel > IEEE80211_CHAN_MAX ||
	    isclr(ic->ic_chan_active, channel) ||
	    ic->ic_channels[channel].ic_freq == 0 ||
	    ic->ic_channels[channel].ic_flags == 0) {
		for (channel = 1; channel <= IEEE80211_CHAN_MAX; channel++)
			if (isset(ic->ic_chan_active, channel) &&
			    ic->ic_channels[channel].ic_freq != 0 &&
			    ic->ic_channels[channel].ic_flags != 0)
				break;
		if (channel > IEEE80211_CHAN_MAX)
			return EINVAL;
		ic->ic_curchan = &ic->ic_channels[channel];
	}
	error = rtw88_start(net->core);
	if (error != 0)
		return error;
	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;
	return ieee80211_new_state(&net->ic, IEEE80211_S_SCAN, -1);
}

static int
rtw88_net_ioctl(struct ifnet *ifp, u_long cmd, void *data)
{
	struct rtw88_net *net = ifp->if_softc;
	int error;

	if (cmd == SIOCSIFFLAGS) {
		error = ifioctl_common(ifp, cmd, data);
		if (error != 0)
			return error;
		if ((ifp->if_flags & IFF_UP) != 0)
			return (ifp->if_flags & IFF_RUNNING) != 0 ? 0 :
			    rtw88_net_init(ifp);
		rtw88_net_stop(ifp, 1);
		return 0;
	}
	error = ieee80211_ioctl(&net->ic, cmd, data);
	/* MAR accepts every multicast address; membership needs no radio reset. */
	if ((cmd == SIOCADDMULTI || cmd == SIOCDELMULTI) &&
	    error == ENETRESET)
		return 0;
	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING))
			return rtw88_net_init(ifp);
		return 0;
	}
	return error;
}

static int
rtw88_net_media_change(struct ifnet *ifp)
{
	struct rtw88_net *net = ifp->if_softc;
	struct ieee80211com *ic = &net->ic;
	enum ieee80211_phymode previous_mode = ic->ic_curmode;
	unsigned previous, media = ic->ic_media.ifm_media;
	int error;

	/* The media layer owns its lock here. Never take it in state_work. */
	mutex_enter(net->lock);
	previous = net->configured_media;
	mutex_exit(net->lock);
	error = ieee80211_media_change(ifp);
	if (error != 0 && error != ENETRESET)
		return error;
	/*
	 * AUTO association changes the runtime mode without narrowing active
	 * channels. Refresh a new configured constraint even when the shared
	 * consumer sees the same runtime mode. Repeats and rate-only changes
	 * within an existing constraint retain its channel-list policy.
	 */
	if (ic->ic_curmode == previous_mode &&
	    (IFM_MODE(previous) != IFM_MODE(media) ||
	    (IFM_MODE(previous) == IFM_AUTO &&
	    IFM_SUBTYPE(previous) == IFM_AUTO &&
	    IFM_SUBTYPE(media) != IFM_AUTO))) {
		error = ieee80211_setmode(ic, ic->ic_curmode);
		if (error != 0)
			return error;
		error = ENETRESET;
	}
	mutex_enter(net->lock);
	net->configured_media = media;
	mutex_exit(net->lock);
	if (error == ENETRESET && (ifp->if_flags & IFF_UP) != 0) {
		error = rtw88_net_init(ifp);
		if (error != 0) {
			/* ifmedia restores the previous word when this callback fails. */
			mutex_enter(net->lock);
			net->configured_media = previous;
			mutex_exit(net->lock);
		}
		return error;
	}
	return 0;
}

static void
rtw88_net_watchdog(struct ifnet *ifp)
{
	struct rtw88_net *net = ifp->if_softc;
	int missed = 0;
	uint32_t timeout;

	ifp->if_timer = 0;
	if (net->tx_timer != 0 && --net->tx_timer == 0) {
		if_statinc(ifp, if_oerrors);
		aprint_error_dev(net->dev, "station TX DMA timeout\n");
	}
	if (net->tx_timer != 0)
		ifp->if_timer = 1;
	ieee80211_watchdog(&net->ic);
	if (net->ic.ic_state == IEEE80211_S_RUN) {
		/* if_timer polls once per second; retain the 7-beacon deadline. */
		timeout = (uint32_t)MAX(hz,
		    mstohz(rtw88_net_tu_to_ms(net->ic.ic_bmisstimeout)));
		mutex_enter(net->lock);
		missed = rtw88_net_beacon_poll(&net->policy,
		    net->policy.epoch, (uint32_t)getticks(), timeout);
		mutex_exit(net->lock);
		if (missed)
			ieee80211_beacon_miss(&net->ic);
		/* Keep checking an idle station even when it has no outstanding TX. */
		ifp->if_timer = 1;
	}
}

int
rtw88_net_attach(struct rtw88_softc *core, device_t dev, kmutex_t *lock,
    const uint8_t mac[6], struct rtw88_net **out)
{
	static const unsigned channels5[] = {36,40,44,48,149,153,157,161,165};
	struct rtw88_net *net;
	struct ieee80211com *ic;
	struct ifnet *ifp;
	unsigned i, ch;
	int error;

	if (core == NULL || dev == NULL || lock == NULL || mac == NULL ||
	    out == NULL || (mac[0] & 1) != 0 ||
	    memcmp(mac, "\0\0\0\0\0\0", 6) == 0)
		return EINVAL;
	net = kmem_zalloc(sizeof(*net), KM_SLEEP);
	net->core = core;
	net->dev = dev;
	net->lock = lock;
	callout_init(&net->scan, 0);
	callout_setfunc(&net->scan, rtw88_net_scan, net);
	error = workqueue_create(&net->wq, "rtw88net", rtw88_net_state_work,
	    net, PRI_NONE, IPL_NET, 0);
	if (error != 0)
		goto fail;
	net->softint = softint_establish(SOFTINT_NET, rtw88_net_softintr, net);
	if (net->softint == NULL) {
		error = ENOMEM;
		workqueue_destroy(net->wq);
		goto fail;
	}
	ic = &net->ic;
	ifp = &net->ec.ec_if;
	ic->ic_ifp = ifp;
	ic->ic_phytype = IEEE80211_T_OFDM;
	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_state = IEEE80211_S_INIT;
	ic->ic_caps = IEEE80211_C_WPA2 | IEEE80211_C_SHSLOT;
	memcpy(ic->ic_myaddr, mac, IEEE80211_ADDR_LEN);
	ic->ic_sup_rates[IEEE80211_MODE_11A] = ieee80211_std_rateset_11a;
	ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
	ic->ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;
	for (ch = 1; ch <= 13; ch++) {
		ic->ic_channels[ch].ic_freq = ieee80211_ieee2mhz(ch,
		    IEEE80211_CHAN_2GHZ);
		ic->ic_channels[ch].ic_flags = IEEE80211_CHAN_B |
		    IEEE80211_CHAN_G | IEEE80211_CHAN_OFDM |
		    IEEE80211_CHAN_PASSIVE;
	}
	/* Passive discovery, without DFS. Board regulatory limits still apply. */
	for (i = 0; i < __arraycount(channels5); i++) {
		ch = channels5[i];
		ic->ic_channels[ch].ic_freq = ieee80211_ieee2mhz(ch,
		    IEEE80211_CHAN_5GHZ);
		ic->ic_channels[ch].ic_flags = IEEE80211_CHAN_A |
		    IEEE80211_CHAN_PASSIVE;
	}
	ifp->if_softc = net;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = rtw88_net_init;
	ifp->if_stop = rtw88_net_stop;
	ifp->if_start = rtw88_net_start;
	ifp->if_ioctl = rtw88_net_ioctl;
	ifp->if_watchdog = rtw88_net_watchdog;
	strlcpy(ifp->if_xname, device_xname(dev), IFNAMSIZ);
	IFQ_SET_READY(&ifp->if_snd);
	if_initialize(ifp);
	ieee80211_ifattach(ic);
	rtw88_net_rxba_attach(net);
	ic->ic_sta_quiet = rtw88_net_sta_quiet;
	ic->ic_scan_probe_admit = rtw88_net_scan_probe_admit;
	/* Register after ifattach, which initializes optional admission to NULL. */
	rtw88_net_ht20_publish(net, NULL);
	ic->ic_ht20_admit = rtw88_net_ht20_admit;
	/* Opt in only after the strict consumer owns STA parameter apply. */
	ic->ic_wme.wme_sta_apply = rtw88_net_wme_apply;
	ic->ic_caps |= IEEE80211_C_WME;
	ic->ic_flags |= IEEE80211_F_WME;
	net->parent_state = ic->ic_newstate;
	ic->ic_newstate = rtw88_net_newstate;
	ifp->if_percpuq = if_percpuq_create(ifp);
	ieee80211_media_init(ic, rtw88_net_media_change, ieee80211_media_status);
	ieee80211_wme_initparams(ic);
	/* ifmedia_set selects ifm_cur; its user word remains IFM_NONE. */
	KASSERT(ic->ic_media.ifm_cur != NULL);
	net->configured_media = ic->ic_media.ifm_cur->ifm_media;
	if_register(ifp);
	mutex_enter(lock);
	core->sc_net_cookie = net;
	core->sc_rx_frame = rtw88_net_rx_frame;
	core->sc_tx_complete = rtw88_net_tx_complete;
	mutex_exit(lock);
	*out = net;
	return 0;
fail:
	callout_destroy(&net->scan);
	kmem_free(net, sizeof(*net));
	return error;
}

void
rtw88_net_detach(struct rtw88_net *net)
{
	unsigned i;

	if (net == NULL)
		return;
	mutex_enter(net->lock);
	rtw88_net_close(&net->policy);
	net->core->sc_rx_frame = NULL;
	net->core->sc_tx_complete = NULL;
	net->core->sc_net_cookie = NULL;
	mutex_exit(net->lock);
	rtw88_net_rxba_quiesce(net);
	callout_halt(&net->scan, NULL);
	workqueue_wait(net->wq, &net->state_work);
	rtw88_net_stop(&net->ec.ec_if, 1);
	softint_disestablish(net->softint);
	workqueue_destroy(net->wq);
	for (i = 0; i < RTW88_NET_RX_LIMIT; i++)
		m_freem(net->rx[i].m);
	ieee80211_ifdetach(&net->ic);
	if_detach(&net->ec.ec_if);
	rtw88_net_rxba_detach(net);
	callout_destroy(&net->scan);
	kmem_free(net, sizeof(*net));
}
