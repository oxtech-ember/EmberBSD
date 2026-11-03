/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright 2024 Fiona Klute */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW8723D_MAC_H_
#define _DEV_IC_RTW8723D_MAC_H_

struct rtw88_io;

/* The caller owns a powered, serialized RTL8723D. Configure the MAC before
 * loading PHY tables. First error/cancellation stops the sequence; the caller
 * unwinds partial setup. This does not establish firmware, calibration,
 * regulatory permission or TX readiness. check may be NULL. */
int rtw8723d_mac_init(struct rtw88_io *, int (*)(void *), void *);

/* Complete legacy SDIO MAC stage: normal FIFO allocation, chip MAC table,
 * RX driver-info/appended-FCS and SDIO interface setup, in that order.
 * After firmware boot, before PHY setup. Device interrupts masked, software TX closed,
 * no active traffic. Hardware TX stays paused. Same-generation errors need
 * power unwind; success does not establish calibration or network readiness.
 * check may be NULL only with full synchronous owner serialization. */
int rtw8723d_mac_configure(struct rtw88_io *, int (*)(void *), void *);

#endif /* _DEV_IC_RTW8723D_MAC_H_ */
