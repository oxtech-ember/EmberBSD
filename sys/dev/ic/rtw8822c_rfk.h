/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Internal actual-worker RFK ownership; no userspace or HT readiness ABI. */
#ifndef _DEV_IC_RTW8822C_RFK_H_
#define _DEV_IC_RTW8822C_RFK_H_

struct rtw88_softc;
struct rtw8822c_rfk;
struct rtw88_rfk_diag;

#include <dev/ic/rtw88_calibration.h>

int rtw8822c_rfk_blocked(void);
int rtw8822c_rfk_active(const struct rtw88_softc *);
int rtw8822c_rfk_begin(struct rtw88_softc *,
    const struct rtw88_calibration_services *);
/* Existing active owner and tuned power receipt only. No allocation or IO.
 * The caller serializes with PCI IRQ; no token/binding/deadline is changed. */
int rtw8822c_rfk_validate(struct rtw88_softc *);
int rtw8822c_rfk_run(struct rtw88_softc *);
int rtw8822c_rfk_retire(struct rtw88_softc *);
/* Current AUTH owner only, immediately after successful retirement. No IO. */
int rtw8822c_rfk_seal(struct rtw88_softc *);
int rtw8822c_rfk_free(struct rtw88_softc *);
/* Same process owner, without PCI. Copy to a PCI-protected mirror before yield. */
void rtw8822c_rfk_progress(struct rtw88_softc *, struct rtw88_rfk_diag *);

#endif /* _DEV_IC_RTW8822C_RFK_H_ */
