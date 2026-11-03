/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW8723D_TRX_H_
#define _DEV_IC_RTW8723D_TRX_H_
struct rtw88_io;

/* Legacy RTL8723D SDIO runtime FIFO/queue allocation, after successful
 * firmware boot and before MAC/PHY configuration. Single serialized owner,
 * no active traffic, software TX closed and device interrupts masked.
 * check may be NULL only when the owner excludes generation/stop changes
 * throughout the call. Polling is bounded; bus callbacks must also return.
 * Any error after mutation requires owner power unwind, not a retry in the
 * same partially initialized generation. This neither unmasks interrupts
 * nor grants software TX admission or publishes a ready interface.
 */
int rtw8723d_trx_init(struct rtw88_io *, int (*)(void *), void *);

/* After trx_init and the chip MAC table, before PHY configuration: program
 * the 32-byte PHY-status report, appended FCS and SDIO interface control.
 * APP_FCS is a native net80211 adaptation of the donor's default format.
 * Same ownership,
 * masked-interrupt and error-unwind contract; TX must remain paused. */
int rtw8723d_trx_post_mac(struct rtw88_io *, int (*)(void *), void *);

#endif /* _DEV_IC_RTW8723D_TRX_H_ */
