/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: RTL8852BE idle station supervision contract, AI-assisted. */

#define _NETBSD_SOURCE 1
#define _DEFAULT_SOURCE 1

#include <sys/mman.h>
#include <unistd.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define IEEE80211_ADDR_LEN 6
#define IEEE80211_SEQ_FRAG_MASK 0x0f
#define IEEE80211_FC0_SUBTYPE_BEACON 0x80
#define IEEE80211_BINTVAL_MIN 25
#define IEEE80211_BINTVAL_MAX 1000
#define IEEE80211_M_STA 1
#define RTW89_SCAN_DWELL_MS 200
#define RTW89_MAC_0 0
#define RTW89_SCH_TX_SEL_ALL 0
#define ENXIO 6
#define EIO 5
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

struct work { unsigned int opaque; };
struct workqueue { unsigned int opaque; };
struct callout { unsigned int opaque; };
struct mutex { unsigned int opaque; };
struct rtw89_nb { unsigned int opaque; };
struct rtw89_nb_bss { unsigned int opaque; };

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
	int sc_nsarg;
	struct mutex sc_nslock;
	struct workqueue *sc_nswq;
	struct work sc_nswk;
	struct callout sc_scan_to;
	bool sc_scanning;
	void *sc_tx_sih;
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
}

static void
callout_stop(struct callout *co)
{
	(void)co;
}

static void
callout_schedule(struct callout *co, int ticks)
{
	(void)co;
	(void)ticks;
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
	(void)sc;
	(void)bss;
}

static int
rtw89_nb_bss_join(struct rtw89_nb *nb, const struct rtw89_nb_bss *bss)
{
	(void)nb;
	(void)bss;
	return 0;
}

static int
rtw89_nb_bss_assoc(struct rtw89_nb *nb, const struct rtw89_nb_bss *bss)
{
	(void)nb;
	(void)bss;
	return 0;
}

static int
test_net80211_newstate(struct ieee80211com *ic,
    enum ieee80211_state nstate, int arg)
{
	(void)arg;
	event(100 + nstate);
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
	printf("rtw89 idle link-loss contract: 13 cases, %d failures\n", failures);
	return failures != 0;
}
