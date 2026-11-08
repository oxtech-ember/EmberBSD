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
/* Firmware owns BA negotiation; the host owns only the receive window.
 * The adapter serializes every operation and the final delivery check.
 * Zero-initialize once. Retire preserves all generation high-water marks.
 * Association, key and per-TID session generations are nonzero, never reused,
 * and never UINT64_MAX. Key generations increase within an association.
 * Start is explicit: input never creates a session.
 *
 * Input consumes an exclusively owned, acyclic m_nextpkt chain on every
 * return. Each member is an ordinary, acyclic mbuf packet. Bound packet,
 * segment and tag counts before retaining it. A slot is one complete MPDU.
 * Outputs must be zeroed before each operation and consumed after unlocking.
 * Ready chains carry their original generations; current() and the final
 * handoff must be atomic with retirement. No callbacks run from this API.
 * A drain permits delivery until the next start, discard or epoch change.
 * Timer providers capture token at callback entry, then age under the lock.
 * Logical cancellation does not replace provider halt at device destruction.
 */
#define IEEE80211_RXBA_OFFLOAD_TIDS 16
#define IEEE80211_RXBA_PACKETS_MAX 256
struct ieee80211_rxba_batch {
	struct mbuf *head, *tail;
	uint64_t epoch, key_epoch, session;
};
struct ieee80211_rxba_output {
	struct ieee80211_rxba_batch ready[IEEE80211_RXBA_OFFLOAD_TIDS];
	struct mbuf *discard, *discard_tail;
	struct mbuf *rejected; /* Invalid input: no unbounded tail walk under lock. */
	unsigned gaps;
};
struct ieee80211_rxba_offload {
	struct ieee80211_rxba_tid *tid[IEEE80211_RXBA_OFFLOAD_TIDS];
	uint64_t session[IEEE80211_RXBA_OFFLOAD_TIDS];
	uint64_t deliver[IEEE80211_RXBA_OFFLOAD_TIDS];
	uint64_t epoch, key_epoch, last_epoch, last_key;
	uint64_t serial, timer_token, deadline, now;
	size_t bytes;
	unsigned hole_ms;
};
int ieee80211_rxba_offload_configure(struct ieee80211_rxba_offload *,
    uint64_t, uint64_t, unsigned, uint64_t, struct ieee80211_rxba_output *);
void ieee80211_rxba_offload_retire(struct ieee80211_rxba_offload *,
    struct ieee80211_rxba_output *);
int ieee80211_rxba_offload_start(struct ieee80211_rxba_offload *, unsigned,
    uint64_t, uint64_t, uint64_t, unsigned, unsigned, uint64_t,
    struct ieee80211_rxba_output *);
int ieee80211_rxba_offload_stop(struct ieee80211_rxba_offload *, unsigned,
    uint64_t, uint64_t, uint64_t, int, struct ieee80211_rxba_output *);
int ieee80211_rxba_offload_input(struct ieee80211_rxba_offload *, unsigned,
    uint64_t, uint64_t, uint64_t, unsigned, uint64_t, struct mbuf *,
    struct ieee80211_rxba_output *);
void ieee80211_rxba_offload_age(struct ieee80211_rxba_offload *, uint64_t,
    uint64_t, struct ieee80211_rxba_output *);
int ieee80211_rxba_offload_current(const struct ieee80211_rxba_offload *,
    unsigned, const struct ieee80211_rxba_batch *);
/* Raw node operations below require Giant. Providers retain no node/key pointers.
 * Callbacks are bounded, non-reentrant and do not call the shared stack.
 * Cleanup cancels logically only. Provider detach owns halt outside its locks. */
struct ieee80211_rxba_ops {
	uint64_t (*now_ms)(struct ieee80211com *);
	int (*schedule)(struct ieee80211com *, uint64_t, uint64_t);
	void (*cancel)(struct ieee80211com *);
};
/*
 * Originator (TX) block ack: one immediate-policy session per TID for the
 * associated AP, requested only while the RX side would be current (RUN,
 * authorized, negotiated HT). No A-MSDU inside A-MPDU, no BlockAckReq and
 * no inactivity timeout of our own: the recipient ends a session by DELBA.
 * A request without a response before RESPONSE_MS, a refusal or a DELBA
 * idles the TID for RETRY_MS; ATTEMPTS bounds requests per association.
 */
#define IEEE80211_TXBA_WINDOW 64
#define IEEE80211_TXBA_RESPONSE_MS 1000
#define IEEE80211_TXBA_RETRY_MS 10000
#define IEEE80211_TXBA_ATTEMPTS 3
#define IEEE80211_TXBA_IDLE 0
#define IEEE80211_TXBA_PENDING 1
#define IEEE80211_TXBA_ACTIVE 2
struct ieee80211_txba_tid {
	uint64_t deadline;	/* PENDING: response due; IDLE: earliest retry. */
	uint16_t window;	/* ACTIVE: recipient buffer, at most WINDOW. */
	uint8_t state, token, attempts;
};
/* Numeric observations for diagnostics; they survive a peer reset. */
struct ieee80211_txba_diag {
	uint32_t requests, accepts, refusals, timeouts, delbas, invalid;
	uint16_t last_status;
};
struct ieee80211_rxba_state {
	struct ieee80211_rxba_tid *tid[IEEE80211_RXBA_TIDS];
	uint64_t epoch, key_epoch, timer_token;
	size_t bytes;
	/* Originator sessions share the peer lifetime and reset. */
	struct ieee80211_txba_tid tx[IEEE80211_RXBA_TIDS];
	struct ieee80211_txba_diag tx_diag;
	uint32_t tx_attempt;	/* ni_ht20.attempt the sessions belong to. */
	uint8_t tx_token;
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
/* Request sessions for a TID mask; the caller is outside the driver start
 * routine, since queueing the request starts output. */
int ieee80211_txba_request(struct ieee80211com *, struct ieee80211_node *,
    unsigned);
/* The established recipient window for one TID, or 0 without a session. */
unsigned ieee80211_txba_window(struct ieee80211com *, struct ieee80211_node *,
    unsigned);
/* Actual output/input continuations. Callers own one synchronous node reference. */
int ieee80211_send_rxba_response(struct ieee80211com *, struct ieee80211_node *,
    uint8_t, uint16_t, uint16_t, uint16_t, struct ieee80211_rxba_candidate *);
int ieee80211_send_txba_request(struct ieee80211com *, struct ieee80211_node *,
    uint8_t, uint16_t, uint16_t, uint16_t);
void ieee80211_input_ordered(struct ieee80211com *, struct ieee80211_node *, struct mbuf *);
#endif
