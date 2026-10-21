/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual native scan copyout; kernel services and node layout are modeled. */
#include <sys/queue.h>
#include <sys/types.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scan-abi.h"
#include "ieee80211_ht20.h"
#if __has_include("ieee80211_phy_ies.h")
#include "ieee80211_phy_ies.h"
#else
/* The baseline has no storage field. No parser implementation is modeled. */
struct ieee80211_phy_ies {
	uint8_t len;
	uint8_t data[73];
};
#endif

#ifndef roundup
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#endif
#define IEEE80211_CHAN_ANYC ((struct ieee80211_channel *)(uintptr_t)-1)
#define IEEE80211_ADDR_COPY(dst, src) memcpy((dst), (src), IEEE80211_ADDR_LEN)

struct ieee80211_channel {
	uint16_t ic_freq, ic_flags;
};
struct ieee80211com;
struct ieee80211_node_table;
struct ieee80211_node {
	struct ieee80211_channel *ni_wme_applied_chan;
	unsigned ni_wme_applied_slot;
	TAILQ_ENTRY(ieee80211_node) ni_list;
	struct ieee80211com *ni_ic;
	struct ieee80211_channel *ni_chan;
	uint8_t ni_esslen, ni_essid[IEEE80211_NWID_LEN];
	uint8_t *ni_wpa_ie, *ni_wme_ie, *ni_rsnx_ie, *ni_extcap_ie, *ni_rrm_ie;
	uint8_t ni_intval, ni_erp, ni_bssid[IEEE80211_ADDR_LEN];
	uint16_t ni_capinfo;
	int8_t ni_rssi_dbm;
	bool ni_rssi_dbm_valid;
	uint32_t ni_scan_epoch;
	struct ieee80211_phy_ies ni_phy_ies;
	struct ieee80211_ht20_node ni_ht20;
	unsigned ni_flags, ni_associd, ni_authmode, ni_txpower, ni_vlan;
	unsigned ni_fhdwell, ni_fhindex, ni_dtim_count, ni_dtim_period,
	    ni_timoff;
	unsigned ni_rssi, ni_rstamp, ni_rsn;
	uint32_t *ni_challenge;
	void *ni_rxfrag[3];
	union {
		uint8_t data[8];
	} ni_tstamp;
	struct {
		uint8_t rs_nrates, rs_rates[IEEE80211_RATE_MAXSIZE];
	} ni_rates;
};
struct ieee80211_node_table {
	TAILQ_HEAD(, ieee80211_node) nt_node;
	uint32_t nt_scan_epoch;
};
struct ieee80211com {
	struct ieee80211_node_table ic_scan;
	void *ic_sta_mgmt;
	struct ieee80211_node *ic_bss;
	struct ieee80211_channel *ic_curchan;
	struct ieee80211_channel ic_channels[256];
	unsigned ic_ps_sta, ic_opmode;
	struct {
		unsigned is_rx_nodealloc;
	} ic_stats;
	struct ieee80211_node *(*ic_node_alloc)(struct ieee80211_node_table *);
	void (*ic_set_tim)(struct ieee80211_node *, int);
	uint8_t (*ic_node_getrssi)(const struct ieee80211_node *);
};
struct ieee80211req {
	uint16_t i_len;
	void *i_data;
};
static unsigned int checks, failures;

static uint8_t
getrssi(const struct ieee80211_node *ni)
{
	(void)ni;
	return 183;
}

static int
copyout(const void *src, void *dst, size_t len)
{
	memcpy(dst, src, len);
	return 0;
}

#define __packed __attribute__((packed))
#include "native-layouts.h"
#define IEEE80211_FC0_SUBTYPE_PROBE_RESP 0x50
#define IEEE80211_M_STA 1
#define IEEE80211_M_HOSTAP 6
#define IEEE80211_NODE_PWR_MGT 0x10
#define IEEE80211_NODE_AREF 0x20
#define IEEE80211_NODE_SAVEQ_DRAIN(n, q)                                       \
	do {                                                                   \
		(void)(n);                                                     \
		(q) = 0;                                                       \
	} while (0)
#define IEEE80211_R_DOSORT 1
#define IEEE80211_DPRINTF(...) ((void)0)
#define M_DEVBUF 0
#define M_80211_NODE 0
#define M_NOWAIT 2
#define M_ZERO 1
static unsigned rate_calls, key_deletes;
static struct ieee80211_node *selected_node;
static void *
test_allocate(size_t n, int flags)
{
	return flags & M_ZERO ? calloc(1, n) : malloc(n);
}
static struct ieee80211_node *
ieee80211_find_node(struct ieee80211_node_table *nt, const uint8_t *a)
{
	(void)nt;
	(void)a;
	return selected_node;
}
static void
ieee80211_setup_node(struct ieee80211_node_table *nt, struct ieee80211_node *ni,
		     const uint8_t *a)
{
	(void)nt;
	(void)ni;
	(void)a;
}
static void
ieee80211_set_chan(struct ieee80211com *ic, struct ieee80211_node *ni,
		   struct ieee80211_channel *ch)
{
	(void)ic;
	ni->ni_chan = ch;
}
static void
ieee80211_free_node(struct ieee80211_node *ni)
{
	(void)ni;
}
static int
ieee80211_setup_rates(struct ieee80211_node *ni, const uint8_t *r,
		      const uint8_t *x, int f)
{
	(void)x;
	assert(f == IEEE80211_R_DOSORT);
	ni->ni_rates.rs_nrates = r[1];
	memcpy(ni->ni_rates.rs_rates, r + 2, r[1]);
	rate_calls++;
	return 0;
}
static void
ieee80211_node_delucastkey(struct ieee80211_node *ni)
{
	(void)ni;
	key_deletes++;
}
static void
m_freem(void *p)
{
	free(p);
}
#define malloc(n, t, f) test_allocate((n), (f))
#define free(p, t) free(p)
#include "native-node.inc"
#undef malloc
#undef free

#include "get_scan_result.h"
#include "getscanresults.h"

static void
check(bool pass, const char *why)
{
	checks++;
	if (!pass) {
		fprintf(stderr, "FAIL: %s\n", why);
		failures++;
	}
}

static const uint8_t phy[] = {
    45, 26, 1,	2,  3,	4,  5,	 6,   7,  8,  9,  10, 11, 12, 13,
    14, 15, 16, 17, 18, 19, 20,	 21,  22, 23, 24, 25, 26, 61, 22,
    1,	2,  3,	4,  5,	6,  7,	 8,   9,  10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22,	 191, 12, 1,  2,  3,  4,  5,  6,
    7,	8,  9,	10, 11, 12, 192, 5,   1,  2,  3,  4,  5};

int
main(void)
{
	struct ieee80211_channel channel = {5180, 0};
	struct ieee80211com ic = {.ic_node_getrssi = getrssi};
	struct ieee80211_node ni = {.ni_ic = &ic, .ni_chan = &channel};
	struct ieee80211_scanparams sp = {0};
	struct ieee80211_frame wh = {0};
	uint8_t ssid[] = {0, 1, 'x'}, rates[] = {1, 2, 0x82, 4}, stamp[8] = {0};
	uint8_t output[1024], wpa[258], wme[258];
	struct ieee80211req req = {.i_data = output};
	struct ieee80211req_scan_result sr;
	const unsigned sizes[][2] = {
	    {0, 0}, {102, 80}, {102, 81}, {128, 127}, {128, 128}};
	const unsigned expected[] = {73, 255, 183, 255, 0};
	unsigned i;

	TAILQ_INIT(&ic.ic_scan.nt_node);
	TAILQ_INSERT_TAIL(&ic.ic_scan.nt_node, &ni, ni_list);
	ni.ni_phy_ies.len = sizeof(phy);
	memcpy(ni.ni_phy_ies.data, phy, sizeof(phy));
	memset(wpa, 0x37, sizeof(wpa));
	wpa[0] = 48;
	memset(wme, 0x71, sizeof(wme));
	wme[0] = 221;
	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		unsigned a = sizes[i][0], b = sizes[i][1];
		ni.ni_wpa_ie = a ? wpa : NULL;
		ni.ni_wme_ie = b ? wme : NULL;
		if (a)
			wpa[1] = a - 2;
		if (b)
			wme[1] = b - 2;
		memset(output, 0xa5, sizeof(output));
		req.i_len = sizeof(output);
		check(ieee80211_ioctl_getscanresults(&ic, &req) == 0,
		      "native scan copyout succeeds");
		memcpy(&sr, output, sizeof(sr));
		check(sr.isr_ie_len == expected[i],
		      "all PHY IEs fit or only legacy IE bytes remain");
		if (expected[i]) {
			check(!a || memcmp(output + sizeof(sr), wpa, a) == 0,
			      "RSN bytes unchanged");
			check(!b ||
				  memcmp(output + sizeof(sr) + a, wme, b) == 0,
			      "WME bytes unchanged");
			if (expected[i] == a + b + sizeof(phy))
				check(memcmp(output + sizeof(sr) + a + b, phy,
					     sizeof(phy)) == 0,
				      "complete observed HT/VHT tail exported");
		}
		check(output[req.i_len] == 0xa5,
		      "copyout respects record length");
	}
	ni.ni_wpa_ie = ni.ni_wme_ie = NULL;
	ni.ni_scan_epoch = 0;
	ic.ic_scan.nt_scan_epoch = 1;
	req.i_len = sizeof(output);
	check(ieee80211_ioctl_getscanresults(&ic, &req) == 0,
	      "legacy cached entry remains visible");
	memcpy(&sr, output, sizeof(sr));
	check(sr.isr_ie_len == 0, "stale epoch cannot export PHY observations");
	/* Execute actual scan storage, legacy IE ownership and cleanup. */
	ni.ni_wpa_ie = ni.ni_wme_ie = NULL;
	selected_node = &ni;
	ic.ic_bss = &ni;
	ic.ic_node_alloc = node_alloc;
	sp.sp_ssid = ssid;
	sp.sp_rates = rates;
	sp.sp_tstamp = stamp;
	sp.sp_chan = 36;
	sp.sp_wpa = wpa;
	sp.sp_wme = wme;
	sp.sp_capinfo = 0x0431;
	ni.ni_flags = 2;
	wpa[1] = 2;
	wme[1] = 2;
	check(ieee80211_phy_ies_parse(&sp.sp_phy_ies, phy, sizeof(phy)) == 0,
	      "wire observation parses for the native scan consumer");
	memset(&ni.ni_phy_ies, 0, sizeof(ni.ni_phy_ies));
	ieee80211_add_scan(&ic, &sp, &wh, 0x50, 42, 123);
	check(ni.ni_phy_ies.len == sizeof(phy),
	      "actual scan consumer stores bounded observation");
	check(ni.ni_scan_epoch == ic.ic_scan.nt_scan_epoch,
	      "observation uses native scan epoch");
	check(rate_calls == 1 && ni.ni_rates.rs_nrates == 2 &&
		  ni.ni_rates.rs_rates[0] == 0x82 &&
		  ni.ni_rates.rs_rates[1] == 4,
	      "legacy rate setup remains DOSORT only");
	check(ni.ni_flags == 2 && ni.ni_capinfo == 0x0431,
	      "observations do not negotiate node flags or capabilities");
	sp.sp_phy_ies.data[2] ^= 0xff;
	check(memcmp(ni.ni_phy_ies.data, phy, sizeof(phy)) == 0,
	      "node owns its snapshot after borrowed scan parameters change");
	req.i_len = sizeof(output);
	check(ieee80211_ioctl_getscanresults(&ic, &req) == 0,
	      "stored observation reaches native copyout");
	memcpy(&sr, output, sizeof(sr));
	check(sr.isr_ie_len == 81 &&
		  memcmp(output + sizeof(sr) + 1 + 8, phy, 73) == 0,
	      "parsed scan IEs reach the original wire ABI");
	sp.sp_phy_ies.data[2] ^= 0xff;
	ic.ic_scan.nt_scan_epoch++;
	req.i_len = sizeof(output);
	check(ieee80211_ioctl_getscanresults(&ic, &req) == 0,
	      "epoch change retains legacy cached entry");
	memcpy(&sr, output, sizeof(sr));
	check(sr.isr_ie_len == 8, "new epoch omits only stale PHY bytes");
	ieee80211_add_scan(&ic, &sp, &wh, 0x50, 42, 124);
	check(ni.ni_scan_epoch == ic.ic_scan.nt_scan_epoch &&
		  ni.ni_phy_ies.len == 73,
	      "actual scan update refreshes existing epoch ownership");
	uint8_t malformed[] = {45, 1, 0};
	check(ieee80211_phy_ies_parse(&sp.sp_phy_ies, malformed,
				      sizeof(malformed)) == -1,
	      "wrong-length HT observation rejected");
	ieee80211_add_scan(&ic, &sp, &wh, 0x50, 42, 125);
	req.i_len = sizeof(output);
	check(ieee80211_ioctl_getscanresults(&ic, &req) == 0,
	      "malformed PHY snapshot leaves legacy scan result usable");
	memcpy(&sr, output, sizeof(sr));
	check(sr.isr_ie_len == 8 && ni.ni_phy_ies.len == 0,
	      "malformed update clears old PHY metadata and preserves RSN/WME");
	check(memcmp(output + sizeof(sr) + 1, wpa, 4) == 0 &&
		  memcmp(output + sizeof(sr) + 5, wme, 4) == 0,
	      "legacy bytes survive malformed PHY metadata");
	(void)ieee80211_phy_ies_parse(&sp.sp_phy_ies, phy, sizeof(phy));
	ieee80211_add_scan(&ic, &sp, &wh, 0x50, 42, 126);
	memset(&sp.sp_phy_ies, 0, sizeof(sp.sp_phy_ies));
	ieee80211_add_scan(&ic, &sp, &wh, 0x50, 42, 124);
	check(ni.ni_phy_ies.len == 0,
	      "missing PHY IEs replace stale observation");
	ni.ni_phy_ies = (struct ieee80211_phy_ies){.len = 73};
	ic.ic_opmode = IEEE80211_M_STA;
	ni.ni_wme_applied_chan = ni.ni_chan;
	ni.ni_wme_applied_slot = 9;
	ni.ni_flags |= IEEE80211_NODE_QOS | IEEE80211_NODE_WME_OFFER |
	    IEEE80211_NODE_WME_VALID;
	ni.ni_ht20.attempt = 41;
	ni.ni_ht20.offered = ni.ni_ht20.valid = 1;
	node_cleanup(&ni);
	check(ni.ni_ht20.attempt == 42 && !ni.ni_ht20.offered &&
	    !ni.ni_ht20.valid,
	    "native cleanup invalidates HT offer and advances attempt");
	check(ni.ni_wme_applied_chan == NULL && ni.ni_wme_applied_slot == 0,
	    "native cleanup retires applied EDCA context");
	check((ni.ni_flags & (IEEE80211_NODE_QOS | IEEE80211_NODE_WME_OFFER |
	    IEEE80211_NODE_WME_VALID)) == 0,
	    "native cleanup retires negotiated QoS and pending WME state");
	check(
	    ni.ni_phy_ies.len == 0 && key_deletes == 1,
	    "native leave cleanup clears observation and retains key cleanup");
	ic.ic_opmode = IEEE80211_M_HOSTAP;
	ni.ni_flags |= IEEE80211_NODE_QOS;
	node_cleanup(&ni);
	check((ni.ni_flags & IEEE80211_NODE_QOS) != 0,
	    "STA WME cleanup does not change the AP node contract");
	ni.ni_ht20.attempt = UINT32_MAX;
	node_cleanup(&ni);
	check(ni.ni_ht20.attempt == 1,
	    "native HT cleanup keeps a nonzero attempt after wrap");
	struct ieee80211_node *fresh = node_alloc(&ic.ic_scan);
	check(fresh != NULL && fresh->ni_phy_ies.len == 0,
	      "native reset allocation starts with no observation");
	free(fresh);
	free(ni.ni_wpa_ie);
	free(ni.ni_wme_ie);
	printf("Native PHY IE scan: %u checks, %u failures\n", checks,
	       failures);
	return failures != 0;
}
