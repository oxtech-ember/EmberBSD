/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _DEV_IC_RTW8821C_BOOT_H_
#define _DEV_IC_RTW8821C_BOOT_H_
struct rtw88_softc;
int rtw8821c_identify(struct rtw88_softc *);
int rtw8821c_read_efuse(struct rtw88_softc *);
int rtw8821c_power_pre(struct rtw88_softc *);
int rtw8821c_power(struct rtw88_softc *, int);
int rtw8821c_power_post(struct rtw88_softc *);
int rtw8821c_mac_init(struct rtw88_softc *);
#endif
