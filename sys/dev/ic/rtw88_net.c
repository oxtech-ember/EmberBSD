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
#include <sys/device.h>
#include <sys/kmem.h>
#include <sys/mbuf.h>
#include <sys/callout.h>
#include <sys/workqueue.h>
#include <sys/intr.h>
#include <net/if.h>
#include <net/if_ether.h>
#include <net/if_media.h>
#include <net/bpf.h>
#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_crypto.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw88_net.h>
#include <dev/ic/rtw88_net_policy.h>

struct rtw88_net_rx {
	struct mbuf *m;
	uint64_t epoch;
	uint32_t tsf;
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
	callout_t scan;
	struct rtw88_net_policy policy;
	struct rtw88_net_rx rx[RTW88_NET_RX_LIMIT];
	unsigned rx_head, rx_tail;
	int work_pending;
	enum ieee80211_state requested;
	unsigned channel;
	int state_arg;
	int (*parent_state)(struct ieee80211com *, enum ieee80211_state, int);
	uint8_t txbuf[RTW88_TX_BUF_SIZE];
	uint64_t rx_dropped;
	unsigned tx_timer;
	unsigned tx_completed;
};

static void rtw88_net_start(struct ifnet *);
static void rtw88_net_stop(struct ifnet *, int);
static int rtw88_net_init(struct ifnet *);

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

/* The workqueue has the kernel lock. Do not hold the PCI lock in net80211. */
static void
rtw88_net_state_work(struct work *work, void *cookie)
{
	struct rtw88_net *net = cookie;
	struct ieee80211com *ic = &net->ic;
	struct ifnet *ifp = &net->ec.ec_if;
	enum ieee80211_state state;
	uint64_t epoch;
	unsigned channel, basic, slot, aid;
	const uint8_t *bssid;
	int arg, error;

	(void)work;
	for (;;) {
		mutex_enter(net->lock);
		if (net->policy.dying) {
			net->work_pending = 0;
			mutex_exit(net->lock);
			return;
		}
		epoch = net->policy.epoch;
		state = net->requested;
		channel = net->channel;
		arg = net->state_arg;
		mutex_exit(net->lock);
		callout_stop(&net->scan);
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
		mutex_enter(net->lock);
		if (epoch != net->policy.epoch || net->policy.dying) {
			mutex_exit(net->lock);
			continue;
		}
		error = 0;
		if (net->core->sc_started) {
			if (state != IEEE80211_S_INIT)
				error = rtw88_channel(net->core, channel);
			if (error == 0)
				error = rtw88_station_config(net->core, bssid, aid,
				    basic, slot, state == IEEE80211_S_RUN);
		} else if (state != IEEE80211_S_INIT) {
			error = ENXIO;
		}
		error = rtw88_net_commit(&net->policy, epoch, error,
		    state != IEEE80211_S_INIT);
		mutex_exit(net->lock);
		if (error != ECANCELED) {
			if (error != 0) {
				aprint_error_dev(net->dev,
				    "station state %d failed: %d\n", state, error);
				ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
				(void)net->parent_state(ic, IEEE80211_S_INIT, -1);
				rtw88_stop(net->core);
			} else {
				(void)net->parent_state(ic, state, arg);
				if (state == IEEE80211_S_SCAN)
					callout_schedule(&net->scan, mstohz(200));
				softint_schedule(net->softint);
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
	int enqueue = 0;

	callout_stop(&net->scan);
	mutex_enter(net->lock);
	if (net->policy.dying) {
		mutex_exit(net->lock);
		return ENXIO;
	}
	(void)rtw88_net_request(&net->policy);
	net->requested = state;
	net->state_arg = arg;
	net->channel = ieee80211_chan2ieee(ic, ic->ic_curchan);
	if (!net->work_pending) {
		net->work_pending = 1;
		enqueue = 1;
	}
	mutex_exit(net->lock);
	if (enqueue)
		workqueue_enqueue(net->wq, &net->state_work, NULL);
	return 0;
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
	int error;

	KASSERT(mutex_owned(net->lock));
	error = rtw88_net_rx_admit(&net->policy, net->policy.epoch, frame,
	    len, info->flags, info->security);
	if (error != 0) {
		net->rx_dropped++;
		return;
	}
	m = m_devget((char *)__UNCONST(frame), (int)len, 0, &net->ec.ec_if);
	if (m == NULL) {
		rtw88_net_rx_release(&net->policy);
		net->rx_dropped++;
		if_statinc(&net->ec.ec_if, if_ierrors);
		return;
	}
	rx = &net->rx[net->rx_tail];
	rx->m = m;
	rx->epoch = net->policy.epoch;
	rx->tsf = info->tsf_low;
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
	unsigned n, completed, outstanding;
	int valid;

	for (n = 0; n < RTW88_NET_RX_LIMIT; n++) {
		mutex_enter(net->lock);
		if (net->policy.queued == 0) {
			mutex_exit(net->lock);
			break;
		}
		rx = net->rx[net->rx_head];
		net->rx[net->rx_head].m = NULL;
		net->rx_head = (net->rx_head + 1) % RTW88_NET_RX_LIMIT;
		rtw88_net_rx_release(&net->policy);
		valid = !net->policy.dying && net->policy.accepting &&
		    rx.epoch == net->policy.epoch;
		mutex_exit(net->lock);
		if (!valid) {
			m_freem(rx.m);
			continue;
		}
		ni = ieee80211_find_rxnode(ic,
		    mtod(rx.m, struct ieee80211_frame_min *));
		/* RSSI is unavailable until the core supplies parsed PHY data. */
		ieee80211_input(ic, rx.m, ni, 0, rx.tsf);
		ieee80211_free_node(ni);
	}
	mutex_enter(net->lock);
	completed = net->tx_completed;
	net->tx_completed = 0;
	outstanding = net->core->sc_txq[RTW88_TXQ_MGMT].queued +
	    net->core->sc_txq[RTW88_TXQ_BE].queued;
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

static void
rtw88_net_start(struct ifnet *ifp)
{
	struct rtw88_net *net = ifp->if_softc;
	struct ieee80211com *ic = &net->ic;
	struct ieee80211_node *ni;
	const struct ieee80211_frame *wh;
	const struct ether_header *eh;
	struct mbuf *m;
	unsigned q, n;
	int available, error;

	if ((ifp->if_flags & (IFF_RUNNING | IFF_OACTIVE)) != IFF_RUNNING)
		return;
	for (n = 0; n < RTW88_TX_SLOTS; n++) {
		IF_POLL(&ic->ic_mgtq, m);
		q = m != NULL ? RTW88_TXQ_MGMT : RTW88_TXQ_BE;
		if (m == NULL) {
			if (ic->ic_state != IEEE80211_S_RUN)
				return;
			IFQ_POLL(&ifp->if_snd, m);
			if (m == NULL)
				return;
		}
		mutex_enter(net->lock);
		available = net->policy.accepting && !net->policy.dying &&
		    rtw88_tx_available(net->core, q) != 0;
		mutex_exit(net->lock);
		if (!available) {
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
			IFQ_DEQUEUE(&ifp->if_snd, m);
			if (m == NULL)
				return;
			if ((m = m_pullup(m, sizeof(*eh))) == NULL) {
				if_statinc(ifp, if_oerrors);
				continue;
			}
			eh = mtod(m, struct ether_header *);
			ni = ieee80211_find_txnode(ic, eh->ether_dhost);
			if (ni == NULL) {
				m_freem(m);
				if_statinc(ifp, if_oerrors);
				continue;
			}
			bpf_mtap(ifp, m, BPF_D_OUT);
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
		bpf_mtap3(ic->ic_rawbpf, m, BPF_D_OUT);
		m_copydata(m, 0, m->m_pkthdr.len, net->txbuf);
		mutex_enter(net->lock);
		error = rtw88_tx_raw(net->core, q, net->txbuf, m->m_pkthdr.len);
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
	}
	softint_schedule(net->softint);
}

static void
rtw88_net_stop(struct ifnet *ifp, int disable)
{
	struct rtw88_net *net = ifp->if_softc;

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
	int error;

	if (net->policy.dying || net->ic.ic_opmode != IEEE80211_M_STA)
		return EOPNOTSUPP;
	if ((ifp->if_flags & IFF_RUNNING) != 0)
		rtw88_net_stop(ifp, 0);
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
	int error = ieee80211_media_change(ifp);

	if (error == ENETRESET && (ifp->if_flags & IFF_UP) != 0)
		return rtw88_net_init(ifp);
	return error == ENETRESET ? 0 : error;
}

static void
rtw88_net_watchdog(struct ifnet *ifp)
{
	struct rtw88_net *net = ifp->if_softc;

	ifp->if_timer = 0;
	if (net->tx_timer != 0 && --net->tx_timer == 0) {
		if_statinc(ifp, if_oerrors);
		aprint_error_dev(net->dev, "station TX DMA timeout\n");
	}
	if (net->tx_timer != 0)
		ifp->if_timer = 1;
	ieee80211_watchdog(&net->ic);
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
		    IEEE80211_CHAN_G | IEEE80211_CHAN_PASSIVE;
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
	net->parent_state = ic->ic_newstate;
	ic->ic_newstate = rtw88_net_newstate;
	ifp->if_percpuq = if_percpuq_create(ifp);
	ieee80211_media_init(ic, rtw88_net_media_change, ieee80211_media_status);
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
	callout_halt(&net->scan, NULL);
	workqueue_wait(net->wq, &net->state_work);
	rtw88_net_stop(&net->ec.ec_if, 1);
	softint_disestablish(net->softint);
	workqueue_destroy(net->wq);
	for (i = 0; i < RTW88_NET_RX_LIMIT; i++)
		m_freem(net->rx[i].m);
	ieee80211_ifdetach(&net->ic);
	if_detach(&net->ec.ec_if);
	callout_destroy(&net->scan);
	kmem_free(net, sizeof(*net));
}
