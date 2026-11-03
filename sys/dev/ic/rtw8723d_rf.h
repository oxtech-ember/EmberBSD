/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: Linux adc218676eef25575469234709c2d87185ca223a rtw88/phy.c;
 * native error handling is AI-assisted. */
#ifndef _DEV_IC_RTW8723D_RF_H_
#define _DEV_IC_RTW8723D_RF_H_
#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stdint.h>
#endif

struct rtw88_io;

/* The caller owns a powered, serialized chip and handles failed I/O unwind.
 * Paths A/B are physical register paths, not a claim of two spatial streams.
 * Addresses are 0..255; masks must be nonzero subsets of the 20-bit RF word.
 * Read output is unchanged on failure. Writes take an unshifted field value;
 * bits outside the mask are discarded. The first I/O error remains sticky. */
int rtw8723d_rf_read(struct rtw88_io *, unsigned, unsigned, uint32_t, uint32_t *);
int rtw8723d_rf_write(struct rtw88_io *, unsigned, unsigned, uint32_t, uint32_t);

#endif /* _DEV_IC_RTW8723D_RF_H_ */
