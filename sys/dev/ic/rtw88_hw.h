/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88. */

#ifndef _DEV_IC_RTW88_HW_H_
#define _DEV_IC_RTW88_HW_H_

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/null.h>
#else
#include <stddef.h>
#include <stdint.h>
#endif

/* Dedicated boot mailbox payload, pinned FreeBSD d2145e9b main.c/efuse.h.
 * A firmware claim only; this does not admit any HT/VHT operating mode. */
#define RTW88_HW_FEATURE_LEN 13
#define RTW88_HW_KNOWN_BW 0x01
#define RTW88_HW_KNOWN_NSS 0x02
#define RTW88_HW_KNOWN_ANTENNAS 0x04
#define RTW88_HW_KNOWN_PROTOCOL 0x08
struct rtw88_hw_feature {
	uint8_t raw[RTW88_HW_FEATURE_LEN];
	uint8_t hci, bw, nss, antennas, protocol;
	uint8_t known, bandwidths; /* bit0=20, bit1=40, bit2=80 MHz */
};
int rtw88_hw_feature_decode(const uint8_t *, size_t, struct rtw88_hw_feature *);

#define RTW8822C_TX_PACKET_DESC_SIZE	48
#define RTW8822C_TX_BUFFER_DESC_SIZE	16
#define RTW8822C_RX_PACKET_DESC_SIZE	24
#define RTW8822C_RX_BUFFER_DESC_SIZE	8
#define RTW8822C_FW_NAME			"rtw88/rtw8822c_fw.bin"
#define RTW8822C_DMA_ADDR_BITS		32

#define RTW88_TX_BMC		0x0001
#define RTW88_TX_DIS_QSELSEQ	0x0002
#define RTW88_TX_AMPDU		0x0004
#define RTW88_TX_REPORT		0x0008
#define RTW88_TX_BT_NULL		0x0010
#define RTW88_TX_USE_RATE		0x0020
#define RTW88_TX_DIS_RATE_FB	0x0040
#define RTW88_TX_RTS		0x0080
#define RTW88_TX_NAV_USE_HDR	0x0100
#define RTW88_TX_SHORT_GI		0x0200
#define RTW88_TX_LDPC		0x0400
#define RTW88_TX_HWSEQ		0x0800
#define RTW88_TX_FLAGS		0x0fff

struct rtw88_tx_info {
	uint32_t packet_size;
	uint32_t flags;
	uint16_t sequence;	/* 12-bit software sequence. */
	uint16_t report_sequence;	/* 12-bit SW_DEFINE field. */
	uint8_t mac_id;
	uint8_t queue;		/* TID 0-15, beacon 16, high 17, mgmt 18, H2C 19. */
	uint8_t rate_id;
	uint8_t security;	/* Raw two-bit SEC_TYPE, not a cipher policy. */
	uint8_t rate;
	uint8_t bandwidth;
	uint8_t stbc;
	uint8_t ampdu_density;
	uint8_t ampdu_factor;	/* MAX_AGG_NUM encoding. */
	uint8_t hw_ssn_sel;
	uint8_t tim_offset;	/* Zero disables TIM insertion. */
	uint8_t packet_offset;	/* Only zero is supported by this fixed48 profile. */
};

/* Access categories use net80211 order: BE, BK, VI, VO. */
#define RTW88_WME_NAC 4U
struct rtw88_wme_map {
	uint32_t edca;
	uint8_t ac, ring, qsel;
};
/* TIDs 0..7 only. Publish no partial output on invalid/unsupported input. */
int rtw88_wme_map(unsigned, struct rtw88_wme_map *);

struct rtw88_edca_params {
	unsigned aifsn, ecw_min, ecw_max;
	unsigned txop;	/* Hardware units are 32 microseconds. */
};
/* Validate the complete set before publishing words in BE/BK/VI/VO order. */
int rtw88_edca_encode(const struct rtw88_edca_params[RTW88_WME_NAC],
    unsigned, unsigned, uint32_t[RTW88_WME_NAC]);

#define RTW88_RX_CRC_ERROR	0x01
#define RTW88_RX_ICV_ERROR	0x02
#define RTW88_RX_PHY_STATUS	0x04
#define RTW88_RX_SWDEC		0x08
#define RTW88_RX_C2H		0x10

/* Optional, generation-bound PHY observations. No calibrated dBm ABI. */
#define RTW88_SIGNAL_MISSING 1U
#define RTW88_SIGNAL_SHORT 2U
#define RTW88_SIGNAL_PAGE 3U
#define RTW88_SIGNAL_RATE 4U
#define RTW88_SIGNAL_CHANNEL 5U
#define RTW88_SIGNAL_WIDTH 6U
#define RTW88_SIGNAL_GAIN 7U
#define RTW88_SIGNAL_GENERATION 8U
#define RTW88_SIGNAL_NUMBER 9U
#define RTW88_SIGNAL_PATH 10U
#define RTW88_SIGNAL_REASONS 11U
struct rtw88_phy_state {
    uint32_t generation;
    int gain_error;
    uint8_t paths, gain_valid, gain_low, gain_high;
};
struct rtw88_rx_signal {
    uint32_t generation;
    int16_t power[2]; /* Vendor power convention; not calibrated RF evidence. */
    uint16_t length;
    uint8_t valid, reason, page, rate, channel, paths, bandwidth, rxsc;
    uint8_t pwdb[2], gain[2], quality; /* Unsigned relative stack index0..100. */
};
struct rtw88_rx_info {
	/* Host DMA consumption identity. Pure wire decode leaves both zero. */
	uint64_t sequence;
	uint32_t generation;
	struct rtw88_rx_signal signal;
	size_t driver_info_offset;
	size_t driver_info_size;
	size_t payload_offset;
	uint32_t tsf_low;
	uint16_t packet_size;
	uint8_t flags;
	uint8_t shift;
	uint8_t security;	/* Raw three-bit ENC_TYPE. */
	uint8_t mac_id;
	uint8_t ppdu_count;
	uint8_t rate;
	uint8_t bandwidth;
};

/*
 * Native fixed48 TX profile: OFFSET=48, LS=1, PKT_OFFSET=0. Compute the
 * RTL8822C XOR checksum over all 24 little-endian halfwords (w7 low16
 * cleared first). Reserved fields are zero. No DMA address is in this
 * packet descriptor; validate all buffer/ring ranges separately.
 * Return EINVAL for null/bounds/field errors, ENOTSUP for packet_offset != 0.
 * Write exactly 48 bytes on success; leave dst unchanged on error.
 */
int rtw88_tx_encode(uint8_t *, size_t, const struct rtw88_tx_info *);

/*
 * len is the available received buffer, including the 24-byte descriptor.
 * Bound driver info, shift and payload before publishing offsets. Trailing
 * buffer capacity is allowed. Hardware error flags remain visible; success
 * proves format bounds, not a valid 802.11 frame or successful decryption.
 * Return EINVAL for null/short/out-of-buffer input. Leave *out unchanged
 * on error. Both codecs accept unaligned byte buffers.
 */
/* PHY bytes are bounded by driver_info_size, not payload capacity. Always
 * replace *out, including invalid observations. Invalid PHY must not drop DATA.
 * Native legacy BW20 only. Power conversion follows pinned RTL8822C page0/1;
 * quality is power_2_db(signal_power), not firmware two-path averaged RSSI. */
int rtw88_rx_signal_decode(const uint8_t *, size_t, const struct rtw88_rx_info *,
    const struct rtw88_phy_state *, uint32_t, unsigned, struct rtw88_rx_signal *);
int rtw88_rx_decode(const uint8_t *, size_t, struct rtw88_rx_info *);

/*
 * Validate every byte of a nonempty DMA range before narrowing an address.
 * addr_bits 1..32 selects an aperture no wider than the evidenced PCI
 * profile. Return EINVAL for zero length or widths outside 1..64, ENOTSUP
 * for widths 33..64, EOVERFLOW for an address/range outside the aperture.
 * The last byte may equal the aperture maximum. No mapping is performed.
 */
int rtw88_dma_validate(uint64_t, size_t, unsigned);

#define RTW88_FW_HEADER_SIZE	64
#define RTW88_FW_CHECKSUM_SIZE	8

/* File ranges include the eight checksum bytes after each payload. */
struct rtw88_fw_section {
	size_t offset;
	size_t size;
	uint32_t payload_size;
	uint32_t load_address;	/* Header address with bit 31 removed. */
};

struct rtw88_fw_info {
	uint16_t signature;	/* Shared by RTL8822B and RTL8822C. */
	uint16_t version;
	uint8_t subversion;
	uint8_t subindex;
	uint16_t h2c_version;
	uint32_t feature;
	struct rtw88_fw_section dmem;
	struct rtw88_fw_section imem;
	struct rtw88_fw_section emem;
};

/*
 * Accept only the pinned RTL8822C normal-firmware load profile documented
 * in rtw88-provenance.md. No alignment is required. Return 0, EINVAL for
 * malformed input, EOVERFLOW for arithmetic overflow, or ENOTSUP for an
 * unsupported signature or load profile. Leave *out unchanged on error.
 * Success validates container ranges, not firmware authenticity, checksum
 * contents, silicon compatibility, or readiness to enable the radio.
 */
int rtw88_fw_parse(const uint8_t *, size_t, struct rtw88_fw_info *);

#endif /* _DEV_IC_RTW88_HW_H_ */
