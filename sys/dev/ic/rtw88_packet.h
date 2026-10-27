/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _DEV_IC_RTW88_PACKET_H_
#define _DEV_IC_RTW88_PACKET_H_

#include <sys/types.h>
#ifndef _KERNEL
#include <stdint.h>
#endif

/* Values copied from the stack. No retained mbuf, node or key pointer. */
struct rtw88_tx_fixed {
	uint16_t rate500, peer_mask, channel;
	uint8_t mode, associated, use_protection;
};

struct rtw88_tx_agg {
	uint8_t tid, density, count;
};

#endif
