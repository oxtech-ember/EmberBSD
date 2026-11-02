/* SPDX-License-Identifier: BSD-2-Clause */
/* Bounded RSN validation for SAE associations. */
#ifndef _NET80211_IEEE80211_RSN_H_
#define _NET80211_IEEE80211_RSN_H_


/* Parse a complete association IE stream and return its unique RSN element.
 * The returned view borrows from p. All element framing must be complete;
 * duplicate RSN/RSNXE elements and malformed RSNXE lengths are rejected.
 * The caller validates RSN security policy and preserves the other elements.
 */
static inline bool
ieee80211_assoc_rsn_parse(const uint8_t *p, size_t len,
    const uint8_t **rsn, size_t *rsn_len, size_t *rsn_offset)
{
	size_t pos = 0;
	const uint8_t *found_rsn = NULL;
	size_t found_rsn_len = 0, found_rsn_offset = 0;
	bool seen_rsn = false, seen_rsnx = false;

	if (rsn != NULL)
		*rsn = NULL;
	if (rsn_len != NULL)
		*rsn_len = 0;
	if (rsn_offset != NULL)
		*rsn_offset = 0;
	if (p == NULL || len == 0 || rsn == NULL || rsn_len == NULL ||
	    rsn_offset == NULL)
		return false;

	while (pos < len) {
		size_t element_len, payload_len;

		if (len - pos < 2)
			return false;
		payload_len = p[pos + 1];
		element_len = payload_len + 2;
		if (element_len > len - pos)
			return false;

		switch (p[pos]) {
		case 48: /* IEEE80211_ELEMID_RSN */
			if (seen_rsn)
				return false;
			seen_rsn = true;
			found_rsn = p + pos;
			found_rsn_len = element_len;
			found_rsn_offset = pos;
			break;
		case 244: /* IEEE80211_ELEMID_RSNX */
			if (seen_rsnx || payload_len == 0 || payload_len > 16 ||
			    ((size_t)(p[pos + 2] & 0x0f) + 1) != payload_len)
				return false;
			seen_rsnx = true;
			break;
		default:
			break;
		}
		pos += element_len;
	}

	if (!seen_rsn)
		return false;
	*rsn = found_rsn;
	*rsn_len = found_rsn_len;
	*rsn_offset = found_rsn_offset;
	return true;
}

/* An SAE association uses one CCMP pairwise cipher and requires PMF. */
static inline bool
ieee80211_sae_rsn_valid(const uint8_t *p, size_t len)
{
	size_t end, pos, count;

	if (p == NULL || len < 22 || p[0] != 48 || p[1] < 20)
		return false;
	end = (size_t)p[1] + 2;
	if (end > len || p[2] != 1 || p[3] != 0 ||
	    memcmp(p + 4, "\x00\x0f\xac\x04", 4) != 0 ||
	    p[8] != 1 || p[9] != 0 ||
	    memcmp(p + 10, "\x00\x0f\xac\x04", 4) != 0 ||
	    p[14] != 1 || p[15] != 0 ||
	    memcmp(p + 16, "\x00\x0f\xac\x08", 4) != 0 ||
	    (p[20] & 0xc0) != 0xc0)
		return false;
	/* Missing group management suite means the BIP-CMAC-128 default. */
	pos = 22;
	if (end == pos)
		return true;
	if (end - pos < 2)
		return false;
	count = p[pos] | (size_t)p[pos + 1] << 8;
	pos += 2;
	if (count > (end - pos) / 16)
		return false;
	pos += count * 16;
	return end == pos || (end - pos == 4 &&
	    memcmp(p + pos, "\x00\x0f\xac\x06", 4) == 0);
}

#endif
