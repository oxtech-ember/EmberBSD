/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _NET80211_IEEE80211_SAE_H_
#define _NET80211_IEEE80211_SAE_H_

#include <sys/types.h>

struct ieee80211com;
struct ieee80211_node;
struct ieee80211_key;
struct ieee80211req_sae;
struct mbuf;

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
