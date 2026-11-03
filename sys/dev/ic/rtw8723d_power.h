/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW8723D_POWER_H_
#define _DEV_IC_RTW8723D_POWER_H_
#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stdbool.h>
#include <stdint.h>
#endif
struct rtw88_io;
struct rtw88_profile;
struct rtw8723d_efuse;
#define RTW8723D_POWER_RATES 20U

struct rtw8723d_power_policy {
	uint8_t rf_paths; /* Runtime TX path count, NOT fixed RF PHY port count. */
	bool tracking_active;
	int8_t remnant_cck, remnant_ofdm;
	bool has_sar;
	/* Optional normalized offset caps [-128,63], CCK/OFDM/HT1S.
	 * Normalization can go below -64 after subtracting the PG reference. */
	int8_t sar_offset[3];
};

/* Initial 2G HT20/1SS RFE0 table, tracking disabled and remnants zero.
 * Explicit WORLD_MIN profile and channel bit(channel-1) required. Reject
 * erased required calibration and unsupported path/profile before I/O.
 * ERANGE if a required index is below zero (cannot meet the limit).
 * Values are power indices, not dBm or physical RF measurements. Pure
 * computation publishes all20 entries together; output unchanged on error. */
int rtw8723d_power_compute(const struct rtw8723d_efuse *,
    const struct rtw88_profile *, const struct rtw8723d_power_policy *,
    unsigned, uint8_t[RTW8723D_POWER_RATES]);

/* Owner retains powered serialized chip and keeps TX admission closed.
 * Precompute full table before first write, snapshot inputs before callbacks.
 * First I/O/cancel error stops programming; partial programming requires
 * power unwind. No readiness, channel, calibration or TX gate publication. */
int rtw8723d_power_program(struct rtw88_io *, const struct rtw8723d_efuse *,
    const struct rtw88_profile *, const struct rtw8723d_power_policy *,
    unsigned, int (*)(void *), void *);
#endif /* _DEV_IC_RTW8723D_POWER_H_ */
