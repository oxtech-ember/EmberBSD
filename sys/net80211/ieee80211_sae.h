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
};
#define IEEE80211_SAE_HW_NO_QOS_TX 0x01
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
