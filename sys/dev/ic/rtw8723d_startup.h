/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW8723D_STARTUP_H_
#define _DEV_IC_RTW8723D_STARTUP_H_
struct rtw88_io;

/* Select the native software-crypto path before station programming or TX.
 * Owner holds a powered chip, device interrupts masked and software TX closed.
 * Hardware TX stays paused; no key material is accepted or programmed.
 * check may be NULL only under full synchronous owner serialization.
 * Failure after mutation requires owner power unwind. No readiness grant.
 */
int rtw8723d_swcrypto_configure(struct rtw88_io *, int (*)(void *), void *);

/* Scalar hardware tail of the initial PHY/DM stage. After PHY tables, call
 * before_lck, the separately owned successful LCK, then after_lck. Software
 * histories/watchdog adaptation are not provided or activated here. TX stays
 * paused and the same serialization/error-unwind contract applies.
 * These calls do not replace LCK, IQK, channel/power or coexistence setup. */
int rtw8723d_dm_before_lck(struct rtw88_io *, int (*)(void *), void *);
int rtw8723d_dm_after_lck(struct rtw88_io *, int (*)(void *), void *);

#endif /* _DEV_IC_RTW8723D_STARTUP_H_ */
