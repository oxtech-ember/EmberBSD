/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW8723D_CHANNEL_H_
#define _DEV_IC_RTW8723D_CHANNEL_H_
struct rtw88_io;
struct rtw8723d_boot_ops;

/* Low-level 2.4GHz HT20 tuning only: primary/center channel 1..13, one
 * spatial stream. The owner serializes a powered chip and keeps TX closed.
 * This does not admit any transmit channel or power: explicit profile,
 * calibration and power programming remain separate owner gates. Channel13
 * needs a bounded 30ms PSD measurement; restore its saved inactive controls
 * even on fault/cancel. A running PSD operation is rejected before retuning.
 * On error after mutation, keep TX closed and unwind/reinitialize power.
 * No software channel-state or readiness publication occurs here. */
int rtw8723d_channel20(struct rtw88_io *,
    const struct rtw8723d_boot_ops *, unsigned);
#endif /* _DEV_IC_RTW8723D_CHANNEL_H_ */
