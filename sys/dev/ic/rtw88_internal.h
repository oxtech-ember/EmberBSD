/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _DEV_IC_RTW88_INTERNAL_H_
#define _DEV_IC_RTW88_INTERNAL_H_
#include <dev/ic/rtw88var.h>

/* Pure shared policy. These helpers do not access registers. */
int rtw88_channel_admit(const struct rtw88_channel_tuple *);
int rtw88_channel_narrow(const struct rtw88_softc *);
int rtw88_channel_dfs(unsigned);
int rtw88_ht20_policy_check(const struct rtw88_ht20_policy *, unsigned);
#ifdef RTW88_VHT80
struct rtw88_vht_rate_set {
	unsigned mask;
	uint8_t rate_id, seed;
};
int rtw88_vht_rates(const struct rtw88_softc *, unsigned, struct rtw88_vht_rate_set *);
#endif
#endif
