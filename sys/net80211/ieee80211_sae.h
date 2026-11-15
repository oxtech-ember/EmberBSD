/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _NET80211_IEEE80211_SAE_H_
#define _NET80211_IEEE80211_SAE_H_

#include <sys/types.h>

struct ieee80211com;
struct ieee80211_node;
struct ieee80211_key;
struct ieee80211req_sae;
struct mbuf;

/* RX-only providers keep software CCMP TX and software BIP verification.
 * replace receives the complete set (maximum seven keys), valid only for the
 * call. It must fence/drain old RX and TX before replacing firmware keys.
 * It may sleep; failure must leave hardware admission fenced. fence must not
 * sleep and must immediately block host admission, including queued TX.
 * The ops object must outlive detach. All callbacks run under serialization;
 * detach must wait for any sleeping request to return before freeing ic.
 *
 * Each key has an instance. The owner assigns it when it accepts new key
 * material, and never uses that value again. An instance is never zero. A
 * key that did not change keeps its instance in each later set. A provider
 * with KEEP_KEYS must not remove or program a slot whose instance is equal
 * to the programmed instance, because firmware counters must continue. It
 * removes a slot that is absent or has a different instance, and programs
 * only new instances.
 */
#define IEEE80211_SAE_HW_PTK 0xffff
#define IEEE80211_SAE_HW_BIP 0x80
struct ieee80211_sae_hw_key {
	uint16_t id;
	uint8_t cipher, flags;
	uint8_t key[16];
	/* CCMP non-QoS counter, or BIP IPN. Shared replay is authoritative;
	 * do not seed every firmware QoS domain from this scalar. */
	uint64_t rsc;
	uint64_t instance;
};
#define IEEE80211_SAE_HW_NO_QOS_TX 0x01
/* Hardware TX capabilities. Without them the provider keeps software TX.
 * KEEP_KEYS: the provider retains a slot whose instance did not change.
 * TX_DATA: firmware encrypts pairwise data and allocates the pairwise TX PN.
 * TX_MGMT: firmware encrypts unicast robust management, host-submitted and
 * firmware-originated, with the same counter.
 * TX_BA: firmware protects the Block Ack actions that it originates.
 * CCMP has one TX PN for a pairwise key, thus TX_DATA and TX_MGMT are valid
 * only together, and both need KEEP_KEYS. A TX provider sets TX_BA, or it
 * sets IEEE80211_SAE_HW_NO_QOS_TX and keeps the non-QoS path; never both.
 * ieee80211_sae_attach_hw() rejects each other combination.
 *
 * A TX provider is the only owner of the pairwise TX PN, which starts at
 * zero one time for each pairwise instance. It calls ieee80211_sae_hw_tx()
 * for each frame and never encrypts in software. It creates aggregation
 * queues only while ieee80211_sae_agg_tx_allowed() is true and removes them
 * before it removes the pairwise instance. A firmware reset loses the
 * counter: the provider must call ieee80211_sae_retire(). A failed replace
 * retires the session of a TX provider. */
#define IEEE80211_SAE_HW_KEEP_KEYS 0x02
#define IEEE80211_SAE_HW_TX_DATA 0x04
#define IEEE80211_SAE_HW_TX_MGMT 0x08
#define IEEE80211_SAE_HW_TX_BA 0x10
struct ieee80211_sae_hw_ops {
	void (*fence)(struct ieee80211com *);
	int (*replace)(struct ieee80211com *, uint64_t,
	    const struct ieee80211_sae_hw_key *, size_t);
	unsigned flags;
};
int ieee80211_sae_attach_hw(struct ieee80211com *,
    const struct ieee80211_sae_hw_ops *);
#define IEEE80211_SAE_RX_AUTHENTICATED 1
#define IEEE80211_SAE_RX_IV_PRESENT 2
#define IEEE80211_SAE_RX_MIC_STRIPPED 4
/* Only trusted driver RX code may attach this provenance after MIC_OK.
 * The driver must drop decrypted frames when tagging fails. Never reconstruct
 * the epoch from the current owner: use the epoch of the hardware key set.
 * Tags do not transfer ownership of m. Allocation is bounded/NOWAIT.
 */
int ieee80211_sae_tag_rx(struct ieee80211com *, struct mbuf *, uint64_t,
    uint16_t, uint8_t, unsigned);
/* Firmware-deaggregated A-MSDU: descriptor index 1 starts an aggregate,
 * indices increase through 127, and last closes it. Each subframe retains
 * its original sequence control, QoS TID, and CCMP IV. */
int ieee80211_sae_tag_rx_amsdu(struct ieee80211com *, struct mbuf *, uint64_t,
    uint16_t, uint8_t, unsigned, unsigned, bool);
bool ieee80211_sae_rx_amsdu(const struct ieee80211com *,
    const struct ieee80211_key *, struct mbuf *, uint64_t *, unsigned *, bool *);
/* Input duplicate filtering may defer only authenticated subframes. */
bool ieee80211_sae_rx_deaggregated(const struct ieee80211com *,
    const struct ieee80211_node *, struct mbuf *);
bool ieee80211_sae_rx_continuation(const struct ieee80211com *,
    const struct ieee80211_node *, struct mbuf *);
/* Crypto internal: -1 reject, 0 ordinary software/provider path, 1 trusted RX. */
int ieee80211_sae_rx_status(const struct ieee80211com *,
    const struct ieee80211_key *, struct mbuf *);

/* Opt-in only: raw management RX/TX, software CCMP, synchronous key fencing,
 * and atomic 64-bit publication. All entry points except serial() require the
 * provider's net80211/Giant serialization. ioctl copyin can yield and checks
 * serial again before dispatch.
 */
int ieee80211_sae_attach(struct ieee80211com *);
void ieee80211_sae_detach(struct ieee80211com *);
void ieee80211_sae_retire(struct ieee80211com *);
void ieee80211_sae_leave(struct ieee80211com *);
/* State arguments use IEEE80211_S_* protocol codes. Call this request hook
 * before a provider queues deferred state work, before driver locks. */
void ieee80211_sae_state_request(struct ieee80211com *, int);
bool ieee80211_sae_active(const struct ieee80211com *);
bool ieee80211_sae_qos_tx_allowed(const struct ieee80211com *);
/* False during SAE unless a TX provider has the pairwise key programmed
 * and the controlled port is open. */
bool ieee80211_sae_agg_tx_allowed(const struct ieee80211com *);
/* TX provider disposition after tagging: -1 drop the frame, 0 transmit with
 * firmware encryption disabled, 1 firmware encrypts with the pairwise key.
 * Without SAE or without a TX provider the result is 0 for a current frame.
 * The owner counts the frames of one pairwise instance and retires the
 * session before the firmware PN can wrap. */
int ieee80211_sae_hw_tx(struct ieee80211com *,
    const struct ieee80211_node *, struct mbuf *);
bool ieee80211_sae_port_allowed(const struct ieee80211com *);
bool ieee80211_sae_data_allowed(const struct ieee80211com *,
    const struct ieee80211_node *, const struct ieee80211_key *, bool);
/* Safe outside Giant while the provider retains ic; no owner pointer read. */
uint64_t ieee80211_sae_serial(const struct ieee80211com *);
int ieee80211_sae_request(struct ieee80211com *, const struct ieee80211req_sae *);
int ieee80211_sae_state(struct ieee80211com *, int);
/* 0: continue ordinary management, 1: handled, -1: discard. */
int ieee80211_sae_input(struct ieee80211com *, struct ieee80211_node *,
    struct mbuf **);
int ieee80211_sae_tag_tx(struct ieee80211com *, const struct ieee80211_node *,
    struct mbuf *);
bool ieee80211_sae_tx_current(const struct ieee80211com *,
    const struct ieee80211_node *, struct mbuf *);
/* Native management selector. result=1 means ciphertext already complete. */
int ieee80211_sae_crypto_tx(const struct ieee80211com *,
    const struct ieee80211_node *, struct mbuf *);
void ieee80211_sae_crypto_done(struct mbuf *);
void ieee80211_sae_key_changed(struct ieee80211com *, const struct ieee80211_key *);

#endif
