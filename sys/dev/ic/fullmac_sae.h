/* SPDX-License-Identifier: BSD-2-Clause */
/* Shared bounded RSN validation for FullMAC external SAE drivers. */
#ifndef _DEV_IC_FULLMAC_SAE_H_
#define _DEV_IC_FULLMAC_SAE_H_

/* An SAE association uses one CCMP pairwise cipher and requires PMF. */
static inline bool
fullmac_sae_rsn_valid(const uint8_t *p, size_t len)
{
	size_t end, pos, count;

	if (len < 22 || p[0] != 48 || p[1] < 20)
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
