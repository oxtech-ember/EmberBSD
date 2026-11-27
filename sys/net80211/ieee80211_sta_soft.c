/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD station management for host-MLME radios, AI-assisted. */
/*
 * BSS Transition Management (IEEE Std 802.11-2024, 11.21.7) for radios
 * whose MLME runs in net80211. The supplicant owns the transition policy.
 * net80211 forwards the BTM Request of the BSS, sends the BTM Query and
 * Response bodies of the supplicant, and runs the connected scan that the
 * supplicant needs before it selects a candidate.
 *
 * The connected scan is passive. Before the radio leaves the operating
 * channel, the station sends a Null frame with the Power Management bit
 * set, so the AP buffers its frames (11.2.3). It listens on at most
 * SOFT_BURST channels, returns, and sends a Null frame with the bit clear.
 * Beacons and Probe Responses heard away from the operating channel go to
 * the common scan cache through ieee80211_scan_observe().
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/callout.h>
#include <sys/workqueue.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_media.h>
#include <net/if_ether.h>
#include <net/route.h>
#include <net80211/ieee80211_netbsd.h>
#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_sta.h>
#include <net80211/ieee80211_rxba.h>
#include <net80211/ieee80211_sta_action.h>
#include <net80211/ieee80211_sta_soft.h>

#define	SOFT_SLEEP_MS	20	/* the PM Null frame leaves the queue */
#define	SOFT_DWELL_MS	120	/* more than one 102.4 ms beacon interval */
#define	SOFT_HOME_MS	100	/* on the operating channel between bursts */
#define	SOFT_BURST	2	/* channels per absence */

enum soft_step {
	SOFT_IDLE,
	SOFT_SLEEP,		/* send PM=1, then wait SOFT_SLEEP_MS */
	SOFT_TUNE,		/* tune to the next channel and listen */
	SOFT_NEXT,		/* listen period ended */
	SOFT_HOME,		/* back on the operating channel */
};

struct ieee80211_sta_soft {
	struct ieee80211com *ss_ic;
	const struct ieee80211_sta_soft_ops *ss_ops;
	struct workqueue *ss_wq;
	struct work ss_wk;
	callout_t ss_to;
	bool ss_enabled;
	bool ss_queued;
	bool ss_abort;
	bool ss_tuned;		/* the adapter holds a scan tune */
	bool ss_sleeping;	/* the AP has seen PM=1 */
	bool ss_detaching;
	enum soft_step ss_step;
	unsigned ss_offchan;	/* channel of the active listen period */
	unsigned ss_next, ss_nchan, ss_burst;
	bool ss_waiting;	/* the step waits until ss_deadline */
	int ss_deadline;	/* getticks() value */
	uint8_t ss_chan[IEEE80211_CHAN_MAX];
	struct ieee80211req_sta_mgmt ss_scan;	/* fixed header only */
};

static uint32_t
soft_generation(const struct ieee80211com *ic)
{
	return ic->ic_bss != NULL ? (uint32_t)ic->ic_bss->ni_assoc_epoch : 0;
}

/* The association that the supplicant enabled for station management. */
static bool
soft_assoc(const struct ieee80211_sta_soft *ss)
{
	const struct ieee80211com *ic = ss->ss_ic;

	return ss->ss_enabled && !ss->ss_detaching &&
	    ic->ic_opmode == IEEE80211_M_STA &&
	    ic->ic_state == IEEE80211_S_RUN && ic->ic_bss != NULL &&
	    ic->ic_bss->ni_chan != NULL &&
	    ic->ic_bss->ni_chan != IEEE80211_CHAN_ANYC &&
	    ic->ic_bss->ni_assoc_epoch != 0 && soft_generation(ic) != 0 &&
	    (ic->ic_ifp->if_flags & IFF_RUNNING) != 0;
}

static bool
soft_tuple(const struct ieee80211_sta_soft *ss,
    const struct ieee80211req_sta_mgmt *req)
{
	const struct ieee80211com *ic = ss->ss_ic;

	return soft_assoc(ss) && req->generation == soft_generation(ic) &&
	    req->freq == ic->ic_bss->ni_chan->ic_freq &&
	    IEEE80211_ADDR_EQ(req->bssid, ic->ic_bss->ni_bssid);
}

static void
soft_kick(struct ieee80211_sta_soft *ss)
{
	if (!ss->ss_queued) {
		ss->ss_queued = true;
		workqueue_enqueue(ss->ss_wq, &ss->ss_wk, NULL);
	}
}

static void
soft_timeout(void *arg)
{
	struct ieee80211_sta_soft *ss = arg;
	int s;

	s = splnet();
	soft_kick(ss);
	splx(s);
}

/* Continue the scan after ms; a stale callout cannot shorten the wait. */
static void
soft_wait(struct ieee80211_sta_soft *ss, unsigned ms)
{
	int t = MAX(mstohz(ms), 1);

	ss->ss_waiting = true;
	ss->ss_deadline = (int)((unsigned)getticks() + (unsigned)t);
	callout_schedule(&ss->ss_to, t);
}

/* Tell the AP whether the station listens (11.2.3.2). */
static void
soft_pm(struct ieee80211com *ic, bool sleep)
{
	struct ieee80211_node *ni = ic->ic_bss;

	if (sleep)
		ni->ni_flags |= IEEE80211_NODE_PWR_MGT;
	else
		ni->ni_flags &= ~IEEE80211_NODE_PWR_MGT;
	(void)ieee80211_send_nulldata(ieee80211_ref_node(ni));
}

/* Call the adapter without splnet; return with splnet again. */
static int
soft_tune(struct ieee80211_sta_soft *ss, struct ieee80211_channel *c, int *s)
{
	int error;

	splx(*s);
	error = ss->ss_ops->sso_tune(ss->ss_ic, c);
	*s = splnet();
	return error;
}

static void
soft_finish(struct ieee80211_sta_soft *ss, bool aborted, int *s)
{
	struct ieee80211com *ic = ss->ss_ic;
	struct ieee80211req_sta_mgmt hdr;

	ss->ss_offchan = 0;
	ss->ss_waiting = false;
	callout_stop(&ss->ss_to);
	if (ss->ss_tuned) {
		(void)soft_tune(ss, NULL, s);
		ss->ss_tuned = false;
		if (ic->ic_bss != NULL)
			ieee80211_rxba_resync(ic, ic->ic_bss);
	}
	if (ss->ss_sleeping) {
		ss->ss_sleeping = false;
		if (soft_assoc(ss))
			soft_pm(ic, false);
		else if (ic->ic_bss != NULL)
			ic->ic_bss->ni_flags &= ~IEEE80211_NODE_PWR_MGT;
	}
	hdr = ss->ss_scan;
	ss->ss_step = SOFT_IDLE;
	ss->ss_abort = false;
	(void)ieee80211_sta_scan_report(ic, &hdr, aborted);
	(void)ieee80211_sta_scan_release(ic, &hdr);
}

static void
soft_work(struct work *wk, void *arg)
{
	struct ieee80211_sta_soft *ss = arg;
	struct ieee80211com *ic = ss->ss_ic;
	struct ieee80211_channel *c;
	int s;

	(void)wk;
	s = splnet();
	ss->ss_queued = false;
	if (ss->ss_waiting && !ss->ss_abort && ss->ss_step != SOFT_IDLE) {
		int left = (int)((unsigned)ss->ss_deadline - (unsigned)getticks());

		if (left > 0) {
			callout_schedule(&ss->ss_to, left);
			goto out;
		}
	}
	ss->ss_waiting = false;
again:
	if (ss->ss_step == SOFT_IDLE)
		goto out;
	if (ss->ss_abort || !soft_tuple(ss, &ss->ss_scan)) {
		soft_finish(ss, true, &s);
		goto out;
	}
	switch (ss->ss_step) {
	case SOFT_SLEEP:
		soft_pm(ic, true);
		ss->ss_sleeping = true;
		ss->ss_burst = 0;
		ss->ss_step = SOFT_TUNE;
		soft_wait(ss, SOFT_SLEEP_MS);
		break;
	case SOFT_TUNE:
		c = &ic->ic_channels[ss->ss_chan[ss->ss_next]];
		ss->ss_tuned = true;
		if (soft_tune(ss, c, &s) != 0) {
			soft_finish(ss, true, &s);
			break;
		}
		if (ss->ss_abort || !soft_tuple(ss, &ss->ss_scan)) {
			soft_finish(ss, true, &s);
			break;
		}
		ss->ss_offchan = ss->ss_chan[ss->ss_next];
		ss->ss_step = SOFT_NEXT;
		soft_wait(ss, SOFT_DWELL_MS);
		break;
	case SOFT_NEXT:
		ss->ss_offchan = 0;
		ss->ss_next++;
		if (ss->ss_next < ss->ss_nchan && ++ss->ss_burst < SOFT_BURST) {
			ss->ss_step = SOFT_TUNE;
			goto again;
		}
		if (soft_tune(ss, NULL, &s) != 0) {
			ss->ss_tuned = false;
			soft_finish(ss, true, &s);
			break;
		}
		ss->ss_tuned = false;
		if (ss->ss_abort || !soft_tuple(ss, &ss->ss_scan)) {
			soft_finish(ss, true, &s);
			break;
		}
		soft_pm(ic, false);
		ss->ss_sleeping = false;
		ieee80211_rxba_resync(ic, ic->ic_bss);
		if (ss->ss_next == ss->ss_nchan) {
			soft_finish(ss, false, &s);
			break;
		}
		ss->ss_step = SOFT_HOME;
		soft_wait(ss, SOFT_HOME_MS);
		break;
	case SOFT_HOME:
		ss->ss_step = SOFT_SLEEP;
		goto again;
	default:
		soft_finish(ss, true, &s);
		break;
	}
out:
	splx(s);
}

int
ieee80211_sta_soft_attach(struct ieee80211com *ic,
    const struct ieee80211_sta_soft_ops *ops)
{
	struct ieee80211_sta_soft *ss;
	int error;

	if (ic->ic_sta_soft != NULL || ops == NULL || ops->sso_tune == NULL)
		return EINVAL;
	ss = kmem_zalloc(sizeof(*ss), KM_SLEEP);
	ss->ss_ic = ic;
	ss->ss_ops = ops;
	error = workqueue_create(&ss->ss_wq, "wlansta", soft_work, ss,
	    PRI_SOFTNET, IPL_NET, 0);
	if (error != 0) {
		kmem_free(ss, sizeof(*ss));
		return error;
	}
	callout_init(&ss->ss_to, 0);
	callout_setfunc(&ss->ss_to, soft_timeout, ss);
	ic->ic_sta_soft = ss;
	ic->ic_sta_mgmt = ieee80211_sta_soft_mgmt;
	return 0;
}

void
ieee80211_sta_soft_detach(struct ieee80211com *ic)
{
	struct ieee80211_sta_soft *ss = ic->ic_sta_soft;
	struct ieee80211req_sta_mgmt hdr;
	int s;

	if (ss == NULL)
		return;
	s = splnet();
	ss->ss_detaching = true;
	ss->ss_abort = true;
	splx(s);
	callout_halt(&ss->ss_to, NULL);
	workqueue_wait(ss->ss_wq, &ss->ss_wk);
	s = splnet();
	if (ss->ss_step != SOFT_IDLE) {
		/* The adapter stopped the radio before detach. */
		hdr = ss->ss_scan;
		ss->ss_step = SOFT_IDLE;
		(void)ieee80211_sta_scan_report(ic, &hdr, true);
		(void)ieee80211_sta_scan_release(ic, &hdr);
	}
	ic->ic_sta_mgmt = NULL;
	ic->ic_sta_soft = NULL;
	splx(s);
	callout_destroy(&ss->ss_to);
	workqueue_destroy(ss->ss_wq);
	kmem_free(ss, sizeof(*ss));
}

/*
 * Channels of the request, or every active channel for an empty list.
 * The station hears its own channel between listen periods, so the plan
 * leaves it out.
 */
static void
soft_plan(struct ieee80211_sta_soft *ss, const struct ieee80211req_sta_mgmt *req)
{
	struct ieee80211com *ic = ss->ss_ic;
	unsigned home = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
	uint16_t freq;
	unsigned i, c, n = 0;

	if (req->len != 0) {
		for (i = 0; i < req->len / sizeof(freq); i++) {
			memcpy(&freq, req->data + i * sizeof(freq), sizeof(freq));
			c = ieee80211_mhz2ieee(freq, 0);
			if (c != home)
				ss->ss_chan[n++] = c;
		}
	} else {
		for (i = 1; i <= IEEE80211_CHAN_MAX &&
		    n < __arraycount(ss->ss_chan); i++)
			if (isset(ic->ic_chan_active, i) && i != home)
				ss->ss_chan[n++] = i;
	}
	ss->ss_nchan = n;
	ss->ss_next = 0;
}

static int
soft_scan(struct ieee80211_sta_soft *ss, const struct ieee80211req_sta_mgmt *req)
{
	struct ieee80211com *ic = ss->ss_ic;
	int error;

	if (ss->ss_step != SOFT_IDLE)
		return EBUSY;
	error = ieee80211_sta_scan_begin(ic, req, soft_generation(ic));
	if (error != 0)
		return error;
	soft_plan(ss, req);
	memset(&ss->ss_scan, 0, sizeof(ss->ss_scan));
	ss->ss_scan.version = req->version;
	ss->ss_scan.op = IEEE80211_STA_SCAN;
	ss->ss_scan.cookie = req->cookie;
	ss->ss_scan.generation = req->generation;
	ss->ss_scan.freq = req->freq;
	IEEE80211_ADDR_COPY(ss->ss_scan.bssid, req->bssid);
	ss->ss_abort = false;
	if (ss->ss_nchan == 0) {
		/* Only the operating channel: its beacons are already here. */
		(void)ieee80211_sta_scan_report(ic, &ss->ss_scan, false);
		(void)ieee80211_sta_scan_release(ic, &ss->ss_scan);
		return 0;
	}
	ss->ss_step = SOFT_SLEEP;
	soft_kick(ss);
	return 0;
}

/* Queue the body; the radio reports no ACK result to net80211. */
static int
soft_tx(struct ieee80211_sta_soft *ss, const struct ieee80211req_sta_mgmt *req)
{
	struct ieee80211com *ic = ss->ss_ic;
	struct ieee80211req_sta_mgmt notice;
	int error;

	if (req->cookie == 0 || !ieee80211_sta_action_valid(req->data,
	    req->len, true, IEEE80211_STA_ACTION_BTM, IEEE80211_STA_MAX_BODY))
		return EINVAL;
	if (!soft_tuple(ss, req))
		return ENOTCONN;
	error = ieee80211_sta_action_begin(&ic->ic_sta_action, req);
	if (error != 0)
		return error;
	error = ieee80211_send_action(ic, ic->ic_bss, req->data, req->len);
	if (error == 0 && ieee80211_sta_action_report(&ic->ic_sta_action, req,
	    0, false, false, &notice))
		rt_ieee80211msg(ic->ic_ifp, RTM_IEEE80211_STA_TX, &notice,
		    offsetof(struct ieee80211req_sta_mgmt, data));
	(void)ieee80211_sta_action_release(&ic->ic_sta_action, req);
	return error;
}

int
ieee80211_sta_soft_mgmt(struct ieee80211com *ic,
    struct ieee80211req_sta_mgmt *req)
{
	struct ieee80211_sta_soft *ss = ic->ic_sta_soft;
	int s, error = 0;

	if (ss == NULL || ss->ss_detaching)
		return ENXIO;
	if (ic->ic_opmode != IEEE80211_M_STA)
		return EOPNOTSUPP;
	s = splnet();
	switch (req->op) {
	case IEEE80211_STA_GET:
		req->flags = IEEE80211_STA_CAP_BTM | IEEE80211_STA_CAP_SCAN;
		req->generation = soft_generation(ic);
		req->len = IEEE80211_ADDR_LEN;
		IEEE80211_ADDR_COPY(req->data, ic->ic_myaddr);
		if (soft_assoc(ss)) {
			req->flags |= IEEE80211_STA_ASSOCIATED;
			req->freq = ic->ic_bss->ni_chan->ic_freq;
			IEEE80211_ADDR_COPY(req->bssid, ic->ic_bss->ni_bssid);
		}
		break;
	case IEEE80211_STA_ENABLE:
	case IEEE80211_STA_DISABLE:
		if (req->len != 0) {
			error = EINVAL;
			break;
		}
		ss->ss_enabled = req->op == IEEE80211_STA_ENABLE;
		if (!ss->ss_enabled && ss->ss_step != SOFT_IDLE) {
			ss->ss_abort = true;
			soft_kick(ss);
		}
		break;
	case IEEE80211_STA_TX:
		error = soft_tx(ss, req);
		break;
	case IEEE80211_STA_CANCEL_TX:
		/* A queued body has already been reported. */
		error = ESTALE;
		break;
	case IEEE80211_STA_SCAN:
		error = soft_scan(ss, req);
		break;
	case IEEE80211_STA_CANCEL_SCAN:
		if (req->len != 0) {
			error = EINVAL;
			break;
		}
		if (ss->ss_step == SOFT_IDLE ||
		    !ieee80211_sta_scan_match(ic, req)) {
			error = ESTALE;
			break;
		}
		ss->ss_abort = true;
		soft_kick(ss);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	splx(s);
	return error;
}

/* Called from ieee80211_newstate(); a scan cannot outlive RUN. */
void
ieee80211_sta_soft_state(struct ieee80211com *ic, int nstate)
{
	struct ieee80211_sta_soft *ss = ic->ic_sta_soft;

	if (ss == NULL || nstate == IEEE80211_S_RUN ||
	    ss->ss_step == SOFT_IDLE)
		return;
	/* The node can go before the work runs; it must not keep the bit. */
	if (ss->ss_sleeping && ic->ic_bss != NULL)
		ic->ic_bss->ni_flags &= ~IEEE80211_NODE_PWR_MGT;
	ss->ss_sleeping = false;
	ss->ss_offchan = 0;
	ss->ss_abort = true;
	soft_kick(ss);
}

bool
ieee80211_sta_soft_rx_scan(struct ieee80211com *ic, const void *frame,
    size_t len, unsigned int freq, int rssi, int rssi_dbm)
{
	struct ieee80211_sta_soft *ss = ic->ic_sta_soft;
	const struct ieee80211_frame *wh = frame;
	unsigned subtype;
	int s;

	if (ss == NULL || len < sizeof(*wh))
		return false;
	s = splnet();
	if (ss->ss_offchan == 0 || ieee80211_mhz2ieee(freq, 0) != ss->ss_offchan) {
		splx(s);
		return false;
	}
	/* Other frames heard away from the BSS have no receiver here. */
	subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
	if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_MGT &&
	    (subtype == IEEE80211_FC0_SUBTYPE_BEACON ||
	    subtype == IEEE80211_FC0_SUBTYPE_PROBE_RESP))
		(void)ieee80211_scan_observe(ic, frame, len, ss->ss_offchan,
		    rssi, (int8_t)MAX(MIN(rssi_dbm, 0), -128), 0);
	splx(s);
	return true;
}

/* Forward a BTM Request of the BSS to the supplicant (RTM_IEEE80211_STA_RX). */
bool
ieee80211_sta_soft_action(struct ieee80211com *ic, struct ieee80211_node *ni,
    const struct ieee80211_frame *wh, const uint8_t *body, size_t len)
{
	struct ieee80211_sta_soft *ss = ic->ic_sta_soft;
	struct ieee80211req_sta_mgmt *ev;

	if (ss == NULL || len < 1 || body[0] != 10)
		return false;
	/* The supplicant owns every WNM frame of an enabled association. */
	if (ni != ic->ic_bss || !soft_assoc(ss) ||
	    !ieee80211_sta_soft_rx_admit(ni->ni_bssid, ic->ic_myaddr,
	    wh->i_addr1, wh->i_addr2, wh->i_addr3, wh->i_fc[1], wh->i_seq[0],
	    body, len) ||
	    !ieee80211_sta_action_valid(body, len, false,
	    IEEE80211_STA_ACTION_BTM, IEEE80211_STA_MAX_BODY)) {
		ic->ic_stats.is_rx_mgtdiscard++;
		return true;
	}
	ev = kmem_intr_zalloc(sizeof(*ev), KM_NOSLEEP);
	if (ev == NULL) {
		ic->ic_stats.is_rx_nobuf++;
		return true;
	}
	ev->version = IEEE80211_STA_VERSION;
	ev->op = IEEE80211_STA_GET;
	ev->generation = soft_generation(ic);
	ev->freq = ni->ni_chan->ic_freq;
	ev->len = len;
	IEEE80211_ADDR_COPY(ev->bssid, ni->ni_bssid);
	memcpy(ev->data, body, len);
	rt_ieee80211msg(ic->ic_ifp, RTM_IEEE80211_STA_RX, ev,
	    offsetof(struct ieee80211req_sta_mgmt, data) + len);
	kmem_intr_free(ev, sizeof(*ev));
	return true;
}
