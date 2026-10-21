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
#define IWX_RLC_CONFIG_CMD 0x4e
#define IWX_MONITOR_STA_ID 1
#define IWX_DQA_INJECT_MONITOR_QUEUE 1
#define IWX_MGMT_TID 8
#define IWX_TX_RING_COUNT 256
#define IPL_NET 0
#define splassert(ipl) ((void)(ipl))
#define device_xname(dev) ((void)(dev), "iwx-test")
#define IEEE80211_ADDR_COPY(dst, src) memcpy((dst), (src), 6)

enum ieee80211_state {
	IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_AUTH,
	IEEE80211_S_ASSOC, IEEE80211_S_RUN
};

struct ieee80211_rateset { int rs_nrates; uint8_t rs_rates[16]; };
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
};
struct ieee80211com {
	void *ic_bss, *ic_ibss_chan;
	int ic_opmode;
	enum ieee80211_state ic_state;
};
struct iwx_softc {
	struct ieee80211com sc_ic;
	void *sc_dev;
	int sc_flags, sc_generation, sc_bss_ht_valid, task_refs;
	int sc_systq, init_task, ns_arg;
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
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static int
iwx_lookup_cmd_ver(struct iwx_softc *sc, int group, int cmd)
{
	(void)sc; (void)group;
	if (cmd == IWX_RLC_CONFIG_CMD)
		return 0;
	return command_version;
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

static int iwx_flush_sta(struct iwx_softc *sc, struct iwx_node *in)
{ (void)sc; (void)in; return flush_error; }
static void iwx_ba_stop(struct iwx_softc *sc, int tid, int force)
{ (void)sc; (void)tid; (void)force; }
static int iwx_sf_config(struct iwx_softc *sc, int state)
{ (void)sc; (void)state; return 0; }
static int iwx_disable_beacon_filter(struct iwx_softc *sc)
{ (void)sc; return 0; }
static int iwx_mac_ctxt_cmd(struct iwx_softc *sc, struct iwx_node *in,
    int action, int assoc)
{
	(void)sc; (void)in; (void)assoc;
	if (action == IWX_FW_CTXT_ACTION_MODIFY)
		disassoc_sequence = ++sequence;
	else if (action == IWX_FW_CTXT_ACTION_REMOVE)
		mac_remove_sequence = ++sequence;
	return 0;
}

static int
iwx_rm_sta(struct iwx_softc *sc, struct iwx_node *in)
{
	(void)sc; (void)in;
	remove_sequence = ++sequence;
	return remove_error;
}

static int
iwx_phy_ctxt_cmd(struct iwx_softc *sc, struct iwx_phy_ctxt *phy,
    int chains, int dynamic, int action, int time, int sco, int width)
{
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
	(void)sc; (void)in;
	if (action == IWX_FW_CTXT_ACTION_REMOVE)
		binding_remove_sequence = ++sequence;
	return 0;
}
static void iwx_ba_reset(struct iwx_softc *sc, int hw)
{ (void)sc; (void)hw; }
static void iwx_unprotect_session(struct iwx_softc *sc, struct iwx_node *in)
{ (void)sc; (void)in; }
static int iwx_rm_sta_cmd(struct iwx_softc *sc, struct iwx_node *in)
{ return iwx_rm_sta(sc, in); }
static int iwx_phy_send_rlc(struct iwx_softc *sc, struct iwx_phy_ctxt *phy,
    int chains, int dynamic)
{ (void)sc; (void)phy; (void)chains; (void)dynamic; return 0; }
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

static int
record_newstate(struct ieee80211com *ic, enum ieee80211_state state, int arg)
{
	(void)arg;
	ic->ic_state = state;
	return 0;
}

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

int
main(void)
{
	int version, width;

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
	for (version = 3; version <= 4; version++) {
		test_dynamic(version, 1, 0);
		test_dynamic(version, 2, 0);
		test_dynamic(version, 2, 1);
		test_dynamic(version, 0, 1);
		test_dynamic(version, 0, 2);
		test_dynamic(version, 1, 1);
	}
	printf("iwx width contract: %u checks, %u failures\n", checks, failures);
	return failures != 0;
}
