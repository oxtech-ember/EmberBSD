/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: RTL8852BE idle station supervision contract, AI-assisted. */

#define _NETBSD_SOURCE 1
#define _DEFAULT_SOURCE 1

#include <sys/mman.h>
#include <unistd.h>

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define IEEE80211_ADDR_LEN 6
#define IEEE80211_SEQ_FRAG_MASK 0x0f
#define IEEE80211_FC0_SUBTYPE_BEACON 0x80
#define IEEE80211_FC1_WEP 0x40
#define RTW89_TXBUF_SIZE 4096
#define IEEE80211_BINTVAL_MIN 25
#define IEEE80211_BINTVAL_MAX 1000
#define IEEE80211_M_STA 1
#define RTW89_SCAN_DWELL_MS 200
#define RTW89_MAC_0 0
#define RTW89_SCH_TX_SEL_ALL 0
#define IFF_RUNNING 0x40
#define IFF_OACTIVE 0x400
#define IEEE80211_ADDR_EQ(a, b) (memcmp((a), (b), IEEE80211_ADDR_LEN) == 0)
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define hz 1000
#define mstohz(ms) (((ms) * hz + 999) / 1000)
#define aprint_error_dev(dev, ...) do { (void)(dev); } while (0)

enum ieee80211_state {
	IEEE80211_S_INIT,
	IEEE80211_S_SCAN,
	IEEE80211_S_AUTH,
	IEEE80211_S_ASSOC,
	IEEE80211_S_RUN
};

struct ieee80211_channel {
	unsigned int ic_freq;
	unsigned int ic_chan;
};

static struct ieee80211_channel ieee80211_channel_anyc;
#define IEEE80211_CHAN_ANYC (&ieee80211_channel_anyc)

struct ieee80211_node {
	uint16_t ni_associd;
	uint16_t ni_intval;
	struct ieee80211_channel *ni_chan;
	uint8_t ni_macaddr[IEEE80211_ADDR_LEN];
	uint8_t ni_bssid[IEEE80211_ADDR_LEN];
};

struct ieee80211_key { unsigned int opaque; };
struct mbuf {
	struct { int len; } m_pkthdr;
	uint8_t m_data[128];
	struct ieee80211_node *tx_owner;
};
#define mtod(m, t) ((t)((m)->m_data))

struct work { unsigned int opaque; };
struct workqueue { unsigned int opaque; };
struct callout { unsigned int opaque; };
struct mutex { unsigned int opaque; };
struct rtw89_nb { unsigned int opaque; };
struct ieee80211com;
struct rtw89_nb_bss { struct ieee80211com *owner; };

struct ifnet {
	void *if_softc;
	int if_timer;
	unsigned int if_flags;
};

struct ethercom {
	struct ifnet ec_if;
};

struct ieee80211com {
	enum ieee80211_state ic_state;
	int ic_opmode;
	uint16_t ic_bmisstimeout;
	struct ifnet *ic_ifp;
	struct ieee80211_node *ic_bss;
	struct ieee80211_channel *ic_curchan;
	uint8_t ic_myaddr[IEEE80211_ADDR_LEN];
	uint64_t test_sae_serial;
};

struct ieee80211_phy_ies {
	uint8_t len;
	uint8_t data[73];
};

struct ieee80211_beacon_ies {
	const uint8_t *ssid, *rates, *ssid_valid, *rates_valid, *xrates;
	const uint8_t *country, *fh, *ds, *ds_exact, *tim, *erp, *ht, *mdie;
	const uint8_t *security, *rsn, *rrm, *extcap, *extcap_bounded;
	const uint8_t *rsnx, *wme;
	const uint8_t *csa, *xcsa, *quiet;
	size_t timoff;
	unsigned wme_duplicate, bad_erp, unknown;
	struct ieee80211_phy_ies phy;
};

struct rtw89_softc {
	void *sc_dev;
	struct ethercom sc_ec;
	struct ieee80211com sc_ic;
	int (*sc_newstate)(struct ieee80211com *, enum ieee80211_state, int);
	struct rtw89_nb *sc_nb;
	bool sc_running;
	volatile bool sc_txstopped;
	volatile bool sc_txqueue_stopped;
	bool sc_csa_quiet;
	bool sc_beacon_active;
	uint32_t sc_beacon_tick;
	bool sc_csa_reset_done;
	volatile unsigned sc_csa_init_pending;
	bool sc_csa_deferred;
	enum ieee80211_state sc_csa_deferred_state;
	int sc_csa_deferred_arg;
	bool sc_stopping;
	bool sc_nspending;
	enum ieee80211_state sc_nsstate;
	uint64_t sc_nsserial;
	int sc_nsarg;
	struct mutex sc_nslock;
	struct workqueue *sc_nswq;
	struct work sc_nswk;
	struct callout sc_scan_to;
	bool sc_scanning;
	void *sc_tx_sih;
	uint8_t *sc_txbuf;
};

struct ieee80211_frame {
	uint8_t i_fc[2];
	uint8_t i_dur[2];
	uint8_t i_addr1[IEEE80211_ADDR_LEN];
	uint8_t i_addr2[IEEE80211_ADDR_LEN];
	uint8_t i_addr3[IEEE80211_ADDR_LEN];
	uint8_t i_seq[2];
};

struct test_beacon {
	struct ieee80211_frame wh;
	uint8_t body[24];
};

static uint32_t fake_ticks;
static unsigned shared_watchdog_calls;
static unsigned beacon_miss_calls;
static unsigned quiet_calls;
static unsigned resume_calls;
static unsigned softint_calls;
static int quiet_error;
static int resume_error;
static unsigned enqueue_calls;
static unsigned pending_work;
static unsigned restart_stop_calls;
static unsigned restart_start_calls;
static unsigned task_event_log[32];
static unsigned task_event_count;
static int rtw89_debug;
static struct rtw89_nb test_nb;
static struct workqueue test_workqueue;
static unsigned sae_retire_calls, sae_state_requests, tx_gate_calls;
static unsigned tx_crypto_calls, tx_copy_calls, tx_bridge_calls;
static unsigned retire_seen_at_quiet, retire_seen_at_stop;
static unsigned requests_seen_at_enqueue;
static unsigned requests_seen_at_callout;
static enum ieee80211_state sae_requested_state;
static bool sae_requested_context_valid;
static bool tx_gate_allow, tx_crypto_fail;
static uint8_t test_txbuf[RTW89_TXBUF_SIZE], bridge_bytes[128];
static int bridge_len, bridge_result;
static unsigned bridge_flags;
static struct ieee80211_key test_key;
static unsigned tx_events[8], tx_event_count;
static unsigned parent_state_calls, join_calls, assoc_calls;
static unsigned scan_schedule_calls;
static int parent_state_error;
static bool parent_queue_auth;
static enum ieee80211_state parent_last_state;
static enum { BRIDGE_UNCHANGED, BRIDGE_RECONFIGURE,
	BRIDGE_RECONFIGURE_QUEUE_AUTH } bridge_transition;

static int rtw89_newstate(struct ieee80211com *, enum ieee80211_state, int);
static void rtw89_newstate_task(struct work *, void *);

uint64_t
ieee80211_sae_serial(const struct ieee80211com *ic)
{
	return ic->test_sae_serial;
}

/* The common SAE owner has its own actual-source phase/epoch contract.
 * Here its hooks are controlled boundaries so omitted or late driver calls
 * are observable independently of that policy. Inactive legacy tests keep
 * their existing state and hardware event ordering. */
void
ieee80211_sae_retire(struct ieee80211com *ic)
{
	(void)ic;
	sae_retire_calls++;
}

void
ieee80211_sae_state_request(struct ieee80211com *ic,
    enum ieee80211_state nstate)
{
	struct rtw89_softc *sc = ic->ic_ifp->if_softc;

	sae_state_requests++;
	/* Validate the borrowed context during the call; do not retain it. */
	sae_requested_context_valid = ic == &sc->sc_ic;
	sae_requested_state = nstate;
}

bool
ieee80211_sae_tx_current(struct ieee80211com *ic,
    struct ieee80211_node *ni, struct mbuf *m)
{
	tx_gate_calls++;
	if (tx_event_count < sizeof(tx_events) / sizeof(tx_events[0]))
		tx_events[tx_event_count++] = 1;
	return tx_gate_allow && ni == ic->ic_bss && ni == m->tx_owner;
}

static struct ieee80211_key *
ieee80211_crypto_encap(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct mbuf *m)
{
	(void)ic;
	(void)ni;
	tx_crypto_calls++;
	if (tx_event_count < sizeof(tx_events) / sizeof(tx_events[0]))
		tx_events[tx_event_count++] = 2;
	if (tx_crypto_fail)
		return NULL;
	/* Mark the lower layer's output; the driver must copy that output. */
	m->m_data[31] = 0xec;
	m->m_pkthdr.len += 8;
	return &test_key;
}

static void
m_copydata(struct mbuf *m, int off, int len, void *data)
{
	assert(off >= 0 && len >= 0 && off + len <= m->m_pkthdr.len);
	assert((size_t)(off + len) <= sizeof(m->m_data));
	tx_copy_calls++;
	if (tx_event_count < sizeof(tx_events) / sizeof(tx_events[0]))
		tx_events[tx_event_count++] = 3;
	memcpy(data, m->m_data + off, (size_t)len);
}

static int
rtw89_nb_tx(struct rtw89_nb *nb, const void *data, int len, unsigned flags)
{
	assert(nb == &test_nb && len <= (int)sizeof(bridge_bytes));
	tx_bridge_calls++;
	if (tx_event_count < sizeof(tx_events) / sizeof(tx_events[0]))
		tx_events[tx_event_count++] = 4;
	bridge_len = len;
	bridge_flags = flags;
	memcpy(bridge_bytes, data, (size_t)len);
	return bridge_result;
}

static void
callout_halt(struct callout *co, void *lock)
{
	(void)co;
	(void)lock;
}

static int
ieee80211_new_state(struct ieee80211com *ic,
    enum ieee80211_state nstate, int arg)
{
	return rtw89_newstate(ic, nstate, arg);
}

static void
workqueue_wait(struct workqueue *wq, struct work *wk)
{
	/* Execute the already queued driver worker when stop joins it. */
	struct rtw89_softc *sc = (struct rtw89_softc *)
	    ((char *)wk - offsetof(struct rtw89_softc, sc_nswk));
	(void)wq;
	for (unsigned i = 0; pending_work != 0 && i < 8; i++) {
		pending_work--;
		rtw89_newstate_task(wk, sc);
	}
	assert(pending_work == 0);
}
static const char *ieee80211_state_name[] = {
	"INIT", "SCAN", "AUTH", "ASSOC", "RUN"
};

static unsigned
atomic_cas_uint(volatile unsigned *p, unsigned old, unsigned new_value)
{
	unsigned current = *p;
	if (current == old)
		*p = new_value;
	return current;
}

static unsigned
atomic_swap_uint(volatile unsigned *p, unsigned new_value)
{
	unsigned old = *p;
	*p = new_value;
	return old;
}

static int
splnet(void)
{
	return 0;
}

static void
splx(int s)
{
	(void)s;
}

static void
mutex_enter(void *lock)
{
	(void)lock;
}

static void
mutex_exit(void *lock)
{
	(void)lock;
}

static void
workqueue_enqueue(void *wq, void *work, void *queue)
{
	(void)wq;
	(void)work;
	(void)queue;
	enqueue_calls++;
	pending_work++;
	requests_seen_at_enqueue = sae_state_requests;
}

static void
callout_stop(struct callout *co)
{
	(void)co;
	requests_seen_at_callout = sae_state_requests;
}

static void
callout_schedule(struct callout *co, int ticks)
{
	(void)co;
	(void)ticks;
	scan_schedule_calls++;
}

static const char *
device_xname(void *dev)
{
	(void)dev;
	return "rtwx-test";
}

static void
event(unsigned int value)
{
	if (task_event_count < sizeof(task_event_log) / sizeof(task_event_log[0]))
		task_event_log[task_event_count++] = value;
}

static uint32_t
getticks(void)
{
	return fake_ticks;
}

static void
ieee80211_watchdog(struct ieee80211com *ic)
{
	(void)ic;
	shared_watchdog_calls++;
}

static void
ieee80211_beacon_miss(struct ieee80211com *ic)
{
	(void)ic;
	beacon_miss_calls++;
}

static unsigned
ieee80211_chan2ieee(struct ieee80211com *ic, struct ieee80211_channel *chan)
{
	(void)ic;
	return chan->ic_chan;
}

static int
ieee80211_phy_ies_parse(struct ieee80211_phy_ies *ies, const uint8_t *data,
    size_t len)
{
	(void)ies;
	(void)data;
	(void)len;
	return 0;
}

static int
rtw89_nb_tx_quiet(void *nb, bool quiet)
{
	(void)nb;
	if (quiet) {
		retire_seen_at_quiet = sae_retire_calls;
		quiet_calls++;
		event(quiet_calls == 1 ? 1 : 4);
		return quiet_error;
	}
	resume_calls++;
	event(7);
	return resume_error;
}

static void
rtw89_nb_stop(void *nb)
{
	(void)nb;
	restart_stop_calls++;
	retire_seen_at_stop = sae_retire_calls;
	event(2);
}

static int
rtw89_nb_start(void *nb, const uint8_t *addr)
{
	(void)nb;
	(void)addr;
	restart_start_calls++;
	event(3);
	return 0;
}

static void
rtw89_nb_bss_leave(struct rtw89_nb *nb)
{
	(void)nb;
	event(5);
}

static void
rtw89_nb_scan_start(struct rtw89_nb *nb)
{
	(void)nb;
	event(6);
}

static void
rtw89_nb_scan_end(struct rtw89_nb *nb)
{
	(void)nb;
}

static void
rtw89_set_curchan(struct rtw89_softc *sc, struct ieee80211_channel *chan)
{
	(void)sc;
	(void)chan;
	event(8);
}

static void
rtw89_fill_bss(struct rtw89_softc *sc, struct rtw89_nb_bss *bss)
{
	bss->owner = &sc->sc_ic;
}

static void
reconfigure_during_bridge_call(const struct rtw89_nb_bss *bss)
{
	struct ieee80211com *ic = bss->owner;
	bool queue_auth = bridge_transition == BRIDGE_RECONFIGURE_QUEUE_AUTH;

	if (bridge_transition == BRIDGE_UNCHANGED)
		return;
	bridge_transition = BRIDGE_UNCHANGED;
	/* Model a replacement native configuration while firmware work sleeps.
	 * Phase policy is separately tested by the common SAE owner. */
	ic->test_sae_serial++;
	ic->ic_state = IEEE80211_S_SCAN;
	if (queue_auth)
		(void)rtw89_newstate(ic, IEEE80211_S_AUTH, -1);
}

static int
rtw89_nb_bss_join(struct rtw89_nb *nb, const struct rtw89_nb_bss *bss)
{
	(void)nb;
	join_calls++;
	reconfigure_during_bridge_call(bss);
	return 0;
}

static int
rtw89_nb_bss_assoc(struct rtw89_nb *nb, const struct rtw89_nb_bss *bss)
{
	(void)nb;
	assoc_calls++;
	reconfigure_during_bridge_call(bss);
	return 0;
}

static int
test_net80211_newstate(struct ieee80211com *ic,
    enum ieee80211_state nstate, int arg)
{
	int error = parent_state_error;

	(void)arg;
	parent_state_calls++;
	parent_last_state = nstate;
	event(100 + nstate);
	if (error != 0) {
		/* A rejected common transition retires its own SAE generation.
		 * The serial change alone does not imply a replacement request. */
		parent_state_error = 0;
		ic->test_sae_serial++;
		if (parent_queue_auth) {
			ic->ic_state = IEEE80211_S_SCAN;
			(void)rtw89_newstate(ic, IEEE80211_S_AUTH, -1);
		}
		return error;
	}
	ic->ic_state = nstate;
	return 0;
}

static void
softint_schedule(void *si)
{
	(void)si;
	softint_calls++;
}

#include "rtw89-link-loss.inc"

static void
reset_counts(void)
{
	shared_watchdog_calls = 0;
	beacon_miss_calls = 0;
	quiet_calls = 0;
	resume_calls = 0;
	softint_calls = 0;
	enqueue_calls = 0;
	pending_work = 0;
	restart_stop_calls = 0;
	restart_start_calls = 0;
	task_event_count = 0;
	quiet_error = 0;
	resume_error = 0;
	sae_retire_calls = sae_state_requests = tx_gate_calls = 0;
	tx_crypto_calls = tx_copy_calls = tx_bridge_calls = 0;
	retire_seen_at_quiet = retire_seen_at_stop = 0;
	requests_seen_at_enqueue = 0;
	requests_seen_at_callout = 0;
	sae_requested_context_valid = false;
	sae_requested_state = IEEE80211_S_INIT;
	tx_gate_allow = true;
	tx_crypto_fail = false;
	bridge_len = bridge_result = 0;
	bridge_flags = tx_event_count = 0;
	parent_state_calls = join_calls = assoc_calls = 0;
	scan_schedule_calls = 0;
	parent_state_error = 0;
	parent_queue_auth = false;
	parent_last_state = IEEE80211_S_INIT;
	bridge_transition = BRIDGE_UNCHANGED;
	memset(test_txbuf, 0, sizeof(test_txbuf));
	memset(bridge_bytes, 0, sizeof(bridge_bytes));
}

static void
init_station(struct rtw89_softc *sc, struct ifnet *ifp,
    struct ieee80211_node *ni, struct ieee80211_channel *chan)
{
	memset(sc, 0, sizeof(*sc));
	memset(ifp, 0, sizeof(*ifp));
	memset(ni, 0, sizeof(*ni));
	memset(chan, 0, sizeof(*chan));
	sc->sc_running = true;
	sc->sc_nb = &test_nb;
	sc->sc_newstate = test_net80211_newstate;
	sc->sc_nswq = &test_workqueue;
	sc->sc_tx_sih = sc;
	sc->sc_txbuf = test_txbuf;
	sc->sc_ic.ic_state = IEEE80211_S_RUN;
	sc->sc_ic.ic_opmode = IEEE80211_M_STA;
	sc->sc_ic.ic_bmisstimeout = 100;
	sc->sc_ic.ic_ifp = ifp;
	sc->sc_ic.ic_bss = ni;
	sc->sc_ic.ic_curchan = chan;
	ifp->if_softc = sc;
	sc->sc_ec.ec_if.if_softc = sc;
	sc->sc_ic.ic_ifp = ifp;
	ni->ni_associd = 1;
	ni->ni_intval = 100;
	ni->ni_chan = chan;
	chan->ic_freq = 5260;
	chan->ic_chan = 52;
	ni->ni_macaddr[0] = 0x02;
	ni->ni_macaddr[1] = 0x11;
	ni->ni_macaddr[2] = 0x22;
	ni->ni_macaddr[3] = 0x33;
	ni->ni_macaddr[4] = 0x44;
	ni->ni_macaddr[5] = 0x55;
	memcpy(ni->ni_bssid, ni->ni_macaddr, IEEE80211_ADDR_LEN);
	fake_ticks = 100;
	rtw89_beacon_state(sc, IEEE80211_S_RUN);
}

static void
make_beacon(struct test_beacon *beacon, struct ieee80211_node *ni)
{
	struct ieee80211_frame *wh = &beacon->wh;
	unsigned i;

	memset(beacon, 0, sizeof(*beacon));
	wh->i_fc[0] = IEEE80211_FC0_SUBTYPE_BEACON;
	for (i = 0; i < IEEE80211_ADDR_LEN; i++)
		wh->i_addr1[i] = 0xff;
	memcpy(wh->i_addr2, ni->ni_macaddr, IEEE80211_ADDR_LEN);
	memcpy(wh->i_addr3, ni->ni_bssid, IEEE80211_ADDR_LEN);
	beacon->body[8] = 100;
	beacon->body[10] = 1;
	beacon->body[12] = 0;
	beacon->body[13] = 0;
	beacon->body[14] = 1;
	beacon->body[15] = 1;
	beacon->body[16] = 2;
}

static int
test_idle_run_keeps_watchdog_armed(void)
{
	struct rtw89_softc sc;
	struct ifnet ifp;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;

	init_station(&sc, &ifp, &ni, &chan);
	ifp.if_timer = 1;
	rtw89_watchdog(&ifp);
	if (shared_watchdog_calls != 1 || ifp.if_timer <= 0) {
		fprintf(stderr,
		    "idle associated station must rearm beacon supervision; if_timer=%d\n",
		    ifp.if_timer);
		return 1;
	}
	return 0;
}

static int
test_init_state_does_not_arm_station_supervision(void)
{
	struct rtw89_softc sc;
	struct ifnet ifp;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;

	init_station(&sc, &ifp, &ni, &chan);
	rtw89_beacon_state(&sc, IEEE80211_S_INIT);
	ifp.if_timer = 1;
	sc.sc_ic.ic_state = IEEE80211_S_INIT;
	rtw89_watchdog(&ifp);
	if (ifp.if_timer != 0) {
		fprintf(stderr, "INIT must leave if_timer disarmed; got %d\n",
		    ifp.if_timer);
		return 1;
	}
	return 0;
}

static int
test_beacon_observation_is_bound_to_current_ap(void)
{
	struct rtw89_softc sc;
	struct ifnet ifp;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;
	struct test_beacon beacon;

	init_station(&sc, &ifp, &ni, &chan);
	make_beacon(&beacon, &ni);
	fake_ticks = 201;
	rtw89_beacon_observe(&sc, &beacon, sizeof(beacon.wh) + 17, chan.ic_freq);
	if (sc.sc_beacon_tick != 201) {
		fprintf(stderr, "valid current-AP beacon did not refresh timer\n");
		return 1;
	}
	memset(beacon.wh.i_addr2, 0x77, IEEE80211_ADDR_LEN);
	fake_ticks = 250;
	rtw89_beacon_observe(&sc, &beacon, sizeof(beacon.wh) + 17, chan.ic_freq);
	if (sc.sc_beacon_tick != 201) {
		fprintf(stderr, "foreign beacon refreshed current-AP timer\n");
		return 1;
	}
	make_beacon(&beacon, &ni);
	fake_ticks = 260;
	rtw89_beacon_observe(&sc, &beacon, sizeof(beacon.wh) + 16, chan.ic_freq);
	if (sc.sc_beacon_tick != 201) {
		fprintf(stderr, "truncated beacon refreshed current-AP timer\n");
		return 1;
	}
	fake_ticks = 270;
	rtw89_beacon_observe(&sc, &beacon, sizeof(beacon.wh) + 17,
	    chan.ic_freq + 20);
	if (sc.sc_beacon_tick != 201) {
		fprintf(stderr, "wrong-channel beacon refreshed current-AP timer\n");
		return 1;
	}
	make_beacon(&beacon, &ni);
	beacon.body[17] = 3;
	beacon.body[18] = 1;
	beacon.body[19] = 149;
	fake_ticks = 280;
	rtw89_beacon_observe(&sc, &beacon, sizeof(beacon.wh) + 20, chan.ic_freq);
	if (sc.sc_beacon_tick != 201) {
		fprintf(stderr, "contradictory channel IE refreshed timer\n");
		return 1;
	}
	make_beacon(&beacon, &ni);
	beacon.body[17] = 37;
	beacon.body[18] = 3;
	beacon.body[19] = 0;
	beacon.body[20] = 60;
	beacon.body[21] = 1;
	fake_ticks = 290;
	rtw89_beacon_observe(&sc, &beacon, sizeof(beacon.wh) + 22, chan.ic_freq);
	if (sc.sc_beacon_tick != 201) {
		fprintf(stderr, "CSA beacon refreshed stable-BSS timer\n");
		return 1;
	}
	return 0;
}

static int
test_healthy_idle_beacons_prevent_false_miss(void)
{
	struct rtw89_softc sc;
	struct ifnet ifp;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;
	struct test_beacon beacon;

	init_station(&sc, &ifp, &ni, &chan);
	make_beacon(&beacon, &ni);
	fake_ticks = 150;
	rtw89_beacon_observe(&sc, &beacon, sizeof(beacon.wh) + 17, chan.ic_freq);
	fake_ticks = 1101;
	rtw89_watchdog(&ifp);
	if (beacon_miss_calls != 0 || ifp.if_timer != 1) {
		fprintf(stderr, "healthy idle station triggered beacon miss\n");
		return 1;
	}
	return 0;
}

static int
test_missing_beacons_trigger_net80211_recovery(void)
{
	struct rtw89_softc sc;
	struct ifnet ifp;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;

	init_station(&sc, &ifp, &ni, &chan);
	fake_ticks += 999;
	rtw89_watchdog(&ifp);
	if (beacon_miss_calls != 0) {
		fprintf(stderr, "beacon watchdog fired before configured deadline\n");
		return 1;
	}
	fake_ticks++;
	rtw89_watchdog(&ifp);
	if (beacon_miss_calls != 1 || ifp.if_timer != 1) {
		fprintf(stderr, "stale beacon timeout did not notify net80211\n");
		return 1;
	}
	return 0;
}

static int
test_beacon_deadline_handles_tick_wrap(void)
{
	struct rtw89_softc sc;
	struct ifnet ifp;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;

	init_station(&sc, &ifp, &ni, &chan);
	fake_ticks = UINT32_MAX - 499;
	rtw89_beacon_state(&sc, IEEE80211_S_RUN);
	fake_ticks = 499;
	rtw89_watchdog(&ifp);
	if (beacon_miss_calls != 0 || ifp.if_timer != 1) {
		fprintf(stderr, "beacon deadline fired early across tick wrap\n");
		return 1;
	}
	fake_ticks = 500;
	rtw89_watchdog(&ifp);
	if (beacon_miss_calls != 1) {
		fprintf(stderr, "beacon deadline missed uint32 tick wrap\n");
		return 1;
	}
	return 0;
}

static int
test_csa_quiet_survives_queue_wake_until_scan(void)
{
	struct rtw89_softc sc;
	struct ifnet ifp;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;

	init_station(&sc, &ifp, &ni, &chan);
	quiet_error = EIO;
	rtw89_sta_quiet(&sc.sc_ic);
	if (!sc.sc_csa_quiet || !sc.sc_txstopped || quiet_calls != 1 ||
	    sc.sc_beacon_active) {
		fprintf(stderr, "CSA did not synchronously quiet station TX\n");
		return 1;
	}
	quiet_error = 0;
	rtw89_nb_queues(&sc, true);
	if (!sc.sc_txstopped || softint_calls != 1) {
		fprintf(stderr, "late queue wake escaped CSA quiet gate\n");
		return 1;
	}
	rtw89_csa_scan_resume(&sc, IEEE80211_S_INIT);
	if (!sc.sc_csa_quiet || resume_calls != 0) {
		fprintf(stderr, "CSA quiet released before fresh scan\n");
		return 1;
	}
	sc.sc_csa_reset_done = true;
	resume_error = EIO;
	rtw89_csa_scan_resume(&sc, IEEE80211_S_AUTH);
	if (!sc.sc_csa_quiet || !sc.sc_txstopped || resume_calls != 1) {
		fprintf(stderr, "resume error failed open the CSA gate\n");
		return 1;
	}
	resume_error = 0;
	rtw89_csa_scan_resume(&sc, IEEE80211_S_SCAN);
	if (sc.sc_csa_quiet || sc.sc_txstopped || resume_calls != 2) {
		fprintf(stderr, "fresh scan did not release quiet gate\n");
		return 1;
	}
	rtw89_sta_quiet(&sc.sc_ic);
	sc.sc_csa_reset_done = true;
	rtw89_nb_queues(&sc, false);
	rtw89_csa_scan_resume(&sc, IEEE80211_S_SCAN);
	if (sc.sc_csa_quiet || !sc.sc_txstopped ||
	    !sc.sc_txqueue_stopped) {
		fprintf(stderr, "CSA release erased ordinary queue stop\n");
		return 1;
	}
	return 0;
}

static int
test_csa_init_cannot_be_coalesced_away(void)
{
	struct rtw89_softc sc;
	struct ifnet ifp;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;
	int arg;
	enum ieee80211_state state;

	init_station(&sc, &ifp, &ni, &chan);
	sc.sc_csa_init_pending = 1;
	arg = 3;
	state = rtw89_csa_defer_state(&sc, IEEE80211_S_SCAN, &arg);
	if (state != IEEE80211_S_INIT || !sc.sc_csa_deferred || arg != -1) {
		fprintf(stderr, "CSA INIT was overwritten by SCAN\n");
		return 1;
	}
	arg = 7;
	state = rtw89_csa_defer_state(&sc, IEEE80211_S_AUTH, &arg);
	if (state != IEEE80211_S_INIT ||
	    sc.sc_csa_deferred_state != IEEE80211_S_AUTH ||
	    sc.sc_csa_deferred_arg != 7) {
		fprintf(stderr, "latest reconnect request was not deferred\n");
		return 1;
	}
	rtw89_csa_finish_init(&sc, IEEE80211_S_INIT);
	if (sc.sc_csa_init_pending || sc.sc_csa_deferred ||
	    !sc.sc_nspending || sc.sc_nsstate != IEEE80211_S_AUTH ||
	    sc.sc_nsarg != 7 || enqueue_calls != 1) {
		fprintf(stderr, "CSA INIT completion lost deferred AUTH\n");
		return 1;
	}
	return 0;
}

static int
test_csa_restart_resets_dma_before_resume(void)
{
	struct rtw89_softc sc;
	struct ifnet ifp;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;

	init_station(&sc, &ifp, &ni, &chan);
	sc.sc_csa_quiet = true;
	if (rtw89_csa_restart(&sc) != 0 || restart_stop_calls != 1 ||
	    restart_start_calls != 1 || quiet_calls != 1 ||
	    !sc.sc_csa_reset_done) {
		fprintf(stderr, "CSA did not reset and re-quiet DMA before scan\n");
		return 1;
	}
	if (rtw89_csa_restart(&sc) != 0 || restart_stop_calls != 1 ||
	    restart_start_calls != 1) {
		fprintf(stderr, "CSA reset repeated before scheduler resume\n");
		return 1;
	}
	return 0;
}

static unsigned
run_state_work(struct rtw89_softc *sc)
{
	unsigned runs = 0;

	while (pending_work != 0 && runs < 8) {
		pending_work--;
		rtw89_newstate_task(&sc->sc_nswk, sc);
		runs++;
	}
	return runs;
}

static int
test_csa_only_init_recovers_to_scan(void)
{
	static const unsigned expected[] = {
		1, 5, 100, 2, 3, 4, 6, 8, 7, 101
	};
	struct rtw89_softc sc;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;
	unsigned runs, i;

	init_station(&sc, &sc.sc_ec.ec_if, &ni, &chan);
	sc.sc_ec.ec_if.if_flags = IFF_RUNNING;
	sc.sc_ic.ic_state = IEEE80211_S_RUN;
	rtw89_sta_quiet(&sc.sc_ic);
	/* The real net80211 CSA path requests only INIT after quiet. */
	(void)rtw89_newstate(&sc.sc_ic, IEEE80211_S_INIT, -1);
	runs = run_state_work(&sc);
	if (sc.sc_ic.ic_state != IEEE80211_S_SCAN || pending_work != 0 ||
	    runs != 2 || sc.sc_csa_quiet || restart_stop_calls != 1 ||
	    restart_start_calls != 1) {
		fprintf(stderr,
		    "CSA INIT without follow-up state did not recover through reset to SCAN\n");
		return 1;
	}
	if (task_event_count != sizeof(expected) / sizeof(expected[0])) {
		fprintf(stderr, "CSA recovery event count: got %u, want %zu\n",
		    task_event_count, sizeof(expected) / sizeof(expected[0]));
		return 1;
	}
	for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
		if (task_event_log[i] != expected[i]) {
			fprintf(stderr, "CSA event %u: got %u, want %u\n", i,
			    task_event_log[i], expected[i]);
			return 1;
		}
	}
	return 0;
}

static int
test_stop_retires_csa_without_default_scan(void)
{
	struct rtw89_softc sc;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;

	init_station(&sc, &sc.sc_ec.ec_if, &ni, &chan);
	sc.sc_ec.ec_if.if_flags = IFF_RUNNING;
	sc.sc_ic.ic_state = IEEE80211_S_RUN;
	rtw89_sta_quiet(&sc.sc_ic);
	sc.sc_stopping = true;
	(void)rtw89_newstate(&sc.sc_ic, IEEE80211_S_INIT, -1);
	(void)run_state_work(&sc);
	if (sc.sc_ic.ic_state != IEEE80211_S_INIT || pending_work != 0 ||
	    restart_stop_calls != 0 || restart_start_calls != 0) {
		fprintf(stderr, "owner stop was followed by an automatic CSA scan\n");
		return 1;
	}
	return 0;
}

static int
test_quiet_and_stop_retire_beacon_supervision(void)
{
	struct rtw89_softc sc;
	struct ifnet ifp;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;

	init_station(&sc, &ifp, &ni, &chan);
	rtw89_beacon_state(&sc, IEEE80211_S_INIT);
	sc.sc_running = false;
	ifp.if_timer = 1;
	sc.sc_ic.ic_state = IEEE80211_S_INIT;
	fake_ticks += 1000;
	rtw89_watchdog(&ifp);
	if (ifp.if_timer != 0 || beacon_miss_calls != 0 || sc.sc_beacon_active) {
		fprintf(stderr, "stop left stale beacon callback armed\n");
		return 1;
	}
	return 0;
}

/* A guard page makes any read beyond a supplied RX frame fail the process. */
static int
test_truncated_beacons_respect_memory_boundary(void)
{
	struct rtw89_softc sc;
	struct ifnet ifp;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;
	struct test_beacon beacon;
	long page_size = sysconf(_SC_PAGESIZE);
	uint8_t *mapping, *frame;
	size_t len;
	unsigned malformed;

	if (page_size <= 0)
		return 1;
	mapping = mmap(NULL, (size_t)page_size * 2, PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_PRIVATE, -1, 0);
	if (mapping == MAP_FAILED) {
		perror("mmap guard page");
		return 1;
	}
	if (mprotect(mapping + page_size, (size_t)page_size, PROT_NONE) != 0) {
		perror("mprotect guard page");
		munmap(mapping, (size_t)page_size * 2);
		return 1;
	}
	init_station(&sc, &ifp, &ni, &chan);
	for (malformed = 0; malformed < 2; malformed++) {
		make_beacon(&beacon, &ni);
		if (malformed)
			beacon.body[15] = 255;
		for (len = 0; len <= sizeof(beacon); len++) {
			frame = mapping + page_size - len;
			memcpy(frame, &beacon, len);
			rtw89_beacon_observe(&sc, frame, len, chan.ic_freq);
		}
	}
	munmap(mapping, (size_t)page_size * 2);
	return 0;
}

static void
make_tx_frame(struct mbuf *m, struct ieee80211_node *ni, bool protected)
{
	memset(m, 0, sizeof(*m));
	m->m_pkthdr.len = 32;
	m->m_data[0] = 0xd0;
	m->m_data[1] = protected ? IEEE80211_FC1_WEP : 0;
	m->m_data[31] = 0x31;
	m->tx_owner = ni;
}

static int
test_tx_stale_gate_precedes_crypto_and_bridge(void)
{
	struct rtw89_softc sc;
	struct ifnet ifp;
	struct ieee80211_node ni, other;
	struct ieee80211_channel chan;
	struct mbuf m, original;

	init_station(&sc, &ifp, &ni, &chan);
	make_tx_frame(&m, &ni, true);
	original = m;
	tx_gate_allow = false;
	if (rtw89_tx(&sc, &m, &ni, 0) != ESTALE || tx_gate_calls != 1 ||
	    tx_crypto_calls != 0 || tx_copy_calls != 0 || tx_bridge_calls != 0 ||
	    memcmp(&m, &original, sizeof(m)) != 0) {
		fprintf(stderr, "rejected TX reached crypto, copy or bridge\n");
		return 1;
	}
	/* Equal address bytes do not make a replacement node the queued owner. */
	other = ni;
	tx_gate_allow = true;
	if (rtw89_tx(&sc, &m, &other, 0) != ESTALE || tx_gate_calls != 2 ||
	    tx_crypto_calls != 0 || tx_copy_calls != 0 || tx_bridge_calls != 0) {
		fprintf(stderr, "TX admission did not receive the actual node\n");
		return 1;
	}
	return 0;
}

static int
test_tx_encrypts_once_then_submits_ciphertext(void)
{
	static const unsigned expected[] = {1, 2, 3, 4};
	struct rtw89_softc sc;
	struct ifnet ifp;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;
	struct mbuf m;

	init_station(&sc, &ifp, &ni, &chan);
	make_tx_frame(&m, &ni, true);
	if (rtw89_tx(&sc, &m, &ni, 0x25) != 0 || tx_gate_calls != 1 ||
	    tx_crypto_calls != 1 || tx_copy_calls != 1 || tx_bridge_calls != 1 ||
	    bridge_len != 40 || bridge_flags != 0x25 || bridge_bytes[31] != 0xec ||
	    memcmp(bridge_bytes, m.m_data, 40) != 0 ||
	    tx_event_count != sizeof(expected) / sizeof(expected[0]) ||
	    memcmp(tx_events, expected, sizeof(expected)) != 0) {
		fprintf(stderr, "protected TX was not admitted/encrypted/submitted once\n");
		return 1;
	}
	return 0;
}

static int
test_tx_crypto_failure_and_size_limit_fail_closed(void)
{
	struct rtw89_softc sc;
	struct ifnet ifp;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;
	struct mbuf m;

	init_station(&sc, &ifp, &ni, &chan);
	make_tx_frame(&m, &ni, true);
	tx_crypto_fail = true;
	if (rtw89_tx(&sc, &m, &ni, 0) != ENOBUFS || tx_crypto_calls != 1 ||
	    tx_copy_calls != 0 || tx_bridge_calls != 0) {
		fprintf(stderr, "crypto failure reached TX bridge\n");
		return 1;
	}
	reset_counts();
	make_tx_frame(&m, &ni, false);
	m.m_pkthdr.len = RTW89_TXBUF_SIZE + 1;
	if (rtw89_tx(&sc, &m, &ni, 0) != EMSGSIZE || tx_crypto_calls != 0 ||
	    tx_copy_calls != 0 || tx_bridge_calls != 0) {
		fprintf(stderr, "oversized TX was copied into driver buffer\n");
		return 1;
	}
	return 0;
}

static int
test_unprotected_tx_preserves_legacy_bridge_result(void)
{
	struct rtw89_softc sc;
	struct ifnet ifp;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;
	struct mbuf m;

	init_station(&sc, &ifp, &ni, &chan);
	make_tx_frame(&m, &ni, false);
	bridge_result = EIO;
	if (rtw89_tx(&sc, &m, &ni, 3) != EIO || tx_gate_calls != 1 ||
	    tx_crypto_calls != 0 || tx_copy_calls != 1 || tx_bridge_calls != 1 ||
	    bridge_len != 32 || bridge_flags != 3 ||
	    memcmp(bridge_bytes, m.m_data, 32) != 0) {
		fprintf(stderr, "unprotected TX changed bytes or bridge result\n");
		return 1;
	}
	return 0;
}

static int
test_quiet_retires_security_before_hardware_quiet(void)
{
	struct rtw89_softc sc;
	struct ifnet ifp;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;

	init_station(&sc, &ifp, &ni, &chan);
	rtw89_sta_quiet(&sc.sc_ic);
	if (sae_retire_calls != 1 || retire_seen_at_quiet != 1 ||
	    quiet_calls != 1 || !sc.sc_txstopped) {
		fprintf(stderr, "quiet did not retire security before pausing TX\n");
		return 1;
	}
	/* Retirement must not be skipped by an already-quiet early return. */
	rtw89_sta_quiet(&sc.sc_ic);
	if (sae_retire_calls != 2 || quiet_calls != 1) {
		fprintf(stderr, "repeated quiet skipped security retirement\n");
		return 1;
	}
	return 0;
}

static int
test_state_request_reaches_security_before_deferred_work(void)
{
	struct rtw89_softc sc;
	struct ifnet ifp;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;

	init_station(&sc, &ifp, &ni, &chan);
	/* CSA rewrites AUTH into deferred INIT; owner must see requested AUTH. */
	sc.sc_csa_init_pending = 1;
	(void)rtw89_newstate(&sc.sc_ic, IEEE80211_S_AUTH, 7);
	if (sae_state_requests != 1 || !sae_requested_context_valid ||
	    sae_requested_state != IEEE80211_S_AUTH ||
	    requests_seen_at_callout != 1 || requests_seen_at_enqueue != 1 ||
	    pending_work != 1 ||
	    sc.sc_ic.ic_state != IEEE80211_S_RUN ||
	    sc.sc_nsstate != IEEE80211_S_INIT || !sc.sc_csa_deferred) {
		fprintf(stderr, "state owner notified late or with rewritten state\n");
		return 1;
	}
	/* A queued worker cannot delay an INIT request's security notification. */
	(void)rtw89_newstate(&sc.sc_ic, IEEE80211_S_INIT, -1);
	if (sae_state_requests != 2 || sae_requested_state != IEEE80211_S_INIT ||
	    pending_work != 1 || sc.sc_ic.ic_state != IEEE80211_S_RUN) {
		fprintf(stderr, "coalesced INIT omitted immediate security notification\n");
		return 1;
	}
	return 0;
}

static int
test_stop_retires_security_even_without_bridge(void)
{
	struct rtw89_softc sc;
	struct ifnet ifp;
	struct ieee80211_node ni;
	struct ieee80211_channel chan;

	init_station(&sc, &ifp, &ni, &chan);
	sc.sc_nb = NULL;
	rtw89_stop(&ifp, 0);
	if (sae_retire_calls != 1 || restart_stop_calls != 0) {
		fprintf(stderr, "stop without hardware bridge skipped security retirement\n");
		return 1;
	}
	sc.sc_nb = &test_nb;
	rtw89_stop(&ifp, 0);
	if (sae_retire_calls != 2 || retire_seen_at_stop != 2 ||
	    sc.sc_running || !sc.sc_txstopped || pending_work != 0 ||
	    sc.sc_ic.ic_state != IEEE80211_S_INIT || restart_stop_calls != 1) {
		fprintf(stderr, "stop left security or queued state work live\n");
		return 1;
	}
	return 0;
}

static int
test_queued_state_is_bound_to_request_serial(void)
{
	const enum ieee80211_state requested[] = {IEEE80211_S_AUTH,
	    IEEE80211_S_RUN};

	for (unsigned i = 0; i < sizeof(requested) / sizeof(requested[0]); i++) {
		struct rtw89_softc sc;
		struct ifnet ifp;
		struct ieee80211_node ni;
		struct ieee80211_channel chan;
		enum ieee80211_state original = i == 0 ? IEEE80211_S_SCAN :
		    IEEE80211_S_ASSOC;

		reset_counts();
		init_station(&sc, &ifp, &ni, &chan);
		sc.sc_ic.ic_state = original;
		sc.sc_ic.test_sae_serial = 41;
		(void)rtw89_newstate(&sc.sc_ic, requested[i], -1);
		/* Reconfigure before the queued worker begins; no new MLME yet. */
		sc.sc_ic.test_sae_serial = 42;
		if (run_state_work(&sc) != 1 || parent_state_calls != 0 ||
		    join_calls != 0 || assoc_calls != 0 ||
		    sc.sc_ic.test_sae_serial != 42 || sc.sc_ic.ic_state != original ||
		    pending_work != 0) {
			fprintf(stderr, "queued stale state %u crossed request generation\n",
			    (unsigned)requested[i]);
			return 1;
		}
	}
	return 0;
}

static int
test_sleeping_state_worker_preserves_replacement_generation(void)
{
	const enum ieee80211_state requested[] = {IEEE80211_S_AUTH,
	    IEEE80211_S_RUN};

	for (unsigned i = 0; i < sizeof(requested) / sizeof(requested[0]); i++) {
		for (unsigned queued = 0; queued < 2; queued++) {
			struct rtw89_softc sc;
			struct ifnet ifp;
			struct ieee80211_node ni;
			struct ieee80211_channel chan;

			reset_counts();
			init_station(&sc, &ifp, &ni, &chan);
			sc.sc_ic.ic_state = i == 0 ? IEEE80211_S_SCAN :
			    IEEE80211_S_ASSOC;
			sc.sc_ic.test_sae_serial = 51;
			bridge_transition = queued ? BRIDGE_RECONFIGURE_QUEUE_AUTH :
			    BRIDGE_RECONFIGURE;
			(void)rtw89_newstate(&sc.sc_ic, requested[i], -1);
			assert(pending_work == 1);
			pending_work--;
			rtw89_newstate_task(&sc.sc_nswk, &sc);
			if (parent_state_calls != 0 || sc.sc_ic.test_sae_serial != 52 ||
			    sc.sc_ic.ic_state != IEEE80211_S_SCAN ||
			    join_calls != (i == 0 ? 1U : 0U) ||
			    assoc_calls != (i == 1 ? 1U : 0U) ||
			    pending_work != queued) {
				fprintf(stderr, "sleeping stale state %u published into replacement\n",
				    (unsigned)requested[i]);
				return 1;
			}
			if (queued && (sc.sc_nsstate != IEEE80211_S_AUTH ||
			    run_state_work(&sc) != 1 || parent_state_calls != 1 ||
			    parent_last_state != IEEE80211_S_AUTH ||
			    sc.sc_ic.test_sae_serial != 52 ||
			    sc.sc_ic.ic_state != IEEE80211_S_AUTH || pending_work != 0)) {
				fprintf(stderr, "stale worker lost replacement AUTH work\n");
				return 1;
			}
		}
	}
	return 0;
}

static int
test_rejected_parent_state_has_no_success_side_effects(void)
{
	const enum ieee80211_state requested[] = {IEEE80211_S_RUN,
	    IEEE80211_S_RUN, IEEE80211_S_RUN, IEEE80211_S_INIT};
	const enum ieee80211_state original[] = {IEEE80211_S_ASSOC,
	    IEEE80211_S_SCAN, IEEE80211_S_ASSOC, IEEE80211_S_SCAN};
	int failures = 0;

	for (unsigned i = 0; i < sizeof(requested) / sizeof(requested[0]); i++) {
		struct rtw89_softc sc;
		struct ieee80211_node ni;
		struct ieee80211_channel chan;
		bool queued = i == 2, csa = i == 3;

		reset_counts();
		init_station(&sc, &sc.sc_ec.ec_if, &ni, &chan);
		sc.sc_ic.ic_state = original[i];
		sc.sc_ic.test_sae_serial = 61;
		sc.sc_ec.ec_if.if_flags = IFF_RUNNING;
		if (csa) {
			sc.sc_csa_quiet = true;
			sc.sc_csa_init_pending = 1;
			sc.sc_csa_deferred = true;
			sc.sc_csa_deferred_state = IEEE80211_S_AUTH;
			sc.sc_csa_deferred_arg = 7;
		}
		parent_state_error = EACCES;
		parent_queue_auth = queued;
		(void)rtw89_newstate(&sc.sc_ic, requested[i], -1);
		assert(pending_work == 1);
		pending_work--;
		rtw89_newstate_task(&sc.sc_nswk, &sc);
		if (parent_state_calls != 1 || parent_last_state != requested[i] ||
		    sc.sc_ic.test_sae_serial != 62 ||
		    sc.sc_ic.ic_state != (queued ? IEEE80211_S_SCAN : original[i]) ||
		    sc.sc_beacon_active || sc.sc_ec.ec_if.if_timer != 0 ||
		    scan_schedule_calls != 0 || pending_work != (unsigned)queued ||
		    sc.sc_nspending != queued || !sc.sc_running || sc.sc_stopping ||
		    sc.sc_ec.ec_if.if_flags != IFF_RUNNING ||
		    restart_stop_calls != 0 || restart_start_calls != 0 ||
		    (csa && (sc.sc_csa_init_pending != 1 ||
		    !sc.sc_csa_deferred || !sc.sc_csa_quiet))) {
			fprintf(stderr, "rejected parent state variant %u ran success cleanup\n", i);
			failures++;
		}
		if (queued && (sc.sc_nsstate != IEEE80211_S_AUTH ||
		    sc.sc_nsserial != 62 || run_state_work(&sc) != 1 ||
		    parent_state_calls != 2 || parent_last_state != IEEE80211_S_AUTH ||
		    sc.sc_ic.ic_state != IEEE80211_S_AUTH || pending_work != 0)) {
			fprintf(stderr, "parent rejection lost replacement AUTH work\n");
			failures++;
		}
	}
	return failures != 0;
}

int
main(void)
{
	int failures = 0;

	reset_counts();
	failures += test_idle_run_keeps_watchdog_armed();
	reset_counts();
	failures += test_init_state_does_not_arm_station_supervision();
	reset_counts();
	failures += test_beacon_observation_is_bound_to_current_ap();
	reset_counts();
	failures += test_healthy_idle_beacons_prevent_false_miss();
	reset_counts();
	failures += test_missing_beacons_trigger_net80211_recovery();
	reset_counts();
	failures += test_beacon_deadline_handles_tick_wrap();
	reset_counts();
	failures += test_csa_quiet_survives_queue_wake_until_scan();
	reset_counts();
	failures += test_csa_init_cannot_be_coalesced_away();
	reset_counts();
	failures += test_csa_restart_resets_dma_before_resume();
	reset_counts();
	failures += test_csa_only_init_recovers_to_scan();
	reset_counts();
	failures += test_stop_retires_csa_without_default_scan();
	reset_counts();
	failures += test_quiet_and_stop_retire_beacon_supervision();
	reset_counts();
	failures += test_truncated_beacons_respect_memory_boundary();
	reset_counts();
	failures += test_tx_stale_gate_precedes_crypto_and_bridge();
	reset_counts();
	failures += test_tx_encrypts_once_then_submits_ciphertext();
	reset_counts();
	failures += test_tx_crypto_failure_and_size_limit_fail_closed();
	reset_counts();
	failures += test_unprotected_tx_preserves_legacy_bridge_result();
	reset_counts();
	failures += test_quiet_retires_security_before_hardware_quiet();
	reset_counts();
	failures += test_state_request_reaches_security_before_deferred_work();
	reset_counts();
	failures += test_stop_retires_security_even_without_bridge();
	reset_counts();
	failures += test_queued_state_is_bound_to_request_serial();
	reset_counts();
	failures += test_sleeping_state_worker_preserves_replacement_generation();
	reset_counts();
	failures += test_rejected_parent_state_has_no_success_side_effects();
	printf("rtw89 idle link-loss/security contract: 23 cases, %d failures\n", failures);
	return failures != 0;
}
