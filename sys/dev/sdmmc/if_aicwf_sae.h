/* SPDX-License-Identifier: BSD-2-Clause */
/* Bounded decoding of the AIC8800D80 external-authentication wire format. */
#ifndef _DEV_SDMMC_IF_AICWF_SAE_H_
#define _DEV_SDMMC_IF_AICWF_SAE_H_

static inline bool
aicwf_sae_peer_valid(const uint8_t *peer, uint32_t generation)
{
	return generation != 0 && (peer[0] & 1) == 0 &&
	    (peer[0] | peer[1] | peer[2] | peer[3] | peer[4] | peer[5]) != 0;
}

static inline bool
aicwf_sae_request_valid(const uint8_t *p, size_t len, uint8_t vif,
    const uint8_t *peer, const uint8_t *ssid, size_t ssid_len)
{
	/* vif, mac_ssid, mac_addr, SAE selector (legacy firmware swaps it). */
	return len >= 44 && ssid_len <= 32 && p[0] == vif &&
	    p[1] == ssid_len && memcmp(p + 2, ssid, ssid_len) == 0 &&
	    memcmp(p + 34, peer, 6) == 0 &&
	    (memcmp(p + 40, "\x08\xac\x0f\x00", 4) == 0 ||
	    memcmp(p + 40, "\x00\x0f\xac\x08", 4) == 0);
}

static inline bool
aicwf_sae_frame_valid(const uint8_t *p, size_t len, const uint8_t *dest,
    const uint8_t *source, const uint8_t *peer)
{
	/* Only unfragmented, unprotected SAE authentication (retry is allowed). */
	return len >= 30 && len <= 1536 && p[0] == 0xb0 &&
	    (p[1] & ~0x08) == 0 && (p[22] & 0x0f) == 0 &&
	    p[24] == 3 && p[25] == 0 &&
	    memcmp(p + 4, dest, 6) == 0 &&
	    memcmp(p + 10, source, 6) == 0 &&
	    memcmp(p + 16, peer, 6) == 0;
}

static inline bool
aicwf_sae_igtk_valid(const uint8_t *p, size_t len)
{
	/* The firmware key command has no initial IPN field. Fail closed. */
	return len == 24 && (p[0] == 4 || p[0] == 5) && p[1] == 0 &&
	    memcmp(p + 2, "\0\0\0\0\0\0", 6) == 0;
}

/* Unfragmented unicast management traffic with the selected peer. */
static inline bool
aicwf_pmf_header_valid(const uint8_t *p, size_t len, const uint8_t *dest,
    const uint8_t *source, const uint8_t *peer, uint8_t protected)
{
	return len >= 24 && len <= 1536 &&
	    (p[1] & ~0x08) == protected && (p[22] & 0x0f) == 0 &&
	    memcmp(p + 4, dest, 6) == 0 &&
	    memcmp(p + 10, source, 6) == 0 &&
	    memcmp(p + 16, peer, 6) == 0;
}

static inline bool
aicwf_pmf_unprot_valid(const uint8_t *p, size_t len, const uint8_t *self,
    const uint8_t *peer)
{
	return len == 26 && (p[0] == 0xc0 || p[0] == 0xa0) &&
	    aicwf_pmf_header_valid(p, len, self, peer, peer, 0) &&
	    (p[24] == 6 || p[24] == 7) && p[25] == 0;
}

static inline bool
aicwf_pmf_query_valid(const uint8_t *p, size_t len, const uint8_t *dest,
    const uint8_t *source, const uint8_t *peer)
{
	return len == 28 && p[0] == 0xd0 &&
	    aicwf_pmf_header_valid(p, len, dest, source, peer, 0) &&
	    p[24] == 8 && p[25] <= 1;
}

/* Firmware retains the CCMP header in protected management uploads. */
static inline bool
aicwf_pmf_query_rx(const uint8_t *p, size_t len, const uint8_t *self,
    const uint8_t *peer, uint32_t status, uint8_t key, uint64_t previous,
    uint8_t *frame, uint64_t *pn)
{
	/* CCMP-128, successful/done, no RX errors, current key SRAM slot. */
	if (len < 36 || p[0] != 0xd0 ||
	    !aicwf_pmf_header_valid(p, len, self, peer, peer, 0x40) ||
	    ((status >> 2) & 7) != 3 || (status & 0x7e0) != 0 ||
	    (status & 0x02006000) != 0x02006000 ||
	    ((status >> 15) & 0x3ff) != key ||
	    p[26] != 0 || p[27] != 0x20 || p[32] != 8 || p[33] > 1)
		return false;
	*pn = (uint64_t)p[24] | (uint64_t)p[25] << 8 |
	    (uint64_t)p[28] << 16 | (uint64_t)p[29] << 24 |
	    (uint64_t)p[30] << 32 | (uint64_t)p[31] << 40;
	if (*pn <= previous)
		return false;
	memcpy(frame, p, 24);
	frame[1] &= ~0x40;
	memcpy(frame + 24, p + 32, 4);
	return true;
}
#endif
