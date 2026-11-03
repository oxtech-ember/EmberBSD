/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW8723D_IQK_H_
#define _DEV_IC_RTW8723D_IQK_H_
#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stdbool.h>
#include <stdint.h>
#endif

struct rtw88_io;
struct rtw8723d_boot_ops;
struct rtw8723d_iqk_result {
	uint16_t x, y; /* Raw ten-bit result fields, not signed matrix products. */
};
struct rtw8723d_iqk_matrix {
	/* S1 and S0 are antenna-switch paths, not spatial streams. */
	struct rtw8723d_iqk_result tx[2], rx[2];
};

/* Caller owns the configured IQK path/grant and has launched the current
 * one-shot operation with valid generation/trigger ordering. Poll DONE at
 * most1000 times with10us gaps (pinned donor check_hw_ready budget).
 * ETIMEDOUT/cancel/I/O faults stick in io and require owner unwind; never
 * interpret default report registers as success after a failed wait.
 * EAGAIN means a completed but rejected measurement, leaves io usable for
 * the owner's bounded calibration retry after path cleanup. No automatic
 * retry, trigger, matrix programming or calibration-ready publication.
 * Success provides just one TX or RX measurement; output unchanged on error.
 * cancelled is required; wait_ms is not used (scalar delay_us is required). */
int rtw8723d_iqk_result_read(struct rtw88_io *,
    const struct rtw8723d_boot_ops *, bool, struct rtw8723d_iqk_result *);

/* Caller supplies the agreed complete measurements, owns paused TX and the
 * IQK grant, and retains the chip generation throughout. Validate both paths
 * and both existing swing coefficients before any matrix write. Snapshot
 * input before callbacks; first error/cancel stops subsequent accesses.
 * Partial programming requires power unwind and never marks calibration
 * ready. No round selection, fallback coefficients or TX admission here. */
int rtw8723d_iqk_matrix_apply(struct rtw88_io *,
    const struct rtw8723d_iqk_matrix *, int (*)(void *), void *);

/* One bounded TX measurement, or the two-stage RX measurement, on antenna
 * switch path 0=S1 / 1=S0. Caller owns powered serialized chip, paused TX,
 * and completed ADDA/MAC/BB IQK preconfiguration/backup. This function owns
 * and restores the temporary LTE/path guard and exits IQK mode on return.
 * EAGAIN means rejected completed measurement with successful cleanup;
 * timeout/cancel/bus/cleanup failure requires owner power unwind. Failed
 * acquisition can also leave uncertain state: never retry a hard error on
 * the live chip. Output unchanged on error. No round retry/selection, full
 * calibration-ready publication, board power, or TX admission is supplied. */
int rtw8723d_iqk_path_measure(struct rtw88_io *,
    const struct rtw8723d_boot_ops *, unsigned, bool,
    struct rtw8723d_iqk_result *);

#endif /* _DEV_IC_RTW8723D_IQK_H_ */
