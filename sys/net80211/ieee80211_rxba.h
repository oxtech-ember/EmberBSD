/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _NET80211_IEEE80211_RXBA_H_
#define _NET80211_IEEE80211_RXBA_H_
#include <sys/types.h>
struct ieee80211com;
struct ieee80211_node;
struct ieee80211_key;
struct mbuf;
struct ieee80211_rxba_tid;
struct ieee80211_rxba_candidate;
#define IEEE80211_RXBA_TIDS 8
#define IEEE80211_RXBA_WINDOW 64
#define IEEE80211_RXBA_BUDGET (2U * 1024 * 1024)
#define IEEE80211_RXBA_HOLE_MS 100
#define IEEE80211_RXBA_CHAIN_MAX 16
#define IEEE80211_RXBA_TAG_MAX 16
/* All operations require Giant. Providers retain no node/key pointers.
 * Callbacks are bounded, non-reentrant and do not call the shared stack.
 * Cleanup cancels logically only. Provider detach owns halt outside its locks. */
struct ieee80211_rxba_ops {
	uint64_t (*now_ms)(struct ieee80211com *);
	int (*schedule)(struct ieee80211com *, uint64_t, uint64_t);
	void (*cancel)(struct ieee80211com *);
};
struct ieee80211_rxba_state {
	struct ieee80211_rxba_tid *tid[IEEE80211_RXBA_TIDS];
	uint64_t epoch, key_epoch, timer_token;
	size_t bytes;
};
int ieee80211_rxba_ready(struct ieee80211com *);
void ieee80211_rxba_reset(struct ieee80211com *, struct ieee80211_node *);
void ieee80211_rxba_key_change(struct ieee80211com *, struct ieee80211_key *);
int ieee80211_rxba_action(struct ieee80211com *, struct ieee80211_node *,
    const uint8_t *, size_t);
int ieee80211_rxba_bar(struct ieee80211com *, struct ieee80211_node *,
    const uint8_t *, size_t);
int ieee80211_rxba_input(struct ieee80211com *, struct ieee80211_node *, struct mbuf *);
void ieee80211_rxba_age(struct ieee80211com *, struct ieee80211_node *, uint64_t);
unsigned ieee80211_rxba_mask(struct ieee80211com *, struct ieee80211_node *);
int ieee80211_rxba_response_check(struct ieee80211com *, struct ieee80211_node *,
    struct mbuf *, struct ieee80211_rxba_candidate *);
void ieee80211_rxba_response_commit(struct ieee80211com *, struct ieee80211_node *,
    struct ieee80211_rxba_candidate *);
/* Actual output/input continuations. Callers own one synchronous node reference. */
int ieee80211_send_rxba_response(struct ieee80211com *, struct ieee80211_node *,
    uint8_t, uint16_t, uint16_t, uint16_t, struct ieee80211_rxba_candidate *);
void ieee80211_input_ordered(struct ieee80211com *, struct ieee80211_node *, struct mbuf *);
#endif
