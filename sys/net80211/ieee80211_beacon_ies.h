/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _NET80211_IEEE80211_BEACON_IES_H_
#define _NET80211_IEEE80211_BEACON_IES_H_

#include <sys/types.h>
#include <net80211/ieee80211_phy_ies.h>

/* Borrowed views: the caller must retain the complete input IE window.
 * No channel selection, security negotiation or scan-cache update occurs here.
 * Last occurrences win. The valid/bounded views preserve filtered consumers.
 */
struct ieee80211_beacon_ies {
	const uint8_t *ssid, *rates, *ssid_valid, *rates_valid, *xrates;
	const uint8_t *country, *fh, *ds, *ds_exact, *tim, *erp, *ht, *mdie;
	const uint8_t *security, *rsn, *rrm, *extcap, *extcap_bounded;
	const uint8_t *rsnx, *wme;
	size_t timoff;		/* From the start of the IE window. */
	unsigned wme_duplicate, bad_erp, unknown;
	struct ieee80211_phy_ies phy;
};

/* Return 0 on success, -1 on malformed framing or short FH/DS/TIM.
 * Failure clears all output. Malformed PHY IEs only clear the PHY snapshot.
 */
int ieee80211_beacon_ies_parse(struct ieee80211_beacon_ies *,
    const uint8_t *, size_t);
#endif
