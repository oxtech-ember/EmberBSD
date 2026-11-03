/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright 2024 Fiona Klute. */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: descriptor fields from Linux adc218676eef25575469234709c2d87185ca223a,
 * rtw88/tx.c and rtw8723x.c checksum. Native field packing follows EmberBSD
 * rtw8822c_hw.c, originally FreeBSD a143197801c680b958ab662c88cf086668a1958b.
 * Separate 40-byte/32-byte-checksum profile; no PCI descriptor is emitted. */
#ifdef _KERNEL
#include <sys/errno.h>
#else
#include <errno.h>
#endif
#include <dev/ic/rtw88_hw.h>
#include <dev/ic/rtw8723d_tx.h>

int
rtw8723d_tx_encode(uint8_t *dst, size_t cap, const struct rtw88_tx_info *tx)
{
	uint32_t w[10] = { 0 };
	uint16_t checksum = 0;
	size_t i;

	if (dst == NULL || tx == NULL || cap < RTW8723D_TX_PACKET_DESC_SIZE)
		return EINVAL;
	if (tx->packet_size == 0 || tx->packet_size > 65535 ||
	    (tx->flags & ~RTW88_TX_FLAGS) != 0 || tx->sequence > 4095 ||
	    tx->report_sequence > 4095 || tx->queue > 19 ||
	    tx->rate_id > 31 || tx->security > 3 || tx->rate > 127 ||
	    tx->bandwidth > 3 || tx->data_sc > 15 || tx->stbc > 3 ||
	    tx->ampdu_density > 7 || tx->ampdu_factor > 31 ||
	    tx->hw_ssn_sel > 3 || tx->tim_offset > 127)
		return EINVAL;
	/* Initial capability envelope, separate from raw field widths above. */
	if (tx->packet_offset != 0 || tx->bandwidth != 0 || tx->data_sc != 0 ||
	    tx->rate > 19 || tx->stbc != 0 || (tx->flags & RTW88_TX_LDPC) != 0)
		return EOPNOTSUPP;

#define TXFLAG(f, s) (((tx->flags & (f)) != 0) ? UINT32_C(1) << (s) : 0)
	w[0] = tx->packet_size | (RTW8723D_TX_PACKET_DESC_SIZE << 16) |
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
	w[4] = tx->rate;
	w[5] = TXFLAG(RTW88_TX_SHORT_GI, 4);
	if ((tx->flags & RTW88_TX_RTS) != 0) {
		w[4] |= UINT32_C(8) << 24;
		w[5] |= UINT32_C(1) << 12;
	}
	w[6] = tx->report_sequence;
	w[8] = TXFLAG(RTW88_TX_HWSEQ, 15);
	w[9] = (uint32_t)tx->sequence << 12;
	if (tx->tim_offset != 0)
		w[9] |= (UINT32_C(1) << 7) | tx->tim_offset;
#undef TXFLAG

	/* w7 checksum field starts zero; only words 0..7 are covered. */
	for (i = 0; i < 8; i++)
		checksum ^= (uint16_t)w[i] ^ (uint16_t)(w[i] >> 16);
	w[7] = (uint16_t)~checksum;
	for (i = 0; i < 10; i++) {
		dst[4 * i] = (uint8_t)w[i];
		dst[4 * i + 1] = (uint8_t)(w[i] >> 8);
		dst[4 * i + 2] = (uint8_t)(w[i] >> 16);
		dst[4 * i + 3] = (uint8_t)(w[i] >> 24);
	}
	return 0;
}
