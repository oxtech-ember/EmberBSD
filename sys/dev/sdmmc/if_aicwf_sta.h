/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * AIC8800D80 station wire facts from Radxa SDIO driver
 * d13d07963cd15d731e2895e8288a04cca6152ac9: ipc_shared.h,
 * hal_desc.h, sdio_host.c, lmac_msg.h and rwnx_msg_tx.c.
 * These bounded BSD codecs do not import the vendor implementation.
 */
#ifndef _DEV_SDMMC_IF_AICWF_STA_H_
#define _DEV_SDMMC_IF_AICWF_STA_H_

#define AICWF_STA_TOKEN_MAX		0x7fffffffU
#define AICWF_STA_CFM_REQUEST		0x80000000U
#define AICWF_STA_CFM_ACK		0x00000008U
#define AICWF_STA_SCAN_LEN		376
#define AICWF_STA_SCAN_CHANNELS		42
#define AICWF_STA_SCAN_VIF		366
#define AICWF_STA_SCAN_COUNT		367
#define AICWF_STA_NOTICE_LEN		\
    offsetof(struct ieee80211req_sta_mgmt, data)

/* The high bit requests confirmation; it is not part of the host token. */
static inline bool
aicwf_sta_cfm_decode(const uint8_t *p, size_t len, uint32_t *status,
    uint32_t *token)
{
	if (len < 8)
		return false;
	*status = le32dec(p);
	*token = le32dec(p + 4) & AICWF_STA_TOKEN_MAX;
	return true;
}

static inline bool
aicwf_sta_ies_valid(const uint8_t *p, size_t len)
{
	while (len != 0) {
		if (len < 2 || p[1] > len - 2)
			return false;
		len -= p[1] + 2;
		p += p[1] + 2;
	}
	return true;
}

/* Keep the selected WPA2 mode distinct from SAE and unsupported PMF modes. */
static inline bool
aicwf_sta_psk_valid(const uint8_t *ie, size_t len)
{
	static const uint8_t ccmp[] = { 0x00, 0x0f, 0xac, 4 };
	static const uint8_t psk[] = { 0x00, 0x0f, 0xac, 2 };

	return ie != NULL && len >= 20 && aicwf_sta_ies_valid(ie, len) &&
	    ie[0] == 48 && ie[1] >= 18 && le16dec(ie + 2) == 1 &&
	    memcmp(ie + 4, ccmp, 4) == 0 && le16dec(ie + 8) == 1 &&
	    memcmp(ie + 10, ccmp, 4) == 0 && le16dec(ie + 14) == 1 &&
	    memcmp(ie + 16, psk, 4) == 0 &&
	    (ie[1] == 18 || (ie[1] >= 20 &&
	    (le16dec(ie + 20) & 0x00c0) == 0));
}

/* Return a complete result span. Never repair a truncated firmware frame. */
static inline bool
aicwf_sta_scan_result_valid(const uint8_t *p, size_t len, uint8_t vif)
{
	return len >= 48 && p[8] == vif && le16dec(p) >= 36 &&
	    le16dec(p) <= len - 12 && p[6] <= 1 &&
	    (p[6] == 0 ? le16dec(p + 4) < 3000 : le16dec(p + 4) >= 4000);
}

static inline bool
aicwf_sta_scan_complete_valid(const uint8_t *p, size_t len, uint8_t vif)
{
	return len >= 3 && p[0] == vif;
}

/* Build a passive scan. The caller validates each channel against its map. */
static inline bool
aicwf_sta_scan_build(uint8_t *req, const uint16_t *freqs, size_t count,
    uint8_t vif, int8_t power)
{
	size_t i;

	if (count == 0 || count > AICWF_STA_SCAN_CHANNELS)
		return false;
	for (i = 0; i < count; i++) {
		if (freqs[i] < 2400 || freqs[i] > 5900)
			return false;
	}
	memset(req, 0, AICWF_STA_SCAN_LEN);
	for (i = 0; i < count; i++) {
		le16enc(req + i * 6, freqs[i]);
		req[i * 6 + 2] = freqs[i] < 3000 ? 0 : 1;
		req[i * 6 + 3] = 1; /* CHAN_NO_IR: receive beacons. */
		req[i * 6 + 4] = (uint8_t)power;
	}
	memset(req + 352, 0xff, 6);
	req[AICWF_STA_SCAN_VIF] = vif;
	req[AICWF_STA_SCAN_COUNT] = count;
	return true;
}

#endif /* _DEV_SDMMC_IF_AICWF_STA_H_ */
