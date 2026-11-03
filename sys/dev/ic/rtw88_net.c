/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original native net80211 integration, AI-assisted. */
/*
 * One always-awake station, legacy/HT20 rates and software crypto; VHT
 * 40/80 only with RTW88_VHT_EXPERIMENT.
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
#include <dev/ic/rtw88_async.h>
#include <dev/ic/rtw88_executor.h>
#define RTW88_NET_TX_BUDGET 16U
#ifdef RTW88_RFK_EXPERIMENT
#include <dev/ic/rtw88_calibration.h>
struct rtw88_net_rfk {
	uint64_t epoch, started_us, deadline_us;
	uint32_t generation;
	unsigned channel, drained, ran;
	uint32_t values[192];
	/* Numeric current-peer watch and IRQ-to-Giant withdrawal notification. */
	uint64_t control_epoch, notice_epoch;
	uint32_t control_generation, notice_generation;
	unsigned control_channel, notice_channel, notice_pending;
	uint8_t control_ra[6], control_ta[6], control_bssid[6];
	/* Numeric beacon observation only; this grants no transmission lease. */
	uint64_t frontier;
	uint32_t frontier_generation;
	unsigned frontier_valid;
	uint64_t beacon_epoch, beacon_us;
	uint32_t beacon_generation;
	unsigned beacon_channel, beacon_interval;
};
#endif

struct rtw88_net_rx {
	struct mbuf *m;
	uint64_t epoch, arrived;
	uint64_t sequence;
	uint64_t key_epoch;
	uint8_t host_epoch_valid;
	uint32_t tsf, generation, tick;
	uint8_t quality;
};

/* Selected numeric publication; no bus/resource/node/key pointers. Only the
 * serialized owner (or quiescent attach) samples mutable core fields. */
struct rtw88_net_hw_snapshot {
	struct rtw88_ra_state ra;
	struct rtw88_phy_state phy;
	uint32_t generation;
	unsigned channel, outstanding;
	int started, running, linked, resources_ready, rf_enabled, stop_requested;
	int power_ready, rfk_sealed;
#ifdef RTW88_DFS64
	int dfs_valid;
#endif
#ifdef RTW88_VHT80
	unsigned vht_nss;
	int vht_committed;
#endif
};

struct rtw88_net {
	struct ethercom ec;
	struct ieee80211com ic;
	struct rtw88_softc *core;
	struct rtw88_caps caps;
	device_t dev;
	kmutex_t *lock;
	struct workqueue *wq;
	struct work state_work;
	void *softint;
	callout_t scan, rxba_callout, tx_retry;
	uint64_t rxba_token, rxba_deadline, rxba_due;
	/* Monotonic generation and profile word are published with atomic CAS. */
	volatile uint64_t ht20_snapshot;
	volatile uint32_t ht20_generation;
	struct rtw88_net_policy policy;
	struct rtw88_async async;
	struct mbuf *tx_pending_m;
	struct ieee80211_node *tx_pending_ni;
	struct rtw88_async_epoch tx_pending_id;
	unsigned tx_producers;
	int stack_detached;
	uint64_t key_epoch;
	unsigned key_depth;
	int key_error;
	int io_active, prep_granted, prep_requested, stop_pending, stop_hold;
	int state_enqueued, state_running, state_again;
	struct rtw88_net_hw_snapshot hw;
	struct rtw88_control_snapshot control_pending, control_owner;
	struct rtw88_control_result control_result;
	struct rtw88_net_state_diag control_pending_diag, control_owner_diag, control_result_diag;
	struct rtw88_async_epoch control_last;
	int control_pending_valid, control_active, control_ready, control_last_valid;
	int control_pending_wme, control_owner_wme, control_result_wme;
	uint64_t wme_epoch, async_failed;
	int wme_pending, init_requested, quiet_pending, quiet_delivered, async_fault;
	int init_failed, parent_init_failed;
	uint64_t stop_epoch;
	int stop_error, stop_pause;
	int (*parent_key_alloc)(struct ieee80211com *, const struct ieee80211_key *, ieee80211_keyix *, ieee80211_keyix *);
	int (*parent_key_delete)(struct ieee80211com *, const struct ieee80211_key *);
	int (*parent_key_set)(struct ieee80211com *, const struct ieee80211_key *, const uint8_t[IEEE80211_ADDR_LEN]);
	void (*parent_key_begin)(struct ieee80211com *);
	void (*parent_key_end)(struct ieee80211com *);
	struct rtw88_net_rx rx[RTW88_NET_RX_LIMIT];
	unsigned rx_head, rx_tail;
	int work_pending;
	int reset_required;	/* Linked/width stop survives request coalescing. */
	int tx_quiet, quiet_error; /* State requests alone cannot clear the fence. */
	int csa_pending; /* 1: fence awaits INIT; 2: INIT owned. Quiet/stop cancels. */
	enum ieee80211_state requested;
	unsigned channel;
	struct rtw88_channel_tuple pending_channel;
	unsigned configured_media; /* User configuration, not peer-selected mode. */
	int state_arg;
	int (*parent_state)(struct ieee80211com *, enum ieee80211_state, int);
	uint8_t txbuf[RTW88_TX_BUF_SIZE];
	uint64_t rx_dropped;
	struct rtw88_net_stats stats;
	struct rtw88_net_neg neg;	/* Negotiation diagnostics, net->lock. */
	int run_pending;		/* A queued RUN awaits its worker pass. */
	unsigned txba_wanted;	/* TIDs sent without a session, net->lock. */
	unsigned tx_timer;
	unsigned tx_completed;
#ifdef RTW88_DFS64
	uint64_t dfs_frontier;	/* RX descriptors completed before this request. */
	uint32_t dfs_frontier_generation;
	unsigned dfs_frontier_valid;
	int dfs_open_pending, dfs_close_pending;
	uint32_t dfs_tick;	/* Arrival of the beacon behind the permit. */
#endif
#ifdef RTW88_RFK_EXPERIMENT
	struct rtw88_net_rfk rfk;
#endif
};

static void
rtw88_net_hw_capture(struct rtw88_net *net, struct rtw88_net_hw_snapshot *hw)
{
	struct rtw88_softc *sc = net->core;
	unsigned q;

	memset(hw, 0, sizeof(*hw));
	hw->ra = sc->sc_ra;
	hw->phy = sc->sc_phy;
	hw->generation = sc->sc_generation;
	hw->channel = sc->sc_channel;
	hw->started = sc->sc_started;
	hw->running = sc->sc_running;
	hw->linked = sc->sc_linked;
	hw->resources_ready = sc->sc_resources_ready;
	hw->rf_enabled = sc->sc_rf_enabled;
	hw->stop_requested = sc->sc_stop_requested;
#ifdef RTW88_DFS64
	hw->dfs_valid = sc->sc_dfs.valid && sc->sc_dfs.channel == sc->sc_channel &&
	    sc->sc_dfs.generation == sc->sc_generation;
#endif
	hw->power_ready = sc->sc_resources_ready && sc->sc_powered &&
	    sc->sc_power_error == 0 && sc->sc_dma_error == 0 &&
	    (sc->sc_dma_master ||
	     (sc->sc_hci != NULL && sc->sc_hci->no_host_dma));
	for (q = 0; q < RTW88_NTXQ; q++) {
		if (sc->sc_txq[q].queued > UINT_MAX - hw->outstanding) {
			hw->outstanding = UINT_MAX;
			break;
		}
		hw->outstanding += sc->sc_txq[q].queued;
	}
#ifdef RTW88_RFK_EXPERIMENT
	/* This seal validation mutates numeric metadata, never registers.
	 * Do not turn an unfinished calibration into a new seal here. */
	hw->rfk_sealed = sc->sc_operating.sealed &&
	    rtw88_calibration_seal(sc) == 0;
#endif
#ifdef RTW88_VHT80
	hw->vht_nss = rtw88_vht_nss(sc);
	hw->vht_committed = rtw88_vht_committed(sc);
#endif
}


/* Only the serialized hardware owner samples live fields. */
void
rtw88_net_hw_update(struct rtw88_net *net)
{
	struct rtw88_net_hw_snapshot hw;

	if (net == NULL)
		return;
	KASSERT(net->core->sc_executor == NULL ||
	    rtw88_executor_owned(net->core));
	rtw88_net_hw_capture(net, &hw);
	mutex_enter(net->lock);
	net->hw = hw;
	mutex_exit(net->lock);
}

static void
rtw88_net_hw_get_locked(struct rtw88_net *net,
    struct rtw88_net_hw_snapshot *hw)
{

	KASSERT(mutex_owned(net->lock));
	if (net->core->sc_executor == NULL || net->prep_granted)
		rtw88_net_hw_capture(net, hw);
	else
		*hw = net->hw;
}

static void
rtw88_net_state_schedule_locked(struct rtw88_net *net)
{

	KASSERT(mutex_owned(net->lock));
	net->state_again = 1;
	if (net->state_enqueued || net->state_running || net->policy.dying)
		return;
	net->state_enqueued = 1;
	workqueue_enqueue(net->wq, &net->state_work, NULL);
}

static void
rtw88_net_state_idle_locked(struct rtw88_net *net)
{

	KASSERT(mutex_owned(net->lock));
	net->state_running = 0;
	if (net->state_again)
		rtw88_net_state_schedule_locked(net);
}

/* The native bus owner calls these under the supplied interlock. A granted
 * Giant preparation window excludes every owner-side core/ring access;
 * hard IRQ read/mask primitives remain permitted while resources are pinned.
 * A deferred reason stays owned by its request or latched HISR, not a busy
 * executor self-reschedule. Releasing the window wakes the single owner. */
int
rtw88_net_io_begin(struct rtw88_net *net)
{

	if (net == NULL)
		return 1;
	KASSERT(mutex_owned(net->lock));
	KASSERT(!net->io_active);
	if (net->prep_granted)
		return 0;
	if (net->prep_requested && !net->policy.dying && !net->stop_pending) {
		net->prep_requested = 0;
		net->prep_granted = 1;
		rtw88_net_state_schedule_locked(net);
		return 0;
	}
	if (net->policy.dying)
		net->prep_requested = 0;
	net->io_active = 1;
	return 1;
}

int
rtw88_net_io_end(struct rtw88_net *net)
{

	if (net == NULL)
		return 1;
	KASSERT(mutex_owned(net->lock));
	KASSERT(net->io_active && !net->prep_granted);
	net->io_active = 0;
	if (net->prep_requested && !net->policy.dying && !net->stop_pending) {
		net->prep_requested = 0;
		net->prep_granted = 1;
		rtw88_net_state_schedule_locked(net);
	}
	if (net->policy.dying)
		net->prep_requested = 0;
	return !net->prep_granted && !net->stop_pending;
}

int
rtw88_net_rx_epoch(struct rtw88_net *net, uint32_t generation,
    struct rtw88_async_epoch *out)
{

	if (net == NULL || out == NULL || generation == 0)
		return EINVAL;
	KASSERT(mutex_owned(net->lock));
	if (!net->io_active || net->prep_granted)
		return EBUSY;
	if (net->policy.dying || net->stop_pending || net->stop_hold ||
	    net->async.closed)
		return ENXIO;
	if (net->key_error != 0)
		return EACCES;
	if (net->key_depth != 0)
		return EAGAIN;
	if (net->async.id.generation != generation ||
	    net->async.id.epoch != net->policy.epoch ||
	    net->async.id.key_epoch != net->key_epoch)
		return ESTALE;
	*out = net->async.id;
	return 0;
}

/* Caller holds the packet-owner interlock. Reservations are not runnable. */
static unsigned
rtw88_net_tx_queued(const struct rtw88_net *net)
{
	unsigned queued = 0;

	KASSERT(mutex_owned(net->lock));
	for (unsigned q = 0; q < RTW88_NTXQ; q++)
		queued += net->async.queued[q];
	return queued;
}

/* Initial sleeping-bus TX is admitted only for the current live peer. */
static int
rtw88_net_tx_accepting_locked(const struct rtw88_net *net)
{
	KASSERT(mutex_owned(net->lock));
	return net->policy.accepting && !net->policy.dying &&
	    !net->work_pending && !net->tx_quiet && !net->stop_pending &&
	    net->key_depth == 0 && net->key_error == 0 &&
	    net->async_fault == 0 && !net->async.closed;
}

static void
rtw88_net_quiet_locked(struct rtw88_net *net)
{

	KASSERT(mutex_owned(net->lock));
	net->tx_quiet = 1;
	net->quiet_pending = !net->quiet_delivered;
	net->policy.accepting = 0;
	net->stop_pending = 1;
	net->init_requested = 0;
	net->wme_pending = 0;
	net->requested = IEEE80211_S_INIT;
	net->state_arg = -1;
	net->work_pending = 1;
}

static void
rtw88_net_fault_locked(struct rtw88_net *net, int error)
{
	struct rtw88_async_epoch id;

	KASSERT(mutex_owned(net->lock));
	if (net->async_fault != 0)
		return;
	net->async_fault = error != 0 ? error : EIO;
	net->stop_pending = 1;
	net->init_requested = 0;
	net->requested = IEEE80211_S_INIT;
	net->state_arg = -1;
	net->work_pending = 1;
	net->run_pending = 0;
	net->reset_required = 1;
	net->async_failed += rtw88_net_tx_queued(net);
	(void)rtw88_net_request(&net->policy);
	/* This may run in the PCI IRQ frontend: use the last queue identity,
	 * never live core fields owned by the hardware worker. Changing the
	 * peer epoch withdraws unsent owners and marks active bytes stale. */
	id = net->async.id;
	id.epoch = net->policy.epoch;
	id.key_epoch = net->key_epoch;
	rtw88_async_withdraw(&net->async, &id);
}

void
rtw88_net_fault(struct rtw88_net *net, int error)
{

	if (net == NULL)
		return;
	KASSERT(mutex_owned(net->lock));
	/* The owner can observe the same sticky fault on its STOP pass. Do
	 * not post STOP again from that pass and keep the worker alive forever. */
	if (net->async_fault != 0)
		return;
	rtw88_net_fault_locked(net, error);
	softint_schedule(net->softint);
	rtw88_executor_schedule(net->core, RTW88_EXEC_STOP);
}

static int
rtw88_net_prepare_locked(struct rtw88_net *net)
{

	KASSERT(mutex_owned(net->lock));
	if (net->policy.dying)
		return 0;
	if (net->prep_granted) {
		KASSERT(!net->io_active);
		return 1;
	}
	net->prep_requested = 1;
	rtw88_executor_schedule(net->core, RTW88_EXEC_CONTROL);
	return 0;
}

static void
rtw88_net_prepare_release_locked(struct rtw88_net *net)
{

	KASSERT(mutex_owned(net->lock));
	KASSERT(net->prep_granted && !net->io_active);
	net->prep_granted = 0;
	rtw88_executor_schedule(net->core,
	    net->policy.dying || net->stop_pending ? RTW88_EXEC_STOP :
	    RTW88_EXEC_CONTROL | RTW88_EXEC_TX);
}

static int
rtw88_net_same_identity(const struct rtw88_async_epoch *a,
    const struct rtw88_async_epoch *b)
{

	return a->generation == b->generation && a->epoch == b->epoch &&
	    a->key_epoch == b->key_epoch;
}

/* Requests contain values only. New mutating requests use a fresh full
 * identity; a successor may replace unsent work with a new identity. The same
 * identity never denotes different work. STOP/QUIET precede ordinary STATE. */
int
rtw88_net_request_control(struct rtw88_net *net,
    const struct rtw88_control_snapshot *request)
{
	struct rtw88_control_snapshot copy;
	unsigned queued;
	uint32_t reason;
	int cleanup, repeat_cleanup = 0, wake = 0, error = 0;

	if (net == NULL || request == NULL)
		return EINVAL;
	copy = *request;
	if ((unsigned)copy.kind >= RTW88_CTL_DIAG ||
	    (copy.valid & ~(RTW88_CONTROL_BSSID | RTW88_CONTROL_EDCA |
	    RTW88_CONTROL_RA)) != 0)
		return EINVAL;
	cleanup = copy.kind == RTW88_CTL_STOP || copy.kind == RTW88_CTL_QUIET;
	reason = cleanup ? RTW88_EXEC_STOP : RTW88_EXEC_CONTROL;
	mutex_enter(net->lock);
	if (net->policy.dying && !cleanup)
		error = ENXIO;
	else if (copy.id.epoch != net->policy.epoch ||
	    copy.id.key_epoch != net->key_epoch ||
	    copy.id.generation != net->hw.generation)
		error = ESTALE;
	else if (!cleanup && (copy.id.epoch == UINT64_MAX ||
	    copy.id.key_epoch == UINT64_MAX || copy.id.generation == UINT32_MAX))
		error = EOVERFLOW;
	else if (!cleanup && net->async.closed)
		error = net->async.error != 0 ? net->async.error : EOVERFLOW;
	else if (!cleanup && net->key_error != 0)
		error = net->key_error;
	else if (!cleanup && net->key_depth != 0 &&
	    copy.kind != RTW88_CTL_KEY_FENCE)
		error = EBUSY;
	else if (net->control_last_valid &&
	    rtw88_net_same_identity(&copy.id, &net->control_last)) {
		error = EALREADY;
		repeat_cleanup = cleanup && net->control_owner.kind == copy.kind;
	} else if (net->control_pending_valid &&
	    rtw88_net_same_identity(&copy.id, &net->control_pending.id)) {
		error = EALREADY;
		repeat_cleanup = cleanup && net->control_pending.kind == copy.kind;
	} else if (net->control_pending_valid &&
	    ((net->control_pending.kind == RTW88_CTL_STOP &&
	    copy.kind != RTW88_CTL_STOP) ||
	    (net->control_pending.kind == RTW88_CTL_QUIET && !cleanup)))
		error = EBUSY;
	if (error == 0) {
		net->control_pending = copy;
		net->control_pending_valid = 1;
		memset(&net->control_pending_diag, 0,
		    sizeof(net->control_pending_diag));
		net->control_pending_diag.state = copy.state;
		net->control_pending_diag.channel = copy.channel.primary;
		net->control_pending_diag.aid = copy.aid;
		net->control_pending_diag.basic = copy.basic;
		net->control_pending_diag.slot = copy.slot;
		if (cleanup)
			net->stop_pending = 1;
		if (copy.kind == RTW88_CTL_STOP) {
			net->stop_hold = 1;
			net->init_requested = 0;
			net->stats.init_pending = 0;
		}
		if (copy.kind == RTW88_CTL_QUIET) {
			/* Old work may cancel before taking this snapshot. Its
			 * cleanup must already see the accepted quiet intent. */
			rtw88_net_quiet_locked(net);
			wake = 1;
		}
		net->control_pending_wme = copy.kind == RTW88_CTL_STATE &&
		    net->wme_pending &&
		    net->wme_epoch == copy.id.epoch;
		if (net->control_ready &&
		    !rtw88_net_same_identity(&copy.id, &net->control_result.id))
			net->control_ready = 0;
		net->policy.accepting = 0;
		queued = rtw88_net_tx_queued(net);
		rtw88_async_withdraw(&net->async, &copy.id);
		net->async_failed += queued - rtw88_net_tx_queued(net);
		wake |= queued != rtw88_net_tx_queued(net);
		if (!cleanup && net->async.closed) {
			net->control_pending_valid = 0;
			error = net->async.error != 0 ? net->async.error : EOVERFLOW;
			rtw88_net_fault(net, error);
		}
	}
	if (repeat_cleanup)
		net->policy.accepting = 0;
	mutex_exit(net->lock);
	if (wake)
		softint_schedule(net->softint);
	if (error == 0 || repeat_cleanup)
		rtw88_executor_schedule(net->core, reason);
	return repeat_cleanup ? 0 : error;
}

static int
rtw88_net_control_take_locked(struct rtw88_net *net,
    struct rtw88_control_snapshot *request)
{

	KASSERT(mutex_owned(net->lock));
	KASSERT(net->io_active && !net->prep_granted);
	if (net->control_active || net->control_ready ||
	    !net->control_pending_valid)
		return EBUSY;
	*request = net->control_pending;
	net->control_pending_valid = 0;
	if (request->kind != RTW88_CTL_STOP && request->kind != RTW88_CTL_QUIET &&
	    (request->id.generation != net->core->sc_generation ||
	    request->id.epoch != net->policy.epoch ||
	    request->id.key_epoch != net->key_epoch || net->policy.dying)) {
		net->prep_requested = !net->policy.dying;
		return ESTALE;
	}
	net->control_owner = *request;
	net->control_owner_diag = net->control_pending_diag;
	net->control_owner_wme = net->control_pending_wme;
	if (net->control_owner_wme)
		net->wme_pending = 0;
	net->control_last = request->id;
	net->control_last_valid = 1;
	net->control_active = 1;
	return 0;
}

void
rtw88_net_control_done(struct rtw88_net *net,
    const struct rtw88_async_epoch *id, int error)
{
	struct rtw88_async_epoch copy;

	if (net == NULL || id == NULL)
		return;
	copy = *id;
	KASSERT(net->core->sc_executor == NULL ||
	    rtw88_executor_owned(net->core));
	mutex_enter(net->lock);
	if (!net->control_active ||
	    !rtw88_net_same_identity(&copy, &net->control_owner.id)) {
		mutex_exit(net->lock);
		return;
	}
	net->control_active = 0;
	/* Start/stop can change hardware generation. Keep the original request
	 * identity here; the owner separately publishes resulting core values
	 * before granting Giant its completion window. No admission opens here. */
	if (!net->policy.dying && copy.epoch == net->policy.epoch &&
	    copy.key_epoch == net->key_epoch) {
		memset(&net->control_result, 0, sizeof(net->control_result));
		net->control_result.id = copy;
		net->control_result.error = error;
		net->control_result_diag = net->control_owner_diag;
		net->control_result_diag.error = error;
		net->control_result_wme = net->control_owner_wme;
		net->control_ready = 1;
	}
	net->prep_requested = !net->policy.dying;
	mutex_exit(net->lock);
}

#ifdef RTW88_RFK_EXPERIMENT
static void rtw88_net_rfk_publish(struct rtw88_net *);
#endif

/* Process STOP/QUIET on the sole hardware owner. Quiesce remains necessary
 * even if pausing or RFK restoration fails; retain the first error and all
 * resource lifetime gates until the caller verifies complete shutdown. */
static int
rtw88_net_hardware_stop(struct rtw88_net *net, int quiet)
{
	struct rtw88_softc *sc = net->core;
	int error = 0, next;

	KASSERT(rtw88_executor_owned(sc));
	if (quiet && sc->sc_started && sc->sc_running)
		error = rtw88_tx_pause(sc);
#ifdef RTW88_RFK_EXPERIMENT
	if (rtw88_calibration_active(sc)) {
		next = rtw88_calibration_retire(sc);
		if (error == 0)
			error = next;
	}
	rtw88_net_rfk_publish(net);
#endif
	next = rtw88_stop(sc);
	return error != 0 ? error : next;
}

static void
rtw88_net_stop_service(struct rtw88_net *net)
{
	struct rtw88_control_snapshot request;
	uint64_t epoch;
	int have_request = 0, quiet, error;

	KASSERT(rtw88_executor_owned(net->core));
	mutex_enter(net->lock);
	if (net->control_pending_valid &&
	    (net->control_pending.kind == RTW88_CTL_STOP ||
	    net->control_pending.kind == RTW88_CTL_QUIET))
		have_request = rtw88_net_control_take_locked(net, &request) == 0;
	if (!have_request && !net->stop_pending &&
	    (net->async_fault == 0 || net->stop_epoch == net->policy.epoch)) {
		mutex_exit(net->lock);
		return;
	}
	net->stop_pending = 0;
	epoch = net->policy.epoch;
	quiet = net->tx_quiet || net->stop_pause ||
	    (have_request && request.kind == RTW88_CTL_QUIET);
	net->stop_pause = 0;
#ifdef RTW88_DFS64
	net->dfs_open_pending = net->dfs_close_pending = 0;
#endif
	mutex_exit(net->lock);
	/* Keep the old RFK generation/stop identity until its cleanup returns.
	 * Pausing TX does not restore anything or grant a transmission lease. */
	error = rtw88_net_hardware_stop(net, quiet);
	mutex_enter(net->lock);
	net->stop_error = error;
	net->stop_epoch = epoch;
	if (quiet && net->quiet_error == 0)
		net->quiet_error = error;
	net->prep_requested = !net->policy.dying;
	mutex_exit(net->lock);
	if (have_request)
		rtw88_net_control_done(net, &request.id, error);
}

static int
rtw88_net_control_current(struct rtw88_net *net,
    const struct rtw88_control_snapshot *request)
{
	int error = 0;

	mutex_enter(net->lock);
	if (net->policy.dying || net->stop_pending || net->tx_quiet ||
	    request->id.epoch != net->policy.epoch ||
	    request->id.key_epoch != net->key_epoch)
		error = ECANCELED;
	else if (request->id.generation != net->core->sc_generation)
		error = ESTALE;
#ifdef RTW88_RFK_EXPERIMENT
	else if (net->rfk.notice_pending)
		error = ECANCELED;
#endif
	mutex_exit(net->lock);
	return error;
}

static uint32_t
rtw88_net_now_ms(void)
{
	struct timespec now;

	nanouptime(&now);
	return (uint32_t)((uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

static int
rtw88_net_control_pause(struct rtw88_net *net,
    const struct rtw88_control_snapshot *request)
{
	int error;

	error = kpause("rtwctl", false, 1, NULL);
	if (error != 0 && error != EWOULDBLOCK)
		return error;
	return rtw88_net_control_current(net, request);
}

static int
rtw88_net_control_drain(struct rtw88_net *net,
    const struct rtw88_control_snapshot *request)
{
	uint32_t start = rtw88_net_now_ms();
	unsigned pending;
	int error;

	for (;;) {
		error = rtw88_net_control_current(net, request);
		if (error != 0)
			return error;
		rtw88_net_hw_update(net);
		(void)rtw88_intr(net->core);
		error = rtw88_net_control_current(net, request);
		if (error != 0)
			return error;
		error = rtw88_tx_drain_snapshot(net->core, &pending);
		if (error == 0)
			error = rtw88_net_control_current(net, request);
		if (error != 0 || pending == 0)
			return error;
		if ((uint32_t)(rtw88_net_now_ms() - start) >= 1000)
			return ETIMEDOUT;
		error = rtw88_net_control_pause(net, request);
		if (error != 0)
			return error;
	}
}

static int
rtw88_net_control_mailbox(struct rtw88_net *net,
    const struct rtw88_control_snapshot *request, unsigned count)
{
	uint32_t start = rtw88_net_now_ms();
	int error, ready;

	if (count == 0)
		return 0;
	for (;;) {
		error = rtw88_net_control_current(net, request);
		if (error != 0)
			return error;
		rtw88_net_hw_update(net);
		(void)rtw88_intr(net->core);
		error = rtw88_net_control_current(net, request);
		if (error != 0)
			return error;
		error = rtw88_fw_mailbox_begin(net->core, count, &ready);
		if (error != 0 || ready)
			return error;
		if ((uint32_t)(rtw88_net_now_ms() - start) >=
		    RTW88_COMMAND_US / 1000)
			return ETIMEDOUT;
		error = rtw88_net_control_pause(net, request);
		if (error != 0)
			return error;
	}
}

/* The initial sleeping-bus implementation supports legacy 2.4 GHz only.
 * Wider/RFK/DFS operation stays on the existing PCI worker. */
static int
rtw88_net_control_execute(struct rtw88_net *net,
    const struct rtw88_control_snapshot *request, int wme)
{
	struct rtw88_softc *sc = net->core;
	struct rtw88_net_state_diag *diag = &net->control_owner_diag;
	struct rtw88_control_snapshot check;
	unsigned count;
	int error, reserved = 0;

	if (net->caps.bands != RTW88_BAND_2G)
		return EOPNOTSUPP;
	diag->stage = RTW88_NET_STAGE_CORE;
	error = rtw88_net_control_current(net, request);
	if (error != 0)
		return error;
	if (request->kind == RTW88_CTL_INIT || request->kind == RTW88_CTL_RECOVER) {
		diag->stage = RTW88_NET_STAGE_RESTART;
		if (sc->sc_started || sc->sc_running || sc->sc_powered ||
		    sc->sc_power_dirty || sc->sc_dma_master) {
			error = rtw88_net_hardware_stop(net, 0);
			if (error != 0)
				return error;
		}
		check = *request;
		check.id.generation = sc->sc_generation;
		error = rtw88_net_control_current(net, &check);
		if (error != 0)
			return error;
		if (sc->sc_generation >= UINT32_MAX - 1)
			return EOVERFLOW;
		error = rtw88_start_tuple(sc, &request->channel);
		if (error == 0) {
			check.id.generation = sc->sc_generation;
			error = rtw88_net_control_current(net, &check);
		}
		return error;
	}
	if (request->kind == RTW88_CTL_KEY_FENCE)
		return 0;
	if (request->kind != RTW88_CTL_STATE)
		return EINVAL;
	if (!sc->sc_started)
		return request->state == IEEE80211_S_INIT ? 0 : ENXIO;
	error = rtw88_net_control_drain(net, request);
	if (error != 0)
		return error;
	if (!wme) {
		rtw88_ra_invalidate(sc);
		rtw88_dfs_close(sc);
	}
	count = wme ? ((request->valid & RTW88_CONTROL_RA) != 0 &&
	    !request->ra.use_protection) :
	    request->state == IEEE80211_S_RUN ? rtw88_station_commands(sc, 1) +
	    ((request->valid & RTW88_CONTROL_RA) != 0 &&
	    !request->ra.use_protection) : 1;
	error = rtw88_net_control_mailbox(net, request, count);
	if (error != 0)
		goto done;
	reserved = count != 0;
	error = rtw88_net_control_current(net, request);
	if (error == 0 && !wme && request->state != IEEE80211_S_INIT) {
		diag->stage = RTW88_NET_STAGE_CHANNEL;
		error = rtw88_channel_apply(sc, &request->channel);
	}
	if (error == 0)
		error = rtw88_net_control_current(net, request);
	if (error == 0 && !wme) {
		diag->stage = RTW88_NET_STAGE_STATION;
		error = rtw88_station_config(sc,
		    (request->valid & RTW88_CONTROL_BSSID) != 0 ? request->bssid : NULL,
		    request->aid, request->basic, request->slot,
		    request->state == IEEE80211_S_RUN);
	}
	if (error == 0)
		error = rtw88_net_control_current(net, request);
	if (error == 0 && (request->valid & RTW88_CONTROL_EDCA) != 0) {
		diag->stage = RTW88_NET_STAGE_EDCA;
		error = rtw88_edca_apply(sc, request->edca, request->slot);
	}
	if (error == 0)
		error = rtw88_net_control_current(net, request);
	if (error == 0 && (request->valid & RTW88_CONTROL_RA) != 0) {
		diag->stage = RTW88_NET_STAGE_RA;
		error = rtw88_ra_configure(sc, &request->ra);
	}
done:
	if (reserved)
		error = rtw88_fw_mailbox_end(sc, error);
	return error;
}

static void
rtw88_net_control_service(struct rtw88_net *net)
{
	struct rtw88_control_snapshot request;
	int error, cleanup_error, quiet;

	KASSERT(rtw88_executor_owned(net->core));
	mutex_enter(net->lock);
	error = rtw88_net_control_take_locked(net, &request);
	mutex_exit(net->lock);
	if (error != 0)
		return;
	error = rtw88_net_control_execute(net, &request, net->control_owner_wme);
	if (error != 0) {
		/* A stale/failed transaction may have mutated the old peer. Close
		 * hardware before preparing a successor; never replay its commands. */
		mutex_enter(net->lock);
		quiet = net->tx_quiet;
		mutex_exit(net->lock);
		cleanup_error = rtw88_net_hardware_stop(net, quiet);
		mutex_enter(net->lock);
		net->reset_required = 1;
		net->stop_error = cleanup_error;
		mutex_exit(net->lock);
	}
	net->control_owner_diag.core_channel = net->core->sc_channel;
	net->control_owner_diag.started = net->core->sc_started;
	net->control_owner_diag.running = net->core->sc_running;
	rtw88_net_control_done(net, &request.id, error);
}

static void
rtw88_net_tx_retry(void *cookie)
{
	struct rtw88_net *net = cookie;
	int schedule;

	mutex_enter(net->lock);
	schedule = rtw88_net_tx_accepting_locked(net) &&
	    rtw88_net_tx_queued(net) != 0;
	mutex_exit(net->lock);
	if (schedule)
		rtw88_executor_schedule(net->core, RTW88_EXEC_TX);
}

/* One process-context hardware owner, without Giant or the short interlock.
 * A control request closes admission between jobs; it is never executed by
 * this TX-only pass. The matching finish alone releases an active payload. */
static void
rtw88_net_tx_service(struct rtw88_net *net)
{
	struct rtw88_softc *sc = net->core;
	struct rtw88_async_epoch id;
	struct rtw88_async_ticket ticket;
	const struct rtw88_tx_job *job;
	enum rtw88_tx_progress progress;
	unsigned before, n;
	int error, finished, retry, stale, fatal;
	int wake = 0, more = 0, stop = 0;

	for (n = 0; n < RTW88_NET_TX_BUDGET; n++) {
		mutex_enter(net->lock);
		if (!rtw88_net_tx_accepting_locked(net)) {
			mutex_exit(net->lock);
			break;
		}
		id.generation = sc->sc_generation;
		id.epoch = net->policy.epoch;
		id.key_epoch = net->key_epoch;
		rtw88_async_withdraw(&net->async, &id);
		before = net->async.total;
		error = rtw88_async_take(&net->async, rtw88_net_now_ms(),
		    &ticket, &job);
		net->async_failed += before - net->async.total;
		wake |= before != net->async.total || net->async.wake_mask != 0;
		net->async.wake_mask = 0;
		mutex_exit(net->lock);
		if (error != 0)
			break;

		progress = RTW88_TX_NOT_STARTED;
		error = rtw88_tx_execute(sc, job, &progress);

		mutex_enter(net->lock);
		id.generation = sc->sc_generation;
		id.epoch = net->policy.epoch;
		id.key_epoch = net->key_epoch;
		rtw88_async_withdraw(&net->async, &id);
		stale = job->id.generation != id.generation ||
		    job->id.epoch != id.epoch || job->id.key_epoch != id.key_epoch;
		finished = rtw88_async_finish(&net->async, &ticket, error,
		    progress, rtw88_net_now_ms());
		KASSERT(finished == 0);
		retry = finished == 0 && error == EBUSY &&
		    progress == RTW88_TX_NOT_STARTED &&
		    net->async.slots[ticket.slot].state == RTW88_ASYNC_QUEUED &&
		    net->async.slots[ticket.slot].serial == ticket.serial;
		if ((error != 0 || finished != 0) && !retry && !stale)
			net->async_failed++;
		fatal = finished != 0 || progress == RTW88_TX_MAY_HAVE_WRITTEN ||
		    (error != 0 && progress == RTW88_TX_ACCEPTED) ||
		    (!stale && error != 0 && error != EBUSY);
		if (fatal) {
			/* Keep core running until the owned stop masks/drains HCI. */
			sc->sc_stop_requested = 1;
			rtw88_net_fault_locked(net,
			    finished != 0 ? finished : error);
			wake = 1;
			stop = 1;
		}
		wake |= net->async.wake_mask != 0;
		net->async.wake_mask = 0;
		if (retry && rtw88_net_tx_accepting_locked(net))
			callout_schedule(&net->tx_retry, MAX(1, mstohz(10)));
		mutex_exit(net->lock);
		if (retry || fatal)
			break;
	}
	if (n == RTW88_NET_TX_BUDGET) {
		mutex_enter(net->lock);
		more = rtw88_net_tx_accepting_locked(net) &&
		    rtw88_net_tx_queued(net) != 0;
		mutex_exit(net->lock);
	}
	if (wake)
		softint_schedule(net->softint);
	if (stop)
		rtw88_executor_schedule(sc, RTW88_EXEC_STOP);
	else if (more)
		rtw88_executor_schedule(sc, RTW88_EXEC_TX);
}

/* Called by the transport owner inside a successful net I/O window. */
void
rtw88_async_service(struct rtw88_softc *sc, uint32_t reasons)
{
	KASSERT(sc != NULL && rtw88_executor_owned(sc));
	if ((reasons & RTW88_EXEC_STOP) != 0) {
		if (sc->sc_net != NULL)
			rtw88_net_stop_service(sc->sc_net);
		else
			(void)rtw88_stop(sc);
		return;
	}
	if (sc->sc_net == NULL)
		return;
	if ((reasons & RTW88_EXEC_CONTROL) != 0)
		rtw88_net_control_service(sc->sc_net);
	if ((reasons & RTW88_EXEC_TX) != 0)
		rtw88_net_tx_service(sc->sc_net);
}

/* Giant owns crypto callbacks and key material. The hardware owner receives
 * only immutable bytes. Withdraw unsent packets before a parent can replace a
 * key; an already active packet retains its bytes until its matching finish.
 * Native software crypto parents perform no hardware key installation. */
static void
rtw88_net_key_enter(struct rtw88_net *net)
{
	struct rtw88_async_epoch id;

	mutex_enter(net->lock);
	if (net->key_depth == UINT_MAX) {
		net->key_error = EOVERFLOW;
	} else if (net->key_depth++ != 0) {
		mutex_exit(net->lock);
		return;
	} else if (net->key_epoch < UINT64_MAX) {
		net->key_epoch++;
	}
	id = net->async.id;
	id.epoch = net->policy.epoch;
	id.key_epoch = net->key_error != 0 ? UINT64_MAX : net->key_epoch;
	rtw88_async_withdraw(&net->async, &id);
	if (net->async.closed)
		net->key_error = net->async.error;
	mutex_exit(net->lock);
}

static void
rtw88_net_key_leave(struct rtw88_net *net)
{
	struct rtw88_async_epoch id;
	int wake;

	mutex_enter(net->lock);
	if (net->key_depth == 0) {
		/* A broken parent nesting contract must not reopen admission. */
		net->key_error = EINVAL;
		id = net->async.id;
		id.key_epoch = UINT64_MAX;
		rtw88_async_withdraw(&net->async, &id);
	} else {
		net->key_depth--;
	}
	wake = net->key_depth == 0 && net->key_error == 0 &&
	    net->policy.accepting && !net->policy.dying && !net->tx_quiet;
	mutex_exit(net->lock);
	if (wake)
		softint_schedule(net->softint);
}

static int
rtw88_net_key_alloc(struct ieee80211com *ic, const struct ieee80211_key *key,
    ieee80211_keyix *tx, ieee80211_keyix *rx)
{
	struct rtw88_net *net = ic->ic_ifp->if_softc;
	int result;

	rtw88_net_key_enter(net);
	result = net->parent_key_alloc != NULL ?
	    net->parent_key_alloc(ic, key, tx, rx) : 0;
	rtw88_net_key_leave(net);
	return result;
}

static int
rtw88_net_key_delete(struct ieee80211com *ic, const struct ieee80211_key *key)
{
	struct rtw88_net *net = ic->ic_ifp->if_softc;
	int result;

	rtw88_net_key_enter(net);
	result = net->parent_key_delete != NULL ?
	    net->parent_key_delete(ic, key) : 0;
	rtw88_net_key_leave(net);
	return result;
}

static int
rtw88_net_key_set(struct ieee80211com *ic, const struct ieee80211_key *key,
    const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct rtw88_net *net = ic->ic_ifp->if_softc;
	int result;

	rtw88_net_key_enter(net);
	result = net->parent_key_set != NULL ?
	    net->parent_key_set(ic, key, mac) : 0;
	rtw88_net_key_leave(net);
	return result;
}

static void
rtw88_net_key_begin(struct ieee80211com *ic)
{
	struct rtw88_net *net = ic->ic_ifp->if_softc;

	rtw88_net_key_enter(net);
	if (net->parent_key_begin != NULL)
		net->parent_key_begin(ic);
}

static void
rtw88_net_key_end(struct ieee80211com *ic)
{
	struct rtw88_net *net = ic->ic_ifp->if_softc;
	int unmatched;

	mutex_enter(net->lock);
	unmatched = net->key_depth == 0;
	mutex_exit(net->lock);
	/* Even a malformed end must close admission before its parent runs. */
	if (unmatched)
		rtw88_net_key_leave(net);
	if (net->parent_key_end != NULL)
		net->parent_key_end(ic);
	if (!unmatched)
		rtw88_net_key_leave(net);
}

static void rtw88_net_start(struct ifnet *);
static void rtw88_net_stop(struct ifnet *, int);
static int rtw88_net_init(struct ifnet *);
static int rtw88_net_newstate(struct ieee80211com *, enum ieee80211_state, int);
#ifdef RTW88_DFS64
static void rtw88_net_dfs_beacon(struct rtw88_net *, const uint8_t *, size_t,
    uint64_t, uint32_t, uint32_t, uint64_t, uint32_t, uint32_t);
static int rtw88_net_dfs_expire(struct rtw88_net *, uint32_t, uint32_t);
#endif
static void rtw88_net_ht20_publish(struct rtw88_net *,
    const struct ieee80211_ht20_profile *);
static int rtw88_net_ht20_admit(struct ieee80211com *,
    struct ieee80211_ht20_profile *);
static int rtw88_net_ht20_local_policy(struct ieee80211com *);
#ifdef RTW88_VHT80
static int rtw88_net_vht_admit(struct ieee80211com *,
    const struct ieee80211_vht_channel *, struct ieee80211_vht_cap *);
#endif
static int rtw88_net_ht20_profile_prepare(struct rtw88_net *,
    struct ieee80211_ht20_profile *);

/* Giant owns the spectrum event. Close admission before serialized device I/O.
 * Shared net80211 purges RX BA and requests INIT after this callback returns.
 * The existing worker owns sleeping stop and DMA release. */
/* Sleeping transports fence admission before requesting owner-side stop. */
static void
rtw88_net_sta_quiet_async(struct ieee80211com *ic)
{
	struct rtw88_net *net = ic->ic_ifp->if_softc;
	struct ifnet *ifp = &net->ec.ec_if;
	struct rtw88_async_epoch id;

	if (net->quiet_delivered)
		return;
	rtw88_net_ht20_publish(net, NULL);
	callout_stop(&net->scan);
	mutex_enter(net->lock);
	net->csa_pending = 0;
	net->tx_quiet = 1;
	net->quiet_pending = 0;
	net->quiet_delivered = 1;
	(void)rtw88_net_request(&net->policy);
	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
	ifp->if_timer = 0;
	net->tx_timer = 0;
	id = net->async.id;
	id.epoch = net->policy.epoch;
	id.key_epoch = net->key_epoch;
	net->async_failed += rtw88_net_tx_queued(net);
	rtw88_async_withdraw(&net->async, &id);
	net->stop_pending = 1;
	mutex_exit(net->lock);
	rtw88_executor_schedule(net->core, RTW88_EXEC_STOP);
}

static void
rtw88_net_sta_quiet(struct ieee80211com *ic)
{
	struct rtw88_net *net = ic->ic_ifp->if_softc;
	struct ifnet *ifp = &net->ec.ec_if;

	if (net->core->sc_executor != NULL) {
		rtw88_net_sta_quiet_async(ic);
		return;
	}

	/* A later Quiet or malformed announcement makes a CSA fence permanent. */
	net->csa_pending = 0;
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

/* A valid switch can recover through the sleepable worker. An earlier
 * ambiguous/Quiet fence must not be upgraded by a later switch. RFK trials
 * retain their explicit cold-reset and single-association lifetime. */
static void
rtw88_net_sta_csa(struct ieee80211com *ic)
{
	struct rtw88_net *net = ic->ic_ifp->if_softc;

	if (net->tx_quiet)
		return;
	rtw88_net_sta_quiet(ic);
	if (net->core->sc_executor != NULL) {
		if (net->quiet_error == 0)
			net->csa_pending = 1;
		return;
	}
#ifndef RTW88_RFK_EXPERIMENT
	if (net->quiet_error == 0)
		net->csa_pending = 1;
#endif
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
	    rtw88_calibration_seal(net->core) != 0)
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

#ifdef RTW88_VHT80
/*
 * Local VHT offer: NSS1 MCS0-7 receive and transmit at up to 80 MHz, with
 * no optional feature and no 160 MHz. net80211 offers it only beside an
 * admitted HT20 offer, for the exact 40 or 80 MHz channel it derived from
 * the AP. That tuple must pass the pure core check of channel policy, PHY
 * geometry and TX power, so the cold start can enter it. Snapshot under
 * Giant; no PHY change happens here. newstate(RUN) then targets the
 * negotiated tuple, and the width stop/restart owner enters it through
 * rtw88_start_tuple().
 */
static int
rtw88_net_vht_admit(struct ieee80211com *ic,
    const struct ieee80211_vht_channel *chan, struct ieee80211_vht_cap *cap)
{
	struct rtw88_net *net;
	struct rtw88_channel_tuple tuple;
	int error = 0;

	if (ic == NULL || chan == NULL || cap == NULL || ic->ic_ifp == NULL)
		return EINVAL;
	memset(cap, 0, sizeof(*cap));
	net = ic->ic_ifp->if_softc;
	if (net == NULL || net->core == NULL)
		return ENXIO;
	KASSERT(!mutex_owned(net->lock));
	tuple.primary = chan->primary;
	tuple.center = chan->center;
	tuple.width_mhz = chan->width_mhz;
	if (net->tx_quiet || net->policy.dying ||
	    ic->ic_opmode != IEEE80211_M_STA ||
	    rtw88_net_ht20_local_policy(ic) != 0 ||
	    ieee80211_chan2ieee(ic, ic->ic_curchan) != chan->primary ||
	    (chan->width_mhz != 40 && chan->width_mhz != 80) ||
	    rtw88_tuple_check(net->core, &tuple) != 0)
		error = EOPNOTSUPP;
	else {
		/* Two streams only on proven 2T2R hardware. */
		cap->rx_map = cap->tx_map = rtw88_vht_nss(net->core) == 2 ?
		    RTW88_VHT_MAP_NSS2_MCS9 : RTW88_VHT_MAP_NSS1_MCS9;
		/* RX LDPC (bit4; rtw8822c rx_ldpc) and SGI80 reception. */
		cap->info = 0x30;
	}
	mutex_enter(net->lock);
	if (error == 0)
		net->neg.vht_admit++;
	else
		net->neg.vht_refuse++;
	net->neg.vht_error = error;
	net->neg.vht_chan[0] = chan->primary;
	net->neg.vht_chan[1] = chan->center;
	net->neg.vht_chan[2] = chan->width_mhz;
	mutex_exit(net->lock);
	return error;
}
#endif

#ifdef RTW88_VHT80
/* The committed core tuple is the one this node negotiated. */
static int
rtw88_net_vht_tuple(const struct rtw88_net *net,
    const struct ieee80211_node *ni)
{
	const struct rtw88_channel_tuple *t = &net->core->sc_phy.channel;
	const struct ieee80211_vht_channel *c = &ni->ni_ht20.vht.channel;

	return rtw88_vht_committed(net->core) && t->primary == c->primary &&
	    t->center == c->center && t->width_mhz == c->width_mhz;
}
#endif

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
	if (!sc->sc_operating.sealed || rtw88_calibration_seal(sc) != 0)
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
	if (ready != RTW88_NET_HT20_READY_ALL) {
		profile->ready = ready; /* Diagnostic only; never published. */
		return EOPNOTSUPP;
	}
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
	cfg->sgi = cfg->ldpc = 0;
#ifdef RTW88_VHT80
	/* HT Capabilities SGI20 (bit5) and LDPC (bit0) for an HT20 entry. */
	cfg->sgi = (protection.peer_cap & 0x20) != 0;
	cfg->ldpc = (protection.peer_cap & 0x01) != 0;
#endif
	if (ni->ni_ht20.vht.valid) {
#ifdef RTW88_VHT80
		struct rtw88_net *net = ic->ic_ifp->if_softc;

		/* The cold-start owner must already have committed its tuple. */
		if (net == NULL || !rtw88_net_vht_tuple(net, ni))
			return EOPNOTSUPP;
		cfg->phy = RTW88_RA_VHT_1SS;
		cfg->ht_mcs = 0;
		cfg->vht_tx_map = ni->ni_ht20.vht.maps.tx_map;
		/* VHT Capabilities SGI80 (bit5) at 80 MHz, HT SGI40 (bit6)
		 * at 40 MHz. */
		cfg->sgi = ni->ni_ht20.vht.channel.width_mhz == 80 ?
		    (ni->ni_ht20.vht.peer.info & 0x20) != 0 :
		    (protection.peer_cap & 0x40) != 0;
		/* VHT Capabilities RX LDPC (bit4). */
		cfg->ldpc = (ni->ni_ht20.vht.peer.info & 0x10) != 0;
#else
		return EOPNOTSUPP;
#endif
	}
	return 0;
}

/* Called under net->lock before accepting another RUN payload. */
static int
rtw88_net_tx_ra_validate(struct rtw88_net *net,
    const struct rtw88_ra_config *policy, uint64_t epoch)
{
	struct ieee80211com *ic = &net->ic;
	struct ieee80211_node *ni = ic->ic_bss;
	const struct ieee80211_ht20_node *h;
	const struct rtw88_ra_state *ra = &net->core->sc_ra;
	int vht;

	KASSERT(mutex_owned(net->lock));
	if (policy == NULL || ni == NULL)
		return EPROTO;
	h = &ni->ni_ht20;
	vht = h->vht.valid;
	if (!h->valid ||
	    (vht && policy->phy != RTW88_RA_VHT_1SS) ||
	    (!vht && policy->phy != RTW88_RA_HT20_1SS) ||
	    ra->phy != policy->phy || !ra->configured || !ra->active ||
	    ra->error != 0 || ra->epoch != epoch ||
	    ra->generation != net->core->sc_generation ||
	    ra->channel != net->core->sc_channel ||
	    ra->channel != ieee80211_chan2ieee(ic, ic->ic_curchan) ||
	    (ni->ni_associd & 0x3fff) == 0 ||
	    (ni->ni_associd & 0x3fff) > 2007 || ra->channel <= 14 ||
	    ic->ic_curmode != IEEE80211_MODE_11A ||
	    ic->ic_fixed_rate != IEEE80211_FIXED_RATE_NONE ||
	    (ic->ic_flags & IEEE80211_F_USEPROT) != 0 ||
	    ra->ht.qualification != policy->ht.qualification ||
	    ra->ht.peer_cap != policy->ht.peer_cap ||
	    ra->ht.secondary != policy->ht.secondary ||
	    ra->ht.mtu != policy->ht.mtu ||
	    ra->ht.max_mpdu != policy->ht.max_mpdu ||
	    ra->ht.rts_threshold != policy->ht.rts_threshold ||
	    ra->sgi != policy->sgi || ra->ldpc != policy->ldpc ||
	    h->profile.rx_mcs != 0xff || h->profile.tx_mcs != 0xff ||
	    h->tx_mcs == 0 || (h->tx_mcs & ~0xffU) != 0 ||
	    ra->profile_generation != h->profile.generation ||
	    ra->ht_mcs != (vht ? 0 : h->tx_mcs) ||
	    ra->ht_operation != h->operation)
		return EPROTO;
	if (vht) {
#ifdef RTW88_VHT80
		if (!rtw88_net_vht_tuple(net, ni) ||
		    h->vht.maps.tx_map != policy->vht_tx_map ||
		    ra->vht_tx_map != policy->vht_tx_map)
			return EPROTO;
#else
		return EPROTO;
#endif
	} else if (ra->vht_tx_map != 0)
		return EPROTO;
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
	struct rtw88_channel_tuple tuple;
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
	tuple = net->pending_channel;
	mutex_exit(net->lock);
	/* INIT must retain a real READY core for the next WPA SCAN_REQ.
	 * Only this stopped core may change RF width, before HCI start. */
	if (start) {
		error = tuple.width_mhz != 20 ?
		    rtw88_start_tuple(net->core, &tuple) :
		    rtw88_start(net->core);
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
	/* A request during the sleeping start may want another width. */
	net->reset_required = start &&
	    net->pending_channel.width_mhz != tuple.width_mhz;
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

static int
rtw88_net_state_snapshot(struct rtw88_net *net,
    struct rtw88_control_snapshot *request, struct rtw88_net_state_diag *diag,
    int wme)
{
	struct ieee80211com *ic = &net->ic;
	unsigned media, slot, basic;
	int error;

	if (wme)
		return EOPNOTSUPP;
	memset(request, 0, sizeof(*request));
	memset(diag, 0, sizeof(*diag));
	mutex_enter(net->lock);
	KASSERT(net->prep_granted && !net->io_active && !net->control_active);
	request->kind = RTW88_CTL_STATE;
	request->id.generation = net->core->sc_generation;
	request->id.epoch = net->policy.epoch;
	request->id.key_epoch = net->key_epoch;
	request->state = net->requested;
	request->arg = net->state_arg;
	request->channel = net->pending_channel;
	media = net->configured_media;
	error = net->policy.dying || net->tx_quiet ? ECANCELED : 0;
	mutex_exit(net->lock);
	if (error != 0)
		return error;
	diag->stage = RTW88_NET_STAGE_CORE;
	slot = request->channel.primary > 14 ? 9 : 20;
	basic = request->channel.primary > 14 ? 0x150 : 0x0f;
	if ((request->state == IEEE80211_S_AUTH ||
	    request->state == IEEE80211_S_ASSOC ||
	    request->state == IEEE80211_S_RUN) && ic->ic_bss != NULL &&
	    ic->ic_bss->ni_rates.rs_nrates != 0)
		basic = rtw88_net_basic_rates(ic);
	if (request->state == IEEE80211_S_RUN) {
		unsigned i;
		uint8_t any = 0;

		if (ic->ic_bss == NULL)
			return EPROTO;
		memcpy(request->bssid, ic->ic_bss->ni_bssid,
		    sizeof(request->bssid));
		request->valid |= RTW88_CONTROL_BSSID;
		request->aid = ic->ic_bss->ni_associd & 0x3fff;
		diag->bssid_flags = 1 | ((request->bssid[0] & 1) << 1);
		for (i = 0; i < sizeof(request->bssid); i++)
			any |= request->bssid[i];
		if (any == 0)
			diag->bssid_flags |= 4;
		if ((ic->ic_flags & IEEE80211_F_SHSLOT) != 0)
			slot = 9;
	}
	request->basic = basic;
	request->slot = slot;
	diag->state = request->state;
	diag->channel = request->channel.primary;
	diag->basic = request->basic;
	diag->slot = request->slot;
	diag->aid = request->aid;
	diag->raw_aid = ic->ic_bss != NULL ? ic->ic_bss->ni_associd : 0;
	diag->nrates = ic->ic_bss != NULL ? ic->ic_bss->ni_rates.rs_nrates : 0;
	diag->flags = ic->ic_flags;
	error = request->state == IEEE80211_S_RUN ?
	    ieee80211_ht20_state_request(ic, request->state) : 0;
	if (error == 0 && request->state == IEEE80211_S_RUN &&
	    ic->ic_bss != NULL && ic->ic_bss->ni_ht20.valid &&
	    ic->ic_fixed_rate != IEEE80211_FIXED_RATE_NONE)
		error = EOPNOTSUPP;
	if (error == 0 && request->state != IEEE80211_S_INIT) {
		diag->stage = RTW88_NET_STAGE_EDCA;
		error = rtw88_net_edca_prepare(ic, media, request->state,
			    request->channel.primary, request->slot, request->edca);
		if (error == 0)
			request->valid |= RTW88_CONTROL_EDCA;
	}
	if (error == 0 && request->state == IEEE80211_S_RUN &&
	    ic->ic_fixed_rate == IEEE80211_FIXED_RATE_NONE) {
		diag->stage = RTW88_NET_STAGE_RA;
		error = rtw88_net_ra_prepare(ic, request->channel.primary, basic,
		    request->id.epoch, &request->ra);
		if (error == 0)
			request->valid |= RTW88_CONTROL_RA;
	}
	mutex_enter(net->lock);
	if (net->policy.dying || net->tx_quiet ||
	    request->id.epoch != net->policy.epoch ||
	    request->id.key_epoch != net->key_epoch ||
	    request->id.generation != net->core->sc_generation)
		error = ECANCELED;
	mutex_exit(net->lock);
	return error;
}


static int
rtw88_net_state_current_locked(struct rtw88_net *net,
    const struct rtw88_async_epoch *id)
{

	KASSERT(mutex_owned(net->lock));
	return !net->policy.dying && id->epoch == net->policy.epoch &&
	    id->key_epoch == net->key_epoch;
}

/* Giant failure publication never performs hardware cleanup or waits for it. */
static void
rtw88_net_state_fail(struct rtw88_net *net,
    const struct rtw88_async_epoch *id, int error,
    const struct rtw88_net_state_diag *diag)
{
	struct ieee80211com *ic = &net->ic;
	struct ifnet *ifp = &net->ec.ec_if;
	char failure[384];
	int parent_error;

	mutex_enter(net->lock);
	if (!rtw88_net_state_current_locked(net, id)) {
		mutex_exit(net->lock);
		return;
	}
	net->policy.accepting = 0;
	net->work_pending = 0;
	net->init_failed = 1;
	net->init_requested = 0;
	net->stats.init_pending = 0;
	net->reset_required = 1;
	net->stop_pending = 1;
	if (diag->state == IEEE80211_S_RUN) {
		net->neg.run_fails++;
		net->neg.run_error = error;
		net->neg.run_stage = (uint8_t)diag->stage;
	}
	mutex_exit(net->lock);
	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
	ifp->if_timer = 0;
	callout_stop(&net->scan);
	rtw88_net_ht20_publish(net, NULL);
	(void)ieee80211_ht20_state_request(ic, IEEE80211_S_INIT);
	(void)rtw88_net_state_format(failure, sizeof(failure), diag);
	aprint_error_dev(net->dev, "%s", failure);
	mutex_enter(net->lock);
	/* Preparatory INIT may already have completed before start failed.
	 * A failed INIT is not replayed, and a newer request owns the stack. */
	parent_error = rtw88_net_state_current_locked(net, id) &&
	    !net->parent_init_failed && ic->ic_state != IEEE80211_S_INIT;
	mutex_exit(net->lock);
	parent_error = parent_error ? net->parent_state(ic, IEEE80211_S_INIT, -1) : 0;
	mutex_enter(net->lock);
	if (parent_error != 0 && rtw88_net_state_current_locked(net, id))
		net->parent_init_failed = 1;
	mutex_exit(net->lock);
}

/* Giant publishes only a still-current completed 2.4 GHz legacy request. */
static int
rtw88_net_state_complete(struct rtw88_net *net,
    const struct rtw88_control_snapshot *request,
    const struct rtw88_net_state_diag *diag, int wme)
{
	struct ieee80211com *ic = &net->ic;
	struct ifnet *ifp = &net->ec.ec_if;
	struct rtw88_net_state_diag failure = *diag;
	int error, current;

	if (wme)
		return EOPNOTSUPP;
	mutex_enter(net->lock);
	current = rtw88_net_state_current_locked(net, &request->id);
	mutex_exit(net->lock);
	if (!current)
		return ECANCELED;
	error = net->parent_state(ic, request->state, request->arg);
	mutex_enter(net->lock);
	current = rtw88_net_state_current_locked(net, &request->id);
	if (current && error == 0) {
		error = rtw88_net_commit(&net->policy, request->id.epoch, 0,
		    request->state != IEEE80211_S_INIT);
		net->work_pending = 0;
		net->run_pending = 0;
		if (request->state == IEEE80211_S_RUN) {
			net->neg.run_published++;
			if (rtw88_net_beacon_start(&net->policy, request->id.epoch,
			    (uint32_t)getticks()) == 0)
				ifp->if_timer = 1;
		}
	} else if (!current && !net->policy.dying) {
		net->prep_requested = 1;
		net->state_again = 1;
	}
	mutex_exit(net->lock);
	if (!current)
		return ECANCELED;
	if (error != 0) {
		failure.stage = RTW88_NET_STAGE_CORE;
		failure.error = error;
		rtw88_net_state_fail(net, &request->id, error, &failure);
	} else {
		if (request->state == IEEE80211_S_SCAN)
			callout_schedule(&net->scan, mstohz(200));
		softint_schedule(net->softint);
	}
	return error;
}

/* Return nonzero when the worker must resnapshot its current request.
 * Admission stays closed through parent INIT and a verified full DMA stop.
 * Never clear the RFK lifetime, a failed fence, or a generic Quiet event. */
static int
rtw88_net_csa_recover(struct rtw88_net *net)
{
	struct ieee80211com *ic = &net->ic;
	struct ifnet *ifp = &net->ec.ec_if;
	uint64_t epoch;
	int error, stop_error, resume;

	mutex_enter(net->lock);
	if (!net->csa_pending) {
		mutex_exit(net->lock);
		return 0;
	}
	epoch = net->policy.epoch;
	mutex_exit(net->lock);
	/* The immediate fence has already removed RUNNING and closed TX. */
	error = net->parent_state(ic, IEEE80211_S_INIT, -1);
	stop_error = rtw88_stop(net->core);
	if (error == 0)
		error = stop_error;
	mutex_enter(net->lock);
	resume = error == 0 && net->quiet_error == 0 && net->csa_pending &&
	    !net->policy.dying && (ifp->if_flags & IFF_UP) != 0;
	if (!resume) {
		net->csa_pending = 0;
		mutex_exit(net->lock);
		if (error != 0)
			aprint_error_dev(net->dev, "CSA stop failed (%d); TX quiet\n",
			    error);
		return 1;
	}
	if (epoch != net->policy.epoch) {
		mutex_exit(net->lock);
		return 1;
	}
	net->reset_required = 0;
	mutex_exit(net->lock);
	/* Keep the host fence while firmware loading can yield. A successor
	 * request, Quiet, stop or detach must not inherit this start. */
	error = rtw88_start(net->core);
	mutex_enter(net->lock);
	resume = error == 0 && net->csa_pending && !net->policy.dying &&
	    epoch == net->policy.epoch && (ifp->if_flags & IFF_UP) != 0;
	if (resume) {
		net->csa_pending = 0;
		net->tx_quiet = 0;
		ifp->if_flags |= IFF_RUNNING;
	}
	mutex_exit(net->lock);
	if (resume) {
		/* Fresh scanning selects the AP; no advertised successor tuple or
		 * old AUTH/RUN request grants a DFS lease or a peer contract. */
		error = rtw88_net_newstate(ic, IEEE80211_S_SCAN, -1);
		if (error != 0) {
			rtw88_net_sta_quiet(ic);
			(void)rtw88_stop(net->core);
		}
	} else {
		stop_error = rtw88_stop(net->core);
		mutex_enter(net->lock);
		if (error != 0 || stop_error != 0)
			net->csa_pending = 0;
		mutex_exit(net->lock);
		if (error != 0 || stop_error != 0)
			aprint_error_dev(net->dev,
			    "CSA restart failed (%d/%d); TX quiet\n",
			    error, stop_error);
	}
	return 1;
}

/* The workqueue has the kernel lock. Do not hold the PCI lock in net80211. */
static void rtw88_net_tx_pending_discard(struct rtw88_net *);

static void
rtw88_net_state_async(struct rtw88_net *net)
{
	struct ieee80211com *ic = &net->ic;
	struct ifnet *ifp = &net->ec.ec_if;
	struct rtw88_control_snapshot request, owner;
	struct rtw88_control_result result;
	struct rtw88_net_state_diag diag;
	struct rtw88_net_hw_snapshot hw;
	uint64_t prepare_epoch, closed_epoch;
	int error = 0, ready, wme, current, restart, initial, parent_error;

	rtw88_net_tx_pending_discard(net);
	mutex_enter(net->lock);
	net->state_enqueued = 0;
	net->state_running = 1;
	net->state_again = 0;
	/* During a reverse RPC, the grant belongs to the active control owner.
	 * Return so the separate capture item on this same queue can execute. */
	if (net->control_active) {
		net->prep_requested = !net->policy.dying;
		goto idle;
	}
	if (net->policy.dying) {
		net->work_pending = 0;
		goto finish;
	}
	if (!rtw88_net_prepare_locked(net))
		goto idle;
	ready = net->control_ready;
	owner = net->control_owner;
	result = net->control_result;
	diag = net->control_result_diag;
	wme = net->control_result_wme;
	if (ready)
		net->control_ready = 0;
	current = ready && rtw88_net_state_current_locked(net, &result.id);
	rtw88_net_hw_get_locked(net, &hw);
	mutex_exit(net->lock);
	if (current) {
		if (owner.kind == RTW88_CTL_INIT || owner.kind == RTW88_CTL_RECOVER) {
			error = result.error;
			if (error == 0 && (!hw.started || !hw.running ||
			    hw.stop_requested || hw.generation == UINT32_MAX))
				error = EIO;
			mutex_enter(net->lock);
			if (!rtw88_net_state_current_locked(net, &result.id)) {
				net->prep_requested = !net->policy.dying;
				net->state_again = !net->policy.dying;
				mutex_exit(net->lock);
				goto complete;
			}
			if ((ifp->if_flags & IFF_UP) == 0 || net->tx_quiet ||
			    net->stop_hold)
				error = ECANCELED;
			net->stats.init_pending = 0;
			net->stats.init_error = error;
			net->stats.init_generation = hw.generation;
			if (error == 0) {
				net->reset_required = 0;
				net->init_failed = 0;
				net->init_requested = 0;
				net->async_fault = 0;
				ifp->if_flags |= IFF_RUNNING;
				ifp->if_flags &= ~IFF_OACTIVE;
			}
			mutex_exit(net->lock);
			if (error != 0) {
				rtw88_net_state_fail(net, &result.id, error, &diag);
				goto complete;
			}
			if (owner.kind == RTW88_CTL_INIT)
				(void)ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		} else if (owner.kind == RTW88_CTL_STATE) {
			error = result.error;
			if (error == 0 && owner.id.generation != hw.generation)
				error = ESTALE;
			if (error != 0)
				rtw88_net_state_fail(net, &result.id, error, &diag);
			else
				(void)rtw88_net_state_complete(net, &owner, &diag, wme);
			goto complete;
		} else if (owner.kind == RTW88_CTL_KEY_FENCE) {
			if (result.error != 0) {
				rtw88_net_state_fail(net, &result.id, result.error, &diag);
			} else {
				mutex_enter(net->lock);
				if (rtw88_net_state_current_locked(net, &result.id)) {
					(void)rtw88_net_commit(&net->policy,
					    result.id.epoch, 0, hw.started && hw.running &&
					    !hw.stop_requested && ic->ic_state != IEEE80211_S_INIT);
					net->work_pending = 0;
				}
				mutex_exit(net->lock);
				softint_schedule(net->softint);
			}
			goto complete;
		}
	}
	mutex_enter(net->lock);
	if ((ifp->if_flags & IFF_UP) == 0 || net->tx_quiet || net->stop_hold ||
	    (net->async_fault != 0 && !net->init_requested) ||
	    (net->init_failed && !net->init_requested)) {
		parent_error = !net->parent_init_failed &&
		    ic->ic_state != IEEE80211_S_INIT;
		closed_epoch = net->policy.epoch;
		if (net->stats.init_pending && net->async_fault != 0)
			net->stats.init_error = net->async_fault;
		net->stats.init_pending = 0;
		net->work_pending = 0;
		net->policy.accepting = 0;
		net->wme_pending = 0;
		if (net->control_pending_valid &&
		    net->control_pending.kind != RTW88_CTL_STOP &&
		    net->control_pending.kind != RTW88_CTL_QUIET)
			net->control_pending_valid = 0;
		mutex_exit(net->lock);
		ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
		ifp->if_timer = 0;
		callout_stop(&net->scan);
		if (parent_error) {
			parent_error = net->parent_state(ic, IEEE80211_S_INIT, -1);
			if (parent_error != 0) {
				mutex_enter(net->lock);
				if (closed_epoch == net->policy.epoch)
					net->parent_init_failed = 1;
				mutex_exit(net->lock);
			}
		}
		goto complete;
	}
	if (net->control_pending_valid) {
		mutex_exit(net->lock);
		goto complete;
	}
	initial = net->init_requested;
	/* INIT withdraws the peer. Only an explicit interface init may cold
	 * start while the protocol has no selected channel. A later scan or
	 * association retains reset_required and supplies its own valid tuple. */
	restart = initial || (net->requested != IEEE80211_S_INIT &&
	    (net->reset_required || !hw.started || !hw.running || hw.stop_requested));
	if (restart) {
		memset(&request, 0, sizeof(request));
		memset(&diag, 0, sizeof(diag));
		request.kind = initial ? RTW88_CTL_INIT : RTW88_CTL_RECOVER;
		request.state = net->requested;
		request.arg = net->state_arg;
		request.channel = net->pending_channel;
		prepare_epoch = net->policy.epoch;
		mutex_exit(net->lock);
		parent_error = initial && ic->ic_state != IEEE80211_S_INIT ?
		    net->parent_state(ic, IEEE80211_S_INIT, -1) : 0;
		mutex_enter(net->lock);
		if (net->policy.dying || prepare_epoch != net->policy.epoch) {
			net->state_again = !net->policy.dying;
			net->prep_requested = !net->policy.dying;
			goto finish;
		}
		request.id.generation = hw.generation;
		request.id.epoch = net->policy.epoch;
		request.id.key_epoch = net->key_epoch;
		mutex_exit(net->lock);
		diag.state = request.state;
		diag.channel = request.channel.primary;
		diag.stage = RTW88_NET_STAGE_RESTART;
		ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
		if (parent_error != 0) {
			mutex_enter(net->lock);
			if (request.id.epoch == net->policy.epoch) {
				net->parent_init_failed = 1;
				net->stats.init_error = parent_error;
			}
			mutex_exit(net->lock);
			diag.error = parent_error;
			rtw88_net_state_fail(net, &request.id, parent_error, &diag);
			goto complete;
		}
		ieee80211_drain_ifq(&ic->ic_mgtq);
	} else {
		wme = 0; /* Initial sleeping-bus profile does not advertise WME. */
		if (net->requested == IEEE80211_S_RUN) {
			net->run_pending = 0;
			net->neg.run_passes++;
		}
		mutex_exit(net->lock);
		error = rtw88_net_state_snapshot(net, &request, &diag, wme);
		if (error != 0) {
			diag.error = error;
			rtw88_net_state_fail(net, &request.id, error, &diag);
			goto complete;
		}
	}
	error = rtw88_net_request_control(net, &request);
	mutex_enter(net->lock);
	if (error == 0 && net->control_pending_valid &&
	    rtw88_net_same_identity(&request.id, &net->control_pending.id)) {
		net->control_pending_diag = diag;
		net->state_again = 0;
		if (restart)
			net->stats.init_pending = 1;
	}
	mutex_exit(net->lock);
	if (error != 0 && error != ESTALE) {
		diag.error = error;
		rtw88_net_state_fail(net, &request.id, error, &diag);
	}
complete:
	mutex_enter(net->lock);
finish:
	if (net->prep_granted)
		rtw88_net_prepare_release_locked(net);
idle:
	rtw88_net_state_idle_locked(net);
	mutex_exit(net->lock);
}


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
	struct rtw88_channel_tuple channel_tuple;
	unsigned media, prepare_stage;
	int use_ra, mailbox_retry, mailbox_reserved;
	struct ieee80211_ht20_profile ht20_profile;
	const uint8_t *bssid;
	char failure[384];
	uint8_t any;
	int arg, error, prepare_error, restart_error, current;

	if (net->core->sc_executor != NULL) {
		rtw88_net_state_async(net);
		return;
	}
	(void)work;
	for (;;) {
#ifdef RTW88_RFK_EXPERIMENT
		rtw88_net_rfk_rx_deliver(net);
		/* Retire the OLD numeric owner before any restart or early dying
		 * return. Detach keeps resources and net allocated through wait. */
		if (rtw88_calibration_active(net->core) &&
		    (net->policy.dying || net->reset_required ||
		     net->rfk.epoch != net->policy.epoch ||
		     net->requested != IEEE80211_S_AUTH))
			(void)rtw88_calibration_retire(net->core);
		rtw88_net_rfk_publish(net);
#endif
		if (rtw88_net_csa_recover(net))
			continue;
		mutex_enter(net->lock);
		if (net->policy.dying) {
			net->work_pending = 0;
			mutex_exit(net->lock);
			return;
		}
		epoch = net->policy.epoch;
		state = net->requested;
		net->run_pending = 0;
		channel = net->channel;
		channel_tuple = net->pending_channel;
		media = net->configured_media;
		arg = net->state_arg;
		mutex_exit(net->lock);
		callout_stop(&net->scan);
		restart_error = rtw88_net_restart_peer(net);
		if (state == IEEE80211_S_RUN) {
			mutex_enter(net->lock);
			net->neg.run_passes++;
			if (restart_error != 0) {
				net->neg.run_restart_fail++;
				net->neg.run_restart_error = restart_error;
			}
			mutex_exit(net->lock);
		}
#ifdef RTW88_RFK_EXPERIMENT
		mutex_enter(net->lock);
		if (epoch == net->policy.epoch && net->rfk.control_epoch == epoch) {
			if (net->rfk.control_generation != net->core->sc_generation) {
				net->rfk.beacon_epoch = 0;
				net->rfk.frontier_valid = 0;
			}
			net->rfk.control_generation = net->core->sc_generation;
		}
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
		/* Giant owns the node; publish only numbers, zero outside RUN. */
		mutex_enter(net->lock);
		memset(net->neg.run_chan, 0, sizeof(net->neg.run_chan));
		net->neg.run_ht = net->neg.run_vht = 0;
		mutex_exit(net->lock);
		if (state == IEEE80211_S_RUN && ic->ic_bss != NULL) {
			mutex_enter(net->lock);
			net->neg.run_ht = ic->ic_bss->ni_ht20.valid;
			net->neg.run_vht = ic->ic_bss->ni_ht20.vht.valid;
			net->neg.run_chan[0] =
			    ic->ic_bss->ni_ht20.vht.channel.primary;
			net->neg.run_chan[1] =
			    ic->ic_bss->ni_ht20.vht.channel.center;
			net->neg.run_chan[2] =
			    ic->ic_bss->ni_ht20.vht.channel.width_mhz;
			mutex_exit(net->lock);
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
		if (prepare_error == 0 && rtw88_calibration_blocked(net->core) &&
		    !rtw88_calibration_active(net->core))
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
			if (mailbox_retry || prepare_error == ECANCELED) {
				if (prepare_error == ECANCELED &&
				    state == IEEE80211_S_RUN) {
					mutex_enter(net->lock);
					net->neg.run_cancels++;
					mutex_exit(net->lock);
				}
				continue;
			}
		}
#ifdef RTW88_RFK_EXPERIMENT
		if (prepare_error == 0 && state == IEEE80211_S_AUTH) {
			/* No producer can add a descriptor while this same numeric
			 * request owns the existing all-five-queue admission fence. */
			net->rfk.drained = 1;
			if (!net->rfk.ran) {
				mutex_enter(net->lock);
				if (epoch != net->policy.epoch || net->policy.dying ||
				    net->rfk.notice_pending) {
					prepare_error = ECANCELED;
				} else {
					prepare_error = rtw88_channel_apply(net->core,
					    &channel_tuple);
					if (prepare_error == 0)
						prepare_error = rtw88_net_rfk_phase(net);
				}
				mutex_exit(net->lock);
				if (prepare_error == 0)
					prepare_error = rtw88_net_rfk_listen(net);
				if (prepare_error == 0)
					prepare_error = rtw88_calibration_run(net->core);
				rtw88_net_rfk_rx_deliver(net);
				if (prepare_error == 0) {
					mutex_enter(net->lock);
					if (epoch != net->policy.epoch || net->policy.dying ||
					    net->rfk.notice_pending) {
						prepare_error = ECANCELED;
					} else {
						net->rfk.ran = 1;
						prepare_error = rtw88_net_rfk_phase(net);
					}
					mutex_exit(net->lock);
				}
			}
		}
		/* Retry retains the completed calibration and its new observation.
		 * It cannot retune, clear the record or start another calibration. */
		if (prepare_error == 0 && state == IEEE80211_S_AUTH) {
			prepare_error = rtw88_net_rfk_listen(net);
			/* Listening yielded Giant. Recheck the existing numeric owner
			 * before ordinary I/O, without allocation or a new budget. */
			if (prepare_error == 0)
				prepare_error = rtw88_calibration_active(net->core) ?
				    rtw88_net_rfk_begin(net) : ESTALE;
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
			if (mailbox_retry || prepare_error == ECANCELED) {
				if (prepare_error == ECANCELED &&
				    state == IEEE80211_S_RUN) {
					mutex_enter(net->lock);
					net->neg.run_cancels++;
					mutex_exit(net->lock);
				}
				continue;
			}
			mailbox_reserved = prepare_error == 0 && mailbox_count != 0;
		}
#ifdef RTW88_RFK_EXPERIMENT
		/* A reserved mailbox did not freeze the stack while Giant slept. */
		if (prepare_error == 0 && state == IEEE80211_S_AUTH)
			prepare_error = rtw88_net_rfk_capture_current(net);
#endif
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
#ifdef RTW88_RFK_EXPERIMENT
		/* The locked epoch check above excluded a successor request.
		 * A same-epoch snapshot change must use the INIT/stop owner. */
		if (error == ECANCELED && state == IEEE80211_S_AUTH)
			error = ESTALE;
		if (error == 0 && state == IEEE80211_S_AUTH)
			error = rtw88_net_rfk_beacon_check(net);
#endif
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
				    rtw88_calibration_seal(net->core) != 0)
#endif
					error = rtw88_channel_apply(net->core,
					    &channel_tuple);
			}
#ifdef RTW88_DFS64
			/* A descriptor may have completed before the state request but
			 * remain in the hardware ring until after this commit. Do not let
			 * that old frame become positive evidence for a new DFS lease. */
			if (error == 0 && state != IEEE80211_S_INIT) {
				net->dfs_frontier_valid = 0;
				error = rtw88_rx_frontier(net->core,
				    net->core->sc_generation, &net->dfs_frontier);
				if (error == 0) {
					net->dfs_frontier_generation =
					    net->core->sc_generation;
					net->dfs_frontier_valid = 1;
				}
			}
#endif
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
#ifdef RTW88_RFK_EXPERIMENT
		if (error == 0 && state == IEEE80211_S_AUTH)
			error = rtw88_net_rfk_beacon_check(net);
#endif
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
		if (rtw88_calibration_active(net->core)) {
			int retire_error = rtw88_calibration_retire(net->core);
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
			mutex_enter(net->lock);
			error = epoch != net->policy.epoch || net->policy.dying ||
			    net->rfk.notice_pending ? ECANCELED :
			    rtw88_net_rfk_beacon_check(net);
			if (error == 0)
				error = rtw88_calibration_seal(net->core);
			mutex_exit(net->lock);
			if (error != 0) {
				diag.stage = RTW88_NET_STAGE_CORE;
				diag.error = error;
			}
		}
		if (error != 0) {
			mutex_enter(net->lock);
			current = epoch == net->policy.epoch && !net->policy.dying;
			if (current) {
				net->rfk.frontier_valid = 0;
				net->rfk.beacon_epoch = 0;
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
		if (state == IEEE80211_S_RUN && error != 0) {
			mutex_enter(net->lock);
			if (error == ECANCELED)
				net->neg.run_cancels++;
			else {
				net->neg.run_fails++;
				net->neg.run_error = error;
				net->neg.run_stage = (uint8_t)diag.stage;
			}
			mutex_exit(net->lock);
		}
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
					int ht20_error;

					memset(&ht20_profile, 0,
					    sizeof(ht20_profile));
					ht20_error = rtw88_net_ht20_profile_prepare(net,
					    &ht20_profile);

					if (ht20_error == 0)
						rtw88_net_ht20_publish(net, &ht20_profile);
					else
						rtw88_net_ht20_publish(net, NULL);
					mutex_enter(net->lock);
					if (ht20_error == 0)
						net->neg.prepare_ok++;
					else
						net->neg.prepare_fail++;
					net->neg.prepare_error = ht20_error;
					net->neg.prepare_ready = ht20_profile.ready;
					mutex_exit(net->lock);
				}
				error = net->parent_state(ic, state, arg);
				/* The parent may have queued an HT/VHT offer. */
				mutex_enter(net->lock);
				RTW88_NET_NEG_HT20(&net->neg, &ic->ic_ht20_diag);
				if (state == IEEE80211_S_RUN && error == 0)
					net->neg.run_published++;
				else if (state == IEEE80211_S_RUN) {
					net->neg.run_fails++;
					net->neg.run_error = error;
					net->neg.run_stage = RTW88_NET_STAGE_CORE;
				}
				mutex_exit(net->lock);
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

/* Giant snapshots stack values; the executor owns all later bus I/O. */
static int
rtw88_net_newstate_async(struct ieee80211com *ic, enum ieee80211_state state,
    int arg)
{
	struct rtw88_net *net = ic->ic_ifp->if_softc;
	struct rtw88_async_epoch id;
	unsigned queued;
	int error, quiet_error, wake;

	quiet_error = net->tx_quiet && state != IEEE80211_S_INIT ? EBUSY : 0;
	if (quiet_error != 0) {
		state = IEEE80211_S_INIT;
		arg = -1;
	}
	if (state != IEEE80211_S_ASSOC && state != IEEE80211_S_RUN)
		rtw88_net_ht20_publish(net, NULL);
	if (state == IEEE80211_S_RUN && ic->ic_state == IEEE80211_S_RUN &&
	    ic->ic_bss != NULL)
		ieee80211_rxba_reset(ic, ic->ic_bss);
	error = ieee80211_ht20_state_request(ic, state);
	if (error != 0) {
		state = IEEE80211_S_INIT;
		arg = -1;
	}
	if (state == IEEE80211_S_RUN)
		ic->ic_mgt_timer = 0;
	callout_stop(&net->scan);
	mutex_enter(net->lock);
	if (net->policy.dying) {
		mutex_exit(net->lock);
		(void)ieee80211_ht20_state_request(ic, IEEE80211_S_INIT);
		return ENXIO;
	}
	net->hw.ra.active = net->hw.ra.configured = 0;
	(void)rtw88_net_request(&net->policy);
	id.generation = net->hw.generation;
	id.epoch = net->policy.epoch;
	id.key_epoch = net->key_epoch;
	queued = rtw88_net_tx_queued(net);
	rtw88_async_withdraw(&net->async, &id);
	net->async_failed += queued - rtw88_net_tx_queued(net);
	wake = queued != rtw88_net_tx_queued(net);
	if (net->async.closed) {
		rtw88_net_fault(net, net->async.error);
		mutex_exit(net->lock);
		return EOVERFLOW;
	}
	if (net->run_pending) {
		net->neg.run_replaced++;
		net->neg.run_replaced_by = (uint8_t)state;
		net->neg.run_replaced_arg = arg;
	}
	net->run_pending = state == IEEE80211_S_RUN;
	if (net->run_pending)
		net->neg.run_queued++;
	net->requested = state;
	net->state_arg = arg;
	net->channel = ieee80211_chan2ieee(ic, ic->ic_curchan);
	net->pending_channel.primary = net->channel;
	net->pending_channel.center = net->channel;
	net->pending_channel.width_mhz = 20;
	if (net->hw.linked || (net->hw.phy.channel_valid &&
	    net->hw.phy.channel.width_mhz != 20))
		net->reset_required = 1;
	net->work_pending = 1;
	rtw88_net_state_schedule_locked(net);
	mutex_exit(net->lock);
	if (wake)
		softint_schedule(net->softint);
	return quiet_error != 0 ? quiet_error : error;
}

static int
rtw88_net_newstate(struct ieee80211com *ic, enum ieee80211_state state,
    int arg)
{
	struct rtw88_net *net = ic->ic_ifp->if_softc;
	int enqueue = 0, ht20_error;
	int run_asked = state == IEEE80211_S_RUN;
	int init_asked = state == IEEE80211_S_INIT;
	int quiet_error = net->tx_quiet && state != IEEE80211_S_INIT ? EBUSY : 0;

	if (net->core->sc_executor != NULL)
		return rtw88_net_newstate_async(ic, state, arg);

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
	/*
	 * The accepted association response completed the management
	 * transaction. The deferred RUN may cold-start a wider tuple for
	 * longer than IEEE80211_TRANS_WAIT; stop the timer that would
	 * otherwise abandon it for SCAN. The worker requests INIT on failure.
	 */
	if (state == IEEE80211_S_RUN)
		ic->ic_mgt_timer = 0;
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
	/* Shared CSA supplies the first mandatory INIT. A later explicit INIT
	 * cancels recovery; a coerced stale AUTH/RUN/SCAN is not a stop owner. */
	if (init_asked && net->csa_pending)
		net->csa_pending = net->csa_pending == 1 ? 2 : 0;
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
	rtw88_ra_invalidate(net->core);
#ifdef RTW88_DFS64
	/* A new request needs new master evidence before any DFS TX. */
	rtw88_dfs_close(net->core);
	net->dfs_frontier_valid = 0;
#endif
	(void)rtw88_net_request(&net->policy);
	if (run_asked) {
		net->neg.run_requests++;
		if (state != IEEE80211_S_RUN)
			net->neg.run_to_init++;
	}
	if (net->run_pending) {
		net->neg.run_replaced++;
		net->neg.run_replaced_by = (uint8_t)state;
		net->neg.run_replaced_arg = arg;
	}
	net->run_pending = state == IEEE80211_S_RUN;
	if (state == IEEE80211_S_RUN)
		net->neg.run_queued++;
	net->requested = state;
	net->state_arg = arg;
	net->channel = ieee80211_chan2ieee(ic, ic->ic_curchan);
	net->pending_channel.primary = net->channel;
	net->pending_channel.center = net->channel;
	net->pending_channel.width_mhz = 20;
#ifdef RTW88_VHT80
	/* A negotiated VHT peer runs at its 40/80 MHz tuple; every other
	 * state is 20. */
	if (state == IEEE80211_S_RUN && ic->ic_bss != NULL &&
	    ic->ic_bss->ni_ht20.valid && ic->ic_bss->ni_ht20.vht.valid &&
	    ic->ic_bss->ni_ht20.vht.channel.primary == net->channel) {
		net->pending_channel.center =
		    ic->ic_bss->ni_ht20.vht.channel.center;
		net->pending_channel.width_mhz =
		    ic->ic_bss->ni_ht20.vht.channel.width_mhz;
	}
#endif
	/* A width change needs the sleepable stop/DMA fence even before the
	 * protocol marks this peer linked. Preserve the warm same-width path. */
	if (net->core->sc_linked || (net->core->sc_phy.channel_valid &&
	    net->core->sc_phy.channel.width_mhz !=
	    net->pending_channel.width_mhz))
		net->reset_required = 1;
#ifdef RTW88_RFK_EXPERIMENT
	net->rfk.beacon_epoch = 0;
	net->rfk.frontier_valid = 0;
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
	/* Deferred RX is admitted only with the current key and peer identity.
	 * The same lock covers this check and copying the frame into the queue. */
	if ((net->core->sc_executor != NULL && !info->host_epoch_valid) ||
	    (info->host_epoch_valid &&
	    (net->key_depth != 0 || net->key_error != 0 || net->async.closed ||
	     info->generation != net->async.id.generation ||
	     info->epoch != net->async.id.epoch ||
	     info->key_epoch != net->async.id.key_epoch ||
	     info->epoch != net->policy.epoch))) {
		net->rx_dropped++;
		return;
	}
	arrival_tick = (uint32_t)getticks();
#ifdef RTW88_RFK_EXPERIMENT
	/* Withdrawal must remain observable with closed/full ordinary admission. */
	if (net->core->sc_executor == NULL)
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
	rx->sequence = info->sequence;
	rx->generation = info->generation;
	rx->host_epoch_valid = info->host_epoch_valid;
	rx->key_epoch = info->key_epoch;
	rx->quality = info->signal.valid && info->signal.quality <= 100 &&
	    info->signal.generation == rx->generation ? info->signal.quality : 0;
	net->rx_tail = (net->rx_tail + 1) % RTW88_NET_RX_LIMIT;
	softint_schedule(net->softint);
}

/* The sleeping-bus owner enters without the short net admission lock. */
static void
rtw88_net_rx_frame_async(void *cookie, const uint8_t *frame, size_t len,
    const struct rtw88_rx_info *info)
{
	struct rtw88_net *net = cookie;

	mutex_enter(net->lock);
	rtw88_net_rx_frame(cookie, frame, len, info);
	mutex_exit(net->lock);
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

#ifdef RTW88_DFS64
/*
 * DFS slave (52-64, 100-144): a complete beacon from the selected master,
 * without CSA or Quiet, received by this core generation in the current
 * request and newer than the request's RX frontier and lease, opens the
 * core TX permit for AUTH, ASSOC and RUN. Frames queued meanwhile wait: a
 * closed permit is no TX capacity.
 */
static void
rtw88_net_dfs_beacon(struct rtw88_net *net, const uint8_t *frame, size_t len,
    uint64_t sequence, uint32_t generation, uint32_t arrived, uint64_t epoch,
    uint32_t now, uint32_t timeout)
{
	uint16_t interval;
	struct ieee80211com *ic = &net->ic;
	struct ieee80211_node *ni = ic->ic_bss;
	unsigned channel, i;

	if (!net->dfs_frontier_valid ||
	    net->dfs_frontier_generation != generation || sequence == 0 ||
	    sequence <= net->dfs_frontier ||
	    ic->ic_opmode != IEEE80211_M_STA ||
	    (ic->ic_state != IEEE80211_S_AUTH &&
	     ic->ic_state != IEEE80211_S_ASSOC &&
	     ic->ic_state != IEEE80211_S_RUN) || ni == NULL ||
	    ic->ic_curchan == NULL || ic->ic_curchan == IEEE80211_CHAN_ANYC ||
	    ni->ni_chan != ic->ic_curchan ||
	    (channel = ieee80211_chan2ieee(ic, ic->ic_curchan)) == 0 ||
	    channel > IEEE80211_CHAN_MAX ||
	    frame == NULL || len < 36 || frame[0] != 0x80 ||
	    (frame[1] & 0xc7) != 0 || (frame[22] & 15) != 0 ||
	    memcmp(frame + 10, ni->ni_macaddr, IEEE80211_ADDR_LEN) != 0 ||
	    memcmp(frame + 16, ni->ni_bssid, IEEE80211_ADDR_LEN) != 0)
		return;
	for (i = 0; i < IEEE80211_ADDR_LEN; i++) {
		if (frame[4 + i] != 0xff)
			return;
	}
	interval = ieee80211_sta_beacon_interval(frame + 24, len - 24, channel);
	if (interval < IEEE80211_BINTVAL_MIN ||
	    interval > IEEE80211_BINTVAL_MAX || now - arrived >= timeout)
		return;
	mutex_enter(net->lock);
	if (!net->tx_quiet && !net->policy.dying && net->policy.accepting &&
	    epoch == net->policy.epoch &&
	    rtw88_dfs_open(net->core, channel, generation) == 0) {
		net->dfs_tick = arrived;
		net->ec.ec_if.if_timer = 1;
	}
	mutex_exit(net->lock);
}

/*
 * Without a current master beacon the slave must stop. Close host and MAC
 * TX through the quiet owner and leave a clean restart to the worker; a
 * clock that moved backward also ends the lease. The watchdog polls once
 * a second, so TX can outlive the last beacon by up to one more second.
 * Return 1 when the caller must request INIT.
 */
static int
rtw88_net_dfs_expire(struct rtw88_net *net, uint32_t now, uint32_t timeout)
{
	uint32_t elapsed;
	int expired = 0;

	mutex_enter(net->lock);
	elapsed = now - net->dfs_tick;
	if (net->core->sc_dfs.valid && timeout != 0 && elapsed >= timeout) {
		net->quiet_error = rtw88_tx_quiet(net->core);
		net->reset_required = 1;
		net->dfs_frontier_valid = 0;
		expired = 1;
	}
	mutex_exit(net->lock);
	return expired;
}
#endif

/* Giant owns the current peer. Parse outside the PCI lock; publish only
 * the validated arrival clock under the existing policy/epoch gate. */
static void
rtw88_net_beacon_observe(struct rtw88_net *net, const struct rtw88_net_rx *rx)
{
	struct ieee80211com *ic = &net->ic;
	struct ieee80211_node *ni = ic->ic_bss;
	const uint8_t *frame;
	uint16_t interval;
	unsigned channel, i;

	if (ic->ic_opmode != IEEE80211_M_STA ||
	    ic->ic_state != IEEE80211_S_RUN || ni == NULL || ni->ni_associd == 0 ||
	    ic->ic_curchan == NULL || ic->ic_curchan == IEEE80211_CHAN_ANYC ||
	    ni->ni_chan != ic->ic_curchan || rx->m->m_len < 36 ||
	    rx->m->m_next != NULL || rx->m->m_len != rx->m->m_pkthdr.len)
		return;
	frame = mtod(rx->m, const uint8_t *);
	if (frame[0] != 0x80 || (frame[1] & 0xc7) != 0 ||
	    (frame[22] & 15) != 0 ||
	    memcmp(frame + 10, ni->ni_macaddr, IEEE80211_ADDR_LEN) != 0 ||
	    memcmp(frame + 16, ni->ni_bssid, IEEE80211_ADDR_LEN) != 0)
		return;
	for (i = 0; i < IEEE80211_ADDR_LEN; i++) {
		if (frame[4 + i] != 0xff)
			return;
	}
	channel = ieee80211_chan2ieee(ic, ic->ic_curchan);
	interval = ieee80211_sta_beacon_interval(frame + 24,
	    (size_t)rx->m->m_len - 24, channel);
	if (interval < IEEE80211_BINTVAL_MIN || interval > IEEE80211_BINTVAL_MAX)
		return;
	mutex_enter(net->lock);
	(void)rtw88_net_beacon_note(&net->policy, rx->epoch, rx->tick,
	    frame, rx->m->m_len, ni->ni_bssid);
	mutex_exit(net->lock);
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
#ifdef RTW88_VHT80
	unsigned wanted;
#endif

#ifdef RTW88_RFK_EXPERIMENT
	/* Giant owns node/capsule cleanup; the IRQ kept only numeric identity. */
	if (net->core->sc_executor == NULL)
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
		if (rx.generation != (net->core->sc_executor != NULL ?
		    net->hw.generation : net->core->sc_generation)) {
			net->stats.fw_stale++;
			valid = 0;
		} else if (rx.host_epoch_valid &&
		    (net->key_depth != 0 || net->key_error != 0 ||
		     net->async.closed || rx.key_epoch != net->async.id.key_epoch)) {
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
		rtw88_net_beacon_observe(net, &rx);
#ifdef RTW88_DFS64
		if (net->core->sc_executor == NULL && rx.m->m_next == NULL &&
		    rx.m->m_len == rx.m->m_pkthdr.len)
			rtw88_net_dfs_beacon(net, mtod(rx.m, const uint8_t *),
			    (size_t)rx.m->m_len, rx.sequence, rx.generation, rx.tick, rx.epoch,
			    (uint32_t)getticks(), (uint32_t)MAX(hz,
			    mstohz(rtw88_net_tu_to_ms(ic->ic_bmisstimeout))));
#endif
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
		RTW88_NET_NEG_HT20(&net->neg, &ic->ic_ht20_diag);
		mutex_exit(net->lock);
	}
	/* Let already queued gap frames complete the window before expiring a
	 * hole. The shared token check discards an event superseded by input. */
	rtw88_net_rxba_softintr(net);
	mutex_enter(net->lock);
	completed = net->tx_completed;
	net->tx_completed = 0;
	outstanding = 0;
	if (net->core->sc_executor != NULL)
		outstanding = net->hw.outstanding;
	else
		for (n = 0; n < RTW88_NTXQ; n++)
			outstanding += net->core->sc_txq[n].queued;
	if (net->policy.queued != 0 && !net->policy.dying)
		softint_schedule(net->softint);
	mutex_exit(net->lock);
	if (completed != 0) {
		if_statadd(ifp, if_opackets, completed);
		net->tx_timer = outstanding != 0 ? 5 : 0;
	}
#ifdef RTW88_VHT80
	/*
	 * Request originator sessions for TIDs the TX path sent without one.
	 * Queueing the request starts output, so it runs here under Giant,
	 * outside rtw88_net_start; net80211 owns state, timeouts and retries.
	 */
	mutex_enter(net->lock);
	wanted = net->txba_wanted;
	net->txba_wanted = 0;
	mutex_exit(net->lock);
	if (wanted != 0 && ic->ic_bss != NULL &&
	    (ifp->if_flags & IFF_RUNNING) != 0 && !net->policy.dying)
		(void)ieee80211_txba_request(ic, ic->ic_bss, wanted);
	if (ic->ic_bss != NULL) {
		mutex_enter(net->lock);
		RTW88_NET_NEG_TXBA(&net->neg, &ic->ic_bss->ni_rxba);
		mutex_exit(net->lock);
	}
#endif
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
	stats->neg = net->neg;
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
	int vht;

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
	    (net->core->sc_ra.phy == RTW88_RA_HT20_1SS ||
	     net->core->sc_ra.phy == RTW88_RA_VHT_1SS)) {
		vht = net->core->sc_ra.phy == RTW88_RA_VHT_1SS;
		if (!ni->ni_ht20.valid || ni->ni_ht20.vht.valid != vht ||
		    (vht && net->core->sc_ra.vht_tx_map !=
		     ni->ni_ht20.vht.maps.tx_map) ||
#ifdef RTW88_VHT80
		    (vht && !rtw88_net_vht_tuple(net, ni)) ||
#endif
		    index != IEEE80211_FIXED_RATE_NONE ||
		    ic->ic_curmode != IEEE80211_MODE_11A ||
		    (ic->ic_flags & IEEE80211_F_USEPROT) != 0 ||
		    ni->ni_ht20.profile.rx_mcs != 0xff ||
		    ni->ni_ht20.profile.tx_mcs != 0xff ||
		    net->core->sc_ra.channel != ieee80211_chan2ieee(ic, ni->ni_chan) ||
		    net->core->sc_ra.profile_generation != ni->ni_ht20.profile.generation ||
		    net->core->sc_ra.ht_mcs != (vht ? 0 : ni->ni_ht20.tx_mcs) ||
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
#ifdef RTW88_VHT80
		/*
		 * QoS data of an established originator session may join an
		 * A-MPDU. Bound MAX_AGG_NUM (2 * count MPDUs) by the recipient
		 * window and the peer's maximum A-MPDU length: the VHT exponent
		 * for a VHT peer, otherwise the HT one. Spacing is the peer's.
		 * Other TIDs note their traffic for the softint's request.
		 */
		if (bytes[0] == 0x88 && len >= 26 && (bytes[24] & 15) < 8) {
			struct rtw88_tx_agg agg;
			unsigned tid = bytes[24] & 15, window, exponent, count;

			window = ieee80211_txba_window(ic, ni, tid);
			exponent = vht ? (ni->ni_ht20.vht.peer.info >> 23) & 7 :
			    ni->ni_ht20.peer_htcap[2] & 3;
			count = (((1U << (13 + exponent)) - 1) /
			    net->core->sc_ra.ht.max_mpdu) / 2;
			if (count > window / 2)
				count = window / 2;
			if (count > 31)
				count = 31;
			if (window == 0)
				net->txba_wanted |= 1U << tid;
			else if (count != 0) {
				agg.tid = (uint8_t)tid;
				agg.density = (ni->ni_ht20.peer_htcap[2] >> 2) & 7;
				agg.count = (uint8_t)count;
				return rtw88_tx_ampdu(net->core, q, frame, len,
				    net->policy.epoch, &agg);
			}
		}
#endif
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

static int
rtw88_net_tx_current_locked(struct rtw88_net *net,
    const struct rtw88_async_epoch *id)
{

	KASSERT(mutex_owned(net->lock));
	return rtw88_net_tx_accepting_locked(net) &&
	    rtw88_net_state_current_locked(net, id) &&
	    id->generation == net->hw.generation &&
	    rtw88_net_same_identity(id, &net->async.id);
}

/* Keep two slots in each data AC available for EAPOL/control traffic. The
 * queue helper owns the global limits and the low-water notification. */
static int
rtw88_net_tx_reserve_locked(struct rtw88_net *net,
    const struct rtw88_async_epoch *id, unsigned q, bool control,
    struct rtw88_async_ticket *ticket, struct rtw88_tx_job **job)
{
	unsigned n, count = 0;

	KASSERT(mutex_owned(net->lock));
	if (!rtw88_net_tx_current_locked(net, id))
		return ESTALE;
	if (q >= RTW88_NTXQ)
		return EINVAL;
	if (!control) {
		for (n = 0; n < RTW88_ASYNC_SLOTS; n++)
			if (net->async.slots[n].state != RTW88_ASYNC_FREE &&
			    net->async.slots[n].q == q &&
			    !net->async.slots[n].control)
				count++;
		if (count >= RTW88_ASYNC_PER_QUEUE - 2) {
			net->async.blocked_data |= UINT32_C(1) << q;
			return ENOBUFS;
		}
	}
	return rtw88_async_reserve(&net->async, id, q, control, ticket, job);
}

/* Only numeric legacy policy leaves Giant with the copied packet. */
static int
rtw88_net_tx_policy_async(struct rtw88_net *net, struct ieee80211_node *ni,
    unsigned q, const void *frame, size_t len, struct rtw88_tx_job *job)
{
	struct ieee80211com *ic = &net->ic;
	const uint8_t *bytes = frame;
	unsigned aid;

	KASSERT(mutex_owned(net->lock));
	if (job == NULL || frame == NULL || len < 24)
		return EINVAL;
	job->mode = RTW88_TX_MODE_RAW;
	job->operation = 0;
	if (q == RTW88_TXQ_MGMT || (bytes[0] & 0x0c) != 0x08 ||
	    (bytes[0] & 0x40) != 0 || (bytes[4] & 1) != 0 ||
	    ((bytes[1] & 3) == 1 && (bytes[16] & 1) != 0))
		return 0;
	if (!net->policy.accepting || net->policy.dying)
		return EBUSY;
	if (ic->ic_state != IEEE80211_S_RUN || ic->ic_opmode != IEEE80211_M_STA ||
	    ni == NULL || ni != ic->ic_bss || ni->ni_chan == NULL ||
	    ni->ni_chan != ic->ic_curchan)
		return EINVAL;
	aid = ni->ni_associd & 0x3fff;
	if (aid == 0 || aid > 2007)
		return EINVAL;
	/* No HT, VHT or fixed-rate capability is admitted on this path. */
	if (ic->ic_fixed_rate != IEEE80211_FIXED_RATE_NONE || ni->ni_ht20.valid ||
	    !net->hw.ra.configured || !net->hw.ra.active ||
	    net->hw.ra.phy != RTW88_RA_LEGACY)
		return EOPNOTSUPP;
	job->mode = RTW88_TX_MODE_AUTO;
	job->operation = (ic->ic_flags & IEEE80211_F_USEPROT) != 0;
	return 0;
}

static void
rtw88_net_tx_pending_discard(struct rtw88_net *net)
{
	struct mbuf *m = net->tx_pending_m;
	struct ieee80211_node *ni = net->tx_pending_ni;

	net->tx_pending_m = NULL;
	net->tx_pending_ni = NULL;
	if (m != NULL) {
		m_freem(m);
		if_statinc(&net->ec.ec_if, if_oerrors);
	}
	if (ni != NULL)
		ieee80211_free_node(ni);
}

static void
rtw88_net_start_async(struct ifnet *ifp)
{
	struct rtw88_net *net = ifp->if_softc;
	struct ieee80211com *ic = &net->ic;
	struct ieee80211_node *ni;
	const struct ieee80211_frame *wh;
	struct rtw88_async_epoch id;
	struct rtw88_async_ticket ticket;
	struct rtw88_tx_job *job;
	struct ether_header eh;
	struct mbuf *m;
	unsigned n, q, actual;
	int error, current, reserved, management;
	bool control;

	mutex_enter(net->lock);
	current = rtw88_net_tx_accepting_locked(net);
	if (!current || net->tx_producers != 0 ||
	    (ifp->if_flags & (IFF_RUNNING | IFF_OACTIVE)) != IFF_RUNNING) {
		mutex_exit(net->lock);
		return;
	}
	net->tx_producers++;
	mutex_exit(net->lock);
	for (n = 0; n < RTW88_NET_TX_BUDGET; n++) {
		m = NULL;
		ni = NULL;
		reserved = 0;
		mutex_enter(net->lock);
		id = net->async.id;
		current = rtw88_net_tx_current_locked(net, &id);
		mutex_exit(net->lock);
		if (!current)
			break;
		IF_POLL(&ic->ic_mgtq, m);
		management = m != NULL;
		if (management) {
			q = RTW88_TXQ_MGMT;
			control = true;
		} else {
			if (ic->ic_state != IEEE80211_S_RUN)
				break;
			if (net->tx_pending_m != NULL) {
				if (!rtw88_net_same_identity(&id, &net->tx_pending_id)) {
					rtw88_net_tx_pending_discard(net);
					continue;
				}
				m = net->tx_pending_m;
				ni = net->tx_pending_ni;
				net->tx_pending_m = NULL;
				net->tx_pending_ni = NULL;
			} else {
				/* Classify the actual dequeued packet, including ALTQ/TBR. */
				IFQ_DEQUEUE(&ifp->if_snd, m);
				if (m == NULL)
					break;
			}
			error = EINVAL;
			if (m->m_pkthdr.len >= sizeof(eh)) {
				m_copydata(m, 0, sizeof(eh), &eh);
				if (ni == NULL)
					ni = ieee80211_find_txnode(ic, eh.ether_dhost);
				error = ni == NULL ? ENOENT :
				    ni != ic->ic_bss ? ESTALE :
				    rtw88_net_tx_queue(ic, m, ni, &q, 0);
			}
			if (error != 0)
				goto discard;
			control = eh.ether_type == htons(ETHERTYPE_PAE);
		}
		mutex_enter(net->lock);
		error = rtw88_net_tx_reserve_locked(net, &id, q, control,
		    &ticket, &job);
		if (error == EOVERFLOW)
			rtw88_net_fault(net, error);
		mutex_exit(net->lock);
		if (error != 0) {
			if (management) {
				/* No dequeue or PN consumption before management admission. */
				if (error == ENOBUFS)
					ifp->if_flags |= IFF_OACTIVE;
				break;
			}
			if (error == ENOBUFS) {
				net->tx_pending_m = m;
				net->tx_pending_ni = ni;
				net->tx_pending_id = id;
				ifp->if_flags |= IFF_OACTIVE;
				break;
			}
			goto discard;
		}
		reserved = 1;
		if (management) {
			IF_DEQUEUE(&ic->ic_mgtq, m);
			if (m == NULL) {
				error = EAGAIN;
				goto discard;
			}
			ni = M_GETCTX(m, struct ieee80211_node *);
			M_CLEARCTX(m);
		}
		mutex_enter(net->lock);
		current = rtw88_net_tx_current_locked(net, &id);
		mutex_exit(net->lock);
		if (!current) {
			error = ESTALE;
			goto discard;
		}
		if (!management) {
			if ((m = m_pullup(m, sizeof(eh))) == NULL)
				goto discard;
			bpf_mtap(ifp, m, BPF_D_OUT);
			error = rtw88_net_tx_queue(ic, m, ni, &actual, 1);
			if (error == 0 && actual != q)
				error = EPROTO;
			if (error != 0)
				goto discard;
			m = ieee80211_encap(ic, m, ni);
			if (m == NULL)
				goto discard;
		}
		if (m->m_len < 24 && (m = m_pullup(m, 24)) == NULL)
			goto discard;
		wh = mtod(m, struct ieee80211_frame *);
		if ((wh->i_fc[1] & IEEE80211_FC1_WEP) != 0 &&
		    (ni == NULL || ieee80211_crypto_encap(ic, ni, m) == NULL))
			goto discard;
		if (m->m_pkthdr.len < 24 ||
		    m->m_pkthdr.len > sizeof(job->bytes) ||
		    m->m_pkthdr.len > RTW88_TX_BUF_SIZE - net->caps.tx_desc_size)
			goto discard;
		if (!management)
			m->m_flags &= ~M_WME_AC_MASK;
		bpf_mtap3(ic->ic_rawbpf, m, BPF_D_OUT);
		m_copydata(m, 0, m->m_pkthdr.len, job->bytes);
		mutex_enter(net->lock);
		current = rtw88_net_tx_current_locked(net, &id);
		error = current ? rtw88_net_tx_policy_async(net, ni, q, job->bytes,
		    m->m_pkthdr.len, job) : ESTALE;
		if (error == 0)
			error = rtw88_async_commit(&net->async, &ticket,
			    m->m_pkthdr.len, rtw88_net_now_ms());
		mutex_exit(net->lock);
		if (error != 0)
			goto discard;
		m_freem(m);
		if (ni != NULL)
			ieee80211_free_node(ni);
		net->tx_timer = 5;
		ifp->if_timer = 1;
		rtw88_executor_schedule(net->core, RTW88_EXEC_TX);
		continue;
	discard:
		mutex_enter(net->lock);
		if (reserved)
			(void)rtw88_async_cancel(&net->async, &ticket);
		current = rtw88_net_tx_current_locked(net, &id);
		mutex_exit(net->lock);
		m_freem(m);
		if (ni != NULL)
			ieee80211_free_node(ni);
		if_statinc(ifp, if_oerrors);
		if (error == EPROTO && current) {
			ieee80211_new_state(ic, IEEE80211_S_INIT, EPROTO);
			break;
		}
	}
	mutex_enter(net->lock);
	KASSERT(net->tx_producers == 1);
	net->tx_producers--;
	mutex_exit(net->lock);
	if (n == RTW88_NET_TX_BUDGET)
		softint_schedule(net->softint);
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

	if (net->core->sc_executor != NULL) {
		rtw88_net_start_async(ifp);
		return;
	}
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
#ifdef RTW88_VHT80
			ht_expected |= net->core->sc_ra.configured &&
			    net->core->sc_ra.phy == RTW88_RA_VHT_1SS;
#endif
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
				if (available && !error)
					error = rtw88_net_tx_ra_validate(net,
					    &ht_policy, ht_epoch);
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

/* Giant requests stop; the executor retains all sleeping bus ownership. */
static void
rtw88_net_stop_async(struct ifnet *ifp, int disable)
{
	struct rtw88_net *net = ifp->if_softc;
	struct rtw88_async_epoch id;

	(void)disable;
	net->csa_pending = 0;
	rtw88_net_tx_pending_discard(net);
	rtw88_net_ht20_publish(net, NULL);
	rtw88_net_rxba_quiesce(net);
	if (net->ic.ic_bss != NULL)
		ieee80211_rxba_reset(&net->ic, net->ic.ic_bss);
	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
	ifp->if_timer = 0;
	callout_halt(&net->scan, NULL);
	mutex_enter(net->lock);
	(void)rtw88_net_request(&net->policy);
	net->requested = IEEE80211_S_INIT;
	net->state_arg = -1;
	net->stop_pending = net->stop_hold = 1;
	net->init_requested = 0;
	net->stats.init_pending = 0;
	net->work_pending = 1;
	id = net->async.id;
	id.epoch = net->policy.epoch;
	id.key_epoch = net->key_epoch;
	net->async_failed += rtw88_net_tx_queued(net);
	rtw88_async_withdraw(&net->async, &id);
	mutex_exit(net->lock);
	rtw88_executor_schedule(net->core, RTW88_EXEC_STOP);
}

static void
rtw88_net_stop(struct ifnet *ifp, int disable)
{
	struct rtw88_net *net = ifp->if_softc;

	if (net->core->sc_executor != NULL) {
		rtw88_net_stop_async(ifp, disable);
		return;
	}
	/* A synchronous stop owner cancels pending automatic CSA recovery. */
	net->csa_pending = 0;
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
#ifdef RTW88_RFK_EXPERIMENT
	net->rfk.beacon_epoch = 0;
	net->rfk.frontier_valid = 0;
#endif
	net->requested = IEEE80211_S_INIT;
	net->state_arg = -1;
	mutex_exit(net->lock);
	workqueue_wait(net->wq, &net->state_work);
	(void)net->parent_state(&net->ic, IEEE80211_S_INIT, -1);
	rtw88_stop(net->core);
}

static int
rtw88_net_init_async(struct rtw88_net *net, unsigned channel)
{
	struct ifnet *ifp = &net->ec.ec_if;
	struct rtw88_async_epoch id;
	int error = 0;

	mutex_enter(net->lock);
	/* A CSA never switches an active SDIO channel. Permit a fresh scan
	 * only after the owner has completed STOP and published its snapshot. */
	if (net->tx_quiet && net->csa_pending) {
		if (net->stop_pending || net->io_active || net->hw.started ||
		    net->hw.running || net->hw.power_ready) {
			mutex_exit(net->lock);
			return EBUSY;
		}
		if (net->stop_error != 0) {
			error = net->stop_error;
			mutex_exit(net->lock);
			return error;
		}
		net->csa_pending = 0;
		net->tx_quiet = 0;
		net->quiet_delivered = 0;
	}
	if (net->policy.dying || net->tx_quiet)
		error = ENXIO;
	else if (net->async.closed || net->key_error != 0)
		error = net->key_error != 0 ? net->key_error :
		    net->async.error != 0 ? net->async.error : EOVERFLOW;
	if (error != 0) {
		ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
		ifp->if_timer = 0;
		net->stats.init_pending = 0;
		net->stats.init_error = error;
		rtw88_net_fault(net, error);
		mutex_exit(net->lock);
		return error;
	}
	(void)rtw88_net_request(&net->policy);
	if (net->policy.dying) {
		rtw88_net_fault(net, EOVERFLOW);
		mutex_exit(net->lock);
		return EOVERFLOW;
	}
	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
	ifp->if_timer = 0;
	net->tx_timer = 0;
	net->init_requested = 1;
	net->init_failed = net->parent_init_failed = net->stop_hold = 0;
	/* A new explicit attempt resets its fault latch under closed admission;
	 * outstanding STOP stays pending and precedes all new hardware work. */
	net->async_fault = 0;
	net->stats.init_pending = 1;
	net->stats.init_error = 0;
	net->stats.init_generation = net->hw.generation;
	net->requested = IEEE80211_S_INIT;
	net->state_arg = -1;
	net->channel = channel;
	net->pending_channel.primary = net->pending_channel.center = channel;
	net->pending_channel.width_mhz = 20;
	net->work_pending = 1;
	net->wme_pending = 0;
	if (net->control_pending_valid &&
	    net->control_pending.kind != RTW88_CTL_STOP &&
	    net->control_pending.kind != RTW88_CTL_QUIET)
		net->control_pending_valid = 0;
	net->control_ready = 0;
	id.generation = net->hw.generation;
	id.epoch = net->policy.epoch;
	id.key_epoch = net->key_epoch;
	net->async_failed += rtw88_net_tx_queued(net);
	rtw88_async_withdraw(&net->async, &id);
	if (net->async.closed) {
		error = net->async.error != 0 ? net->async.error : EOVERFLOW;
		net->stats.init_pending = 0;
		net->stats.init_error = error;
		rtw88_net_fault(net, error);
	}
	net->prep_requested = 1;
	rtw88_net_state_schedule_locked(net);
	mutex_exit(net->lock);
	callout_stop(&net->scan);
	rtw88_net_ht20_publish(net, NULL);
	rtw88_executor_schedule(net->core,
	    error == 0 ? RTW88_EXEC_CONTROL : RTW88_EXEC_STOP);
	return error;
}

static int
rtw88_net_init(struct ifnet *ifp)
{
	struct rtw88_net *net = ifp->if_softc;
	struct ieee80211com *ic = &net->ic;
	unsigned channel;
	int error;

	if (net->tx_quiet && net->core->sc_executor == NULL)
		return EBUSY;
	if (net->policy.dying || net->ic.ic_opmode != IEEE80211_M_STA)
		return EOPNOTSUPP;
	if (net->core->sc_executor == NULL &&
	    (ifp->if_flags & IFF_RUNNING) != 0)
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
	if (net->core->sc_executor != NULL)
		return rtw88_net_init_async(net, channel);
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
#ifdef RTW88_DFS64
	/* DFS is a PCI-only capability; SDIO never reads its live state here. */
	if (net->core->sc_executor == NULL) {
		timeout = (uint32_t)MAX(hz,
		    mstohz(rtw88_net_tu_to_ms(net->ic.ic_bmisstimeout)));
		if (rtw88_net_dfs_expire(net, (uint32_t)getticks(), timeout)) {
			aprint_error_dev(net->dev, "DFS master beacon lost; TX quiet\n");
			(void)ieee80211_new_state(&net->ic, IEEE80211_S_INIT, -1);
		}
		mutex_enter(net->lock);
		if (net->core->sc_dfs.valid)
			ifp->if_timer = 1;
		mutex_exit(net->lock);
	}
#endif
	if (net->ic.ic_state == IEEE80211_S_RUN) {
		/* if_timer polls once per second; retain the 7-beacon deadline. */
		timeout = (uint32_t)MAX(hz,
		    mstohz(rtw88_net_tu_to_ms(net->ic.ic_bmisstimeout)));
		mutex_enter(net->lock);
		missed = rtw88_net_beacon_poll(&net->policy,
		    net->policy.epoch, (uint32_t)getticks(), timeout);
		/* Expired AP beacon evidence cannot retain a DFS TX permit. */
		if (missed && net->core->sc_executor == NULL)
			rtw88_dfs_close(net->core);
		mutex_exit(net->lock);
		if (missed)
			ieee80211_beacon_miss(&net->ic);
		/* Keep checking an idle station even when it has no outstanding TX. */
		ifp->if_timer = 1;
	}
}

/* Called before publication, while attach owns the quiescent core. */
static void
rtw88_net_async_attach_prepare(struct rtw88_net *net)
{
	struct rtw88_async_epoch id = {
		.generation = net->core->sc_generation,
		.epoch = net->policy.epoch, .key_epoch = 1
	};

	net->key_epoch = 1;
	rtw88_async_init(&net->async, &id);
	rtw88_net_hw_capture(net, &net->hw);
	callout_init(&net->tx_retry, CALLOUT_MPSAFE);
	callout_setfunc(&net->tx_retry, rtw88_net_tx_retry, net);
}

static void
rtw88_net_async_attach_crypto(struct rtw88_net *net)
{
	struct ieee80211com *ic = &net->ic;

	net->parent_key_alloc = ic->ic_crypto.cs_key_alloc;
	net->parent_key_delete = ic->ic_crypto.cs_key_delete;
	net->parent_key_set = ic->ic_crypto.cs_key_set;
	net->parent_key_begin = ic->ic_crypto.cs_key_update_begin;
	net->parent_key_end = ic->ic_crypto.cs_key_update_end;
	ic->ic_crypto.cs_key_alloc = rtw88_net_key_alloc;
	ic->ic_crypto.cs_key_delete = rtw88_net_key_delete;
	ic->ic_crypto.cs_key_set = rtw88_net_key_set;
	ic->ic_crypto.cs_key_update_begin = rtw88_net_key_begin;
	ic->ic_crypto.cs_key_update_end = rtw88_net_key_end;
}

static void
rtw88_net_async_attach_cleanup(struct rtw88_net *net)
{
	callout_destroy(&net->tx_retry);
}

int
rtw88_net_attach(struct rtw88_softc *core, device_t dev, kmutex_t *lock,
    const uint8_t mac[6], struct rtw88_net **out)
{
	static const unsigned channels5[] = {36,40,44,48,
#ifdef RTW88_DFS64
	    /* Passive DFS slave only: TX needs the master's beacon. */
	    52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144,
#endif
	    149,153,157,161,165};
	struct rtw88_net *net;
	struct rtw88_caps caps;
	struct ieee80211com *ic;
	struct ifnet *ifp;
	unsigned i, ch;
	int error;

	if (core == NULL || dev == NULL || lock == NULL || mac == NULL ||
	    out == NULL || (mac[0] & 1) != 0 ||
	    memcmp(mac, "\0\0\0\0\0\0", 6) == 0)
		return EINVAL;
	if (core->sc_executor != NULL &&
	    (core->sc_profile_channels_2g == 0 ||
	     (core->sc_profile_channels_2g & ~0x1fffU) != 0))
		return EINVAL;
	error = rtw88_get_caps(core, &caps);
	if (error != 0)
		return error;
	net = kmem_zalloc(sizeof(*net), KM_SLEEP);
	net->core = core;
	net->caps = caps;
	net->dev = dev;
	net->lock = lock;
	if (core->sc_executor != NULL)
		rtw88_net_async_attach_prepare(net);
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
	ic->ic_caps = IEEE80211_C_SHSLOT;
	if ((caps.features & RTW88_CAP_WPA2) != 0)
		ic->ic_caps |= IEEE80211_C_WPA2;
	memcpy(ic->ic_myaddr, mac, IEEE80211_ADDR_LEN);
	ic->ic_sup_rates[IEEE80211_MODE_11A] = ieee80211_std_rateset_11a;
	ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
	ic->ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;
	for (ch = 1; (caps.bands & RTW88_BAND_2G) != 0 && ch <= 13; ch++) {
		if (core->sc_executor != NULL &&
		    (core->sc_profile_channels_2g & (1U << (ch - 1))) == 0)
			continue;
		ic->ic_channels[ch].ic_freq = ieee80211_ieee2mhz(ch,
		    IEEE80211_CHAN_2GHZ);
		ic->ic_channels[ch].ic_flags = IEEE80211_CHAN_B |
		    IEEE80211_CHAN_G | IEEE80211_CHAN_OFDM |
		    IEEE80211_CHAN_PASSIVE;
	}
	/* Passive discovery; DFS channels only as a slave with RTW88_DFS64.
	 * Board regulatory limits still apply. */
	for (i = 0; (caps.bands & RTW88_BAND_5G) != 0 &&
	    i < __arraycount(channels5); i++) {
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
	if (core->sc_executor != NULL)
		rtw88_net_async_attach_crypto(net);
	if ((caps.features & RTW88_CAP_RXBA) != 0)
		rtw88_net_rxba_attach(net);
	ic->ic_sta_quiet = rtw88_net_sta_quiet;
	ic->ic_sta_csa = rtw88_net_sta_csa;
	ic->ic_scan_probe_admit = rtw88_net_scan_probe_admit;
	/* Register after ifattach, which initializes optional admission to NULL. */
	rtw88_net_ht20_publish(net, NULL);
	ic->ic_ht20_admit = rtw88_net_ht20_admit;
#ifdef RTW88_VHT80
	if ((caps.features & RTW88_CAP_VHT) != 0)
		ic->ic_vht_admit = rtw88_net_vht_admit;
#endif
	/* Opt in only after the strict consumer owns STA parameter apply. */
	if ((caps.features & RTW88_CAP_WME) != 0) {
		ic->ic_wme.wme_sta_apply = rtw88_net_wme_apply;
		ic->ic_caps |= IEEE80211_C_WME;
		ic->ic_flags |= IEEE80211_F_WME;
	}
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
	if (core->sc_executor != NULL)
		core->sc_net = net;
	core->sc_rx_frame = core->sc_executor != NULL ?
	    rtw88_net_rx_frame_async : rtw88_net_rx_frame;
	core->sc_tx_complete = rtw88_net_tx_complete;
	mutex_exit(lock);
	*out = net;
	return 0;
fail:
	if (core->sc_executor != NULL)
		rtw88_net_async_attach_cleanup(net);
	callout_destroy(&net->scan);
	kmem_free(net, sizeof(*net));
	return error;
}

static int
rtw88_net_shutdown_check(struct rtw88_net *net)
{
	struct rtw88_softc *sc = net->core;
	int error;

	mutex_enter(net->lock);
	error = net->stop_error;
	if (error == 0)
		error = sc->sc_hci_error != 0 ? sc->sc_hci_error :
		    sc->sc_dma_error != 0 ? sc->sc_dma_error : sc->sc_power_error;
	if (error == 0 && (net->io_active || net->prep_granted ||
	    net->tx_producers != 0 || net->async.total != 0 ||
	    sc->sc_resources_ready || sc->sc_started || sc->sc_running ||
	    sc->sc_rf_enabled ||
	    sc->sc_bootstrapping || sc->sc_powered || sc->sc_power_dirty ||
	    sc->sc_dma_master))
		error = EBUSY;
#ifdef RTW88_RFK_EXPERIMENT
	if (error == 0 && rtw88_calibration_active(sc))
		error = EBUSY;
#endif
	mutex_exit(net->lock);
	return error;
}

/* Giant protects retained packet/node references and the native stack. The
 * executor and both work items stay alive through reverse RFK recapture. */
static int
rtw88_net_detach_async(struct rtw88_net *net)
{
	struct rtw88_softc *sc = net->core;
	struct rtw88_async_epoch id;
	uint32_t started;
	unsigned producers, i;
	int error, nlocks = 0;

	if (rtw88_executor_owned(sc))
		return EDEADLK;
	mutex_enter(net->lock);
	if (!net->policy.dying)
		rtw88_net_close(&net->policy);
	net->stop_pending = net->stop_hold = 1;
	net->init_requested = net->work_pending = net->wme_pending = 0;
	net->stats.init_pending = 0;
	net->control_pending_valid = net->control_ready = 0;
	net->prep_requested = 0;
	id = net->async.id;
	id.epoch = net->policy.epoch;
	id.key_epoch = net->key_epoch;
	net->async_failed += rtw88_net_tx_queued(net);
	rtw88_async_withdraw(&net->async, &id);
	mutex_exit(net->lock);
	net->ec.ec_if.if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
	net->ec.ec_if.if_timer = 0;
	callout_halt(&net->scan, NULL);
	callout_halt(&net->tx_retry, NULL);
	if (!net->stack_detached)
		rtw88_net_rxba_quiesce(net);
	rtw88_executor_schedule(sc, RTW88_EXEC_STOP);
	/* A producer that already owns reserved bytes must cancel/commit and
	 * release its node before any storage can be freed. */
	started = rtw88_net_now_ms();
	KERNEL_UNLOCK_ALL(NULL, &nlocks);
	for (;;) {
		mutex_enter(net->lock);
		producers = net->tx_producers;
		mutex_exit(net->lock);
		if (producers == 0 ||
		    (uint32_t)(rtw88_net_now_ms() - started) >= 1000)
			break;
		(void)kpause("rtwtxclose", false, 1, NULL);
	}
	if (producers == 0) {
		workqueue_wait(net->wq, &net->state_work);
#ifdef RTW88_RFK_EXPERIMENT
		workqueue_wait(net->wq, &net->rfk.capture_work);
#endif
	}
	KERNEL_LOCK(nlocks, NULL);
	if (producers != 0)
		return EBUSY;
	rtw88_net_tx_pending_discard(net);
	/* A preparation grant may have deferred the first STOP until its Giant
	 * item returned. This post is cleanup only, after admission closed. */
	rtw88_executor_schedule(sc, RTW88_EXEC_STOP);
	error = rtw88_executor_drain(sc);
	if (error == 0)
		error = rtw88_net_shutdown_check(net);
	if (error != 0)
		return error;
	mutex_enter(net->lock);
	sc->sc_rx_frame = NULL;
	sc->sc_tx_complete = NULL;
	sc->sc_net_cookie = NULL;
	mutex_exit(net->lock);
	/* Native detach callbacks may request one more STOP. Keep the executor,
	 * its frontend wrapper and numeric publications alive through them. */
	if (!net->stack_detached) {
		ieee80211_ifdetach(&net->ic);
		if_detach(&net->ec.ec_if);
		net->stack_detached = 1;
	}
	error = rtw88_executor_drain(sc);
	if (error == 0)
		error = rtw88_net_shutdown_check(net);
	if (error == 0)
		error = rtw88_executor_destroy(sc);
	if (error != 0)
		return error;
	softint_disestablish(net->softint);
	workqueue_destroy(net->wq);
	for (i = 0; i < RTW88_NET_RX_LIMIT; i++)
		m_freem(net->rx[i].m);
	mutex_enter(net->lock);
	sc->sc_net = NULL;
	mutex_exit(net->lock);
	rtw88_net_rxba_detach(net);
	callout_destroy(&net->tx_retry);
	callout_destroy(&net->scan);
	kmem_free(net, sizeof(*net));
	return 0;
}

int
rtw88_net_detach(struct rtw88_net *net)
{
	unsigned i;
	int error;

	if (net == NULL)
		return 0;
	if (net->core->sc_executor != NULL) {
		KERNEL_LOCK(1, NULL);
		error = rtw88_net_detach_async(net);
		KERNEL_UNLOCK_ONE(NULL);
		return error;
	}
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
	return 0;
}
