/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD bounded beacon views and switch-announcement contracts. */
#ifndef _NET80211_IEEE80211_BEACON_IES_H_
#define _NET80211_IEEE80211_BEACON_IES_H_

#include <sys/types.h>
#include <net80211/ieee80211_phy_ies.h>

/* Borrowed views: the caller must retain the complete input IE window.
 * No channel selection, security negotiation or scan-cache update occurs here.
 * CSA, Extended CSA and Quiet must be unique and have exact valid bodies.
 * Other last occurrences win. The valid/bounded views preserve filtered consumers.
 */
struct ieee80211_beacon_ies {
	const uint8_t *ssid, *rates, *ssid_valid, *rates_valid, *xrates;
	const uint8_t *country, *fh, *ds, *ds_exact, *tim, *erp, *ht, *mdie;
	const uint8_t *security, *rsn, *rrm, *extcap, *extcap_bounded;
	const uint8_t *rsnx, *wme;
	const uint8_t *csa, *xcsa, *quiet;
	size_t timoff;		/* From the start of the IE window. */
	unsigned wme_duplicate, bad_erp, unknown;
	struct ieee80211_phy_ies phy;
};

/* Return 0 on success, -1 on malformed framing, short FH/DS/TIM,
 * or malformed/duplicate CSA, Extended CSA or Quiet.
 * Failure clears all output. Malformed PHY IEs only clear the PHY snapshot.
 */
int ieee80211_beacon_ies_parse(struct ieee80211_beacon_ies *,
    const uint8_t *, size_t);
/* Pure body classifiers after current-peer/RX-envelope validation.
 * Beacon accepts the fixed body plus its IE window. Action recognizes the
 * two channel-switch prefixes. Return 1 to withdraw transmission, else 0.
 * Malformed beacon windows withdraw; short unidentified Actions do not.
 * No peer selection, TX enable, timer mutation or memory allocation occurs.
 */
int ieee80211_sta_beacon_announcement(const uint8_t *, size_t);
int ieee80211_sta_action_announcement(const uint8_t *, size_t);
/* Pure validation of a borrowed fixed beacon body and complete IE window.
 * Return its wire interval in TU, or 0 for invalid input or an announcement.
 * Allocate no memory and retain no pointers. The caller validates the RX
 * envelope, peer and lifecycle, and applies its interval bounds. This helper
 * grants no TX or DFS permission and changes no state or timer.
 */
uint16_t ieee80211_sta_beacon_interval(const uint8_t *, size_t, unsigned);
#endif
