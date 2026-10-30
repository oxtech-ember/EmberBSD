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
 * separate. Tuple geometry/programming supports 20/40/80 MHz; the core
 * owns channel admission and 40 MHz pair alignment. EINVAL means invalid
 * geometry or a channel outside the reference 2.4/5 GHz bands.
 * Sequences match ember/tools/rtw8822c-phy.trace.
 */
int rtw8822c_phy_pre(struct rtw88_softc *);
int rtw8822c_phy_post(struct rtw88_softc *);
struct rtw8822c_channel_geometry {
	unsigned band;
	unsigned channel_index;
	unsigned cch20, cch40, cch80;
	unsigned data_sc;
};
/* Pure channel geometry validation; performs no register access. */
int rtw8822c_channel_geometry(const struct rtw88_channel_tuple *,
	struct rtw8822c_channel_geometry *);
/* Optional current-generation CCK gain readback; failure is recorded, not defaulted. */
int rtw8822c_signal_cache(struct rtw88_softc *);
/* Caller serializes against stop. This helper withdraws combined channel
 * qualification before I/O and never publishes it. Cancellation is checked
 * between BB, MAC, RF and IGI phases; the first bus error is retained. */
int rtw8822c_set_channel_tuple(struct rtw88_softc *,
	const struct rtw88_channel_tuple *);
/* Narrow 20 MHz compatibility wrapper. */
int rtw8822c_set_channel(struct rtw88_softc *, unsigned);

/*
 * Tuple-aware two-path TX power consumer. Accept explicit ETSI (2) or WW (12)
 * table domains only. The caller must authorize the channel and serialize
 * access while TX is stopped. It withdraws combined channel qualification
 * before register I/O and never publishes it. No SAR, thermal correction or channel policy
 * is supplied here. For 80 MHz, intersect limits across both 40 MHz groups.
 * Reject erased base calibration and signed arithmetic
 * outside 0..127 before any register access; never reproduce byte wrap.
 * Missing required limits return ENOTSUP. Unused 2G VHT rates inherit
 * absolute HT1/HT2 limits; this intentionally differs from the reference.
 * Propagate the first bus error or cancellation; a partial write is not
 * rolled back. The caller must keep TX stopped after any failure.
 */
int rtw8822c_set_tx_power_tuple(struct rtw88_softc *sc,
	const struct rtw8822c_efuse *efuse,
	const struct rtw88_channel_tuple *, unsigned regd);
/* Every validation of rtw8822c_set_tx_power_tuple() with no I/O or state
 * change: zero means the programmer would reach its first write. */
int rtw8822c_tx_power_check(const struct rtw88_softc *sc,
	const struct rtw8822c_efuse *efuse,
	const struct rtw88_channel_tuple *, unsigned regd);
/* Narrow 20 MHz compatibility wrapper. */
int rtw8822c_set_tx_power(struct rtw88_softc *sc,
	const struct rtw8822c_efuse *efuse, unsigned channel, unsigned regd);

enum rtw8822c_dack_stage {
	RTW8822C_DACK_SNAPSHOT = 1,
	RTW8822C_DACK_BB_SETUP,
	RTW8822C_DACK_ADC_SETUP,
	RTW8822C_DACK_ADC_PRE,
	RTW8822C_DACK_ADC_PROGRAM,
	RTW8822C_DACK_ADC_POST,
	RTW8822C_DACK_DAC_SETUP,
	RTW8822C_DACK_DAC_PRE,
	RTW8822C_DACK_DAC_PROGRAM,
	RTW8822C_DACK_DAC_POST,
	RTW8822C_DACK_RESTORE,
	RTW8822C_DACK_DONE
};

#define RTW8822C_DACK_ADC_TRACE_SAMPLES 100
struct rtw8822c_dack_adc_trace {
	/* Initial accepted raw 0x2dbc words, before sorting or replacement. */
	uint32_t samples[RTW8822C_DACK_ADC_TRACE_SAMPLES];
	/* reads counts sample requests, including failed/canceled requests. */
	uint16_t count, reads, filter_passes, spread[2];
	int16_t mean[2];
	int error;
	uint8_t valid;	/* This measurement completed, not RF qualification. */
};

struct rtw8822c_dack_result {
	uint32_t operations;
	uint32_t delay_us;
	uint16_t adc[2][2];		/* Encoded 10-bit compensation. */
	uint8_t dac[2][2];		/* Programmed 8-bit vectors. */
	int16_t adc_residual[2][2];
	int16_t dac_residual[2][2];
	int16_t adc_before[2][2];
	/*
	 * Last measurement only. A mean is usable only when mean_valid is set.
	 * Stage/path freeze at the first error; path 2 is a shared operation.
	 * Counts can be partial on failure. sample_reads counts requests,
	 * including the request that fails or is canceled before bus access.
	 */
	uint16_t sample_count, sample_reads, filter_passes;
	uint16_t spread[2];
	int16_t mean[2];
	uint8_t stage, path, mean_valid;
	uint8_t sample_stage, sample_path;
	uint8_t adc_attempts[2], dac_attempts[2];
	/* Native read-only snapshot on the tenth ADC post measurement. */
	uint32_t adc_snapshot[5];
	int adc_snapshot_error;
	uint8_t adc_snapshot_valid, adc_snapshot_path;
	/* Last path-B ADC attempt only: pre/post; no extra device reads. */
	struct rtw8822c_dack_adc_trace adc_trace[2];
	/* Per-path observations and residual quality, not radio qualification. */
	uint8_t adc_measured, dac_measured;
	uint8_t adc_converged, dac_converged;
	uint8_t adc_nonconverged, dac_nonconverged;
	uint8_t completed_paths, restored; /* Temporary BB/RF controls only. */
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
 * Return zero only after measured/completed paths AB and common restore.
 * Residual nonconvergence after ten valid attempts is advisory, as in the
 * reference. The measured/converged masks report observation and quality
 * separately; nonconverged masks remain set after later paths complete.
 * Any hard failure leaves restored zero, including a failure during restore.
 */
int rtw8822c_dack(struct rtw88_softc *, struct rtw8822c_dack_result *);

/* Wi-Fi-only antenna ownership; no dynamic Bluetooth policy. */
int rtw8822c_coex_power_on(struct rtw88_softc *);
int rtw8822c_coex_init(struct rtw88_softc *);
/* Serialized PCI3081 setup around the chip power table. */
int rtw8822c_power_pre(struct rtw88_softc *);
int rtw8822c_power_post(struct rtw88_softc *);

#endif /* _DEV_IC_RTW8822C_H_ */
