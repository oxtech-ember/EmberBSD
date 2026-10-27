/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
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
