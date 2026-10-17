/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original native implementation, AI-assisted. */
#ifndef _DEV_IC_RTW88_NET_POLICY_H_
#define _DEV_IC_RTW88_NET_POLICY_H_
#include <sys/types.h>
#ifdef _KERNEL
#include <sys/null.h>
#else
#include <stdint.h>
#include <stddef.h>
#endif
#define RTW88_NET_RX_LIMIT 64U
#define RTW88_NET_MPDU_LIMIT 4096U
struct rtw88_net_policy {
	uint64_t epoch;
	unsigned queued;
	int accepting;
	int dying;
	uint64_t beacon_epoch;
	uint32_t beacon_tick;
	uint32_t beacon_report_tick;
	int beacon_valid;
};
uint64_t	rtw88_net_request(struct rtw88_net_policy *);
int		rtw88_net_commit(struct rtw88_net_policy *, uint64_t, int, int);
void		rtw88_net_close(struct rtw88_net_policy *);
int		rtw88_net_rx_admit(struct rtw88_net_policy *, uint64_t,
    const uint8_t *, size_t, unsigned, unsigned);
void		rtw88_net_rx_release(struct rtw88_net_policy *);
int		rtw88_net_beacon_start(struct rtw88_net_policy *, uint64_t, uint32_t);
int		rtw88_net_beacon_note(struct rtw88_net_policy *, uint64_t, uint32_t,
    const uint8_t *, size_t, const uint8_t[6]);
int		rtw88_net_beacon_poll(struct rtw88_net_policy *, uint64_t, uint32_t,
    uint32_t);
uint32_t	rtw88_net_tu_to_ms(uint16_t);
#endif
