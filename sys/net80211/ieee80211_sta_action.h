/* SPDX-License-Identifier: BSD-2-Clause */
/* Pure state transitions for bounded station Action TX. */
#ifndef _NET80211_IEEE80211_STA_ACTION_H_
#define _NET80211_IEEE80211_STA_ACTION_H_

#include <sys/errno.h>
#include <net80211/ieee80211_ioctl.h>
#include <net80211/ieee80211_sta.h>

/* Body bytes and transport ownership stay with the adapter. */
struct ieee80211_sta_action_state {
	bool active;
	bool cancelled;
	bool reported;
	uint32_t cookie;
	uint32_t generation;
	uint16_t freq;
	uint8_t bssid[IEEE80211_ADDR_LEN];
};

/* Caller supplies the serialization used for this station interface. */
static inline int
ieee80211_sta_action_begin(struct ieee80211_sta_action_state *state,
    const struct ieee80211req_sta_mgmt *req)
{
	bool bssid_zero = true;

	if (state == NULL || req == NULL ||
	    req->version != IEEE80211_STA_VERSION || req->op != IEEE80211_STA_TX ||
	    req->cookie == 0 || req->generation == 0 || req->freq == 0 ||
	    req->len > IEEE80211_STA_MAX_BODY || req->reserved != 0 ||
	    req->flags != 0)
		return EINVAL;
	for (size_t i = 0; i < IEEE80211_ADDR_LEN; i++)
		bssid_zero &= req->bssid[i] == 0;
	if (bssid_zero || (req->bssid[0] & 1) != 0)
		return EINVAL;
	if (state->active)
		return EBUSY;
	state->active = true;
	state->cancelled = false;
	state->reported = false;
	state->cookie = req->cookie;
	state->generation = req->generation;
	state->freq = req->freq;
	memcpy(state->bssid, req->bssid, sizeof(state->bssid));
	return 0;
}

static inline bool
ieee80211_sta_action_match(const struct ieee80211_sta_action_state *state,
    const struct ieee80211req_sta_mgmt *req)
{
	return state != NULL && req != NULL && state->active &&
	    req->version == IEEE80211_STA_VERSION && req->op == IEEE80211_STA_TX &&
	    req->cookie == state->cookie && req->generation == state->generation &&
	    req->freq == state->freq &&
	    memcmp(req->bssid, state->bssid, IEEE80211_ADDR_LEN) == 0;
}

static inline bool
ieee80211_sta_action_cancel(struct ieee80211_sta_action_state *state,
    const struct ieee80211req_sta_mgmt *req)
{
	if (state == NULL || req == NULL || !state->active ||
	    req->version != IEEE80211_STA_VERSION ||
	    req->op != IEEE80211_STA_CANCEL_TX || req->cookie == 0 ||
	    req->len != 0 || req->reserved != 0 || req->flags != 0 ||
	    req->cookie != state->cookie || req->generation != state->generation ||
	    (req->freq != 0 && req->freq != state->freq) ||
	    memcmp(req->bssid, state->bssid, IEEE80211_ADDR_LEN) != 0)
		return false;
	state->cancelled = true;
	return true;
}

static inline void
ieee80211_sta_action_invalidate(struct ieee80211_sta_action_state *state)
{
	if (state != NULL && state->active)
		state->cancelled = true;
}

/* Fills only the fixed STA_TX route header; caller routes after unlock. */
static inline bool
ieee80211_sta_action_report(struct ieee80211_sta_action_state *state,
    const struct ieee80211req_sta_mgmt *req, bool ack,
    struct ieee80211req_sta_mgmt *notice)
{
	if (!ieee80211_sta_action_match(state, req) || state->cancelled ||
	    state->reported || notice == NULL)
		return false;
	state->reported = true;
	memset(notice, 0, offsetof(struct ieee80211req_sta_mgmt, data));
	notice->version = IEEE80211_STA_VERSION;
	notice->op = IEEE80211_STA_TX;
	notice->cookie = state->cookie;
	notice->generation = state->generation;
	notice->flags = ack ? IEEE80211_STA_TX_ACK : 0;
	notice->freq = state->freq;
	memcpy(notice->bssid, state->bssid, sizeof(notice->bssid));
	return true;
}

/* Release follows when old work cannot touch, release, or complete a
 * replacement tuple; borrowed transport buffers may require actual drain.
 */
static inline bool
ieee80211_sta_action_release(struct ieee80211_sta_action_state *state,
    const struct ieee80211req_sta_mgmt *req)
{
	if (!ieee80211_sta_action_match(state, req))
		return false;
	memset(state, 0, sizeof(*state));
	return true;
}

#endif /* _NET80211_IEEE80211_STA_ACTION_H_ */
