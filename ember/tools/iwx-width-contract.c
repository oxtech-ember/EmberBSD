/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD firmware width-order regression, AI-assisted (OpenAI Codex). */
/*
 * The generator inserts unmodified driver functions and firmware wire types.
 * Kernel types below provide only the fields those functions access. Transport
 * doubles observe firmware commands; this is not a kernel or hardware test.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "if_iwx_ht.h"

#ifndef __packed
#define __packed __attribute__((__packed__))
#endif
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define htole16(x) __builtin_bswap16((uint16_t)(x))
#else
#ifndef htole16
#define htole16(x) ((uint16_t)(x))
#endif
#endif

/* WIRE_TYPES */

#define IEEE80211_RATE_VAL 0x7f
#define IEEE80211_MAX_LEN 2300
#define IEEE80211_HTOP0_SCO_SCN 0
#define IEEE80211_VHTOP0_CHAN_WIDTH_HT 0
#define IEEE80211_VHTOP0_CHAN_WIDTH_80 1
#define IEEE80211_M_STA 1
#define IEEE80211_M_MONITOR 8
#define IWX_STATION_ID 0
#define IWX_TLC_MNG_CONFIG_CMD 0x0f
#define IWX_DATA_PATH_GROUP 5
#define IWX_CMD_ASYNC 1
#define IWX_SF_INIT_OFF 0
#define IWX_FW_CTXT_ACTION_MODIFY 2
#define IWX_FW_CTXT_ACTION_ADD 1
#define IWX_FW_CTXT_ACTION_REMOVE 3
#define IWX_FLAG_STA_ACTIVE 1
#define IWX_FLAG_BINDING_ACTIVE 2
#define IWX_FLAG_MAC_ACTIVE 4
#define IWX_FLAG_PHY_ACTIVE 8
#define IWX_FLAG_SHUTDOWN 16
#define IWX_FLAG_SCANNING 32
#define IWX_FLAG_BGSCAN 64
#define IWX_RLC_CONFIG_CMD 0x4e
#define IWX_LINK_CONTEXT_MODIFY_ACTIVE 1
#define IWX_LINK_CONTEXT_MODIFY_RATES_INFO 2
#define IWX_MONITOR_STA_ID 1
#define IWX_DQA_INJECT_MONITOR_QUEUE 1
#define IWX_MGMT_TID 8
#define IWX_TX_RING_COUNT 256
#define IWX_SCAN_ASSOCIATED 2
#define IWX_SCAN_ASSOC_TIMEOUT 30
#define IEEE80211_IOC_WPA 26
#define IEEE80211_IOC_SCAN_REQ 23
#define IEEE80211_ROAMING_AUTO 1
#define IEEE80211_ROAMING_MANUAL 2
#define KAUTH_NETWORK_INTERFACE 0
#define KAUTH_REQ_NETWORK_INTERFACE_SETPRIV 0
#define kauth_cred_get() 0
#define IC2IFP(ic) ((void)(ic), (void *)0)
#define u_long unsigned long
#define IPL_NET 0
#define splassert(ipl) ((void)(ipl))
#define device_xname(dev) ((void)(dev), "iwx-test")
#define IEEE80211_ADDR_COPY(dst, src) memcpy((dst), (src), 6)

enum ieee80211_state {
	IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_AUTH,
	IEEE80211_S_ASSOC, IEEE80211_S_RUN
};

struct ieee80211_rateset { int rs_nrates; uint8_t rs_rates[16]; };
struct ieee80211_channel { int ic_freq; };
struct ieee80211_node {
	struct ieee80211_rateset ni_rates;
	void *ni_chan;
	uint8_t ni_macaddr[6];
	unsigned int ni_intval;
};
struct iwx_phy_ctxt { int sco, vht_chan_width; void *channel; };
struct iwx_node {
	struct ieee80211_node in_ni;
	struct iwx_phy_ctxt *in_phyctxt;
	int in_ht;
	struct iwx_ht_tlc in_htt;
	struct iwx_ht_chan in_htc;
	uint8_t in_macaddr[6];
	int in_id, in_color, in_flags, dup_data;
};
struct ieee80211req { int i_type; };
struct ieee80211com {
	void *ic_bss, *ic_ibss_chan;
	int ic_opmode, ic_roaming;
	enum ieee80211_state ic_state;
	void (*ic_node_cleanup)(struct ieee80211_node *);
};
struct iwx_softc {
	struct ieee80211com sc_ic;
	void *sc_dev;
	int sc_flags, sc_generation, sc_bss_ht_valid, task_refs;
	uint32_t sc_bmiss_level;
	int sc_systq, init_task, ns_arg;
	int sc_csa_valid, sc_csa_wait, sc_scan_assoc_to, scan_assoc_task;
	struct iwx_node *sc_fw_in;
	uint8_t sc_ba_start, sc_ba_stop, sc_txq_want;
	int sc_agg_busy;
	uint64_t sc_ns_sae_serial;
	enum ieee80211_state ns_nstate;
	struct iwx_phy_ctxt sc_phyctxt[1];
	int (*sc_newstate)(struct ieee80211com *, enum ieee80211_state, int);
};

static unsigned int checks, failures, tlc_count, phy_count, unsafe_width;
static int command_version, tlc_error, phy_error, flush_error, firmware_width;
static int sent_width, sent_mode, sequence, tlc_sequence, phy_sequence;
static int physical_width, remove_sequence, phy_remove_sequence, phy_add_sequence;
static int binding_remove_sequence, mac_remove_sequence, disassoc_sequence;
static int remove_error, phy_add_error, recovery_count, task_releases;
/* Firmware commands seen while a channel switch is followed. */
enum { EV_LINK_OFF = 1, EV_PHY_REMOVE, EV_PHY_ADD, EV_RLC, EV_LINK_RATES,
	EV_TLC, EV_LINK_ON };
static int csa_mode, csa_events[16], csa_nevents, csa_fail_at, rlc_version;
static int csa_added_sco, csa_added_width;
static void *csa_removed_channel, *csa_added_channel;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static int
iwx_lookup_cmd_ver(struct iwx_softc *sc, int group, int cmd)
{
	(void)sc; (void)group;
	if (cmd == IWX_RLC_CONFIG_CMD)
		return rlc_version;
	return command_version;
}

/* Record one command; the csa_fail_at'th command fails. */
static int
csa_event(int event)
{
	if (csa_nevents < (int)(sizeof(csa_events) / sizeof(csa_events[0])))
		csa_events[csa_nevents] = event;
	csa_nevents++;
	return csa_nevents == csa_fail_at ? EIO : 0;
}

static int
iwx_mld_link_modify(struct iwx_softc *sc, struct iwx_node *in, int changes,
    int active)
{
	(void)sc; (void)in;
	if (changes == IWX_LINK_CONTEXT_MODIFY_ACTIVE)
		return csa_event(active ? EV_LINK_ON : EV_LINK_OFF);
	CHECK(changes == IWX_LINK_CONTEXT_MODIFY_RATES_INFO && active == 0);
	return csa_event(EV_LINK_RATES);
}

static int
iwx_rs_rval2idx(uint8_t rate)
{
	return rate == 12 ? 0 : -1;
}

static int
iwx_mimo_enabled(struct iwx_softc *sc)
{
	(void)sc;
	return 1;
}

static uint32_t
iwx_cmd_id(int cmd, int group, int version)
{
	return (uint32_t)(cmd | group << 8 | version << 16);
}

static int
iwx_send_cmd_pdu(struct iwx_softc *sc, uint32_t id, uint32_t flags,
    uint16_t len, const void *data)
{
	(void)sc;
	CHECK(id == 0x050f && flags == IWX_CMD_ASYNC);
	if (csa_mode)
		return csa_event(EV_TLC);
	if (command_version == 4) {
		const struct iwx_tlc_config_cmd_v4 *cfg = data;
		CHECK(len == sizeof(*cfg));
		sent_width = cfg->max_ch_width;
		sent_mode = cfg->mode;
	} else {
		const struct iwx_tlc_config_cmd_v3 *cfg = data;
		CHECK(len == sizeof(*cfg));
		sent_width = cfg->max_ch_width;
		sent_mode = cfg->mode;
	}
	tlc_count++;
	tlc_sequence = ++sequence;
	if (tlc_error)
		return tlc_error;
	if (sent_width > physical_width)
		unsafe_width++;
	firmware_width = sent_width;
	return 0;
}

static int
iwx_phy_ctxt_update(struct iwx_softc *sc, struct iwx_phy_ctxt *phy,
    void *channel, int chains, int dynamic, int time, int sco, int width)
{
	(void)sc; (void)channel;
	CHECK(chains == dynamic && time == 0);
	phy_count++;
	phy_sequence = ++sequence;
	physical_width = width ? 2 : sco ? 1 : 0;
	if (firmware_width > physical_width)
		unsafe_width++;
	if (phy_error)
		return phy_error;
	phy->sco = sco;
	phy->vht_chan_width = width;
	return 0;
}

/* Sleeping for the aggregation tasks: the sleep lets them finish. */
#define SEC_TO_NSEC(s) ((s) * 1000000000ULL)
static int agg_sleeps, agg_busy_at_flush = -1;
static struct iwx_softc *agg_sc;
static int
tsleep_nsec(void *ident, int prio, const char *msg, unsigned long long ns)
{
	(void)ident; (void)prio; (void)msg; (void)ns;
	/* The task records the session it was adding, then finishes. */
	if (++agg_sleeps >= 2 && agg_sc != NULL)
		agg_sc->sc_agg_busy = 0;
	return 0;
}

/* The node each teardown command was given. */
static struct iwx_node *flush_in, *disassoc_in, *rm_in, *binding_in, *mac_in;
static int iwx_flush_sta(struct iwx_softc *sc, struct iwx_node *in)
{ agg_busy_at_flush = sc->sc_agg_busy; flush_in = in; return flush_error; }
static void iwx_ba_stop(struct iwx_softc *sc, int tid, int force)
{ (void)sc; (void)tid; (void)force; }
static int iwx_sf_config(struct iwx_softc *sc, int state)
{ (void)sc; (void)state; return 0; }
static int iwx_disable_beacon_filter(struct iwx_softc *sc)
{ (void)sc; return 0; }
static int iwx_mac_ctxt_cmd(struct iwx_softc *sc, struct iwx_node *in,
    int action, int assoc)
{
	(void)sc; (void)assoc;
	if (action == IWX_FW_CTXT_ACTION_MODIFY) {
		disassoc_sequence = ++sequence;
		disassoc_in = in;
	} else if (action == IWX_FW_CTXT_ACTION_REMOVE) {
		mac_remove_sequence = ++sequence;
		mac_in = in;
	}
	return 0;
}

static int
iwx_rm_sta(struct iwx_softc *sc, struct iwx_node *in)
{
	(void)sc;
	rm_in = in;
	remove_sequence = ++sequence;
	return remove_error;
}

static int
iwx_phy_ctxt_cmd(struct iwx_softc *sc, struct iwx_phy_ctxt *phy,
    int chains, int dynamic, int action, int time, int sco, int width)
{
	if (csa_mode) {
		/* The station stays while the PHY context is replaced. */
		CHECK(chains == dynamic && time == 0);
		CHECK(sc->sc_flags & IWX_FLAG_STA_ACTIVE);
		if (action == IWX_FW_CTXT_ACTION_REMOVE) {
			csa_removed_channel = phy->channel;
			return csa_event(EV_PHY_REMOVE);
		}
		CHECK(action == IWX_FW_CTXT_ACTION_ADD);
		csa_added_channel = phy->channel;
		csa_added_sco = sco;
		csa_added_width = width;
		return csa_event(EV_PHY_ADD);
	}
	(void)chains; (void)dynamic; (void)time;
	CHECK((sc->sc_flags & IWX_FLAG_STA_ACTIVE) == 0);
	CHECK(sco == 0 && width == 0);
	if (action == IWX_FW_CTXT_ACTION_REMOVE) {
		phy_remove_sequence = ++sequence;
	} else {
		CHECK(action == IWX_FW_CTXT_ACTION_ADD);
		CHECK((sc->sc_flags & IWX_FLAG_PHY_ACTIVE) == 0);
		phy_add_sequence = ++sequence;
		if (phy_add_error)
			return phy_add_error;
	}
	phy->sco = sco;
	phy->vht_chan_width = width;
	physical_width = 0;
	return 0;
}

static int iwx_binding_cmd(struct iwx_softc *sc, struct iwx_node *in, int action)
{
	(void)sc;
	if (action == IWX_FW_CTXT_ACTION_REMOVE) {
		binding_remove_sequence = ++sequence;
		binding_in = in;
	}
	return 0;
}
static void iwx_ba_reset(struct iwx_softc *sc, int hw)
{ (void)sc; (void)hw; }
static void iwx_csa_clear(struct iwx_softc *sc) { (void)sc; }
static void iwx_unprotect_session(struct iwx_softc *sc, struct iwx_node *in)
{ (void)sc; (void)in; }
static int iwx_rm_sta_cmd(struct iwx_softc *sc, struct iwx_node *in)
{ return iwx_rm_sta(sc, in); }
static int iwx_phy_send_rlc(struct iwx_softc *sc, struct iwx_phy_ctxt *phy,
    int chains, int dynamic)
{ (void)sc; (void)phy; (void)chains; (void)dynamic;
  return csa_mode ? csa_event(EV_RLC) : 0; }
static int iwx_enable_txq(struct iwx_softc *sc, int sta, int queue, int tid, int size)
{ (void)sc; (void)sta; (void)queue; (void)tid; (void)size; return 0; }
static int iwx_enable_mgmt_queue(struct iwx_softc *sc)
{ (void)sc; return 0; }
static void iwx_disable_mgmt_queue(struct iwx_softc *sc)
{ (void)sc; }
static int iwx_clear_statistics(struct iwx_softc *sc)
{ (void)sc; return 0; }
static int iwx_schedule_session_protection(struct iwx_softc *sc,
    struct iwx_node *in, uint32_t duration)
{ (void)sc; (void)in; (void)duration; return 0; }
static int iwx_add_sta_cmd(struct iwx_softc *sc, struct iwx_node *in, int update)
{ (void)sc; (void)in; (void)update; return 0; }
static int splnet(void) { return 0; }
static void splx(int s) { (void)s; }
static void refcnt_rele_wake(int *refs) { (void)refs; task_releases++; }
static void task_add(int queue, int *task)
{ (void)queue; (void)task; recovery_count++; }
static int iwx_scan(struct iwx_softc *sc) { (void)sc; return 0; }
static int iwx_run(struct iwx_softc *sc) { (void)sc; return 0; }
/* This existing width fixture models a station without a native SAE owner. */
static uint64_t ieee80211_sae_serial(struct ieee80211com *ic)
{ (void)ic; return 0; }
static void ieee80211_sae_retire(struct ieee80211com *ic) { (void)ic; }

static enum ieee80211_state newstate_from;
static int
record_newstate(struct ieee80211com *ic, enum ieee80211_state state, int arg)
{
	(void)arg;
	newstate_from = ic->ic_state;
	ic->ic_state = state;
	return 0;
}

/* Node references and the cleanup net80211 does for a BSS that is left. */
static int node_refs, node_frees, node_cleanups, sae_active;
static struct ieee80211_node *node_cleaned;
static struct ieee80211_node *
ieee80211_ref_node(struct ieee80211_node *ni) { node_refs++; return ni; }
static void ieee80211_free_node(struct ieee80211_node *ni)
{ (void)ni; node_frees++; }
static void record_cleanup(struct ieee80211_node *ni)
{ node_cleaned = ni; node_cleanups++; }
static int ieee80211_sae_active(struct ieee80211com *ic)
{ (void)ic; return sae_active; }
void iwx_bss_own(struct iwx_softc *, struct iwx_node *);
struct iwx_node *iwx_bss_owner(struct iwx_softc *);
int iwx_bss_joined(struct iwx_softc *);
void iwx_bss_disown(struct iwx_softc *, int);

/* A scan while associated: what the driver asks of its surroundings. */
static int scan_mode, scan_error, scan_starts, scan_aborts, scan_notified;
static int scan_ended, scan_timer, kauth_error, kauth_calls;
static int scan_flags_at_send, scan_timer_at_send;
static int
iwx_initiate_scan(struct iwx_softc *sc, int bgscan)
{
	scan_mode = bgscan;
	scan_starts++;
	/* The command sleeps: what the receive path sees meanwhile. */
	scan_flags_at_send = sc->sc_flags;
	scan_timer_at_send = scan_timer;
	return scan_error;
}
static int iwx_umac_scan_abort(struct iwx_softc *sc)
{ (void)sc; scan_aborts++; return 0; }
static void ieee80211_notify_scan_done(struct ieee80211com *ic)
{ (void)ic; scan_notified++; }
static void ieee80211_end_scan(struct ieee80211com *ic)
{ (void)ic; scan_ended++; }
static void timeout_add_sec(int *to, int sec)
{ (void)to; scan_timer = sec; }
static void timeout_del(int *to) { (void)to; scan_timer = 0; }
static int timeout_pending(int *to) { (void)to; return scan_timer != 0; }
static int
kauth_authorize_network(int cred, int action, int req, void *ifp, void *cmd,
    void *arg)
{
	(void)cred; (void)action; (void)req; (void)ifp; (void)cmd; (void)arg;
	kauth_calls++;
	return kauth_error;
}
void iwx_endscan(struct iwx_softc *);

/* DRIVER_FUNCTIONS */

static void
reset_transport(int version, int width)
{
	command_version = version;
	firmware_width = physical_width = width;
	tlc_error = phy_error = flush_error = remove_error = phy_add_error = 0;
	tlc_count = phy_count = unsafe_width = 0;
	sequence = tlc_sequence = phy_sequence = 0;
	remove_sequence = phy_remove_sequence = phy_add_sequence = 0;
	binding_remove_sequence = mac_remove_sequence = disassoc_sequence = 0;
	recovery_count = task_releases = 0;
	sent_width = sent_mode = -1;
}

static void
setup(struct iwx_softc *sc, struct iwx_node *in, int width, int monitor)
{
	memset(sc, 0, sizeof(*sc));
	memset(in, 0, sizeof(*in));
	sc->sc_ic.ic_bss = in;
	sc->sc_ic.ic_opmode = monitor ? IEEE80211_M_MONITOR : IEEE80211_M_STA;
	sc->sc_ic.ic_state = IEEE80211_S_RUN;
	sc->sc_newstate = record_newstate;
	sc->sc_ic.ic_node_cleanup = record_cleanup;
	sc->sc_fw_in = in;
	node_refs = node_frees = node_cleanups = sae_active = 0;
	node_cleaned = NULL;
	sc->sc_flags = IWX_FLAG_STA_ACTIVE | IWX_FLAG_BINDING_ACTIVE |
	    IWX_FLAG_MAC_ACTIVE | IWX_FLAG_PHY_ACTIVE;
	sc->sc_phyctxt[0].sco = width == 1 ? 1 : 0;
	sc->sc_phyctxt[0].vht_chan_width = width == 2 ? 1 : 0;
	in->in_phyctxt = &sc->sc_phyctxt[0];
	in->in_ht = 1;
	in->in_ni.ni_rates.rs_nrates = 1;
	in->in_ni.ni_rates.rs_rates[0] = 12;
	in->in_htt.width = (uint8_t)width;
	in->in_htt.mode = IWX_HT_TLC_MODE_VHT;
}

/* RUN stop must not reconfigure a PHY while the old STA still exists. */
static void
test_stop(int width, int monitor, int flush_fail)
{
	struct iwx_softc sc;
	struct iwx_node in;
	int result;

	setup(&sc, &in, width, monitor);
	reset_transport(4, width);
	flush_error = flush_fail;
	result = iwx_run_stop(&sc);
	CHECK(result == flush_fail);
	CHECK(tlc_count == 0 && phy_count == 0);
	CHECK(in.in_ht == 1);
	CHECK(physical_width == width);
	CHECK(sc.sc_phyctxt[0].sco == (width == 1 ? 1 : 0));
	CHECK(sc.sc_phyctxt[0].vht_chan_width == (width == 2 ? 1 : 0));
	CHECK((disassoc_sequence > 0) == (flush_fail == 0));
}

/*
 * The teardown does not overtake the tasks that set up Block Ack sessions
 * and aggregation queues: a session added behind its back would outlive
 * the station in the firmware.
 */
static void
test_stop_waits(void)
{
	struct iwx_softc sc;
	struct iwx_node in;

	setup(&sc, &in, 2, 0);
	reset_transport(4, 2);
	agg_sc = &sc;
	agg_sleeps = 0;
	agg_busy_at_flush = -1;
	sc.sc_agg_busy = 1;
	sc.sc_ba_start = 0x21;
	sc.sc_ba_stop = 0x80;
	sc.sc_txq_want = 0x01;
	CHECK(iwx_run_stop(&sc) == 0);
	/* Nothing more is started, and the task in flight was waited for. */
	CHECK(sc.sc_ba_start == 0 && sc.sc_ba_stop == 0 && sc.sc_txq_want == 0);
	CHECK(agg_sleeps == 2 && agg_busy_at_flush == 0);
	CHECK(disassoc_sequence > 0);
	/* No task in flight: no sleep. */
	setup(&sc, &in, 2, 0);
	reset_transport(4, 2);
	agg_sleeps = 0;
	CHECK(iwx_run_stop(&sc) == 0 && agg_sleeps == 0);
	agg_sc = NULL;
}

/*
 * Ways out of RUN.  `join' makes another node ic_bss, as net80211 does
 * when it joins another BSS.  A join is taken down in place like any
 * other way out, through the node the firmware contexts belong to,
 * unless native SAE owns the keys; a scan in flight always restarts the
 * device.
 */
static void
test_leave(enum ieee80211_state next, int arg, int bgscan, int join, int sae,
    int restart)
{
	struct iwx_softc sc;
	struct iwx_node in, other;

	setup(&sc, &in, 2, 0);
	reset_transport(4, 2);
	flush_in = disassoc_in = rm_in = binding_in = mac_in = NULL;
	newstate_from = IEEE80211_S_INIT;
	sc.ns_nstate = next;
	sc.ns_arg = arg;
	sae_active = sae;
	if (bgscan)
		sc.sc_flags |= IWX_FLAG_BGSCAN;
	CHECK(iwx_bss_owner(&sc) == &in && !iwx_bss_joined(&sc));
	if (join) {
		memset(&other, 0, sizeof(other));
		sc.sc_ic.ic_bss = &other;
		/* The contexts still belong to the BSS being left. */
		CHECK(iwx_bss_owner(&sc) == &in && iwx_bss_joined(&sc));
	}
	iwx_newstate_task(&sc);
	CHECK(task_releases == 1);
	if (restart) {
		CHECK(recovery_count == 1);
		CHECK(disassoc_sequence == 0 && remove_sequence == 0);
		CHECK(binding_remove_sequence == 0 && mac_remove_sequence == 0);
		CHECK(phy_remove_sequence == 0 && phy_add_sequence == 0);
		CHECK(sc.sc_flags & IWX_FLAG_STA_ACTIVE);
		CHECK(node_cleanups == 0 && sc.sc_fw_in == &in);
		CHECK(in.in_phyctxt == &sc.sc_phyctxt[0] && in.in_ht == 1);
		return;
	}
	CHECK(recovery_count == 0);
	/* The association is taken down through the node that owns it. */
	CHECK(disassoc_sequence > 0);
	CHECK(flush_in == &in && disassoc_in == &in);
	if (next == IEEE80211_S_ASSOC) {
		/* The contexts stay, and so does their owner. */
		CHECK(remove_sequence == 0 && sc.sc_fw_in == &in);
		CHECK(node_cleanups == 0 && node_frees == 0);
		return;
	}
	CHECK(remove_sequence > disassoc_sequence);
	CHECK(binding_remove_sequence > remove_sequence);
	CHECK(mac_remove_sequence > binding_remove_sequence);
	CHECK(phy_remove_sequence > mac_remove_sequence);
	CHECK(rm_in == &in && binding_in == &in && mac_in == &in);
	/* Its node then owns nothing. */
	CHECK(node_frees == 1);
	CHECK(in.in_phyctxt == NULL || sc.sc_fw_in == &in);
	/* Only a BSS left by a join needs the net80211 cleanup here. */
	CHECK(node_cleanups == (join ? 1 : 0));
	if (join)
		CHECK(node_cleaned == &in.in_ni);
	if (next != IEEE80211_S_AUTH) {
		CHECK(sc.sc_fw_in == NULL && phy_add_sequence == 0);
		return;
	}
	/* The BSS to authenticate with gets contexts of its own. */
	CHECK(phy_add_sequence > phy_remove_sequence);
	CHECK(sc.sc_fw_in == (join ? &other : &in) && node_refs == 1);
	CHECK(iwx_bss_owner(&sc)->in_phyctxt == &sc.sc_phyctxt[0]);
	CHECK(!iwx_bss_joined(&sc));
	CHECK(sc.sc_ic.ic_state == IEEE80211_S_AUTH);
	/*
	 * A join has no frame behind it: net80211 is entered as from a
	 * scan, so that it sends the first authentication frame.
	 */
	CHECK(newstate_from ==
	    (arg == -1 && !sae ? IEEE80211_S_SCAN : IEEE80211_S_RUN));
}

/* Who owns the firmware contexts. */
static void
test_owner(void)
{
	struct iwx_softc sc;
	struct iwx_node in, other;

	setup(&sc, &in, 2, 0);
	memset(&other, 0, sizeof(other));
	/* Without an owner on record it is ic_bss. */
	sc.sc_fw_in = NULL;
	CHECK(iwx_bss_owner(&sc) == &in && !iwx_bss_joined(&sc));
	iwx_bss_disown(&sc, 1);
	CHECK(node_frees == 0 && node_cleanups == 0);
	/* The contexts made for a node make it the owner, once. */
	iwx_bss_own(&sc, &in);
	CHECK(sc.sc_fw_in == &in && node_refs == 1 && node_frees == 0);
	iwx_bss_own(&sc, &in);
	CHECK(node_refs == 1);
	/* Another ic_bss is a join only while contexts exist. */
	sc.sc_ic.ic_bss = &other;
	CHECK(iwx_bss_owner(&sc) == &in && iwx_bss_joined(&sc));
	sc.sc_flags = 0;
	CHECK(iwx_bss_owner(&sc) == &in && !iwx_bss_joined(&sc));
	/* A new owner lets the old one go without the net80211 cleanup. */
	iwx_bss_own(&sc, &other);
	CHECK(sc.sc_fw_in == &other && node_refs == 2 && node_frees == 1);
	CHECK(node_cleanups == 0);
	CHECK(in.in_phyctxt == NULL && in.in_ht == 0);
	/* Letting go with the cleanup. */
	other.in_phyctxt = &sc.sc_phyctxt[0];
	other.in_ht = 1;
	other.in_flags = 3;
	iwx_bss_disown(&sc, 1);
	CHECK(sc.sc_fw_in == NULL && node_frees == 2 && node_cleanups == 1);
	CHECK(node_cleaned == &other.in_ni);
	CHECK(other.in_phyctxt == NULL && other.in_ht == 0 &&
	    other.in_flags == 0);
}

/* A scan requested by user space while associated. */
static void
test_scan_assoc(void)
{
	struct iwx_softc sc;
	struct iwx_node in;
	struct ieee80211req scan = { IEEE80211_IOC_SCAN_REQ };
	struct ieee80211req other = { IEEE80211_IOC_WPA };
	const int active = IWX_FLAG_STA_ACTIVE | IWX_FLAG_BINDING_ACTIVE |
	    IWX_FLAG_MAC_ACTIVE | IWX_FLAG_PHY_ACTIVE;
	int err;

	setup(&sc, &in, 2, 0);
	sc.ns_nstate = IEEE80211_S_RUN;
	scan_starts = scan_notified = scan_ended = scan_aborts = 0;
	scan_error = kauth_error = kauth_calls = scan_timer = 0;

	/* Only a scan request is taken from net80211. */
	err = -1;
	CHECK(iwx_scan_assoc_req(&sc, 0, &other, &err) == 0);
	CHECK(err == -1 && kauth_calls == 0 && scan_starts == 0);

	/* Not associated, or about to leave: net80211 runs the scan. */
	sc.sc_ic.ic_state = IEEE80211_S_SCAN;
	CHECK(iwx_scan_assoc_req(&sc, 0, &scan, &err) == 0);
	sc.sc_ic.ic_state = IEEE80211_S_RUN;
	sc.ns_nstate = IEEE80211_S_AUTH;
	CHECK(iwx_scan_assoc_req(&sc, 0, &scan, &err) == 0);
	sc.ns_nstate = IEEE80211_S_RUN;
	sc.sc_flags |= IWX_FLAG_SHUTDOWN;
	CHECK(iwx_scan_assoc_req(&sc, 0, &scan, &err) == 0);
	sc.sc_flags &= ~IWX_FLAG_SHUTDOWN;
	sc.sc_ic.ic_opmode = IEEE80211_M_MONITOR;
	CHECK(iwx_scan_assoc_req(&sc, 0, &scan, &err) == 0);
	sc.sc_ic.ic_opmode = IEEE80211_M_STA;
	CHECK(err == -1 && kauth_calls == 0 && scan_starts == 0);

	/* The privilege check comes first. */
	kauth_error = EPERM;
	CHECK(iwx_scan_assoc_req(&sc, 0, &scan, &err) == 1);
	CHECK(err == EPERM && kauth_calls == 1 && scan_starts == 0);
	CHECK(sc.sc_flags == active);
	kauth_error = 0;

	/* A channel switch of the AP is announced or under way. */
	sc.sc_csa_valid = 1;
	CHECK(iwx_scan_assoc_req(&sc, 0, &scan, &err) == 1);
	CHECK(err == EBUSY && scan_starts == 0);
	sc.sc_csa_valid = 0;
	sc.sc_csa_wait = 1;
	CHECK(iwx_scan_assoc_req(&sc, 0, &scan, &err) == 1);
	CHECK(err == EBUSY && scan_starts == 0);
	sc.sc_csa_wait = 0;

	/* The firmware refuses the scan: nothing is left behind. */
	scan_error = EIO;
	CHECK(iwx_scan_assoc_req(&sc, 0, &scan, &err) == 1);
	CHECK(err == EIO && scan_starts == 1 && scan_timer == 0);
	CHECK(sc.sc_flags == active);
	scan_error = 0;
	/* The scan was marked before the command went out. */
	CHECK(scan_flags_at_send ==
	    (active | IWX_FLAG_SCANNING | IWX_FLAG_BGSCAN));
	CHECK(scan_timer_at_send == IWX_SCAN_ASSOC_TIMEOUT);

	/* The scan starts with a command that reports its failure. */
	CHECK(iwx_scan_assoc_req(&sc, 0, &scan, &err) == 1);
	CHECK(err == 0 && scan_starts == 2);
	CHECK(scan_mode == IWX_SCAN_ASSOCIATED);
	CHECK(sc.sc_flags ==
	    (active | IWX_FLAG_SCANNING | IWX_FLAG_BGSCAN));
	CHECK(scan_timer == IWX_SCAN_ASSOC_TIMEOUT);
	/* A second request joins the scan in flight. */
	CHECK(iwx_scan_assoc_req(&sc, 0, &scan, &err) == 1);
	CHECK(err == 0 && scan_starts == 2);

	/* Its end is reported; net80211 picks no BSS. */
	iwx_endscan(&sc);
	CHECK(scan_notified == 1 && scan_ended == 0 && scan_timer == 0);
	CHECK(sc.sc_flags == active);
	iwx_endscan(&sc);
	CHECK(scan_notified == 1 && scan_ended == 0);

	/* A scan by net80211 still ends through net80211. */
	sc.sc_ic.ic_state = IEEE80211_S_SCAN;
	sc.sc_flags |= IWX_FLAG_SCANNING;
	iwx_endscan(&sc);
	CHECK(scan_notified == 1 && scan_ended == 1);
	/* So does one that net80211 took over after the BSS was left. */
	sc.sc_flags |= IWX_FLAG_SCANNING | IWX_FLAG_BGSCAN;
	iwx_endscan(&sc);
	CHECK(scan_notified == 1 && scan_ended == 2);
	CHECK(sc.sc_flags == active);
	sc.sc_ic.ic_state = IEEE80211_S_RUN;

	/* A scan that overran is aborted and its results reported. */
	CHECK(iwx_scan_assoc_req(&sc, 0, &scan, &err) == 1 && err == 0);
	task_releases = 0;
	/* A pending timeout: the task was queued for an earlier scan. */
	iwx_scan_assoc_task(&sc);
	CHECK(scan_aborts == 0 && scan_notified == 1 && task_releases == 1);
	CHECK(sc.sc_flags ==
	    (active | IWX_FLAG_SCANNING | IWX_FLAG_BGSCAN));
	scan_timer = 0;		/* the timeout has fired */
	iwx_scan_assoc_task(&sc);
	CHECK(scan_aborts == 1 && scan_notified == 2 && task_releases == 2);
	CHECK(sc.sc_flags == active && scan_timer == 0);
	/* The task finds nothing to do once the scan has ended. */
	iwx_scan_assoc_task(&sc);
	CHECK(scan_aborts == 1 && scan_notified == 2 && task_releases == 3);
	/* Nor during a shutdown. */
	sc.sc_flags |= IWX_FLAG_SCANNING | IWX_FLAG_BGSCAN | IWX_FLAG_SHUTDOWN;
	iwx_scan_assoc_task(&sc);
	CHECK(scan_aborts == 1 && scan_notified == 2 && task_releases == 4);
	sc.sc_flags = active;
	/* An overrun after the BSS was left: net80211 scans, or nobody. */
	sc.sc_flags |= IWX_FLAG_SCANNING | IWX_FLAG_BGSCAN;
	sc.sc_ic.ic_state = IEEE80211_S_SCAN;
	iwx_scan_assoc_task(&sc);
	CHECK(scan_aborts == 2 && scan_notified == 2 && scan_ended == 3);
	CHECK(sc.sc_flags == active);
	sc.sc_flags |= IWX_FLAG_SCANNING | IWX_FLAG_BGSCAN;
	sc.sc_ic.ic_state = IEEE80211_S_INIT;
	iwx_scan_assoc_task(&sc);
	CHECK(scan_aborts == 3 && scan_notified == 2 && scan_ended == 3);
	CHECK(sc.sc_flags == active);
}

/* Execute the real state dispatcher and its teardown/auth callees. */
static void
test_transition(int width, enum ieee80211_state next, int rm_error, int add_error)
{
	struct iwx_softc sc;
	struct iwx_node in;

	setup(&sc, &in, width, 0);
	reset_transport(4, width);
	sc.ns_nstate = next;
	remove_error = rm_error;
	phy_add_error = add_error;
	iwx_newstate_task(&sc);
	CHECK(tlc_count == 0 && phy_count == 0);
	CHECK(task_releases == 1);
	if (next == IEEE80211_S_ASSOC) {
		CHECK(remove_sequence == 0 && phy_remove_sequence == 0);
		CHECK(in.in_ht == 1 && physical_width == width);
	} else {
		CHECK(remove_sequence > 0);
		CHECK(disassoc_sequence > 0 && disassoc_sequence < remove_sequence);
		if (rm_error) {
			CHECK(phy_remove_sequence == 0 && phy_add_sequence == 0);
			CHECK(binding_remove_sequence == 0 && mac_remove_sequence == 0);
			CHECK(recovery_count == 1);
			CHECK(sc.sc_flags & IWX_FLAG_STA_ACTIVE);
			CHECK(physical_width == width);
		} else {
			CHECK(binding_remove_sequence > remove_sequence);
			CHECK(mac_remove_sequence > binding_remove_sequence);
			CHECK(phy_remove_sequence > mac_remove_sequence);
			if (next == IEEE80211_S_AUTH) {
				CHECK(phy_add_sequence > phy_remove_sequence);
				CHECK(in.in_ht == 0);
				if (add_error) {
					CHECK(recovery_count == 1);
					CHECK(sc.sc_flags == 0);
				} else {
					CHECK(recovery_count == 0);
					CHECK(sc.sc_ic.ic_state == IEEE80211_S_AUTH);
					CHECK(sc.sc_flags & IWX_FLAG_STA_ACTIVE);
				}
			} else {
				CHECK(phy_add_sequence == 0);
				CHECK(sc.sc_flags == 0);
			}
		}
	}
}

/* Associated width changes remain separate from removal and new auth. */
static void
test_dynamic(int version, int old_width, int new_width)
{
	struct iwx_softc sc;
	struct iwx_node in;

	setup(&sc, &in, old_width, 0);
	reset_transport(version, old_width);
	in.in_htc.sco = new_width == 1 ? 1 : 0;
	in.in_htc.vht80 = new_width == 2 ? 1 : 0;
	in.in_htt.width = (uint8_t)new_width;
	iwx_phy_ctxt_task(&sc);
	CHECK(task_releases == 1);
	CHECK(remove_sequence == 0 && phy_remove_sequence == 0 && phy_add_sequence == 0);
	if (old_width == new_width) {
		CHECK(tlc_count == 0 && phy_count == 0);
	} else {
		CHECK(tlc_count == 1 && phy_count == 1);
		CHECK(sent_width == new_width);
		CHECK(sent_mode == IWX_HT_TLC_MODE_VHT);
		if (new_width < old_width)
			CHECK(tlc_sequence < phy_sequence);
		else
			CHECK(phy_sequence < tlc_sequence);
	}
	CHECK(unsafe_width == 0);
}

/*
 * Following an AP to a new channel: the link goes inactive, the PHY
 * context is replaced, the link gets its rates and rate scaling again and
 * only then goes active.  A failed command ends the sequence.
 */
static void
test_csa(int version, int rlc, int fail_at)
{
	static const int order[] = { EV_LINK_OFF, EV_PHY_REMOVE, EV_PHY_ADD,
	    EV_RLC, EV_LINK_RATES, EV_TLC, EV_LINK_ON };
	struct ieee80211_channel oldchan, newchan;
	struct iwx_softc sc;
	struct iwx_node in;
	int expected[8], i, n = 0, result;

	for (i = 0; i < 7; i++)
		if (order[i] != EV_RLC || rlc == 2)
			expected[n++] = order[i];
	reset_transport(version, 2);
	setup(&sc, &in, 2, 0);
	sc.ns_nstate = IEEE80211_S_RUN;
	sc.sc_phyctxt[0].channel = &oldchan;
	csa_nevents = 0;
	csa_fail_at = fail_at;
	csa_removed_channel = csa_added_channel = NULL;
	rlc_version = rlc;
	csa_mode = 1;
	result = iwx_csa_switch(&sc, &in, &newchan, 2, 1, 1);
	csa_mode = 0;
	rlc_version = 0;

	if (fail_at == 0 || fail_at > n) {
		CHECK(result == 0);
		CHECK(csa_nevents == n);
		for (i = 0; i < n && i < csa_nevents; i++)
			CHECK(csa_events[i] == expected[i]);
		CHECK(csa_removed_channel == &oldchan);
		CHECK(csa_added_channel == &newchan);
		CHECK(csa_added_sco == 1 && csa_added_width == 1);
		CHECK(sc.sc_phyctxt[0].channel == &newchan);
		CHECK(sc.sc_phyctxt[0].sco == 1 &&
		    sc.sc_phyctxt[0].vht_chan_width == 1);
	} else {
		CHECK(result == EIO);
		CHECK(csa_nevents == fail_at);
		for (i = 0; i < fail_at; i++)
			CHECK(csa_events[i] == expected[i]);
	}
	/* Nothing is sent once the association is gone. */
	if (fail_at == 0) {
		csa_nevents = 0;
		csa_mode = 1;
		sc.sc_ic.ic_state = IEEE80211_S_AUTH;
		CHECK(iwx_csa_switch(&sc, &in, &oldchan, 2, 1, 1) == ENXIO);
		sc.sc_ic.ic_state = IEEE80211_S_RUN;
		sc.sc_flags |= IWX_FLAG_SHUTDOWN;
		CHECK(iwx_csa_switch(&sc, &in, &oldchan, 2, 1, 1) == ENXIO);
		sc.sc_flags &= ~IWX_FLAG_SHUTDOWN;
		/* A requested state change counts before it is carried out. */
		sc.ns_nstate = IEEE80211_S_AUTH;
		CHECK(iwx_csa_switch(&sc, &in, &oldchan, 2, 1, 1) == ENXIO);
		sc.ns_nstate = IEEE80211_S_RUN;
		csa_mode = 0;
		CHECK(csa_nevents == 0);
		CHECK(sc.sc_phyctxt[0].channel == &newchan);
	}
	/* The station is never removed on this path. */
	CHECK(remove_sequence == 0 && (sc.sc_flags & IWX_FLAG_STA_ACTIVE));
}

int
main(void)
{
	int version, width, fail;

	for (width = 0; width <= 2; width++) {
		test_stop(width, 0, 0);
		test_stop(width, 1, 0);
		test_stop(width, 0, EIO);
		test_transition(width, IEEE80211_S_AUTH, 0, 0);
		test_transition(width, IEEE80211_S_INIT, 0, 0);
		test_transition(width, IEEE80211_S_SCAN, 0, 0);
		test_transition(width, IEEE80211_S_ASSOC, 0, 0);
		test_transition(width, IEEE80211_S_AUTH, EIO, 0);
		test_transition(width, IEEE80211_S_AUTH, 0, EIO);
	}

	/* A join of another BSS is taken down in place. */
	test_leave(IEEE80211_S_AUTH, -1, 0, 1, 0, 0);
	/* Not with native SAE, whose keys live in the firmware. */
	test_leave(IEEE80211_S_AUTH, -1, 0, 1, 1, 1);
	/* The same request for the BSS in use. */
	test_leave(IEEE80211_S_AUTH, -1, 0, 0, 0, 0);
	test_leave(IEEE80211_S_AUTH, 0xc0, 0, 0, 0, 0);
	test_leave(IEEE80211_S_ASSOC, -1, 0, 0, 0, 0);
	test_leave(IEEE80211_S_SCAN, -1, 0, 0, 0, 0);
	test_leave(IEEE80211_S_INIT, -1, 0, 0, 0, 0);
	/* A join followed by a deauthentication request from user space. */
	test_leave(IEEE80211_S_INIT, -1, 0, 1, 0, 0);
	/* A scan in flight. */
	test_leave(IEEE80211_S_AUTH, 0xc0, 1, 0, 0, 1);
	test_leave(IEEE80211_S_AUTH, -1, 1, 1, 0, 1);
	test_leave(IEEE80211_S_ASSOC, 0, 1, 0, 0, 1);
	test_leave(IEEE80211_S_SCAN, 0, 1, 0, 0, 1);
	test_leave(IEEE80211_S_INIT, -1, 1, 0, 0, 0);
	test_owner();
	test_stop_waits();
	test_scan_assoc();
	for (version = 3; version <= 4; version++) {
		test_dynamic(version, 1, 0);
		test_dynamic(version, 2, 0);
		test_dynamic(version, 2, 1);
		test_dynamic(version, 0, 1);
		test_dynamic(version, 0, 2);
		test_dynamic(version, 1, 1);
		test_csa(version, 2, 0);
		test_csa(version, 0, 0);
		for (fail = 1; fail <= 7; fail++)
			test_csa(version, 2, fail);
	}
	printf("iwx width contract: %u checks, %u failures\n", checks, failures);
	return failures != 0;
}
