/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW8723D_CHIP_H_
#define _DEV_IC_RTW8723D_CHIP_H_
#include <dev/ic/rtw8723d_runtime.h>
struct rtw88_chip_ops;
extern const struct rtw88_chip_ops rtw8723d_chip_ops;
/* Quiescent frontend, fresh embedded context, full SDIO HCI selected first.
 * Publishes no netif/RF readiness. After identify, the frontend supplies an
 * explicit profile, power policy and qualified PHY package selector in
 * ctx->config before start. No regulatory/profile fallback is constructed. */
int rtw8723d_chip_bind(struct rtw88_softc *, struct rtw8723d_runtime *,
    const struct rtw8723d_core_hooks *);
#endif
