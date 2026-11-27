/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * scan-abort.py inserts the production scan cancel and abort functions.
 * A foreground scan that a stop or reset ends early still gives its
 * requester one completion event; a finished scan gives none.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define IEEE80211_F_SCAN	0x00000001
#define IEEE80211_F_ASCAN	0x00000002
#define IEEE80211_FEXT_PROBECHAN 0x00020000
#define IEEE80211_DPRINTF(ic, m, fmt, ...) do { (void)(ic); } while (0)

struct ieee80211com {
	uint32_t ic_flags;
	uint32_t ic_flags_ext;
};

static unsigned notices;

static void
ieee80211_notify_scan_done(struct ieee80211com *ic)
{
	(void)ic;
	notices++;
}

/* PRODUCTION */

int
main(void)
{
	struct ieee80211com ic = { 0 };

	/* No scan: no event. */
	ieee80211_abort_scan(&ic);
	assert(notices == 0);
	/* An active scan with a passive-channel probe pending. */
	ic.ic_flags = IEEE80211_F_SCAN | IEEE80211_F_ASCAN | 0x100;
	ic.ic_flags_ext = IEEE80211_FEXT_PROBECHAN | 0x8;
	ieee80211_abort_scan(&ic);
	assert(notices == 1 && ic.ic_flags == 0x100 && ic.ic_flags_ext == 0x8);
	/* A second stop of the same scan gives no second event. */
	ieee80211_abort_scan(&ic);
	assert(notices == 1);
	/* A scan that ended through ieee80211_end_scan() is already done. */
	ic.ic_flags |= IEEE80211_F_SCAN;
	ieee80211_cancel_scan(&ic);
	ieee80211_abort_scan(&ic);
	assert(notices == 1);
	printf("PASS: aborted scans end once for their requester\n");
	return 0;
}
