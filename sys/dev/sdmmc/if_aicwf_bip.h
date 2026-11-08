/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Compatibility contract for the isolated AIC host BIP preparation tests.
 * Actual AIC8800D80 BIP processing belongs to the firmware path; this helper
 * is not called by the driver. net80211 owns the common CMAC/BIP implementation.
 */
#ifndef _DEV_SDMMC_IF_AICWF_BIP_H_
#define _DEV_SDMMC_IF_AICWF_BIP_H_

#include <net80211/ieee80211_bip.h>

#define AICWF_BIP_MAX_FRAME IEEE80211_BIP_MAX_FRAME
#define AICWF_BIP_MAX_PN IEEE80211_BIP_MAX_PN

/* Preserve the original, narrower RRM/WNM action preparation contract. */
static inline bool
aicwf_bip_verify(ieee80211_bip_block_fn encrypt, const void *ctx,
    const uint8_t *frame, size_t len, const uint8_t source[6],
    const uint8_t bssid[6], uint16_t keyid, uint64_t previous,
    uint64_t *candidate)
{
	if (frame == NULL || len < 44 || len > AICWF_BIP_MAX_FRAME)
		return false;
	if (frame[0] == 0xd0 &&
	    (len < 45 || (frame[24] != 5 && frame[24] != 10)))
		return false;
	return ieee80211_bip_verify(encrypt, ctx, frame, len, source, bssid,
	    keyid, previous, candidate);
}

#endif /* _DEV_SDMMC_IF_AICWF_BIP_H_ */
