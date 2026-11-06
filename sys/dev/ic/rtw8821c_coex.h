/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _DEV_IC_RTW8821C_COEX_H_
#define _DEV_IC_RTW8821C_COEX_H_
struct rtw88_softc;
/* Caller serializes with PHY, power and LTE transport. */
int rtw8821c_coex_init(struct rtw88_softc *);
#endif
