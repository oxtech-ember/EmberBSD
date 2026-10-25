/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original native net80211 integration, AI-assisted. */
#ifndef _DEV_IC_RTW88_NET_H_
#define _DEV_IC_RTW88_NET_H_
#include <sys/device.h>
#include <sys/mutex.h>
#include <dev/ic/rtw88_net_policy.h>
struct rtw88_softc;
struct rtw88_net;
struct rtw88_net_stats {
	struct rtw88_net_rx_counts rx;
	uint64_t nomem, chained, first_short, fw_stale;
	uint32_t beacon, chanmismatch, badchan, elem_missing;
	uint32_t elem_toobig, elem_toosmall, nodealloc, mgtdiscard;
	struct rtw88_net_neg neg;
};
/* PCI owns identify, the supplied lock, and core lifetime. No attach RF start. */
int rtw88_net_attach(struct rtw88_softc *, device_t, kmutex_t *,
    const uint8_t[6], struct rtw88_net **);
/* Process context. Stop users and remove this wrapper before core detach. */
void rtw88_net_detach(struct rtw88_net *);
/* Caller holds the supplied lock and pins wrapper lifetime. No stack entry. */
void rtw88_net_get_stats(struct rtw88_net *, struct rtw88_net_stats *);
#endif
