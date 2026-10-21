/* SPDX-License-Identifier: BSD-2-Clause */
/* Original bounded STA A-MSDU policy, AI-assisted. */
#ifndef _NET80211_IEEE80211_AMSDU_H_
#define _NET80211_IEEE80211_AMSDU_H_
#include <sys/types.h>
#define IEEE80211_AMSDU_MAXLEN 3839
struct ieee80211_amsdu_subframe {
	size_t offset;
	size_t length;
	uint8_t header[22];
};
typedef int (*ieee80211_amsdu_read)(void *, size_t, size_t, void *);
/* Return one subframe, end, or invalid input as 1, 0, or -1. */
int ieee80211_amsdu_next(ieee80211_amsdu_read, void *, size_t,
    const uint8_t[6], size_t *, struct ieee80211_amsdu_subframe *);
#endif
