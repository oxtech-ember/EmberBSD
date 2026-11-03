/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW8723D_IQK_GUARD_H_
#define _DEV_IC_RTW8723D_IQK_GUARD_H_
#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stdbool.h>
#include <stdint.h>
#endif

struct rtw88_io;
struct rtw8723d_boot_ops;
struct rtw8723d_iqk_guard {
	uint32_t lte_path, lte_grant, bb_select;
	uint8_t btg_select;
	bool held;
};

/* Zero-initialized owner storage, serialized powered chip, software TX shut.
 * Save the shared path/grant selection before IQK changes it. Required wait
 * and cancel callbacks implement the donor's 1ms indirect-read settling time.
 * Acquisition failure attempts bounded release after any modifying write;
 * held remains true if restoration is uncertain. Never drop bus resources
 * or reuse the guard while held. Caller must unwind chip power on any error.
 * No GPIO, board power, RF calibration or TX readiness is provided here. */
int rtw8723d_iqk_guard_acquire(struct rtw88_io *,
    const struct rtw8723d_boot_ops *, struct rtw8723d_iqk_guard *);

/* Retryable restoration ignores cancellation and preserves the first errno.
 * Grant commit is skipped if its prerequisite data write fails; independent
 * path selections are still restored. Only fully successful cleanup clears
 * held. An already released guard causes no I/O and preserves sticky error. */
int rtw8723d_iqk_guard_release(struct rtw88_io *, struct rtw8723d_iqk_guard *);

#endif /* _DEV_IC_RTW8723D_IQK_GUARD_H_ */
