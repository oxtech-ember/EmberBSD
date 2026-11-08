/* SPDX-License-Identifier: BSD-2-Clause */
/* Bounded station management syntax. Firmware capabilities are explicit. */
#ifndef _NET80211_IEEE80211_STA_H_
#define _NET80211_IEEE80211_STA_H_

#define IEEE80211_STA_ACTION_BTM		0x01U
#define IEEE80211_STA_ACTION_NR		0x02U
#define IEEE80211_STA_ACTION_RM_REPORT	0x04U
#define IEEE80211_STA_ACTION_RM_REQUEST	0x08U

static inline bool
ieee80211_sta_ies_valid(const uint8_t *ie, size_t len)
{
	if (ie == NULL)
		return len == 0;
	while (len != 0) {
		if (len < 2 || ie[1] > len - 2)
			return false;
		len -= ie[1] + 2;
		ie += ie[1] + 2;
	}
	return true;
}

/* WNM advertisement follows the supplicant's complete selected IE stream. */
static inline bool
ieee80211_sta_btm_requested(const uint8_t *ie, size_t len)
{
	bool requested = false;

	if (!ieee80211_sta_ies_valid(ie, len))
		return false;
	while (len != 0) {
		if (ie[0] == 127 && ie[1] >= 3 && (ie[4] & 0x08) != 0)
			requested = true;
		len -= ie[1] + 2;
		ie += ie[1] + 2;
	}
	return requested;
}

/* Bound common action elements; type-specific measurement data is opaque. */
static inline bool
ieee80211_sta_action_ies_valid(const uint8_t *ie, size_t len)
{
	if (!ieee80211_sta_ies_valid(ie, len))
		return false;
	while (len != 0) {
		switch (ie[0]) {
		case 0: /* SSID */
			if (ie[1] > 32)
				return false;
			break;
		case 38: /* Measurement request */
		case 39: /* Measurement report: token, mode and type */
			if (ie[1] < 3)
				return false;
			break;
		case 52: /* Neighbor report: fixed fields, then subelements */
			if (ie[1] < 13 ||
			    !ieee80211_sta_ies_valid(ie + 15, ie[1] - 13))
				return false;
			break;
		default:
			break;
		}
		len -= ie[1] + 2;
		ie += ie[1] + 2;
	}
	return true;
}

/* to_ap selects TX from a station; false selects RX from its associated AP.
 * This validates syntax, not peer identity, PMF protection or authorization.
 */
static inline bool
ieee80211_sta_action_valid(const uint8_t *body, size_t len, bool to_ap,
    unsigned caps, size_t max_body)
{
	size_t pos;
	unsigned required;

	if (body == NULL || len < 3 || len > max_body)
		return false;
	if (body[0] == 5) { /* RRM */
		pos = 3;
		if ((to_ap && body[1] == 4) || (!to_ap && body[1] == 5))
			required = IEEE80211_STA_ACTION_NR;
		else if (to_ap && body[1] == 1)
			required = IEEE80211_STA_ACTION_RM_REPORT;
		else if (!to_ap && body[1] == 0) {
			required = IEEE80211_STA_ACTION_RM_REQUEST;
			pos = 5; /* Repetitions */
		} else
			return false;
		if ((caps & required) == 0 || len < pos)
			return false;
	} else if (body[0] == 10 && (caps & IEEE80211_STA_ACTION_BTM)) {
		if (to_ap && body[1] == 6)
			pos = 4; /* BTM query reason */
		else if (to_ap && body[1] == 8) {
			if (len < 5)
				return false;
			pos = body[3] == 0 ? 11 : 5; /* Success requires target BSSID. */
		} else if (!to_ap && body[1] == 7) {
			pos = 7;
			if (len < pos)
				return false;
			if (body[3] & 0x08) { /* BSS termination duration */
				if (len - pos < 12 || body[pos] != 4 ||
				    body[pos + 1] != 10)
					return false;
				pos += 12;
			}
			if (body[3] & 0x10) { /* ESS disassociation URL */
				if (pos == len || body[pos] > len - pos - 1)
					return false;
				pos += 1 + body[pos];
			}
		} else
			return false;
		if (len < pos)
			return false;
	} else
		return false;
	return ieee80211_sta_action_ies_valid(body + pos, len - pos);
}

#endif /* _NET80211_IEEE80211_STA_H_ */
