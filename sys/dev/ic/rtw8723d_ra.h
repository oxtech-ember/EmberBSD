/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW8723D_RA_H_
#define _DEV_IC_RTW8723D_RA_H_
struct rtw88_softc;
struct rtw88_ra_config;
/* Initial 2.4GHz legacy B/G rate policy for the shared netif. HT/VHT,
 * LDPC/SGI, power training and firmware offloads are not admitted here.
 * Caller owns the actor and reserves one register-mailbox command, unless
 * use_protection selects the existing software CCK fallback (zero commands).
 * Successful publication is firmware command submission, not a radio ACK. */
int rtw8723d_runtime_ra(struct rtw88_softc *, const struct rtw88_ra_config *);
#endif
