/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright 2024 Fiona Klute. */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: Linux adc218676eef25575469234709c2d87185ca223a,
 * rtw88/rtw8723x.h and main.h; native decoder is AI-assisted. */
#ifndef _DEV_IC_RTW8723D_H_
#define _DEV_IC_RTW8723D_H_

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stddef.h>
#include <stdint.h>
#endif

#define RTW8723D_EFUSE_PHYSICAL_SIZE 512U
#define RTW8723D_EFUSE_PROTECTED_SIZE 97U
#define RTW8723D_EFUSE_USABLE_SIZE \
    (RTW8723D_EFUSE_PHYSICAL_SIZE - RTW8723D_EFUSE_PROTECTED_SIZE)
#define RTW8723D_EFUSE_LOGICAL_SIZE 512U
#define RTW8723D_EFUSE_SDIO_MAC 0x11aU
#define RTW8723D_TXPOWER_PATHS 4U
#define RTW8723D_TXPOWER_SIZE 42U

/* All calibration bytes retain erased values. Decode does not establish
 * valid calibration, a regulatory domain, or permission to transmit. */
struct rtw88_io;

struct rtw8723d_efuse {
	uint8_t logical[RTW8723D_EFUSE_LOGICAL_SIZE];
	uint8_t mac[6], rfe, xtal, channel_plan;
	uint8_t thermal, iqk_lck, afe, pa_type, lna_2g[2];
	uint8_t board_option, feature_option, bt_setting, country[2];
	/* Each path has 18 bytes for 2 GHz and 24 for 5 GHz in the map.
	 * Retaining that wire layout does not give this chip a 5 GHz band. */
	uint8_t txpower[RTW8723D_TXPOWER_PATHS][RTW8723D_TXPOWER_SIZE];
};

/* Decode a physical Wi-Fi-bank prefix of 1..512 bytes. Ignore bytes in
 * the chip's protected tail at offsets 415..511. Reject partial headers and
 * words, logical overflow and invalid unicast MAC. Leave the
 * complete output unchanged on error. No device I/O occurs here. */
int rtw8723d_efuse_decode(const uint8_t *, size_t, struct rtw8723d_efuse *);

/* Serialized owner reads the physical Wi-Fi bank; output is atomic. */
int rtw8723d_efuse_read(struct rtw88_io *, uint8_t[RTW8723D_EFUSE_PHYSICAL_SIZE]);


#endif /* _DEV_IC_RTW8723D_H_ */
