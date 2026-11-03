/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW8723D_IQK_CAL_H_
#define _DEV_IC_RTW8723D_IQK_CAL_H_
#include <dev/ic/rtw8723d_iqk.h>

/* Serialized powered chip, software TX admission closed, valid generation.
 * Save ADDA/MAC/BB state before mutation. Up to three rounds, two attempts
 * per TX/RX measurement, retrying only completed rejected measurements.
 * Require two complete measured rounds agreeing within the donor's signed
 * ten-bit tolerance five; do not substitute default or hybrid coefficients.
 * Restore temporary registers after every round, keep hardware TX paused
 * until agreed matrix programming succeeds. All errors after mutation need
 * owner power unwind, including EAGAIN (no agreeing complete rounds).
 * Output is unchanged on failure. Success establishes IQK only, not radio
 * channel, regulatory power, netif, firmware or full TX readiness.
 * No board power or hardware invocation is provided by this source API. */
int rtw8723d_iqk_calibrate(struct rtw88_io *,
    const struct rtw8723d_boot_ops *, struct rtw8723d_iqk_matrix *);
#endif /* _DEV_IC_RTW8723D_IQK_CAL_H_ */
