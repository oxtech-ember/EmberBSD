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
#define IEEE80211_M_STA 1
#define IEEE80211_M_MONITOR 8
#define IWX_STATION_ID 0
#define IWX_TLC_MNG_CONFIG_CMD 0x0f
#define IWX_DATA_PATH_GROUP 5
#define IWX_CMD_ASYNC 1
#define IWX_SF_INIT_OFF 0
#define IWX_FW_CTXT_ACTION_MODIFY 2
#define IPL_NET 0
#define splassert(ipl) ((void)(ipl))
#define device_xname(dev) ((void)(dev), "iwx-test")

struct ieee80211_rateset { int rs_nrates; uint8_t rs_rates[16]; };
struct ieee80211_node { struct ieee80211_rateset ni_rates; };
struct iwx_phy_ctxt { int sco, vht_chan_width; void *channel; };
struct iwx_node {
	struct ieee80211_node in_ni;
	struct iwx_phy_ctxt *in_phyctxt;
	int in_ht;
	struct iwx_ht_tlc in_htt;
};
struct ieee80211com { void *ic_bss; int ic_opmode; };
struct iwx_softc { struct ieee80211com sc_ic; void *sc_dev; };

static unsigned int checks, failures, tlc_count, phy_count, unsafe_width;
static int command_version, tlc_error, phy_error, flush_error, firmware_width;
static int sent_width, sent_mode, sequence, tlc_sequence, phy_sequence;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static int
iwx_lookup_cmd_ver(struct iwx_softc *sc, int group, int cmd)
{
	(void)sc; (void)group; (void)cmd;
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
	firmware_width = sent_width;
	return 0;
}

static int
iwx_phy_ctxt_update(struct iwx_softc *sc, struct iwx_phy_ctxt *phy,
    void *channel, int chains, int dynamic, int time, int sco, int width)
{
	(void)sc; (void)channel;
	CHECK(chains == 1 && dynamic == 1 && time == 0);
	CHECK(sco == 0 && width == 0);
	phy_count++;
	phy_sequence = ++sequence;
	if (firmware_width > 0)
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
{ (void)sc; (void)in; (void)action; (void)assoc; return 0; }

/* DRIVER_FUNCTIONS */

static void
test_stop(int version, int width, int ht, int monitor, int tlc_fail,
    int phy_fail, int flush_fail)
{
	struct iwx_phy_ctxt phy = { width == 1 ? 1 : 0, width == 2 ? 1 : 0, NULL };
	struct iwx_node in = { .in_phyctxt = &phy, .in_ht = ht };
	struct iwx_softc sc = { .sc_ic = { &in,
	    monitor ? IEEE80211_M_MONITOR : IEEE80211_M_STA } };
	int result;

	in.in_ni.ni_rates.rs_nrates = 1;
	in.in_ni.ni_rates.rs_rates[0] = 12;
	/* Leave stale HT rates populated: in_ht must select the legacy branch. */
	in.in_htt.width = (uint8_t)width;
	in.in_htt.mode = IWX_HT_TLC_MODE_VHT;
	command_version = version;
	firmware_width = monitor ? 0 : width;
	tlc_error = tlc_fail;
	phy_error = phy_fail;
	flush_error = flush_fail;
	tlc_count = phy_count = unsafe_width = 0;
	sequence = tlc_sequence = phy_sequence = 0;
	sent_width = sent_mode = -1;
	result = iwx_run_stop(&sc);
	CHECK(result == (flush_fail ? flush_fail :
	    width && !monitor && tlc_fail ? tlc_fail : width ? phy_fail : 0));
	if (flush_fail || width == 0) {
		CHECK(tlc_count == 0 && phy_count == 0);
	} else {
		CHECK(in.in_ht == 0);
		if (monitor) {
			CHECK(tlc_count == 0 && phy_count == 1);
		} else {
			CHECK(tlc_count == 1);
			CHECK(sent_width == 0 && sent_mode == 0);
			if (tlc_fail)
				CHECK(phy_count == 0);
			else {
				CHECK(phy_count == 1);
				CHECK(tlc_sequence < phy_sequence);
			}
		}
	}
	CHECK(unsafe_width == 0);
}

int
main(void)
{
	int version;

	for (version = 3; version <= 4; version++) {
		test_stop(version, 1, 1, 0, 0, 0, 0);
		test_stop(version, 2, 1, 0, 0, 0, 0);
		test_stop(version, 1, 0, 0, 0, 0, 0);
		test_stop(version, 0, 1, 0, 0, 0, 0);
		test_stop(version, 2, 1, 1, 0, 0, 0);
		test_stop(version, 1, 1, 0, EIO, 0, 0);
		test_stop(version, 1, 1, 0, 0, EBUSY, 0);
		test_stop(version, 1, 1, 0, 0, 0, EBUSY);
	}
	printf("iwx width contract: %u checks, %u failures\n", checks, failures);
	return failures != 0;
}
