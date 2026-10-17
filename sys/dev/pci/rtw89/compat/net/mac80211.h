/*	$NetBSD$	*/

/*
 * Copyright (c) 2026 The EmberBSD Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * The subset of the Linux mac80211/cfg80211 interface used by rtw89.
 * Only the fields and helpers that rtw89 touches are provided; the
 * functions are implemented in rtw89_bridge.c, which emulates mac80211 for
 * a single station interface on top of NetBSD net80211.
 */

#ifndef _RTW89_COMPAT_MAC80211_H_
#define _RTW89_COMPAT_MAC80211_H_

#include "rtw89_compat.h"

/* ------------------------------------------------------------------ */
/* nl80211                                                             */
/* ------------------------------------------------------------------ */

enum nl80211_band {
	NL80211_BAND_2GHZ,
	NL80211_BAND_5GHZ,
	NL80211_BAND_60GHZ,
	NL80211_BAND_6GHZ,
	NL80211_BAND_S1GHZ,
	NL80211_BAND_LC,
	NUM_NL80211_BANDS,
};

enum nl80211_chan_width {
	NL80211_CHAN_WIDTH_20_NOHT,
	NL80211_CHAN_WIDTH_20,
	NL80211_CHAN_WIDTH_40,
	NL80211_CHAN_WIDTH_80,
	NL80211_CHAN_WIDTH_80P80,
	NL80211_CHAN_WIDTH_160,
	NL80211_CHAN_WIDTH_5,
	NL80211_CHAN_WIDTH_10,
	NL80211_CHAN_WIDTH_320 = 13,
};

enum nl80211_channel_type {
	NL80211_CHAN_NO_HT,
	NL80211_CHAN_HT20,
	NL80211_CHAN_HT40MINUS,
	NL80211_CHAN_HT40PLUS,
};

enum nl80211_iftype {
	NL80211_IFTYPE_UNSPECIFIED,
	NL80211_IFTYPE_ADHOC,
	NL80211_IFTYPE_STATION,
	NL80211_IFTYPE_AP,
	NL80211_IFTYPE_AP_VLAN,
	NL80211_IFTYPE_WDS,
	NL80211_IFTYPE_MONITOR,
	NL80211_IFTYPE_MESH_POINT,
	NL80211_IFTYPE_P2P_CLIENT,
	NL80211_IFTYPE_P2P_GO,
	NL80211_IFTYPE_P2P_DEVICE,
	NL80211_IFTYPE_OCB,
	NL80211_IFTYPE_NAN,
	NUM_NL80211_IFTYPES,
};

enum nl80211_cqm_rssi_threshold_event {
	NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW,
	NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH,
	NL80211_CQM_RSSI_BEACON_LOSS_EVENT,
};

enum nl80211_tid_config {
	NL80211_TID_CONFIG_NOT_SET,
	NL80211_TID_CONFIG_ENABLE,
	NL80211_TID_CONFIG_DISABLE,
};

enum nl80211_tid_config_attr {
	NL80211_TID_CONFIG_ATTR_AMPDU_CTRL = 9,
	NL80211_TID_CONFIG_ATTR_AMSDU_CTRL = 11,
};

enum nl80211_reg_initiator {
	NL80211_REGDOM_SET_BY_CORE,
	NL80211_REGDOM_SET_BY_USER,
	NL80211_REGDOM_SET_BY_DRIVER,
	NL80211_REGDOM_SET_BY_COUNTRY_IE,
};

enum nl80211_sar_type {
	NL80211_SAR_TYPE_POWER,
};

enum nl80211_txrate_gi {
	NL80211_TXRATE_DEFAULT_GI,
	NL80211_TXRATE_FORCE_SGI,
	NL80211_TXRATE_FORCE_LGI,
};

enum nl80211_he_gi {
	NL80211_RATE_INFO_HE_GI_0_8,
	NL80211_RATE_INFO_HE_GI_1_6,
	NL80211_RATE_INFO_HE_GI_3_2,
};

enum nl80211_he_ltf {
	NL80211_RATE_INFO_HE_1XLTF,
	NL80211_RATE_INFO_HE_2XLTF,
	NL80211_RATE_INFO_HE_4XLTF,
};

#define NL80211_STA_INFO_TX_BITRATE		8
#define NL80211_EXT_FEATURE_CAN_REPLACE_PTK0	0
#define NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR	BIT(29)
#define NL80211_SCAN_FLAG_RANDOM_ADDR		BIT(3)
#define NL80211_SCAN_FLAG_RANDOM_SN		BIT(12)

/* ------------------------------------------------------------------ */
/* 802.11 frame format                                                 */
/* ------------------------------------------------------------------ */

#define IEEE80211_FCTL_VERS		0x0003
#define IEEE80211_FCTL_FTYPE		0x000c
#define IEEE80211_FCTL_STYPE		0x00f0
#define IEEE80211_FCTL_TODS		0x0100
#define IEEE80211_FCTL_FROMDS		0x0200
#define IEEE80211_FCTL_MOREFRAGS	0x0400
#define IEEE80211_FCTL_RETRY		0x0800
#define IEEE80211_FCTL_PM		0x1000
#define IEEE80211_FCTL_MOREDATA		0x2000
#define IEEE80211_FCTL_PROTECTED	0x4000
#define IEEE80211_FCTL_ORDER		0x8000

#define IEEE80211_FTYPE_MGMT		0x0000
#define IEEE80211_FTYPE_CTL		0x0004
#define IEEE80211_FTYPE_DATA		0x0008

#define IEEE80211_STYPE_ASSOC_REQ	0x0000
#define IEEE80211_STYPE_ASSOC_RESP	0x0010
#define IEEE80211_STYPE_PROBE_REQ	0x0040
#define IEEE80211_STYPE_PROBE_RESP	0x0050
#define IEEE80211_STYPE_BEACON		0x0080
#define IEEE80211_STYPE_DISASSOC	0x00a0
#define IEEE80211_STYPE_AUTH		0x00b0
#define IEEE80211_STYPE_DEAUTH		0x00c0
#define IEEE80211_STYPE_ACTION		0x00d0
#define IEEE80211_STYPE_TRIGGER		0x0020
#define IEEE80211_STYPE_PSPOLL		0x00a0
#define IEEE80211_STYPE_DATA		0x0000
#define IEEE80211_STYPE_NULLFUNC	0x0040
#define IEEE80211_STYPE_QOS_DATA	0x0080
#define IEEE80211_STYPE_QOS_NULLFUNC	0x00c0

#define IEEE80211_SCTL_FRAG		0x000f
#define IEEE80211_SCTL_SEQ		0xfff0

#define IEEE80211_QOS_CTL_LEN		2
#define IEEE80211_QOS_CTL_TAG1D_MASK	0x0007
#define IEEE80211_QOS_CTL_EOSP		0x0010
#define IEEE80211_HT_CTL_LEN		4
#define IEEE80211_MAX_SSID_LEN		32
#define IEEE80211_NUM_ACS		4
#define IEEE80211_NUM_TIDS		16
#define IEEE80211_MAX_AMPDU_BUF_HT	0x40

enum ieee80211_ac_numbers {
	IEEE80211_AC_VO	= 0,
	IEEE80211_AC_VI	= 1,
	IEEE80211_AC_BE	= 2,
	IEEE80211_AC_BK	= 3,
};

struct ieee80211_hdr {
	__le16	frame_control;
	__le16	duration_id;
	u8	addr1[ETH_ALEN];
	u8	addr2[ETH_ALEN];
	u8	addr3[ETH_ALEN];
	__le16	seq_ctrl;
	u8	addr4[ETH_ALEN];
} __packed __aligned(2);

struct ieee80211_hdr_3addr {
	__le16	frame_control;
	__le16	duration_id;
	u8	addr1[ETH_ALEN];
	u8	addr2[ETH_ALEN];
	u8	addr3[ETH_ALEN];
	__le16	seq_ctrl;
} __packed __aligned(2);

struct ieee80211_qos_hdr {
	__le16	frame_control;
	__le16	duration_id;
	u8	addr1[ETH_ALEN];
	u8	addr2[ETH_ALEN];
	u8	addr3[ETH_ALEN];
	__le16	seq_ctrl;
	__le16	qos_ctrl;
} __packed __aligned(2);

struct ieee80211_pspoll {
	__le16	frame_control;
	__le16	aid;
	u8	bssid[ETH_ALEN];
	u8	ta[ETH_ALEN];
} __packed __aligned(2);

struct ieee80211_mgmt {
	__le16	frame_control;
	__le16	duration;
	u8	da[ETH_ALEN];
	u8	sa[ETH_ALEN];
	u8	bssid[ETH_ALEN];
	__le16	seq_ctrl;
	union {
		struct {
			__le64	timestamp;
			__le16	beacon_int;
			__le16	capab_info;
			u8	variable[];
		} __packed beacon;
		struct {
			__le64	timestamp;
			__le16	beacon_int;
			__le16	capab_info;
			u8	variable[];
		} __packed probe_resp;
		struct {
			u8	variable[0];
		} __packed probe_req;
	} u;
} __packed __aligned(2);

struct ieee80211_trigger {
	__le16	frame_control;
	__le16	duration;
	u8	ra[ETH_ALEN];
	u8	ta[ETH_ALEN];
	__le64	common_info;
	u8	variable[];
} __packed __aligned(2);
#define IEEE80211_TRIGGER_TYPE_MASK	0x000f
#define IEEE80211_TRIGGER_TYPE_BASIC	0x0

#define WLAN_EID_SSID			0
#define WLAN_EID_EXT_CAPABILITY		127
#define WLAN_EID_VENDOR_SPECIFIC	221
#define WLAN_OUI_WFA			0x506f9a
#define WLAN_OUI_TYPE_WFA_P2P		9
#define WLAN_EXT_CAPA10_OBSS_NARROW_BW_RU_TOLERANCE_SUPPORT BIT(7)
#define IEEE80211_P2P_ATTR_ABSENCE_NOTICE	12
#define IEEE80211_P2P_OPPPS_ENABLE_BIT		BIT(7)
#define IEEE80211_P2P_OPPPS_CTWINDOW_MASK	0x7f

struct element {
	u8	id;
	u8	datalen;
	u8	data[];
} __packed;

#define WLAN_CIPHER_SUITE_WEP40		0x000FAC01
#define WLAN_CIPHER_SUITE_TKIP		0x000FAC02
#define WLAN_CIPHER_SUITE_CCMP		0x000FAC04
#define WLAN_CIPHER_SUITE_WEP104	0x000FAC05
#define WLAN_CIPHER_SUITE_AES_CMAC	0x000FAC06
#define WLAN_CIPHER_SUITE_GCMP		0x000FAC08
#define WLAN_CIPHER_SUITE_GCMP_256	0x000FAC09
#define WLAN_CIPHER_SUITE_CCMP_256	0x000FAC0A

static inline bool ieee80211_has_tods(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_TODS)) != 0;
}
static inline bool ieee80211_has_fromds(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FROMDS)) != 0;
}
static inline bool ieee80211_has_a4(__le16 fc)
{
	__le16 t = cpu_to_le16(IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS);
	return (fc & t) == t;
}
static inline bool ieee80211_has_protected(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_PROTECTED)) != 0;
}
static inline bool ieee80211_has_pm(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_PM)) != 0;
}
static inline bool ieee80211_is_mgmt(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE)) ==
	    cpu_to_le16(IEEE80211_FTYPE_MGMT);
}
static inline bool ieee80211_is_ctl(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE)) ==
	    cpu_to_le16(IEEE80211_FTYPE_CTL);
}
static inline bool ieee80211_is_data(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE)) ==
	    cpu_to_le16(IEEE80211_FTYPE_DATA);
}
static inline bool ieee80211_is_data_qos(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | 0x0080)) ==
	    cpu_to_le16(IEEE80211_FTYPE_DATA | 0x0080);
}
static inline bool ieee80211_is_data_present(__le16 fc)
{
	return (fc & cpu_to_le16(0x40 | IEEE80211_FCTL_FTYPE)) ==
	    cpu_to_le16(IEEE80211_FTYPE_DATA);
}
#define __RTW89_IS_STYPE(n, ft, st)					\
static inline bool ieee80211_is_##n(__le16 fc)				\
{									\
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE |			\
	    IEEE80211_FCTL_STYPE)) == cpu_to_le16((ft) | (st));		\
}
__RTW89_IS_STYPE(beacon, IEEE80211_FTYPE_MGMT, IEEE80211_STYPE_BEACON)
__RTW89_IS_STYPE(probe_resp, IEEE80211_FTYPE_MGMT, IEEE80211_STYPE_PROBE_RESP)
__RTW89_IS_STYPE(probe_req, IEEE80211_FTYPE_MGMT, IEEE80211_STYPE_PROBE_REQ)
__RTW89_IS_STYPE(action, IEEE80211_FTYPE_MGMT, IEEE80211_STYPE_ACTION)
__RTW89_IS_STYPE(auth, IEEE80211_FTYPE_MGMT, IEEE80211_STYPE_AUTH)
__RTW89_IS_STYPE(deauth, IEEE80211_FTYPE_MGMT, IEEE80211_STYPE_DEAUTH)
__RTW89_IS_STYPE(disassoc, IEEE80211_FTYPE_MGMT, IEEE80211_STYPE_DISASSOC)
__RTW89_IS_STYPE(assoc_req, IEEE80211_FTYPE_MGMT, IEEE80211_STYPE_ASSOC_REQ)
__RTW89_IS_STYPE(trigger, IEEE80211_FTYPE_CTL, IEEE80211_STYPE_TRIGGER)
__RTW89_IS_STYPE(pspoll, IEEE80211_FTYPE_CTL, IEEE80211_STYPE_PSPOLL)
__RTW89_IS_STYPE(qos_nullfunc, IEEE80211_FTYPE_DATA, IEEE80211_STYPE_QOS_NULLFUNC)
#undef __RTW89_IS_STYPE
static inline bool ieee80211_is_nullfunc(__le16 fc)
{
	return (fc & cpu_to_le16(IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
	    cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_NULLFUNC);
}
static inline bool ieee80211_is_any_nullfunc(__le16 fc)
{
	return ieee80211_is_nullfunc(fc) || ieee80211_is_qos_nullfunc(fc);
}
static inline unsigned int ieee80211_hdrlen(__le16 fc)
{
	unsigned int hdrlen = 24;

	if (ieee80211_is_data(fc)) {
		if (ieee80211_has_a4(fc))
			hdrlen = 30;
		if (ieee80211_is_data_qos(fc)) {
			hdrlen += IEEE80211_QOS_CTL_LEN;
			if (fc & cpu_to_le16(IEEE80211_FCTL_ORDER))
				hdrlen += IEEE80211_HT_CTL_LEN;
		}
		return hdrlen;
	}
	if (ieee80211_is_mgmt(fc)) {
		if (fc & cpu_to_le16(IEEE80211_FCTL_ORDER))
			hdrlen += IEEE80211_HT_CTL_LEN;
		return hdrlen;
	}
	if (ieee80211_is_ctl(fc))
		return 16;
	return hdrlen;
}
static inline u8 *ieee80211_get_qos_ctl(struct ieee80211_hdr *hdr)
{
	if (ieee80211_has_a4(hdr->frame_control))
		return (u8 *)hdr + 30;
	return (u8 *)hdr + 24;
}
static inline u32 ieee80211_tu_to_usec(u32 tu)
{
	return tu * 1024;
}

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

#define IEEE80211_HT_CAP_LDPC_CODING		0x0001
#define IEEE80211_HT_CAP_SUP_WIDTH_20_40	0x0002
#define IEEE80211_HT_CAP_SGI_20			0x0020
#define IEEE80211_HT_CAP_SGI_40			0x0040
#define IEEE80211_HT_CAP_TX_STBC		0x0080
#define IEEE80211_HT_CAP_RX_STBC		0x0300
#define IEEE80211_HT_CAP_RX_STBC_SHIFT		8
#define IEEE80211_HT_CAP_MAX_AMSDU		0x0800
#define IEEE80211_HT_CAP_DSSSCCK40		0x1000
#define IEEE80211_HT_MCS_TX_DEFINED		0x01
#define IEEE80211_HT_MAX_AMPDU_64K		3
#define IEEE80211_HT_MPDU_DENSITY_NONE		0
#define IEEE80211_HT_MCS_MASK_LEN		10

#define IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454		0x00000002
#define IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ	0x00000004
#define IEEE80211_VHT_CAP_RXLDPC			0x00000010
#define IEEE80211_VHT_CAP_SHORT_GI_80			0x00000020
#define IEEE80211_VHT_CAP_SHORT_GI_160			0x00000040
#define IEEE80211_VHT_CAP_TXSTBC			0x00000080
#define IEEE80211_VHT_CAP_RXSTBC_1			0x00000100
#define IEEE80211_VHT_CAP_RXSTBC_MASK			0x00000700
#define IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE		0x00000800
#define IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE		0x00001000
#define IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT		13
#define IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT	16
#define IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK	0x00070000
#define IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE		0x00080000
#define IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE		0x00100000
#define IEEE80211_VHT_CAP_HTC_VHT			0x00400000
#define IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT 23
#define IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK 0x03800000
#define IEEE80211_VHT_MCS_SUPPORT_0_7			0
#define IEEE80211_VHT_MCS_SUPPORT_0_8			1
#define IEEE80211_VHT_MCS_SUPPORT_0_9			2
#define IEEE80211_VHT_MCS_NOT_SUPPORTED			3
#define IEEE80211_VHT_MAX_AMPDU_1024K			7

#define IEEE80211_HE_MAC_CAP0_HTC_HE				0x01
#define IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US		0x08
#define IEEE80211_HE_MAC_CAP2_ALL_ACK				0x02
#define IEEE80211_HE_MAC_CAP2_BSR				0x08
#define IEEE80211_HE_MAC_CAP3_OMI_CONTROL			0x02
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_2		0x10
#define IEEE80211_HE_MAC_CAP4_OPS				0x02
#define IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU			0x40
#define IEEE80211_HE_MAC_CAP5_HT_VHT_TRIG_FRAME_RX		0x40
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G	0x02
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G 0x04
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G	0x08
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G 0x10
#define IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A			0x10
#define IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD		0x20
#define IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US	0x40
#define IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US		0x02
#define IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ		0x04
#define IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ		0x08
#define IEEE80211_HE_PHY_CAP2_DOPPLER_TX			0x10
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_16_QAM		0x02
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_2			0x04
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM		0x10
#define IEEE80211_HE_PHY_CAP3_RX_PARTIAL_BW_SU_IN_20MHZ_MU	0x40
#define IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER			0x80
#define IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE			0x01
#define IEEE80211_HE_PHY_CAP4_MU_BEAMFORMER			0x02
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4	0x0c
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_4	0x60
#define IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK 0x07
#define IEEE80211_HE_PHY_CAP5_NG16_SU_FEEDBACK			0x40
#define IEEE80211_HE_PHY_CAP5_NG16_MU_FEEDBACK			0x80
#define IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU		0x01
#define IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU		0x02
#define IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB		0x04
#define IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE		0x40
#define IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT		0x80
#define IEEE80211_HE_PHY_CAP7_POWER_BOOST_FACTOR_SUPP		0x01
#define IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI	0x02
#define IEEE80211_HE_PHY_CAP7_MAX_NC_1				0x08
#define IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI	0x01
#define IEEE80211_HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU		0x04
#define IEEE80211_HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU		0x08
#define IEEE80211_HE_PHY_CAP8_HE_ER_SU_1XLTF_AND_08_US_GI	0x10
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_996			0x80
#define IEEE80211_HE_PHY_CAP9_LONGER_THAN_16_SIGB_OFDM_SYM	0x01
#define IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU	0x04
#define IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU	0x08
#define IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB 0x10
#define IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB 0x20
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_16US		0x80
#define IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_MASK		0xc0
#define IEEE80211_HE_MCS_SUPPORT_0_7				0
#define IEEE80211_HE_MCS_SUPPORT_0_9				1
#define IEEE80211_HE_MCS_SUPPORT_0_11				2
#define IEEE80211_HE_MCS_NOT_SUPPORTED				3
#define IEEE80211_PPE_THRES_INFO_PPET_SIZE			3
#define IEEE80211_PPE_THRES_NSS_MASK				7
#define IEEE80211_PPE_THRES_RU_INDEX_BITMASK_MASK		0x78
#define IEEE80211_HE_OPERATION_ER_SU_DISABLE			0x00010000
#define IEEE80211_HE_6GHZ_CAP_MIN_MPDU_START			0x0007
#define IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP			0x0038
#define IEEE80211_HE_6GHZ_CAP_MAX_MPDU_LEN			0x00c0
#define IEEE80211_HE_PPE_THRES_MAX_LEN				25
#define IEEE80211_WMM_IE_STA_QOSINFO_SP_ALL			0x00

struct ieee80211_mcs_info {
	u8	rx_mask[IEEE80211_HT_MCS_MASK_LEN];
	__le16	rx_highest;
	u8	tx_params;
	u8	reserved[3];
} __packed;

struct ieee80211_sta_ht_cap {
	u16	cap;
	bool	ht_supported;
	u8	ampdu_factor;
	u8	ampdu_density;
	struct ieee80211_mcs_info mcs;
};

struct ieee80211_vht_mcs_info {
	__le16	rx_mcs_map;
	__le16	rx_highest;
	__le16	tx_mcs_map;
	__le16	tx_highest;
} __packed;

struct ieee80211_sta_vht_cap {
	bool	vht_supported;
	u32	cap;
	struct ieee80211_vht_mcs_info vht_mcs;
};

struct ieee80211_he_cap_elem {
	u8	mac_cap_info[6];
	u8	phy_cap_info[11];
} __packed;

struct ieee80211_he_mcs_nss_supp {
	__le16	rx_mcs_80;
	__le16	tx_mcs_80;
	__le16	rx_mcs_160;
	__le16	tx_mcs_160;
	__le16	rx_mcs_80p80;
	__le16	tx_mcs_80p80;
} __packed;

struct ieee80211_sta_he_cap {
	bool	has_he;
	struct ieee80211_he_cap_elem he_cap_elem;
	struct ieee80211_he_mcs_nss_supp he_mcs_nss_supp;
	u8	ppe_thres[IEEE80211_HE_PPE_THRES_MAX_LEN];
};

struct ieee80211_he_6ghz_capa {
	__le16	capa;
} __packed;

struct ieee80211_sband_iftype_data {
	u16	types_mask;
	struct ieee80211_sta_he_cap he_cap;
	struct ieee80211_he_6ghz_capa he_6ghz_capa;
};

struct ieee80211_he_mu_edca_param_ac_rec {
	u8	aifsn;
	u8	ecw_min_max;
	u8	mu_edca_timer;
} __packed;

struct ieee80211_p2p_noa_desc {
	u8	count;
	__le32	duration;
	__le32	interval;
	__le32	start_time;
} __packed;

struct ieee80211_p2p_noa_attr {
	u8	index;
	u8	oppps_ctwindow;
	struct ieee80211_p2p_noa_desc desc[4];
} __packed;

struct ieee80211_radiotap_he {
	__le16	data1, data2, data3, data4, data5, data6;
};
#define IEEE80211_RADIOTAP_HE_DATA1_DATA_MCS_KNOWN	0x0020
#define IEEE80211_RADIOTAP_HE_DATA1_BW_RU_ALLOC_KNOWN	0x4000
#define IEEE80211_RADIOTAP_HE_DATA2_GI_KNOWN		0x0002

/* ------------------------------------------------------------------ */
/* cfg80211                                                            */
/* ------------------------------------------------------------------ */

#define IEEE80211_CHAN_DISABLED		BIT(0)
#define IEEE80211_CHAN_NO_IR		BIT(1)
#define IEEE80211_CHAN_RADAR		BIT(3)
#define IEEE80211_CHAN_NO_HT40PLUS	BIT(4)
#define IEEE80211_CHAN_NO_HT40MINUS	BIT(5)

struct ieee80211_channel {
	enum nl80211_band	band;
	u32			center_freq;
	u16			freq_offset;
	u16			hw_value;
	u32			flags;
	int			max_power;
};

struct ieee80211_rate {
	u32	flags;
	u16	bitrate;
	u16	hw_value;
	u16	hw_value_short;
};

struct ieee80211_supported_band {
	struct ieee80211_channel	*channels;
	struct ieee80211_rate		*bitrates;
	enum nl80211_band		 band;
	int				 n_channels;
	int				 n_bitrates;
	struct ieee80211_sta_ht_cap	 ht_cap;
	struct ieee80211_sta_vht_cap	 vht_cap;
	u16				 n_iftype_data;
	const struct ieee80211_sband_iftype_data *iftype_data;
};

struct cfg80211_chan_def {
	struct ieee80211_channel	*chan;
	enum nl80211_chan_width		 width;
	u32				 center_freq1;
	u32				 center_freq2;
};

struct cfg80211_ssid {
	u8	ssid[IEEE80211_MAX_SSID_LEN];
	u8	ssid_len;
};

struct cfg80211_scan_6ghz_params {
	u32	short_ssid;
	u32	channel_idx;
	u8	bssid[ETH_ALEN];
	bool	unsolicited_probe;
	bool	short_ssid_valid;
	bool	psc_no_listen;
	s8	psd_20;
};

struct cfg80211_scan_request {
	struct cfg80211_ssid	*ssids;
	int			 n_ssids;
	u32			 n_channels;
	const u8		*ie;
	size_t			 ie_len;
	u16			 duration;
	bool			 duration_mandatory;
	u32			 flags;
	u8			 mac_addr[ETH_ALEN] __aligned(2);
	u8			 mac_addr_mask[ETH_ALEN] __aligned(2);
	u8			 bssid[ETH_ALEN] __aligned(2);
	u32			 n_6ghz_params;
	struct cfg80211_scan_6ghz_params *scan_6ghz_params;
	struct ieee80211_channel *channels[];
};

struct cfg80211_scan_info {
	u64	scan_start_tsf;
	u8	tsf_bssid[ETH_ALEN] __aligned(2);
	bool	aborted;
};

struct ieee80211_scan_ies {
	const u8	*ies[NUM_NL80211_BANDS];
	size_t		 len[NUM_NL80211_BANDS];
	const u8	*common_ies;
	size_t		 common_ie_len;
};

struct ieee80211_scan_request {
	struct ieee80211_scan_ies ies;
	struct cfg80211_scan_request req;
};

struct cfg80211_bitrate_mask {
	struct {
		u32	legacy;
		u8	ht_mcs[IEEE80211_HT_MCS_MASK_LEN];
		u16	vht_mcs[8];
		u16	he_mcs[8];
		enum nl80211_txrate_gi gi;
		enum nl80211_he_gi he_gi;
		enum nl80211_he_ltf he_ltf;
	} control[NUM_NL80211_BANDS];
};

struct cfg80211_tid_cfg {
	bool	config_override;
	u8	tids;
	u64	mask;
	enum nl80211_tid_config noack;
	u8	retry_long, retry_short;
	enum nl80211_tid_config ampdu;
	enum nl80211_tid_config rtscts;
	enum nl80211_tid_config amsdu;
};

struct cfg80211_tid_config {
	const u8	*peer;
	u32		 n_tid_conf;
	struct cfg80211_tid_cfg tid_conf[];
};

struct cfg80211_bss_ies {
	u64	tsf;
	int	len;
	bool	from_beacon;
	u8	data[];
};

struct cfg80211_bss {
	struct ieee80211_channel *channel;
	const struct cfg80211_bss_ies *ies;
	u8	bssid[ETH_ALEN];
};

struct cfg80211_sar_freq_ranges {
	u32	start_freq;
	u32	end_freq;
};

struct cfg80211_sar_capa {
	enum nl80211_sar_type	type;
	u32			num_freq_ranges;
	const struct cfg80211_sar_freq_ranges *freq_ranges;
};

struct cfg80211_sar_sub_specs {
	s32	power;
	u32	freq_range_index;
};

struct cfg80211_sar_specs {
	enum nl80211_sar_type	type;
	u32			num_sub_specs;
	struct cfg80211_sar_sub_specs sub_specs[];
};

struct cfg80211_wowlan {
	bool	any, disconnect, magic_pkt;
	int	n_patterns;
	void	*patterns;
};

#define WIPHY_WOWLAN_MAGIC_PKT		BIT(1)
#define WIPHY_WOWLAN_DISCONNECT		BIT(2)
struct wiphy_wowlan_support {
	u32	flags;
	int	n_patterns;
	int	pattern_max_len;
	int	pattern_min_len;
	int	max_pkt_offset;
};

struct ieee80211_iface_limit {
	u16	max;
	u16	types;
};

struct ieee80211_iface_combination {
	const struct ieee80211_iface_limit *limits;
	u32	num_different_channels;
	u16	max_interfaces;
	u8	n_limits;
	bool	beacon_int_infra_match;
	u8	radar_detect_widths;
};

struct ieee80211_regdomain {
	char	alpha2[3];
};

struct regulatory_request {
	char	alpha2[3];
	enum nl80211_reg_initiator initiator;
};

#define WIPHY_FLAG_SUPPORTS_TDLS		BIT(0)
#define WIPHY_FLAG_TDLS_EXTERNAL_SETUP		BIT(1)
#define WIPHY_FLAG_AP_UAPSD			BIT(2)
#define WIPHY_FLAG_SPLIT_SCAN_6GHZ		BIT(3)
#define REGULATORY_WIPHY_SELF_MANAGED		BIT(0)
#define REGULATORY_DISABLE_BEACON_HINTS		BIT(1)
#define REGULATORY_COUNTRY_IE_IGNORE		BIT(2)
#define REGULATORY_STRICT_REG			BIT(3)

struct wiphy {
	u16	interface_modes;
	u32	flags;
	u32	regulatory_flags;
	u32	features;
	u32	available_antennas_tx;
	u32	available_antennas_rx;
	u32	rts_threshold;
	u8	max_scan_ssids;
	u16	max_scan_ie_len;
	u32	max_remain_on_channel_duration;
	struct ieee80211_supported_band *bands[NUM_NL80211_BANDS];
	const struct ieee80211_iface_combination *iface_combinations;
	int	n_iface_combinations;
	struct {
		u64	vif;
		u64	peer;
	} tid_config_support;
	const struct cfg80211_sar_capa *sar_capa;
	const struct wiphy_wowlan_support *wowlan;
	void	(*reg_notifier)(struct wiphy *, struct regulatory_request *);
	const struct ieee80211_regdomain *regd;
	void	*priv;
};

static inline int regulatory_hint(struct wiphy *w, const char *a)
{
	return 0;
}

enum rate_info_flags {
	RATE_INFO_FLAGS_MCS		= BIT(0),
	RATE_INFO_FLAGS_VHT_MCS		= BIT(1),
	RATE_INFO_FLAGS_SHORT_GI	= BIT(2),
	RATE_INFO_FLAGS_HE_MCS		= BIT(4),
};

enum rate_info_bw {
	RATE_INFO_BW_20 = 0,
	RATE_INFO_BW_5,
	RATE_INFO_BW_10,
	RATE_INFO_BW_40,
	RATE_INFO_BW_80,
	RATE_INFO_BW_160,
	RATE_INFO_BW_HE_RU,
	RATE_INFO_BW_320,
	RATE_INFO_BW_EHT_RU,
};

struct rate_info {
	u16	flags;
	u16	legacy;
	u8	mcs;
	u8	nss;
	u8	bw;
	u8	he_gi;
	u8	he_dcm;
	u8	he_ru_alloc;
	u8	n_bonded_ch;
};

struct station_info {
	u64	filled;
	struct rate_info txrate;
};

/* ------------------------------------------------------------------ */
/* mac80211                                                            */
/* ------------------------------------------------------------------ */

#define IEEE80211_CONF_MONITOR		BIT(0)
#define IEEE80211_CONF_PS		BIT(1)
#define IEEE80211_CONF_IDLE		BIT(2)
#define IEEE80211_CONF_CHANGE_CHANNEL	BIT(6)
#define IEEE80211_CONF_CHANGE_IDLE	BIT(8)

struct ieee80211_conf {
	u32	flags;
	int	power_level;
	struct cfg80211_chan_def chandef;
};

struct ieee80211_ops;

struct ieee80211_hw {
	struct ieee80211_conf	 conf;
	struct wiphy		*wiphy;
	const char		*rate_control_algorithm;
	void			*priv;
	unsigned long		 flags[1];
	unsigned int		 extra_tx_headroom;
	int			 vif_data_size;
	int			 sta_data_size;
	int			 chanctx_data_size;
	int			 txq_data_size;
	u16			 queues;
	u16			 max_rx_aggregation_subframes;
	u16			 max_tx_aggregation_subframes;
	u8			 uapsd_max_sp_len;
	u8			 perm_addr[ETH_ALEN];
	const struct ieee80211_ops *ops;
	void			*bridge;	/* rtw89_bridge state */
};
#define ieee80211_hw_set(hw, f)		do { (void)(hw); } while (0)
#define SET_IEEE80211_PERM_ADDR(hw, a)	memcpy((hw)->perm_addr, (a), ETH_ALEN)
#define SET_IEEE80211_DEV(hw, d)	do { } while (0)
#define wiphy_ext_feature_set(w, f)	do { } while (0)

enum ieee80211_reg_ap_power {
	IEEE80211_REG_UNSET_AP,
	IEEE80211_REG_LPI_AP,
	IEEE80211_REG_SP_AP,
	IEEE80211_REG_VLP_AP,
};

struct ieee80211_bss_conf {
	const u8	*bssid;
	u8		 bssid_buf[ETH_ALEN];
	bool		 use_short_slot;
	bool		 he_support;
	bool		 nontransmitted;
	u8		 dtim_period;
	u16		 beacon_int;
	u32		 basic_rates;
	s32		 cqm_rssi_thold;
	u32		 cqm_rssi_hyst;
	struct cfg80211_chan_def chandef;
	struct {
		u8	color;
		bool	enabled;
	} he_bss_color;
	struct {
		u32	params;
		u16	nss_set;
	} he_oper;
	struct {
		u8	membership[8];
		u8	position[16];
	} mu_group;
	struct ieee80211_p2p_noa_attr p2p_noa_attr;
	enum ieee80211_reg_ap_power power_type;
};

#define BSS_CHANGED_ASSOC		BIT(0)
#define BSS_CHANGED_ERP_SLOT		BIT(3)
#define BSS_CHANGED_BSSID		BIT(7)
#define BSS_CHANGED_BEACON		BIT(8)
#define BSS_CHANGED_CQM			BIT(12)
#define BSS_CHANGED_PS			BIT(17)
#define BSS_CHANGED_P2P_PS		BIT(19)
#define BSS_CHANGED_MU_GROUPS		BIT(23)
#define BSS_CHANGED_HE_BSS_COLOR	BIT(29)

struct ieee80211_vif_cfg {
	bool	assoc;
	bool	ps;
	u16	aid;
};

struct ieee80211_txq {
	struct ieee80211_vif	*vif;
	struct ieee80211_sta	*sta;
	u8			 tid;
	u8			 ac;
	u8			 drv_priv[] __aligned(sizeof(void *));
};

#define IEEE80211_VIF_BEACON_FILTER	BIT(0)
#define IEEE80211_VIF_SUPPORTS_CQM_RSSI	BIT(1)

struct ieee80211_vif {
	enum nl80211_iftype	 type;
	struct ieee80211_vif_cfg cfg;
	struct ieee80211_bss_conf bss_conf;
	u8			 addr[ETH_ALEN] __aligned(2);
	bool			 p2p;
	u32			 driver_flags;
	struct ieee80211_txq	*txq;
	u8			 drv_priv[] __aligned(sizeof(void *));
};

enum ieee80211_sta_rx_bandwidth {
	IEEE80211_STA_RX_BW_20 = 0,
	IEEE80211_STA_RX_BW_40,
	IEEE80211_STA_RX_BW_80,
	IEEE80211_STA_RX_BW_160,
	IEEE80211_STA_RX_BW_320,
};

struct ieee80211_sta_aggregates {
	u16	max_amsdu_len;
	u16	max_rc_amsdu_len;
	u16	max_tid_amsdu_len[IEEE80211_NUM_TIDS];
};

struct ieee80211_link_sta {
	u32				 supp_rates[NUM_NL80211_BANDS];
	struct ieee80211_sta_ht_cap	 ht_cap;
	struct ieee80211_sta_vht_cap	 vht_cap;
	struct ieee80211_sta_he_cap	 he_cap;
	struct ieee80211_he_6ghz_capa	 he_6ghz_capa;
	struct ieee80211_sta_aggregates	 agg;
	u8				 rx_nss;
	enum ieee80211_sta_rx_bandwidth	 bandwidth;
};

enum ieee80211_sta_state {
	IEEE80211_STA_NOTEXIST,
	IEEE80211_STA_NONE,
	IEEE80211_STA_AUTH,
	IEEE80211_STA_ASSOC,
	IEEE80211_STA_AUTHORIZED,
};

struct ieee80211_sta {
	u8			 addr[ETH_ALEN] __aligned(2);
	u16			 aid;
	bool			 tdls;
	bool			 wme;
	u16			 max_amsdu_subframes;
	struct ieee80211_link_sta deflink;
	struct ieee80211_txq	*txq[IEEE80211_NUM_TIDS + 1];
	u8			 drv_priv[] __aligned(sizeof(void *));
};

#define IEEE80211_RC_BW_CHANGED		BIT(0)
#define IEEE80211_RC_SMPS_CHANGED	BIT(1)
#define IEEE80211_RC_SUPP_RATES_CHANGED	BIT(2)
#define IEEE80211_RC_NSS_CHANGED	BIT(3)

#define IEEE80211_KEY_FLAG_GENERATE_IV	BIT(1)
#define IEEE80211_KEY_FLAG_PAIRWISE	BIT(3)
#define IEEE80211_KEY_FLAG_SW_MGMT_TX	BIT(4)

struct ieee80211_key_conf {
	atomic64_t	tx_pn;
	u32		cipher;
	u8		icv_len;
	u8		iv_len;
	u8		hw_key_idx;
	s8		keyidx;
	u16		flags;
	s8		link_id;
	u8		keylen;
	u8		key[];
};

enum set_key_cmd {
	SET_KEY,
	DISABLE_KEY,
};

struct ieee80211_chanctx_conf {
	struct cfg80211_chan_def def;
	struct cfg80211_chan_def min_def;
	u8	rx_chains_static, rx_chains_dynamic;
	bool	radar_enabled;
	u8	drv_priv[] __aligned(sizeof(void *));
};
#define IEEE80211_CHANCTX_CHANGE_WIDTH	BIT(0)

struct ieee80211_tx_queue_params {
	u16	txop;
	u16	cw_min;
	u16	cw_max;
	u8	aifs;
	bool	acm;
	bool	uapsd;
	bool	mu_edca;
	struct ieee80211_he_mu_edca_param_ac_rec mu_edca_param_rec;
};

enum ieee80211_ampdu_mlme_action {
	IEEE80211_AMPDU_RX_START,
	IEEE80211_AMPDU_RX_STOP,
	IEEE80211_AMPDU_TX_START,
	IEEE80211_AMPDU_TX_STOP_CONT,
	IEEE80211_AMPDU_TX_STOP_FLUSH,
	IEEE80211_AMPDU_TX_STOP_FLUSH_CONT,
	IEEE80211_AMPDU_TX_OPERATIONAL,
};
#define IEEE80211_AMPDU_TX_START_IMMEDIATE	1

struct ieee80211_ampdu_params {
	enum ieee80211_ampdu_mlme_action action;
	struct ieee80211_sta	*sta;
	u16			 tid;
	u16			 ssn;
	u16			 buf_size;
	bool			 amsdu;
	u16			 timeout;
};

enum ieee80211_roc_type {
	IEEE80211_ROC_TYPE_NORMAL = 0,
	IEEE80211_ROC_TYPE_MGMT_TX,
};

enum ieee80211_reconfig_type {
	IEEE80211_RECONFIG_TYPE_RESTART,
	IEEE80211_RECONFIG_TYPE_SUSPEND,
};

#define IEEE80211_IFACE_ITER_NORMAL	0

/* TX control/status, kept in skb->cb. */
#define IEEE80211_TX_CTL_REQ_TX_STATUS		BIT(0)
#define IEEE80211_TX_CTL_ASSIGN_SEQ		BIT(1)
#define IEEE80211_TX_CTL_NO_ACK			BIT(2)
#define IEEE80211_TX_CTL_SEND_AFTER_DTIM	BIT(3)
#define IEEE80211_TX_CTL_AMPDU			BIT(6)
#define IEEE80211_TX_CTL_TX_OFFCHAN		BIT(7)
#define IEEE80211_TX_STAT_ACK			BIT(9)
#define IEEE80211_TX_CTL_NO_CCK_RATE		BIT(27)
#define IEEE80211_TX_STAT_NOACK_TRANSMITTED	BIT(30)
#define IEEE80211_TX_CTRL_PORT_CTRL_PROTO	BIT(0)

struct ieee80211_tx_info {
	u32	flags;
	u8	band;
	u8	hw_queue;
	u16	pad;
	struct {
		struct ieee80211_vif		*vif;
		struct ieee80211_key_conf	*hw_key;
		u32				 flags;
	} control;
	struct {
		void	*status_driver_data[16 / sizeof(void *)];
	} status;
};
__CTASSERT(sizeof(struct ieee80211_tx_info) <= 48);

static inline struct ieee80211_tx_info *IEEE80211_SKB_CB(struct sk_buff *skb)
{
	return (struct ieee80211_tx_info *)skb->cb;
}
static inline void ieee80211_tx_info_clear_status(struct ieee80211_tx_info *info)
{
	info->flags &= ~(IEEE80211_TX_STAT_ACK |
	    IEEE80211_TX_STAT_NOACK_TRANSMITTED);
}

struct ieee80211_tx_control {
	struct ieee80211_sta *sta;
};

/* RX status, kept in skb->cb. */
#define RX_FLAG_FAILED_FCS_CRC		BIT(0)
#define RX_FLAG_MACTIME_START		BIT(2)
#define RX_FLAG_DECRYPTED		BIT(4)
#define RX_FLAG_RADIOTAP_HE		BIT(5)
#define RX_FLAG_IV_STRIPPED		BIT(6)
#define RX_FLAG_MMIC_STRIPPED		BIT(7)

enum mac80211_rx_encoding {
	RX_ENC_LEGACY = 0,
	RX_ENC_HT,
	RX_ENC_VHT,
	RX_ENC_HE,
	RX_ENC_EHT,
};
#define RX_ENC_FLAG_SHORTPRE		BIT(0)
#define RX_ENC_FLAG_SHORT_GI		BIT(2)

struct ieee80211_rx_status {
	u64	mactime;
	u32	flag;
	u16	freq;
	u8	enc_flags;
	u8	encoding;
	u8	bw;
	u8	he_ru;
	u8	he_gi;
	u8	he_dcm;
	u8	rate_idx;
	u8	nss;
	u8	band;
	u8	antenna;
	s8	signal;
	u8	chains;
	s8	chain_signal[4];
};
__CTASSERT(sizeof(struct ieee80211_rx_status) <= 48);

static inline struct ieee80211_rx_status *IEEE80211_SKB_RXCB(struct sk_buff *skb)
{
	return (struct ieee80211_rx_status *)skb->cb;
}

struct napi_struct {
	struct net_device	*dev;
	int			(*poll)(struct napi_struct *, int);
	void			*nb_sih;	/* NetBSD softint */
	unsigned long		 state;
	bool			 enabled;
};
struct net_device {
	int	dummy;
};
#define NAPI_POLL_WEIGHT		64
#define NAPI_STATE_SCHED		0
void	rtw89_napi_schedule(struct napi_struct *);
bool	rtw89_napi_complete_done(struct napi_struct *, int);
void	rtw89_napi_synchronize(struct napi_struct *);
void	rtw89_netif_napi_add(struct net_device *, struct napi_struct *,
	    int (*)(struct napi_struct *, int));
void	rtw89_netif_napi_del(struct napi_struct *);
#define napi_schedule(n)		rtw89_napi_schedule(n)
#define napi_complete_done(n, w)	rtw89_napi_complete_done((n), (w))
#define napi_synchronize(n)		rtw89_napi_synchronize(n)
#define napi_enable(n)			((n)->enabled = true)
#define napi_disable(n)			((n)->enabled = false, rtw89_napi_synchronize(n))
#define init_dummy_netdev(d)		do { } while (0)
#define netif_napi_add(d, n, p)		rtw89_netif_napi_add((d), (n), (p))
#define netif_napi_del(n)		rtw89_netif_napi_del(n)

/* EWMA, same arithmetic as linux/average.h. */
#define DECLARE_EWMA(name, _precision, _weight_rcp)			\
	struct ewma_##name {						\
		unsigned long internal;					\
	};								\
	static inline void ewma_##name##_init(struct ewma_##name *e)	\
	{								\
		e->internal = 0;					\
	}								\
	static inline unsigned long					\
	ewma_##name##_read(struct ewma_##name *e)			\
	{								\
		return e->internal >> (_precision);			\
	}								\
	static inline void ewma_##name##_add(struct ewma_##name *e,	\
	    unsigned long val)						\
	{								\
		unsigned long internal = READ_ONCE(e->internal);	\
		unsigned long weight_rcp = ilog2(_weight_rcp);		\
		unsigned long precision = _precision;			\
		WRITE_ONCE(e->internal, internal ?			\
		    (((internal << weight_rcp) - internal) +		\
		     (val << precision)) >> weight_rcp :		\
		    (val << precision));				\
	}

/* ------------------------------------------------------------------ */
/* mac80211 entry points, implemented by rtw89_bridge.c                */
/* ------------------------------------------------------------------ */

#define FIF_ALLMULTI		BIT(1)
#define FIF_FCSFAIL		BIT(2)
#define FIF_PLCPFAIL		BIT(3)
#define FIF_BCN_PRBRESP_PROMISC	BIT(4)
#define FIF_CONTROL		BIT(5)
#define FIF_OTHER_BSS		BIT(6)
#define FIF_PSPOLL		BIT(7)
#define FIF_PROBE_REQ		BIT(8)

struct ieee80211_ops {
	void (*tx)(struct ieee80211_hw *, struct ieee80211_tx_control *,
	    struct sk_buff *);
	void (*wake_tx_queue)(struct ieee80211_hw *, struct ieee80211_txq *);
	int (*start)(struct ieee80211_hw *);
	void (*stop)(struct ieee80211_hw *);
	int (*config)(struct ieee80211_hw *, u32);
	int (*add_interface)(struct ieee80211_hw *, struct ieee80211_vif *);
	int (*change_interface)(struct ieee80211_hw *, struct ieee80211_vif *,
	    enum nl80211_iftype, bool);
	void (*remove_interface)(struct ieee80211_hw *, struct ieee80211_vif *);
	void (*configure_filter)(struct ieee80211_hw *, unsigned int,
	    unsigned int *, u64);
	void (*bss_info_changed)(struct ieee80211_hw *, struct ieee80211_vif *,
	    struct ieee80211_bss_conf *, u64);
	int (*start_ap)(struct ieee80211_hw *, struct ieee80211_vif *,
	    struct ieee80211_bss_conf *);
	void (*stop_ap)(struct ieee80211_hw *, struct ieee80211_vif *,
	    struct ieee80211_bss_conf *);
	int (*set_tim)(struct ieee80211_hw *, struct ieee80211_sta *, bool);
	int (*conf_tx)(struct ieee80211_hw *, struct ieee80211_vif *,
	    unsigned int, u16, const struct ieee80211_tx_queue_params *);
	int (*sta_state)(struct ieee80211_hw *, struct ieee80211_vif *,
	    struct ieee80211_sta *, enum ieee80211_sta_state,
	    enum ieee80211_sta_state);
	int (*set_key)(struct ieee80211_hw *, enum set_key_cmd,
	    struct ieee80211_vif *, struct ieee80211_sta *,
	    struct ieee80211_key_conf *);
	int (*ampdu_action)(struct ieee80211_hw *, struct ieee80211_vif *,
	    struct ieee80211_ampdu_params *);
	int (*set_rts_threshold)(struct ieee80211_hw *, u32);
	void (*sta_statistics)(struct ieee80211_hw *, struct ieee80211_vif *,
	    struct ieee80211_sta *, struct station_info *);
	void (*flush)(struct ieee80211_hw *, struct ieee80211_vif *, u32, bool);
	int (*set_bitrate_mask)(struct ieee80211_hw *, struct ieee80211_vif *,
	    const struct cfg80211_bitrate_mask *);
	int (*set_antenna)(struct ieee80211_hw *, u32, u32);
	int (*get_antenna)(struct ieee80211_hw *, u32 *, u32 *);
	void (*sw_scan_start)(struct ieee80211_hw *, struct ieee80211_vif *,
	    const u8 *);
	void (*sw_scan_complete)(struct ieee80211_hw *, struct ieee80211_vif *);
	void (*reconfig_complete)(struct ieee80211_hw *,
	    enum ieee80211_reconfig_type);
	int (*hw_scan)(struct ieee80211_hw *, struct ieee80211_vif *,
	    struct ieee80211_scan_request *);
	void (*cancel_hw_scan)(struct ieee80211_hw *, struct ieee80211_vif *);
	int (*add_chanctx)(struct ieee80211_hw *, struct ieee80211_chanctx_conf *);
	void (*remove_chanctx)(struct ieee80211_hw *, struct ieee80211_chanctx_conf *);
	void (*change_chanctx)(struct ieee80211_hw *, struct ieee80211_chanctx_conf *, u32);
	int (*assign_vif_chanctx)(struct ieee80211_hw *, struct ieee80211_vif *,
	    struct ieee80211_bss_conf *, struct ieee80211_chanctx_conf *);
	void (*unassign_vif_chanctx)(struct ieee80211_hw *, struct ieee80211_vif *,
	    struct ieee80211_bss_conf *, struct ieee80211_chanctx_conf *);
	int (*remain_on_channel)(struct ieee80211_hw *, struct ieee80211_vif *,
	    struct ieee80211_channel *, int, enum ieee80211_roc_type);
	int (*cancel_remain_on_channel)(struct ieee80211_hw *, struct ieee80211_vif *);
	int (*set_sar_specs)(struct ieee80211_hw *, const struct cfg80211_sar_specs *);
	void (*sta_rc_update)(struct ieee80211_hw *, struct ieee80211_vif *,
	    struct ieee80211_sta *, u32);
	int (*set_tid_config)(struct ieee80211_hw *, struct ieee80211_vif *,
	    struct ieee80211_sta *, struct cfg80211_tid_config *);
};

struct ieee80211_hw *ieee80211_alloc_hw(size_t, const struct ieee80211_ops *);
void	ieee80211_free_hw(struct ieee80211_hw *);
int	ieee80211_register_hw(struct ieee80211_hw *);
void	ieee80211_unregister_hw(struct ieee80211_hw *);
#define wiphy_to_ieee80211_hw(w)	((struct ieee80211_hw *)(w)->priv)

void	ieee80211_queue_work(struct ieee80211_hw *, struct work_struct *);
void	ieee80211_queue_delayed_work(struct ieee80211_hw *,
	    struct delayed_work *, unsigned long);
void	ieee80211_iterate_stations_atomic(struct ieee80211_hw *,
	    void (*)(void *, struct ieee80211_sta *), void *);
void	ieee80211_iterate_active_interfaces_atomic(struct ieee80211_hw *, u32,
	    void (*)(void *, u8 *, struct ieee80211_vif *), void *);
struct ieee80211_sta *ieee80211_find_sta(struct ieee80211_vif *, const u8 *);
struct ieee80211_sta *ieee80211_find_sta_by_ifaddr(struct ieee80211_hw *,
	    const u8 *, const u8 *);
void	ieee80211_rx_napi(struct ieee80211_hw *, struct ieee80211_sta *,
	    struct sk_buff *, struct napi_struct *);
void	ieee80211_tx_status_ni(struct ieee80211_hw *, struct sk_buff *);
void	ieee80211_tx_status_irqsafe(struct ieee80211_hw *, struct sk_buff *);
void	ieee80211_free_txskb(struct ieee80211_hw *, struct sk_buff *);
void	ieee80211_stop_queues(struct ieee80211_hw *);
void	ieee80211_wake_queues(struct ieee80211_hw *);
void	ieee80211_restart_hw(struct ieee80211_hw *);
void	ieee80211_connection_loss(struct ieee80211_vif *);
void	ieee80211_beacon_loss(struct ieee80211_vif *);
void	ieee80211_cqm_rssi_notify(struct ieee80211_vif *,
	    enum nl80211_cqm_rssi_threshold_event, s32, gfp_t);
void	ieee80211_scan_completed(struct ieee80211_hw *, struct cfg80211_scan_info *);
void	ieee80211_ready_on_channel(struct ieee80211_hw *);
void	ieee80211_remain_on_channel_expired(struct ieee80211_hw *);
struct sk_buff *ieee80211_nullfunc_get(struct ieee80211_hw *,
	    struct ieee80211_vif *, int, bool);
struct sk_buff *ieee80211_pspoll_get(struct ieee80211_hw *, struct ieee80211_vif *);
struct sk_buff *ieee80211_probereq_get(struct ieee80211_hw *, const u8 *,
	    const u8 *, size_t, size_t);
struct sk_buff *ieee80211_proberesp_get(struct ieee80211_hw *, struct ieee80211_vif *);
struct sk_buff *ieee80211_beacon_get_tim(struct ieee80211_hw *,
	    struct ieee80211_vif *, u16 *, u16 *, unsigned int);
void	ieee80211_stop_tx_ba_session(struct ieee80211_sta *, u16);
int	ieee80211_start_tx_ba_session(struct ieee80211_sta *, u16, u16);
void	ieee80211_stop_tx_ba_cb_irqsafe(struct ieee80211_vif *, const u8 *, u16);

/* TXQ scheduling is not used: frames come straight from net80211. */
static inline struct ieee80211_txq *ieee80211_next_txq(struct ieee80211_hw *hw, u8 ac)
{
	return NULL;
}
static inline void ieee80211_return_txq(struct ieee80211_hw *hw,
    struct ieee80211_txq *txq, bool force) { }
static inline void ieee80211_schedule_txq(struct ieee80211_hw *hw,
    struct ieee80211_txq *txq) { }
static inline void ieee80211_txq_schedule_start(struct ieee80211_hw *hw, u8 ac) { }
static inline void ieee80211_txq_schedule_end(struct ieee80211_hw *hw, u8 ac) { }
static inline struct sk_buff *ieee80211_tx_dequeue_ni(struct ieee80211_hw *hw,
    struct ieee80211_txq *txq)
{
	return NULL;
}
static inline void ieee80211_txq_get_depth(struct ieee80211_txq *txq,
    unsigned long *frame_cnt, unsigned long *byte_cnt)
{
	if (frame_cnt)
		*frame_cnt = 0;
	if (byte_cnt)
		*byte_cnt = 0;
}

int	ieee80211_channel_to_frequency(int, enum nl80211_band);
int	ieee80211_frequency_to_channel(int);
void	cfg80211_chandef_create(struct cfg80211_chan_def *,
	    struct ieee80211_channel *, enum nl80211_channel_type);
const u8 *cfg80211_find_ie(u8, const u8 *, unsigned int);
const struct element *cfg80211_find_elem(u8, const u8 *, unsigned int);
const u8 *cfg80211_find_vendor_ie(unsigned int, int, const u8 *, unsigned int);
u32	cfg80211_calculate_bitrate(struct rate_info *);
static inline void cfg80211_bss_iter(struct wiphy *wiphy,
    struct cfg80211_chan_def *chandef,
    void (*iter)(struct wiphy *, struct cfg80211_bss *, void *), void *data)
{
	/* net80211 owns the scan results; nothing to iterate here. */
}
static inline void get_random_mask_addr(u8 *buf, const u8 *addr, const u8 *mask)
{
	int i;
	u64 rnd = ((u64)cprng_fast32() << 32) | cprng_fast32();
	for (i = 0; i < ETH_ALEN; i++) {
		buf[i] = (addr[i] & mask[i]) | ((rnd >> (i * 8)) & ~mask[i]);
	}
	buf[0] &= ~0x01;
}
static inline bool cfg80211_channel_is_psc(struct ieee80211_channel *chan)
{
	return false;
}
static inline const struct ieee80211_sta_he_cap *
ieee80211_get_he_iftype_cap(const struct ieee80211_supported_band *sband,
    enum nl80211_iftype iftype)
{
	return NULL;
}
static inline void
ieee80211_iter_keys_rcu(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
    void (*iter)(struct ieee80211_hw *, struct ieee80211_vif *,
    struct ieee80211_sta *, struct ieee80211_key_conf *, void *), void *data)
{
	/* Keys are handled by net80211 in software. */
}
#define for_each_set_bit_from(b, m, s) \
	for ((b) = find_next_bit(m, s, b); (b) < (s); (b) = find_next_bit(m, s, (b) + 1))

#endif /* _RTW89_COMPAT_MAC80211_H_ */
