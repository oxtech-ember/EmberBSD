/* SPDX-License-Identifier: BSD-2-Clause */
/* Original bounded HT20 negotiation, AI-assisted. */
#ifndef _NET80211_IEEE80211_HT20_H_
#define _NET80211_IEEE80211_HT20_H_
#include <sys/types.h>
#include <net80211/ieee80211_vht_ies.h>
#define IEEE80211_HT20_READY_RXBA 0x01
#define IEEE80211_HT20_READY_SCHEDULER 0x02
#define IEEE80211_HT20_READY_AMSDU 0x04
#define IEEE80211_HT20_READY_RADIO 0x08
#define IEEE80211_HT20_READY_POWER 0x10
#define IEEE80211_HT20_READY_PROTECTION 0x20
#define IEEE80211_HT20_READY_ALL 0x3f
struct ieee80211com;
struct ieee80211_node;
struct ieee80211_channel;
struct ieee80211_frame;
/* Provider must supply current radio, power, protection, RXBA, scheduler and
 * AMSDU readiness. No provider means legacy. Equal RX/TX masks only initially.
 * Called with Giant, without a driver lock. Snapshot only; no sleeping or PHY
 * mutation. Withdraw active admission before changing the profile/generation.
 * Register after protocol attach, which resets the provider to NULL. */
struct ieee80211_ht20_profile {
	uint32_t generation;
	uint16_t rx_mcs, tx_mcs;
	uint8_t ready, ampdu; /* Factor0, provider-qualified spacing0..7. */
};
struct ieee80211_ht20_vht {
	struct ieee80211_vht_cap local, peer;
	struct ieee80211_vht_operation operation;
	struct ieee80211_vht_mcs_maps maps;
	struct ieee80211_vht_channel channel;	/* Negotiated 40/80 MHz. */
	uint8_t offered, valid;
};
struct ieee80211_ht20_node {
	uint32_t attempt, offer_attempt;
	struct ieee80211_ht20_profile profile;
	struct ieee80211_ht20_vht vht;
	uint32_t assoc_attempt; /* Snapshot of the WME association attempt. */
	struct ieee80211_channel *channel;
	uint8_t bssid[6], subtype, offered, valid, security;
	uint16_t tx_mcs, peer_rx_mcs, basic_mcs, peer_cap, operation;
	uint8_t peer_htcap[26], peer_htop[22];
};
/*
 * Numeric HT20/VHT negotiation observations owned by Giant, for read-only
 * diagnostics. offers/vht_offers count queued association requests; ht,
 * vht, legacy and unoffered count accepted responses (vht is also ht);
 * bad counts rejected responses; resets counts beacon withdrawals of a
 * committed profile, with the last reason. status/norate/failures count
 * association responses that end before or around the HT consumer: a
 * nonzero status (the last kept), a basic rate mismatch, and the HT
 * failure path (short frame, refused IEs, consumer or RUN failure).
 */
/* Peer, attempt, security, WME, or local provider readiness/generation. */
#define IEEE80211_HT20_RESET_STALE 1
#define IEEE80211_HT20_RESET_FRAMING 2
#define IEEE80211_HT20_RESET_HT 3	/* HT Capabilities or Operation. */
#define IEEE80211_HT20_RESET_VHT 4	/* VHT Operation. */
/*
 * The clause of a beacon reset: for RESET_HT and RESET_VHT the field that
 * left the contract; for RESET_STALE the ht20_stale() clause; 0 for
 * framing.
 */
#define IEEE80211_HT20_BEACON_SMPS 1	/* HT Capabilities SMPS/Greenfield. */
#define IEEE80211_HT20_BEACON_MCS 2	/* HT Capabilities MCS 0-7. */
#define IEEE80211_HT20_BEACON_PRIMARY 3	/* HT Operation primary channel. */
#define IEEE80211_HT20_BEACON_SECONDARY 4 /* Secondary offset, STA width. */
#define IEEE80211_HT20_BEACON_PROTECTION 5 /* Operation word envelope. */
#define IEEE80211_HT20_BEACON_OCTET4 6	/* HT Operation information octet 4. */
#define IEEE80211_HT20_BEACON_OCTET5 7	/* HT Operation information octet 5. */
#define IEEE80211_HT20_BEACON_BASIC_MCS 8 /* Basic HT-MCS set. */
#define IEEE80211_HT20_BEACON_NO_CAP 9	/* No HT Capabilities. */
#define IEEE80211_HT20_BEACON_NO_OP 10	/* No HT Operation. */
#define IEEE80211_HT20_BEACON_VHT_MISSING 11 /* VHT Operation absent/bad. */
#define IEEE80211_HT20_BEACON_VHT_CHANGED 12 /* VHT Operation differs. */
/* Association HT failure path, by stage. */
#define IEEE80211_HT20_FAIL_SHORT 1	/* Fixed fields missing. */
#define IEEE80211_HT20_FAIL_IES 2	/* ieee80211_ht20_check_ies(). */
#define IEEE80211_HT20_FAIL_CONSUMER 3	/* ieee80211_ht20_response(). */
#define IEEE80211_HT20_FAIL_RUN 4	/* RUN transition. */
struct ieee80211_ht20_diag {
	uint32_t offers, vht_offers;
	uint32_t ht, vht, legacy, unoffered, bad;
	uint32_t resets;
	uint32_t status, norate, failures;
	uint32_t state_fail;	/* RUN requests refused as stale. */
	uint16_t last_status;
	uint8_t reset_reason, failure_reason;
	uint8_t state_reason;	/* First stale clause, 15 = unanswered offer. */
	/* The last beacon reset: its clause (IEEE80211_HT20_BEACON_*), the
	 * beacon's and the committed HT Capabilities octets 0-3 and HT
	 * Operation octets 0-5 (element bodies, zero for an absent element),
	 * and the first differing Basic HT-MCS octet: index, beacon,
	 * committed (all zero unless that clause). */
	uint8_t reset_clause;
	uint8_t reset_cap[4], reset_peer_cap[4];
	uint8_t reset_op[6], reset_peer_op[6];
	uint8_t reset_mcs[3];
};
/* A source-qualified primary20/mixed-format, one-PPDU protection result: a
 * single MPDU or one A-MPDU, without RTS or TXOP bursts. This is not a
 * radio or hardware BlockAck attestation. */
#define IEEE80211_HT20_PROT_MF20_NO_RTS 1
struct ieee80211_ht20_protection {
	uint32_t generation;
	uint16_t operation, peer_cap;
	uint8_t secondary, qualification;
};
int ieee80211_ht20_tx_protection(struct ieee80211com *, struct ieee80211_node *,
    struct ieee80211_ht20_protection *);
int ieee80211_ht20_check_ies(const uint8_t *, size_t);
int ieee80211_ht20_optional(const void *, size_t);
int ieee80211_ht20_prepare(struct ieee80211com *, struct ieee80211_node *,
    int, struct ieee80211_ht20_profile *);
void ieee80211_ht20_write(uint8_t *, const struct ieee80211_ht20_profile *);
void ieee80211_ht20_offer(struct ieee80211com *, struct ieee80211_node *, int,
    const struct ieee80211_ht20_profile *, const struct ieee80211_vht_cap *,
    const struct ieee80211_vht_channel *);
void ieee80211_ht20_reset(struct ieee80211_node *);
int ieee80211_ht20_response(struct ieee80211com *, struct ieee80211_node *,
    int, const struct ieee80211_ht20_node *, const uint8_t *, size_t, int);
int ieee80211_ht20_beacon(struct ieee80211com *, struct ieee80211_node *,
    const struct ieee80211_frame *, const uint8_t *, size_t);
int ieee80211_ht20_state_request(struct ieee80211com *, int);
#endif
