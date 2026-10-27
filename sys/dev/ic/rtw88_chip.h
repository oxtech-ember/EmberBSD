/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _DEV_IC_RTW88_CHIP_H_
#define _DEV_IC_RTW88_CHIP_H_

#include <sys/types.h>
#ifndef _KERNEL
#include <stdint.h>
#endif

struct rtw88_softc;
struct rtw88_calibration_ops;
struct rtw88_channel_tuple;
struct rtw88_tx_info;
struct rtw88_rx_info;
struct rtw88_edca_params;
struct rtw88_ra_config;

#define RTW88_BAND_2G 1U
#define RTW88_BAND_5G 2U
#define RTW88_WIDTH_20 1U
#define RTW88_WIDTH_40 2U
#define RTW88_WIDTH_80 4U
#define RTW88_CAP_WPA2 (1U << 0)
#define RTW88_CAP_WME (1U << 1)
#define RTW88_CAP_RXBA (1U << 2)
#define RTW88_CAP_AMSDU (1U << 3)
#define RTW88_CAP_TX_AMPDU (1U << 4)
#define RTW88_CAP_VHT (1U << 5)

struct rtw88_caps {
	uint8_t bands, widths, nss, tx_desc_size, rx_desc_size;
	uint16_t ht_mcs;
	uint32_t features;
};

struct rtw88_chip_ops {
	struct rtw88_caps caps;
	const struct rtw88_calibration_ops *calibration;
	/* Optional chip-owned allocation and exclusive diagnostic lifecycle. */
	int (*attach_check)(struct rtw88_softc *);
	int (*detach_check)(struct rtw88_softc *);
	int (*detach)(struct rtw88_softc *);
	int (*identify)(struct rtw88_softc *);
	int (*start)(struct rtw88_softc *, const struct rtw88_channel_tuple *);
	int (*stop)(struct rtw88_softc *);
	int (*fw_init)(struct rtw88_softc *);
	int (*channel)(struct rtw88_softc *, const struct rtw88_channel_tuple *);
	int (*station)(struct rtw88_softc *, const uint8_t *, unsigned,
	    unsigned, unsigned, int);
	int (*edca)(struct rtw88_softc *, const struct rtw88_edca_params *, unsigned);
	int (*ra)(struct rtw88_softc *, const struct rtw88_ra_config *);
	int (*mailbox_begin)(struct rtw88_softc *, unsigned, int *);
	int (*mailbox_end)(struct rtw88_softc *, int);
	int (*command)(struct rtw88_softc *, uint8_t, const void *, size_t);
	int (*diag)(struct rtw88_softc *, uint32_t *);
	int (*loopback)(struct rtw88_softc *, int);
	/* Optional pure diagnostics and wide-channel descriptor placement. */
	int (*tuple_check)(const struct rtw88_softc *, const struct rtw88_channel_tuple *);
	int (*tx_primary_sc)(struct rtw88_softc *, const struct rtw88_channel_tuple *, uint8_t *);
	int (*tx_desc)(struct rtw88_softc *, uint8_t *, size_t,
	    const struct rtw88_tx_info *);
	int (*rx_desc)(struct rtw88_softc *, const uint8_t *, size_t,
	    struct rtw88_rx_info *);
};

extern const struct rtw88_chip_ops rtw8822c_chip_ops;
int rtw88_chip_valid(const struct rtw88_softc *);
int rtw88_get_caps(const struct rtw88_softc *, struct rtw88_caps *);
int rtw88_chip_tx_desc(struct rtw88_softc *, uint8_t *, size_t,
    const struct rtw88_tx_info *);
int rtw88_chip_rx_desc(struct rtw88_softc *, const uint8_t *, size_t,
    struct rtw88_rx_info *);

#endif
