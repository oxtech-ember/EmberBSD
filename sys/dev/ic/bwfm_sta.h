/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _DEV_IC_BWFM_STA_H_
#define _DEV_IC_BWFM_STA_H_

static inline bool
bwfm_sta_ies_valid(const uint8_t *ie, size_t len)
{
	while (len != 0) {
		if (len < 2 || ie[1] > len - 2)
			return false;
		len -= ie[1] + 2;
		ie += ie[1] + 2;
	}
	return true;
}

/* WNM advertisement must follow the supplicant's selected capabilities. */
static inline bool
bwfm_sta_btm_requested(const uint8_t *ie, size_t len)
{
	bool requested = false;

	while (len != 0) {
		if (len < 2 || ie[1] > len - 2)
			return false;
		if (ie[0] == 127 && ie[1] >= 3 && (ie[4] & 0x08) != 0)
			requested = true;
		len -= ie[1] + 2;
		ie += ie[1] + 2;
	}
	return requested;
}

/* Bound the supported action bodies before passing them across the ABI. */
static inline bool
bwfm_sta_action_valid(const uint8_t *body, size_t len, bool rx)
{
	if (len < 3 || len > 1024)
		return false;
	if (body[0] == 5) {
		/* RRM: Neighbor Report request/response and measurement report. */
		return rx ? body[1] == 5 : body[1] == 4 || body[1] == 1;
	}
	if (body[0] != 10)
		return false;
	if (rx)
		return body[1] == 7 && len >= 7;
	if (body[1] == 6)
		return len >= 4;
	return body[1] == 8 && len >= 5 && (body[3] != 0 || len >= 11);
}
#endif
