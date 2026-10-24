/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88. */

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/param.h>
#include <sys/errno.h>
#else
#include <stddef.h>
#include <errno.h>
#endif

#include "rtw88_hw.h"

/* FreeBSD d2145e9b main.c/efuse.h; BSD-3-Clause option. No defaults for
 * IGNORE or unsupported encodings: unknown must remain distinguishable. */
int
rtw88_hw_feature_decode(const uint8_t *p, size_t len,
    struct rtw88_hw_feature *out)
{
	struct rtw88_hw_feature value = { 0 };
	uint32_t word;
	unsigned i;

	if (p == NULL || out == NULL || len != RTW88_HW_FEATURE_LEN)
		return EINVAL;
	for (i = 0; i < RTW88_HW_FEATURE_LEN; i++)
		value.raw[i] = p[i];
	word = (uint32_t)p[4] | (uint32_t)p[5] << 8 |
	    (uint32_t)p[6] << 16 | (uint32_t)p[7] << 24;
	value.hci = word & 15;
	value.bw = (word >> 16) & 7;
	value.nss = (word >> 19) & 3;
	value.antennas = (word >> 21) & 7;
	value.protocol = (word >> 26) & 3;
	if (value.bw == 6 || value.bw == 7) {
		value.known |= RTW88_HW_KNOWN_BW;
		value.bandwidths = value.bw == 6 ? 3 : 7;
	}
	if (value.nss == 1 || value.nss == 2)
		value.known |= RTW88_HW_KNOWN_NSS;
	if (value.antennas == 1 || value.antennas == 2)
		value.known |= RTW88_HW_KNOWN_ANTENNAS;
	if (value.protocol == 3)
		value.known |= RTW88_HW_KNOWN_PROTOCOL;
	*out = value;
	return 0;
}

/*
 * WME queue and EDCA formats follow FreeBSD rtw88 at
 * d2145e9b96448da34d3cfede24c509af6d830c92, pci.c and mac80211.c.
 * The BSD-3-Clause option applies. No LinuxKPI policy is imported.
 */
int
rtw88_wme_map(unsigned tid, struct rtw88_wme_map *out)
{
	static const uint8_t ac[8] = { 0, 1, 1, 0, 2, 2, 3, 3 };
	static const uint8_t ring[4] = { 1, 0, 2, 3 };
	static const uint32_t edca[4] = { 0x0508, 0x050c, 0x0504, 0x0500 };
	struct rtw88_wme_map map;

	if (out == NULL || tid > 15)
		return EINVAL;
	if (tid > 7)
		return EOPNOTSUPP;
	map.ac = ac[tid];
	map.ring = ring[map.ac];
	map.edca = edca[map.ac];
	map.qsel = (uint8_t)tid;
	*out = map;
	return 0;
}

int
rtw88_edca_encode(const struct rtw88_edca_params p[RTW88_WME_NAC],
    unsigned slot, unsigned sifs, uint32_t out[RTW88_WME_NAC])
{
	uint32_t words[RTW88_WME_NAC];
	unsigned i, aifs;

	if (p == NULL || out == NULL || (slot != 9 && slot != 20) ||
	    (sifs != 10 && sifs != 16))
		return EINVAL;
	for (i = 0; i < RTW88_WME_NAC; i++) {
		if (p[i].aifsn > 15 || p[i].ecw_min > 15 ||
		    p[i].ecw_max > 15 || p[i].ecw_min > p[i].ecw_max ||
		    p[i].txop > 0xffff)
			return EINVAL;
		aifs = p[i].aifsn * slot + sifs;
		if (aifs > 0xff)
			return ERANGE;
		words[i] = (uint32_t)p[i].txop << 16 |
		    (uint32_t)p[i].ecw_max << 12 |
		    (uint32_t)p[i].ecw_min << 8 | aifs;
	}
	for (i = 0; i < RTW88_WME_NAC; i++)
		out[i] = words[i];
	return 0;
}

/* Byte decoding avoids unaligned accesses and host-endian assumptions. */
static uint16_t
rtw88_get16(const uint8_t *p)
{

	return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t
rtw88_get32(const uint8_t *p)
{

	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int
rtw88_fw_section(size_t *cursor, size_t size, uint32_t payload,
    uint32_t address, int checksum, struct rtw88_fw_section *section)
{
	uint32_t length;

	if (checksum && payload > UINT32_MAX - RTW88_FW_CHECKSUM_SIZE)
		return EOVERFLOW;
	length = payload + (checksum ? RTW88_FW_CHECKSUM_SIZE : 0);
	if (*cursor > size || length > size - *cursor)
		return EINVAL;
	section->offset = *cursor;
	section->size = length;
	section->payload_size = payload;
	section->load_address = address & UINT32_C(0x7fffffff);
	/* The subtraction above proves this addition cannot wrap size_t. */
	*cursor += length;
	return 0;
}

int
rtw88_fw_parse(const uint8_t *data, size_t size, struct rtw88_fw_info *out)
{
	struct rtw88_fw_info info = { 0 };
	size_t cursor = RTW88_FW_HEADER_SIZE;
	uint32_t emem_size;
	int error;

	if (data == NULL || out == NULL || size < RTW88_FW_HEADER_SIZE)
		return EINVAL;
	info.signature = rtw88_get16(data);
	/* This signature is common to B and C, not a chip identifier. */
	if (info.signature != 0x8822)
		return ENOTSUP;
	error = rtw88_fw_section(&cursor, size, rtw88_get32(data + 36),
	    rtw88_get32(data + 32), 1, &info.dmem);
	if (error != 0)
		return error;
	error = rtw88_fw_section(&cursor, size, rtw88_get32(data + 48),
	    rtw88_get32(data + 60), 1, &info.imem);
	if (error != 0)
		return error;
	emem_size = (data[24] & 0x10) ? rtw88_get32(data + 52) : 0;
	error = rtw88_fw_section(&cursor, size, emem_size,
	    rtw88_get32(data + 56), emem_size != 0, &info.emem);
	if (error != 0)
		return error;
	if (cursor != size)
		return EINVAL;

	/*
	 * Fail closed to the evidenced 9.9.11 load profile. Exact bases and
	 * payload lengths bound every destination, including its checksum.
	 * Raw address tags must match too. Future layouts need new evidence.
	 */
	if ((data[24] & 0x10) == 0 ||
	    rtw88_get32(data + 32) != UINT32_C(0x80200000) ||
	    rtw88_get32(data + 60) != UINT32_C(0x80030000) ||
	    rtw88_get32(data + 56) != UINT32_C(0x80100000) ||
	    info.dmem.payload_size != 19328 ||
	    info.imem.payload_size != 54064 ||
	    info.emem.payload_size != 128976)
		return ENOTSUP;

	info.version = rtw88_get16(data + 4);
	info.subversion = data[6];
	info.subindex = data[7];
	info.h2c_version = rtw88_get16(data + 28);
	info.feature = rtw88_get32(data + 12);
	*out = info;
	return 0;
}

int
rtw88_tx_encode(uint8_t *dst, size_t cap, const struct rtw88_tx_info *tx)
{
	uint32_t w[12] = { 0 };
	uint16_t checksum = 0;
	size_t i;

	if (dst == NULL || tx == NULL || cap < RTW8822C_TX_PACKET_DESC_SIZE)
		return EINVAL;
	if (tx->packet_size == 0 || tx->packet_size > 65535 ||
	    (tx->flags & ~RTW88_TX_FLAGS) != 0 || tx->sequence > 4095 ||
	    tx->report_sequence > 4095 || tx->queue > 19 ||
	    tx->rate_id > 31 || tx->security > 3 || tx->rate > 127 ||
	    tx->bandwidth > 3 || tx->stbc > 3 || tx->ampdu_density > 7 ||
	    tx->ampdu_factor > 31 || tx->hw_ssn_sel > 3 || tx->tim_offset > 127)
		return EINVAL;
	if (tx->packet_offset != 0)
		return ENOTSUP;

#define TXFLAG(f, s) (((tx->flags & (f)) != 0) ? UINT32_C(1) << (s) : 0)
	w[0] = tx->packet_size | (RTW8822C_TX_PACKET_DESC_SIZE << 16) |
	    TXFLAG(RTW88_TX_BMC, 24) | UINT32_C(1) << 26 |
	    TXFLAG(RTW88_TX_DIS_QSELSEQ, 31);
	w[1] = tx->mac_id | (uint32_t)tx->queue << 8 |
	    (uint32_t)tx->rate_id << 16 | (uint32_t)tx->security << 22;
	if (tx->queue == 17)
		w[1] |= UINT32_C(1) << 29;
	w[2] = TXFLAG(RTW88_TX_AMPDU, 12) | TXFLAG(RTW88_TX_REPORT, 19) |
	    (uint32_t)tx->ampdu_density << 20 | TXFLAG(RTW88_TX_BT_NULL, 23);
	w[3] = (uint32_t)tx->hw_ssn_sel << 6 | TXFLAG(RTW88_TX_USE_RATE, 8) |
	    TXFLAG(RTW88_TX_DIS_RATE_FB, 10) | TXFLAG(RTW88_TX_RTS, 12) |
	    TXFLAG(RTW88_TX_NAV_USE_HDR, 15) | (uint32_t)tx->ampdu_factor << 17;
	/* RTL8822C does not use the old DATA_RATE_FB_LIMIT field. */
	w[4] = tx->rate;
	w[5] = TXFLAG(RTW88_TX_SHORT_GI, 4) | (uint32_t)tx->bandwidth << 5 |
	    TXFLAG(RTW88_TX_LDPC, 7) | (uint32_t)tx->stbc << 8;
	if ((tx->flags & RTW88_TX_RTS) != 0) {
		w[4] |= UINT32_C(8) << 24; /* DESC_RATE24M. */
		w[5] |= UINT32_C(1) << 12;
	}
	w[6] = tx->report_sequence;
	w[8] = TXFLAG(RTW88_TX_HWSEQ, 15);
	w[9] = (uint32_t)tx->sequence << 12;
	if (tx->tim_offset != 0)
		w[9] |= (UINT32_C(1) << 7) | tx->tim_offset;
#undef TXFLAG

	/* w7 low16 is zero. RTL8822C covers 48 bytes, not the older 32. */
	for (i = 0; i < 12; i++)
		checksum ^= (uint16_t)w[i] ^ (uint16_t)(w[i] >> 16);
	w[7] = checksum;
	/* All input reads and validation precede publication. */
	for (i = 0; i < 12; i++) {
		dst[4 * i] = (uint8_t)w[i];
		dst[4 * i + 1] = (uint8_t)(w[i] >> 8);
		dst[4 * i + 2] = (uint8_t)(w[i] >> 16);
		dst[4 * i + 3] = (uint8_t)(w[i] >> 24);
	}
	return 0;
}

int
rtw88_rx_decode(const uint8_t *src, size_t len, struct rtw88_rx_info *out)
{
	struct rtw88_rx_info rx = { 0 };
	uint32_t w0, w2;

	if (src == NULL || out == NULL || len < RTW8822C_RX_PACKET_DESC_SIZE)
		return EINVAL;
	w0 = rtw88_get32(src);
	w2 = rtw88_get32(src + 8);
	rx.packet_size = w0 & 0x3fff;
	rx.driver_info_size = ((w0 >> 16) & 0xf) * 8;
	rx.shift = (w0 >> 24) & 3;
	rx.driver_info_offset = RTW8822C_RX_PACKET_DESC_SIZE + rx.shift;
	rx.payload_offset = rx.driver_info_offset + rx.driver_info_size;
	/* Bounded fields make these offsets <= 147, regardless of len. */
	if (rx.payload_offset > len || rx.packet_size > len - rx.payload_offset)
		return EINVAL;
	if ((w0 & (UINT32_C(1) << 14)) != 0)
		rx.flags |= RTW88_RX_CRC_ERROR;
	if ((w0 & (UINT32_C(1) << 15)) != 0)
		rx.flags |= RTW88_RX_ICV_ERROR;
	if ((w0 & (UINT32_C(1) << 26)) != 0)
		rx.flags |= RTW88_RX_PHY_STATUS;
	if ((w0 & (UINT32_C(1) << 27)) != 0)
		rx.flags |= RTW88_RX_SWDEC;
	if ((w2 & (UINT32_C(1) << 28)) != 0)
		rx.flags |= RTW88_RX_C2H;
	rx.security = (w0 >> 20) & 7;
	rx.mac_id = rtw88_get32(src + 4) & 0x7f;
	rx.ppdu_count = (w2 >> 29) & 3;
	rx.rate = rtw88_get32(src + 12) & 0x7f;
	rx.bandwidth = (rtw88_get32(src + 16) >> 4) & 3;
	rx.tsf_low = rtw88_get32(src + 20);
	*out = rx;
	return 0;
}

int
rtw88_dma_validate(uint64_t addr, size_t len, unsigned addr_bits)
{
	uint64_t maximum;

	if (len == 0 || addr_bits == 0 || addr_bits > 64)
		return EINVAL;
	if (addr_bits > RTW8822C_DMA_ADDR_BITS)
		return ENOTSUP;
	maximum = (UINT64_C(1) << addr_bits) - 1;
	/* Check the last byte without forming a wrapping addr + len. */
	if (addr > maximum || len - 1 > maximum - addr)
		return EOVERFLOW;
	return 0;
}

/* RTL8822C query_phy_status_page0/page1 and phy.c rtw_phy_power_2_db:
 * FreeBSD d2145e9b96448da34d3cfede24c509af6d830c92, Realtek 2018-2019,
 * GPL-2.0 OR BSD-3-Clause. Native bounded byte loads and wide arithmetic. */
int
rtw88_rx_signal_decode_tuple(const uint8_t *p, size_t len,
    const struct rtw88_rx_info *rx, const struct rtw88_phy_state *ctx,
    uint32_t generation, const struct rtw88_channel_tuple *channel,
    struct rtw88_rx_signal *out)
{
    struct rtw88_rx_signal v = { 0 };
    unsigned path, vht;
    int value, selected;

    if (out == NULL)
        return EINVAL;
    v.generation = generation;
    v.length = len > UINT16_MAX ? UINT16_MAX : (uint16_t)len;
    v.page = UINT8_MAX;
    v.reason = RTW88_SIGNAL_MISSING;
    if (rx == NULL || (rx->flags & RTW88_RX_PHY_STATUS) == 0 ||
        (rx->flags & (RTW88_RX_CRC_ERROR | RTW88_RX_ICV_ERROR | RTW88_RX_C2H)) != 0)
        goto invalid;
    v.rate = rx->rate;
    vht = rx->rate >= 0x2c && rx->rate <= 0x3f;
    v.bandwidth = rx->bandwidth;
    v.reason = RTW88_SIGNAL_SHORT;
    if (p == NULL || len < 1 || len > 120)
        goto invalid;
    v.page = p[0] & 15;
    v.reason = RTW88_SIGNAL_PAGE;
    if (v.page > 1)
        goto invalid;
    v.reason = RTW88_SIGNAL_SHORT;
    if (len < (v.page == 0 ? 20U : 8U))
        goto invalid;
    v.channel = p[6];
    v.pwdb[0] = p[1];
    v.pwdb[1] = p[v.page == 0 ? 16 : 2];
    if (v.page == 0) {
        v.gain[0] = p[2] & 63;
        v.gain[1] = p[19] & 63;
    } else {
        /* Legacy OFDM uses L_RXSC; bounded native VHT uses HT_RXSC. */
        v.rxsc = vht ? (p[5] >> 4) & 15 : p[5] & 15;
    }
    v.reason = RTW88_SIGNAL_GENERATION;
    if (ctx == NULL || generation == 0 || ctx->generation != generation)
        goto invalid;
    v.paths = ctx->paths;
    v.reason = RTW88_SIGNAL_PATH;
    if (ctx->paths != 3) /* Actual native TRX AB profile only. */
        goto invalid;
    v.reason = RTW88_SIGNAL_CHANNEL;
    if (channel == NULL || channel->primary == 0 ||
        channel->primary != v.channel ||
        !((channel->primary >= 1 && channel->primary <= 13) ||
          (channel->primary >= 36 && channel->primary <= 64 &&
          channel->primary % 4 == 0) ||
          (channel->primary >= 100 && channel->primary <= 144 &&
          channel->primary % 4 == 0) ||
          (channel->primary >= 149 && channel->primary <= 165 &&
          (channel->primary - 149) % 4 == 0)))
        goto invalid;
    v.reason = RTW88_SIGNAL_RATE;
    if ((v.page == 0 && (rx->rate > 3 || channel->primary > 14)) ||
        (v.page == 1 && !((rx->rate >= 4 && rx->rate <= 11) ||
          (vht && channel->primary > 14))))
        goto invalid;
    v.reason = RTW88_SIGNAL_WIDTH;
    if (channel->width_mhz != 20 || channel->center != channel->primary ||
        rx->bandwidth != 0 || (v.page == 1 &&
        (v.rxsc > 8 || (vht && v.rxsc == 0 && !ctx->channel_valid))))
        goto invalid;
    v.reason = RTW88_SIGNAL_GAIN;
    if (v.page == 0 && (!ctx->gain_valid || ctx->gain_error != 0 ||
        ctx->gain_low > ctx->gain_high || ctx->gain_high > 63))
        goto invalid;
    v.reason = RTW88_SIGNAL_NUMBER;
    for (path = 0; path < 2; path++) {
        value = v.pwdb[path];
        if (v.page == 0) {
            if (v.gain[path] < ctx->gain_low)
                value += 2 * (ctx->gain_low - v.gain[path]);
            else if (v.gain[path] > ctx->gain_high)
                value -= 2 * (v.gain[path] - ctx->gain_high);
        }
        value -= 110;
        /* Do not reproduce signed-byte wrapping or invent a usable index. */
        if (value < -120 || value >= 20)
            goto invalid;
        v.power[path] = (int16_t)value;
    }
    selected = v.power[0]; /* CCK reference uses pathA, not max(A,B). */
    if (v.page == 1 && v.power[1] > selected)
        selected = v.power[1];
    v.quality = selected <= -100 ? 0 : selected >= 0 ? 100 :
        (uint8_t)(100 + selected);
    v.valid = 1;
    v.reason = 0;
    *out = v;
    return 0;
invalid:
    v.valid = v.quality = 0;
    *out = v;
    return ENOENT;
}

int
rtw88_rx_signal_decode(const uint8_t *p, size_t len,
    const struct rtw88_rx_info *rx, const struct rtw88_phy_state *ctx,
    uint32_t generation, unsigned channel, struct rtw88_rx_signal *out)
{
    const struct rtw88_channel_tuple tuple = { channel, channel, 20 };

    return rtw88_rx_signal_decode_tuple(p, len, rx, ctx, generation,
        &tuple, out);
}
