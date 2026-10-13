/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise the production scan-result serializer and its alignment padding. */
#include <sys/types.h>
#include <sys/queue.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scan-abi.h"

#ifndef roundup
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#endif
#define IEEE80211_CHAN_ANYC ((struct ieee80211_channel *)(uintptr_t)-1)
#define IEEE80211_ADDR_COPY(dst, src) memcpy((dst), (src), IEEE80211_ADDR_LEN)

struct ieee80211_channel {
	uint16_t ic_freq, ic_flags;
};
struct ieee80211com;
struct ieee80211_node {
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

static void
check_record(const uint8_t *data, const uint8_t *payload, size_t len,
    unsigned int padding)
{
	struct ieee80211req_scan_result sr;
	size_t pos;
	bool zero = true;

	memcpy(&sr, data, sizeof(sr));
	check(sr.isr_len == sizeof(sr) + len + padding,
	    "record length includes the expected alignment padding");
	check(memcmp(data + sizeof(sr), payload, len) == 0,
	    "SSID and IE payload is unchanged");
	for (pos = sizeof(sr) + len; pos < sr.isr_len; pos++)
		zero = zero && data[pos] == 0;
	check(zero, "all copied alignment bytes are zero");
}

int
main(void)
{
	struct ieee80211_channel channel = { 5180, 0 };
	struct ieee80211com ic = { .ic_node_getrssi = getrssi };
	struct ieee80211_node ni = { .ni_ic = &ic, .ni_chan = &channel };
	struct ieee80211_node next = { .ni_ic = &ic, .ni_chan = &channel };
	uint8_t output[1024], payload[IEEE80211_NWID_LEN + 8];
	uint8_t ie[] = { 221, 2, 0x71, 0x72 };
	struct ieee80211req req = { .i_data = output };
	struct ieee80211req_scan_result sr;
	unsigned int padding;
	size_t len;

	TAILQ_INIT(&ic.ic_scan.nt_node);
	TAILQ_INSERT_TAIL(&ic.ic_scan.nt_node, &ni, ni_list);
	memset(ni.ni_essid, 0xcc, sizeof(ni.ni_essid));
	memset(payload, 0xcc, sizeof(payload));
	for (padding = 0; padding < 4; padding++) {
		len = (4 - (sizeof(sr) + padding) % 4) % 4;
		ni.ni_esslen = (uint8_t)len;
		memset(output, 0xa5, sizeof(output));
		req.i_len = sizeof(output);
		check(ieee80211_ioctl_getscanresults(&ic, &req) == 0,
		    "scan serialization succeeds");
		check_record(output, payload, len, padding);
		check(output[req.i_len] == 0xa5,
		    "serialization does not write beyond the reported length");
	}

	/* The second record must not expose bytes from the first record. */
	ni.ni_esslen = IEEE80211_NWID_LEN;
	len = (4 - (sizeof(sr) + 3) % 4) % 4;
	next.ni_esslen = (uint8_t)len;
	memset(next.ni_essid, 0xcc, sizeof(next.ni_essid));
	TAILQ_INSERT_TAIL(&ic.ic_scan.nt_node, &next, ni_list);
	memset(output, 0xa5, sizeof(output));
	req.i_len = sizeof(output);
	check(ieee80211_ioctl_getscanresults(&ic, &req) == 0,
	    "two scan records serialize successfully");
	memcpy(&sr, output, sizeof(sr));
	check_record(output + sr.isr_len, payload, len, 3);
	TAILQ_REMOVE(&ic.ic_scan.nt_node, &next, ni_list);

	/* A variable IE tail also needs three cleared alignment bytes. */
	len = (4 - (sizeof(sr) + sizeof(ie) + 3) % 4) % 4;
	ni.ni_esslen = (uint8_t)len;
	ni.ni_wpa_ie = ie;
	memcpy(payload + len, ie, sizeof(ie));
	memset(output, 0xa5, sizeof(output));
	req.i_len = sizeof(output);
	check(ieee80211_ioctl_getscanresults(&ic, &req) == 0,
	    "a scan record with an IE tail serializes successfully");
	check_record(output, payload, len + sizeof(ie), 3);
	printf("%s: %u production scan padding assertions\n",
	    failures ? "FAIL" : "PASS", checks);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
