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
/* A source-qualified primary20/mixed-format, single-MPDU protection result.
 * This is not a radio/PPDU or hardware BlockAck attestation. */
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
    const struct ieee80211_ht20_profile *, const struct ieee80211_vht_cap *);
void ieee80211_ht20_reset(struct ieee80211_node *);
int ieee80211_ht20_response(struct ieee80211com *, struct ieee80211_node *,
    int, const struct ieee80211_ht20_node *, const uint8_t *, size_t, int);
int ieee80211_ht20_beacon(struct ieee80211com *, struct ieee80211_node *,
    const struct ieee80211_frame *, const uint8_t *, size_t);
int ieee80211_ht20_state_request(struct ieee80211com *, int);
#endif
