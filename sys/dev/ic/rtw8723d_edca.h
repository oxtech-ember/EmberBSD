/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD; AI-assisted RTL8723D EDCA interface. */
#ifndef _DEV_IC_RTW8723D_EDCA_H_
#define _DEV_IC_RTW8723D_EDCA_H_
struct rtw88_io;
struct rtw88_edca_params;
/* Serialized powered 2G chip, TX drained and software admission closed.
 * Four ACs in BE/BK/VI/VO order, slot9/20us, SIFS10us. Prevalidate all four
 * before I/O; snapshot encoded words before callbacks. The owner must hold
 * a lifecycle/stop/generation lease for the entire synchronous call. The
 * optional check is cooperative cancellation, not an atomic guard between
 * the final check and restoring TX pause. Restore hardware TX pause only
 * after complete programming and current-owner checks. Any error after
 * mutation attempts an independent pause and requires owner unwind; no retry
 * of uncertain configuration or publication of TX readiness. */
int rtw8723d_edca_apply(struct rtw88_io *, const struct rtw88_edca_params *,
    unsigned, int (*)(void *), void *);
#endif /* _DEV_IC_RTW8723D_EDCA_H_ */
