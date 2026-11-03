/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW88_PACKET_H_
#define _DEV_IC_RTW88_PACKET_H_

#include <sys/types.h>
#ifndef _KERNEL
#include <stdint.h>
#endif

#define RTW88_TXQ_MGMT		0U
#define RTW88_TXQ_BE		1U
#define RTW88_TXQ_BK		2U
#define RTW88_TXQ_VI		3U
#define RTW88_TXQ_VO		4U
#define RTW88_NTXQ		5U

#define RTW88_PACKET_SIZE 4096U

/* Numeric protection and frame limits; no retained stack/peer pointer. */
struct rtw88_ht20_policy {
	uint16_t peer_cap, mtu, max_mpdu, rts_threshold;
	uint8_t secondary, qualification;
};
/* Numeric rate policy; no retained stack/peer pointer. */
struct rtw88_ra_config {
	uint64_t epoch;
	unsigned channel, common_rates;
	uint8_t mode, use_protection;
	uint8_t phy;
	uint16_t ht_mcs, ht_operation, vht_tx_map;
	uint32_t profile_generation;
	struct rtw88_ht20_policy ht;
	/* RA_INFO SGI_EN: the AP receives SGI at the committed width.
	 * LDPC: the AP receives LDPC (RA_INFO LDPC and the descriptor).
	 * HT20 and VHT entries of the VHT experiment only. */
	uint8_t sgi, ldpc;
};

enum rtw88_tx_mode {
	RTW88_TX_MODE_RAW,
	RTW88_TX_MODE_FIXED,
	RTW88_TX_MODE_AUTO,
	RTW88_TX_MODE_AMPDU
};

/* Values copied from the stack. No retained mbuf, node or key pointer. */
struct rtw88_tx_fixed {
	uint16_t rate500, peer_mask, channel;
	uint8_t mode, associated, use_protection;
};

struct rtw88_tx_agg {
	uint8_t tid, density, count;
};

#endif
