/* SPDX-License-Identifier: BSD-2-Clause */
/* Original bounded scan observations, AI-assisted. */
#ifndef _NET80211_IEEE80211_PHY_IES_H_
#define _NET80211_IEEE80211_PHY_IES_H_
#include <sys/types.h>
#define IEEE80211_PHY_IES_MAXLEN 73
struct ieee80211_phy_ies {
	uint8_t len;
	uint8_t data[IEEE80211_PHY_IES_MAXLEN];
};
int ieee80211_phy_ies_parse(struct ieee80211_phy_ies *, const uint8_t *,
			    size_t);
size_t ieee80211_phy_ies_size(const struct ieee80211_phy_ies *, size_t);
#endif
