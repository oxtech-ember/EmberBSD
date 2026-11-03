/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright 2024 Fiona Klute */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW8723D_LCK_H_
#define _DEV_IC_RTW8723D_LCK_H_

struct rtw88_io;
struct rtw8723d_boot_ops;

/* STA calibration only; continuous manufacturing TX is rejected. The caller
 * owns the powered, serialized chip and keeps its software TX gate closed.
 * wait_ms and cancelled callbacks are required, write_window is not used.
 * Poll at most 100 times, with 10 ms waits between unsuccessful polls.
 * Restore the saved RF configuration; restore the original TX pause only on
 * success. On any error the caller must unwind power, even when cleanup
 * succeeds. Return the first error, never a successful calibration on timeout.
 * Success establishes only LCK, not IQK, regulatory power or TX readiness. */
int rtw8723d_lck(struct rtw88_io *, const struct rtw8723d_boot_ops *);

#endif /* _DEV_IC_RTW8723D_LCK_H_ */
