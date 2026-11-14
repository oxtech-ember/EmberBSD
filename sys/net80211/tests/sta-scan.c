/* SPDX-License-Identifier: BSD-2-Clause */
/* Contract fixture for the production station connected-scan helpers. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef unsigned int u_int;

#define IEEE80211_M_STA 1
#define IEEE80211_S_RUN 4
#define IEEE80211_S_INIT 0
#define IEEE80211_F_SCAN 0x0001
#define IEEE80211_F_ASCAN 0x0002
#define IEEE80211_CHAN_MAX 15
#define IEEE80211_CHAN_BYTES 4
#define IEEE80211_ADDR_LEN 6
#define IEEE80211_STA_MAX_CHANNELS 32
#define IEEE80211_STA_VERSION 2
#define IEEE80211_STA_SCAN 5
#define IEEE80211_STA_CANCEL_SCAN 6
#define IEEE80211_STA_SCAN_ABORTED 0x0020
#define RTM_IEEE80211_STA_SCAN 114
#define isset(a, i) (((a)[(i) >> 3] & (1U << ((i) & 7))) != 0)
#define IEEE80211_ADDR_EQ(a, b) (memcmp((a), (b), IEEE80211_ADDR_LEN) == 0)
#define IEEE80211_ADDR_COPY(d, s) memcpy((d), (s), IEEE80211_ADDR_LEN)
#define IEEE80211_CHAN_ANYC ((struct ieee80211_channel *)(uintptr_t)-1)
#define CTASSERT(e) _Static_assert((e), #e)

struct ieee80211_channel { uint16_t ic_freq; };
struct ieee80211_node {
	uint8_t ni_bssid[IEEE80211_ADDR_LEN];
	struct ieee80211_channel *ni_chan;
};
struct ieee80211_node_table { unsigned nt_scan_epoch; };
struct ieee80211req_sta_mgmt {
	uint16_t version, op;
	uint32_t cookie, generation, flags;
	uint16_t freq, len;
	uint8_t bssid[IEEE80211_ADDR_LEN];
	uint16_t reserved;
	uint8_t data[IEEE80211_STA_MAX_CHANNELS * 2];
};
struct ieee80211_sta_scan_state {
	bool active, reported;
	uint32_t cookie, generation;
	uint16_t freq;
	uint8_t bssid[IEEE80211_ADDR_LEN];
};
struct ieee80211com {
	int ic_opmode, ic_state;
	uint32_t ic_flags;
	void *ic_ifp;
	uint8_t ic_chan_active[IEEE80211_CHAN_BYTES];
	struct ieee80211_channel ic_channels[IEEE80211_CHAN_MAX + 1];
	struct ieee80211_node *ic_bss;
	struct ieee80211_node_table ic_scan;
	struct ieee80211_sta_scan_state ic_sta_scan;
};

static unsigned notices;
static struct ieee80211req_sta_mgmt last_notice;
static unsigned ieee80211_mhz2ieee(uint16_t freq, int flags) {
	(void)flags;
	if (freq >= 2412 && freq <= 2472 && (freq - 2407) % 5 == 0)
		return (freq - 2407) / 5;
	return 0;
}
static void ieee80211_cancel_scan(struct ieee80211com *ic) {
	ic->ic_flags &= ~(IEEE80211_F_SCAN | IEEE80211_F_ASCAN);
}
static int ieee80211_sta_request_validate(
    const struct ieee80211req_sta_mgmt *req) {
	return req->version == IEEE80211_STA_VERSION && req->reserved == 0 &&
	    req->flags == 0 ? 0 : EINVAL;
}
static void rt_ieee80211msg(void *ifp, unsigned what, const void *payload,
    size_t len) {
	(void)ifp;
	assert(what == RTM_IEEE80211_STA_SCAN);
	assert(len == offsetof(struct ieee80211req_sta_mgmt, data));
	memset(&last_notice, 0, sizeof(last_notice));
	memcpy(&last_notice, payload, len);
	notices++;
}
static void ieee80211_notify_sta_scan(struct ieee80211com *,
	const struct ieee80211_sta_scan_state *, bool);
static int ieee80211_sta_scan_begin(struct ieee80211com *,
    const struct ieee80211req_sta_mgmt *, uint32_t);
static bool ieee80211_sta_scan_match(struct ieee80211com *,
    const struct ieee80211req_sta_mgmt *);
static bool ieee80211_sta_scan_report(struct ieee80211com *,
    const struct ieee80211req_sta_mgmt *, bool);
static bool ieee80211_sta_scan_release(struct ieee80211com *,
    const struct ieee80211req_sta_mgmt *);

static struct ieee80211com ic;
static struct ieee80211_node bss;
static struct ieee80211req_sta_mgmt req;

static void
reset_fixture(void)
{
	memset(&ic, 0, sizeof(ic));
	memset(&bss, 0, sizeof(bss));
	memset(&req, 0, sizeof(req));
	ic.ic_opmode = IEEE80211_M_STA;
	ic.ic_state = IEEE80211_S_RUN;
	ic.ic_bss = &bss;
	bss.ni_chan = &ic.ic_channels[1];
	memcpy(bss.ni_bssid, "ABCDEF", IEEE80211_ADDR_LEN);
	ic.ic_channels[1].ic_freq = 2412;
	ic.ic_channels[6].ic_freq = 2437;
	ic.ic_chan_active[0] = 0x42;
	req.version = IEEE80211_STA_VERSION;
	req.op = IEEE80211_STA_SCAN;
	req.cookie = 9;
	req.generation = 23;
	req.freq = 2412;
	memcpy(req.bssid, bss.ni_bssid, sizeof(req.bssid));
	req.len = 4;
	req.data[0] = 0x6c; req.data[1] = 0x09; /* 2412 MHz */
	/* The channel table maps 2437 MHz to channel 6. */
	req.data[2] = 0x85; req.data[3] = 0x09;
	notices = 0;
	memset(&last_notice, 0, sizeof(last_notice));
}

int
main(void)
{
	reset_fixture();
	assert(ieee80211_sta_scan_begin(&ic, &req, 23) == 0);
	assert(ic.ic_sta_scan.active && !ic.ic_sta_scan.reported);
	assert(ic.ic_scan.nt_scan_epoch == 1 && (ic.ic_flags & IEEE80211_F_SCAN));
	assert(ieee80211_sta_scan_begin(&ic, &req, 23) == EBUSY);

	struct ieee80211req_sta_mgmt stale = req;
	stale.cookie++;
	assert(!ieee80211_sta_scan_match(&ic, &stale));
	assert(!ieee80211_sta_scan_report(&ic, &stale, true));
	assert(notices == 0 && ic.ic_sta_scan.active);
	struct ieee80211req_sta_mgmt cancel = req;
	cancel.op = IEEE80211_STA_CANCEL_SCAN;
	cancel.freq = 0; /* v2 cancellation deliberately omits channel frequency. */
	assert(ieee80211_sta_scan_match(&ic, &cancel));
	assert(ieee80211_sta_scan_report(&ic, &cancel, true));
	assert(notices == 1 && last_notice.freq == req.freq &&
	    last_notice.flags == IEEE80211_STA_SCAN_ABORTED);
	assert(ieee80211_sta_scan_release(&ic, &req));

	/* A valid completion is accepted once even if RUN was retired meanwhile. */
	reset_fixture();
	assert(ieee80211_sta_scan_begin(&ic, &req, 23) == 0);
	ic.ic_state = IEEE80211_S_INIT;
	assert(ieee80211_sta_scan_match(&ic, &req));
	assert(ieee80211_sta_scan_report(&ic, &req, false));
	assert(!ieee80211_sta_scan_report(&ic, &req, true));
	assert(notices == 1 && last_notice.op == IEEE80211_STA_SCAN);
	assert(last_notice.cookie == req.cookie && last_notice.generation == req.generation);
	assert(last_notice.freq == req.freq && !memcmp(last_notice.bssid, req.bssid, 6));
	assert(last_notice.flags == 0 && last_notice.len == 0);
	assert(!(ic.ic_flags & IEEE80211_F_SCAN) && ic.ic_sta_scan.active);
	assert(ieee80211_sta_scan_release(&ic, &req));
	assert(!ieee80211_sta_scan_release(&ic, &req));

	/* Admission rollback before hardware ownership emits no event. */
	reset_fixture();
	assert(ieee80211_sta_scan_begin(&ic, &req, 23) == 0);
	assert(ieee80211_sta_scan_release(&ic, &req));
	assert(!ic.ic_sta_scan.active && !(ic.ic_flags & IEEE80211_F_SCAN));
	assert(ic.ic_scan.nt_scan_epoch == 2 && notices == 0);

	/* Abort reports once but keeps the common operation busy through drain. */
	reset_fixture();
	assert(ieee80211_sta_scan_begin(&ic, &req, 23) == 0);
	assert(ieee80211_sta_scan_report(&ic, &req, true));
	assert(ic.ic_scan.nt_scan_epoch == 2 && !(ic.ic_flags & IEEE80211_F_SCAN));
	assert(notices == 1 && last_notice.flags == IEEE80211_STA_SCAN_ABORTED);
	assert(!ieee80211_sta_scan_report(&ic, &req, true) && notices == 1);
	assert(ieee80211_sta_scan_begin(&ic, &req, 23) == EBUSY);
	assert(ieee80211_sta_scan_release(&ic, &req));

	/* Admission rejects stale association identity and invalid channels. */
	reset_fixture();
	assert(ieee80211_sta_scan_begin(&ic, &req, 24) == ESTALE);
	assert(ieee80211_sta_scan_begin(&ic, &req, 23) == 0);
	assert(ieee80211_sta_scan_report(&ic, &req, false));
	assert(ieee80211_sta_scan_release(&ic, &req));
	reset_fixture();
	req.data[2] = 0x7d; req.data[3] = 0x09;
	assert(ieee80211_sta_scan_begin(&ic, &req, 23) == EINVAL);
	assert(!ic.ic_sta_scan.active && ic.ic_scan.nt_scan_epoch == 0);
	reset_fixture();
	req.len = 3;
	assert(ieee80211_sta_scan_begin(&ic, &req, 23) == EINVAL);
	reset_fixture();
	bss.ni_chan = &ic.ic_channels[6];
	assert(ieee80211_sta_scan_begin(&ic, &req, 23) == ESTALE);
	reset_fixture();
	ic.ic_flags |= IEEE80211_F_SCAN;
	assert(ieee80211_sta_scan_begin(&ic, &req, 23) == EBUSY);

	return 0;
}
