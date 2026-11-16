/* SPDX-License-Identifier: BSD-2-Clause */
/* Association capabilities shared by host-managed FullMAC adapters. */
#ifndef _NET80211_IEEE80211_ASSOC_CAPS_H_
#define _NET80211_IEEE80211_ASSOC_CAPS_H_

/*
 * Extract the unique Extended Capabilities element from a complete IE stream.
 * Retain the other IEs, including the unique five-octet RRM capabilities IE.
 * The caller supplies its hardware capacity; unsupported lengths fail before
 * modifying the stream. Outputs borrow no memory from the input.
 */
static inline bool
ieee80211_assoc_caps_extract(uint8_t *ies, size_t *len, uint8_t *extcap,
    size_t capacity, size_t *extcap_len, bool *rrm)
{
	size_t offset = 0, found = 0, found_len = 0;
	bool seen_extcap = false, seen_rrm = false;

	if (ies == NULL || len == NULL || extcap == NULL || capacity == 0 ||
	    extcap_len == NULL || rrm == NULL)
		return false;
	*extcap_len = 0;
	*rrm = false;
	memset(extcap, 0, capacity);
	while (offset < *len) {
		size_t size;

		if (*len - offset < 2 || ies[offset + 1] > *len - offset - 2)
			return false;
		size = (size_t)ies[offset + 1] + 2;
		switch (ies[offset]) {
		case 70: /* RRM Enabled Capabilities */
			if (seen_rrm || size != 7)
				return false;
			seen_rrm = true;
			break;
		case 127: /* Extended Capabilities */
			if (seen_extcap || size == 2 || size - 2 > capacity)
				return false;
			seen_extcap = true;
			found = offset;
			found_len = size;
			break;
		}
		offset += size;
	}
	if (seen_extcap) {
		memcpy(extcap, ies + found + 2, found_len - 2);
		*extcap_len = found_len - 2;
		memmove(ies + found, ies + found + found_len,
		    *len - found - found_len);
		*len -= found_len;
	}
	*rrm = seen_rrm;
	return true;
}

/*
 * Merge payloads without enlarging the hardware-owned element. WNM Sleep,
 * BSS Transition Management, and SSID List belong to the host protocol stack:
 * their host values replace firmware defaults, including explicit zeroes.
 * Other host capabilities augment the radio's capabilities.
 */
static inline bool
ieee80211_sta_extcap_merge(uint8_t *radio, size_t radio_len,
    const uint8_t *host, size_t host_len)
{
	if (radio == NULL || (host == NULL && host_len != 0) ||
	    host_len > radio_len)
		return false;
	for (size_t i = 0; i < radio_len; i++) {
		uint8_t mask = i == 2 ? 0x0a : i == 3 ? 0x02 : 0;
		uint8_t value = i < host_len ? host[i] : 0;

		radio[i] = (radio[i] & (uint8_t)~mask) | value;
	}
	return true;
}

#endif /* _NET80211_IEEE80211_ASSOC_CAPS_H_ */
