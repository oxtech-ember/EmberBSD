/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88. */

#ifndef _DEV_IC_RTW8822C_H_
#define _DEV_IC_RTW8822C_H_

#include "rtw88_hw.h"

#define RTW8822C_EFUSE_PHYSICAL_SIZE	512
#define RTW8822C_EFUSE_PROTECTED_SIZE	124
#define RTW8822C_EFUSE_LOGICAL_SIZE	768

/* Raw calibration bytes retain erased (0xff) values; no defaults applied. */
struct rtw8822c_efuse {
	uint8_t logical[RTW8822C_EFUSE_LOGICAL_SIZE];
	uint8_t mac[6];
	uint8_t txpower[4][42];
	uint8_t channel_plan;
	uint8_t xtal;
	uint8_t board_option;
	uint8_t bt_setting;
	uint8_t rfe_option;
	uint8_t country[2];
	uint8_t thermal[2];
	uint8_t power_track_type;
};

/* PCI IDs are observed configuration-space values, not EFUSE contents. */
struct rtw8822c_identity {
	uint16_t vendor;
	uint16_t product;
	uint32_t sys_cfg1;	/* Snapshot of register 0x00f0. */
	uint8_t rfe_option;
};

/*
 * Decode a physical WIFI-bank EFUSE prefix, at most 512 bytes. Only the
 * first 388 bytes contain logical records; the protected tail is not
 * record data. A 0xff header (also an extended second header) ends records.
 * Erased logical bytes remain 0xff. Missing words/header bytes return
 * EINVAL; writes outside the 768-byte map return EOVERFLOW. Invalid MACs
 * return EINVAL. Leave *out unchanged on failure. No hardware operations.
 * Raw calibration is not a validated transmit-power or regulatory policy.
 */
int rtw8822c_efuse_decode(const uint8_t *, size_t, struct rtw8822c_efuse *);

/*
 * Accept only PCI 10ec:c822 production silicon, cut encodings A-G, and
 * evidenced RFE definitions 0-6. ENOTSUP means outside this source profile;
 * EINVAL means NULL input. This does not establish physical support.
 */
int rtw8822c_validate_identity(const struct rtw8822c_identity *);

struct rtw88_softc;
struct rtw8822c_table;

int rtw8822c_identify(struct rtw88_softc *);
int rtw8822c_power(struct rtw88_softc *, int);
int rtw8822c_load_tables(struct rtw88_softc *);
int rtw8822c_apply_table(struct rtw88_softc *, const struct rtw8822c_table *);

/*
 * Post-firmware MAC setup: TX/RX queue mapping, FIFO pages, H2C queue,
 * WMAC defaults, RX driver info and the PCIe interface quirk. The write
 * sequence matches the pinned reference (ember/tools/rtw8822c-mac.trace).
 */
int rtw8822c_mac_init(struct rtw88_softc *);

/*
 * rtw8822c_phy_set_param around rtw8822c_load_tables: BB/RF power-on
 * before, crystal, TRX paths AB, EDCCA and beamforming defaults after.
 * Postinit includes cold ADC/DAC calibration and RF trims; TX power is
 * separate. Channel switch is 20 MHz only;
 * EINVAL for a channel outside the reference 2.4/5 GHz bands.
 * Sequences match ember/tools/rtw8822c-phy.trace.
 */
int rtw8822c_phy_pre(struct rtw88_softc *);
int rtw8822c_phy_post(struct rtw88_softc *);
int rtw8822c_set_channel(struct rtw88_softc *, unsigned);

/*
 * 20 MHz, two-path TX power consumer. Accept explicit ETSI (2) or WW (12)
 * table domains only. The caller must authorize the channel and serialize
 * access while TX is stopped. No SAR, thermal correction or channel policy
 * is supplied here. Reject erased base calibration and signed arithmetic
 * outside 0..127 before any register access; never reproduce byte wrap.
 * Missing required limits return ENOTSUP. Unused 2G VHT rates inherit
 * absolute HT1/HT2 limits; this intentionally differs from the reference.
 * Propagate the first bus error or cancellation; a partial write is not
 * rolled back. The caller must keep TX stopped after any failure.
 */
int rtw8822c_set_tx_power(struct rtw88_softc *sc,
    const struct rtw8822c_efuse *efuse, unsigned channel, unsigned regd);

struct rtw8822c_dack_result {
	uint32_t operations;
	uint32_t delay_us;
	uint16_t adc[2][2];		/* Encoded 10-bit compensation. */
	uint8_t dac[2][2];		/* Programmed 8-bit vectors. */
	int16_t adc_residual[2][2];
	int16_t dac_residual[2][2];
	uint8_t valid_paths;		/* 3 only after complete success. */
	uint8_t reset_required;
};

/*
 * Cold ADCK/DACK for paths AB. Require resources_ready, powered and
 * bootstrapping; running/started must both be zero. The caller serializes
 * access before HCI/interrupt start. No warm cache. Check all measurements
 * and restore temporary controls on success. At most 1000000 bus operations
 * and 2000000 us of explicit delays; these are software bounds, not RF timing
 * guarantees.
 * The first error stops all access. Any attempted write followed by failure
 * sets reset_required; the caller MUST perform a full power reset before
 * retry or scan/TX. Register snapshots cannot roll back analog/MSBK state.
 * Leave valid_paths zero on any error, including failure during restore.
 */
int rtw8822c_dack(struct rtw88_softc *, struct rtw8822c_dack_result *);

/* Wi-Fi-only antenna ownership; no dynamic Bluetooth policy. */
int rtw8822c_coex_power_on(struct rtw88_softc *);
int rtw8822c_coex_init(struct rtw88_softc *);
/* Serialized PCI3081 setup around the chip power table. */
int rtw8822c_power_pre(struct rtw88_softc *);
int rtw8822c_power_post(struct rtw88_softc *);

#endif /* _DEV_IC_RTW8822C_H_ */
