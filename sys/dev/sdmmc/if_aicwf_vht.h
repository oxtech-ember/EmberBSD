/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * AIC8800D80 capability and association wire fields. Protocol reference:
 * Radxa SDIO driver d13d07963cd15d731e2895e8288a04cca6152ac9,
 * lmac_mac.h, lmac_msg.h, hal_desc.h and rwnx_mod_params.c.
 */
#ifndef _DEV_SDMMC_IF_AICWF_VHT_H_
#define _DEV_SDMMC_IF_AICWF_VHT_H_

#include <net80211/ieee80211_vht_ies.h>

#define AICWF_ME_CONFIG_LEN		112
#define AICWF_ME_CONFIG_HT_INFO		0
#define AICWF_ME_CONFIG_HT_AMPDU		2
#define AICWF_ME_CONFIG_HT_MCS		3
#define AICWF_ME_CONFIG_VHT_INFO		32
#define AICWF_ME_CONFIG_VHT_RX_MAP	36
#define AICWF_ME_CONFIG_VHT_RX_HIGH	38
#define AICWF_ME_CONFIG_VHT_TX_MAP	40
#define AICWF_ME_CONFIG_VHT_TX_HIGH	42
#define AICWF_ME_CONFIG_TX_LIFETIME	100
#define AICWF_ME_CONFIG_BW_MAX		102
#define AICWF_ME_CONFIG_HT_SUPP		103
#define AICWF_ME_CONFIG_VHT_SUPP		104
#define AICWF_HT_INFO			0x016f
#define AICWF_HT_AMPDU			0x1f
#define AICWF_TX_LIFETIME		100
#define AICWF_BW_40			1
#define AICWF_BW_80			2

static inline bool
aicwf_vht80_available(uint32_t fw_version, uint32_t phy, uint32_t features,
    bool band5)
{
	return fw_version == 0x06090101 && band5 &&
	    (features & 0x00000200) != 0 && (phy & 0x00400000) != 0 &&
	    ((phy >> 8) & 15) >= 1 && ((phy >> 24) & 3) >= AICWF_BW_80;
}

/* Return the configured capability, not the negotiated association mode. */
static inline bool
aicwf_me_config_build(uint8_t *req, uint32_t fw_version, uint32_t phy,
    uint32_t features, bool band5)
{
	const bool vht = aicwf_vht80_available(fw_version, phy, features, band5);

	memset(req, 0, AICWF_ME_CONFIG_LEN);
	le16enc(req + AICWF_ME_CONFIG_HT_INFO, AICWF_HT_INFO);
	req[AICWF_ME_CONFIG_HT_AMPDU] = AICWF_HT_AMPDU;
	req[AICWF_ME_CONFIG_HT_MCS] = 0xff;
	le16enc(req + AICWF_ME_CONFIG_TX_LIFETIME, AICWF_TX_LIFETIME);
	req[AICWF_ME_CONFIG_HT_SUPP] = 1;
	req[AICWF_ME_CONFIG_BW_MAX] = AICWF_BW_40;
	if (vht) {
		/*
		 * RX LDPC, SGI80 and RX STBC1. Limit MPDUs to 3895 bytes
		 * and A-MPDUs to 65535 bytes. Width bits zero allow 80 MHz,
		 * without 160/80+80. No beamforming or HE is advertised.
		 */
		le32enc(req + AICWF_ME_CONFIG_VHT_INFO, 0x01800130);
		/* NSS 1 supports MCS 0-9; NSS 2-8 are unsupported. */
		le16enc(req + AICWF_ME_CONFIG_VHT_RX_MAP, 0xfffe);
		le16enc(req + AICWF_ME_CONFIG_VHT_TX_MAP, 0xfffe);
		/* Vendor long-GI capability maximum, not a live PHY rate. */
		le16enc(req + AICWF_ME_CONFIG_VHT_RX_HIGH, 390);
		le16enc(req + AICWF_ME_CONFIG_VHT_TX_HIGH, 390);
		req[AICWF_ME_CONFIG_BW_MAX] = AICWF_BW_80;
		req[AICWF_ME_CONFIG_VHT_SUPP] = 1;
	}
	return vht;
}

struct aicwf_assoc_ies {
	bool		vht;
	bool		operation;
	uint32_t	cap;
	uint16_t	rx_map;
	uint16_t	tx_map;
	uint8_t		op_width;
	uint8_t		seg0;
	uint8_t		seg1;
};

struct aicwf_assoc_info {
	struct aicwf_assoc_ies req;
	struct aicwf_assoc_ies rsp;
	bool		channel;
	uint8_t		band;
	uint8_t		width;
	uint16_t	primary;
	uint32_t	center1;
	uint32_t	center2;
};

static inline bool
aicwf_assoc_ies_parse(const uint8_t *p, size_t len,
    struct aicwf_assoc_ies *info)
{
	struct ieee80211_vht_ies ies;

	if (info == NULL)
		return false;
	memset(info, 0, sizeof(*info));
	if (ieee80211_vht_ies_parse(&ies, p, len) != 0)
		return false;
	info->vht = ies.cap_present;
	info->operation = ies.operation_present;
	info->cap = ies.cap.info;
	info->rx_map = ies.cap.rx_map;
	info->tx_map = ies.cap.tx_map;
	info->op_width = ies.operation.width;
	info->seg0 = ies.operation.seg0;
	info->seg1 = ies.operation.seg1;
	return true;
}

/* Optional diagnostics never turn a truncated indication into VHT evidence. */
static inline bool
aicwf_assoc_parse(const uint8_t *ind, size_t len,
    struct aicwf_assoc_info *info)
{
	size_t req_len, rsp_len;

	memset(info, 0, sizeof(*info));
	if (len < 20)
		return false;
	req_len = le16dec(ind + 14);
	rsp_len = le16dec(ind + 16);
	/* The fixed IE array has 800 bytes; its lengths exclude padding. */
	if (req_len > 800 || rsp_len > 800 - req_len ||
	    req_len + rsp_len > len - 20)
		return false;
	if (!aicwf_assoc_ies_parse(ind + 20, req_len, &info->req) ||
	    !aicwf_assoc_ies_parse(ind + 20 + req_len, rsp_len, &info->rsp))
		return false;
	/* Channel fields follow the complete fixed-size IE array. */
	if (len >= 836) {
		info->channel = true;
		info->band = ind[822];
		info->primary = le16dec(ind + 824);
		info->width = ind[826];
		info->center1 = le32dec(ind + 828);
		info->center2 = le32dec(ind + 832);
	}
	return true;
}

#endif /* _DEV_SDMMC_IF_AICWF_VHT_H_ */
