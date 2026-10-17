/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original native net80211 integration, AI-assisted. */
#ifndef _DEV_IC_RTW88_NET_H_
#define _DEV_IC_RTW88_NET_H_
#include <sys/device.h>
#include <sys/mutex.h>
struct rtw88_softc;
struct rtw88_net;
/* PCI owns identify, the supplied lock, and core lifetime. No attach RF start. */
int rtw88_net_attach(struct rtw88_softc *, device_t, kmutex_t *,
    const uint8_t[6], struct rtw88_net **);
/* Process context. Stop users and remove this wrapper before core detach. */
void rtw88_net_detach(struct rtw88_net *);
#endif
