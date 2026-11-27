/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD station management for host-MLME radios, AI-assisted. */
#ifndef _NET80211_IEEE80211_STA_SOFT_H_
#define _NET80211_IEEE80211_STA_SOFT_H_

#include <sys/types.h>

struct ieee80211com;
struct ieee80211_channel;
struct ieee80211_node;
struct ieee80211_frame;
struct ieee80211req_sta_mgmt;

/*
 * A radio whose MLME runs in net80211 (frames pass through
 * ieee80211_input() and ieee80211_mgmt_output()) uses this owner for
 * IEEE80211_IOC_STA_MGMT. net80211 sends and receives the BSS Transition
 * Management frames itself. The adapter supplies only radio tuning for
 * the connected scan.
 *
 * sso_tune() moves the radio to the 20 MHz primary channel c for a passive
 * listen period, or back to the BSS operating channel and width when c is
 * NULL. While the radio is away it holds station TX in its queues and does
 * not count missing beacons of the BSS. The call can sleep. It runs with
 * the kernel lock and without splnet. A NULL call without an earlier tune,
 * or after the association left, only releases the adapter scan state.
 */
struct ieee80211_sta_soft_ops {
	int (*sso_tune)(struct ieee80211com *, struct ieee80211_channel *);
};

/* Attach after ieee80211_ifattach(); set ic_sta_mgmt. Detach first. */
int	ieee80211_sta_soft_attach(struct ieee80211com *,
	    const struct ieee80211_sta_soft_ops *);
void	ieee80211_sta_soft_detach(struct ieee80211com *);

/* The ic_sta_mgmt implementation. */
int	ieee80211_sta_soft_mgmt(struct ieee80211com *,
	    struct ieee80211req_sta_mgmt *);

/*
 * Consume a beacon or probe response that the radio received on freq
 * (MHz) during a connected-scan listen period. Return false when no
 * listen period is active; the frame then goes to ieee80211_input().
 * rssi uses the ieee80211_input() scale; rssi_dbm is the signal in dBm.
 */
bool	ieee80211_sta_soft_rx_scan(struct ieee80211com *, const void *, size_t,
	    unsigned int, int, int);

/* Internal net80211 hooks. */
bool	ieee80211_sta_soft_action(struct ieee80211com *,
	    struct ieee80211_node *, const struct ieee80211_frame *,
	    const uint8_t *, size_t);
void	ieee80211_sta_soft_state(struct ieee80211com *, int);

/*
 * Pure admission of a BSS Transition Management Request from the BSS.
 * bss and own are the BSSID and the station address; a1..a3 come from the
 * frame header. Return true when the frame can go to the supplicant.
 */
static inline bool
ieee80211_sta_soft_rx_admit(const uint8_t *bss, const uint8_t *own,
    const uint8_t *a1, const uint8_t *a2, const uint8_t *a3,
    uint8_t fc1, uint8_t seq0, const uint8_t *body, size_t len)
{
	size_t i;

	/* Protected frames arrive decrypted with the bit cleared. */
	if ((fc1 & 0x07) != 0 || (fc1 & 0x40) != 0 || (fc1 & 0x80) != 0 ||
	    (seq0 & 0x0f) != 0)
		return false;
	for (i = 0; i < 6; i++)
		if (a1[i] != own[i] || a2[i] != bss[i] || a3[i] != bss[i])
			return false;
	/* WNM category, BSS Transition Management Request. */
	return body != NULL && len >= 7 && body[0] == 10 && body[1] == 7;
}

#endif /* _NET80211_IEEE80211_STA_SOFT_H_ */
