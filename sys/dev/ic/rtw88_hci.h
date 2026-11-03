/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW88_HCI_H_
#define _DEV_IC_RTW88_HCI_H_

#include <sys/types.h>
#ifndef _KERNEL
#include <stdbool.h>
#include <stdint.h>
#endif

struct rtw88_softc;
struct rtw88_tx_info;
struct rtw88_fw_section;
struct rtw88_async_epoch;

/* An error after publication does not return ownership to the caller. */
enum rtw88_tx_progress {
	RTW88_TX_NOT_STARTED,
	RTW88_TX_MAY_HAVE_WRITTEN,
	RTW88_TX_ACCEPTED
};

struct rtw88_hci_ops {
	int (*attach)(struct rtw88_softc *);
	int (*detach)(struct rtw88_softc *);
	/* Acquire runtime transport resources before chip power-on. */
	int (*prepare)(struct rtw88_softc *);
	int (*setup)(struct rtw88_softc *);
	int (*start)(struct rtw88_softc *);
	/* Retryable. Failure retains resources and prevents chip power-off. */
	int (*quiesce)(struct rtw88_softc *);
	/* Retryable. Failure retains resources and blocks detach and restart. */
	int (*stop)(struct rtw88_softc *);
	int (*intr)(struct rtw88_softc *);
	unsigned (*space)(struct rtw88_softc *, unsigned);
	int (*drain)(struct rtw88_softc *, unsigned *);
	int (*frontier)(struct rtw88_softc *, uint32_t, uint64_t *);
	/* EBADMSG reports a completed transfer with a failed chip checksum. */
	int (*fw_section)(struct rtw88_softc *, uint32_t, const uint8_t *,
	    size_t, const struct rtw88_fw_section *);
	int (*xmit)(struct rtw88_softc *, unsigned, const uint8_t *, size_t,
	    const struct rtw88_tx_info *, enum rtw88_tx_progress *);
	/* Optional. The public wrapper returns EOPNOTSUPP when absent. */
	int (*fw_packet)(struct rtw88_softc *, const uint8_t *, size_t);
	bool irq_can_run_inline;
	/* Only transports without host DMA opt out of the DMA readiness gate. */
	bool no_host_dma;
	/* Optional, serialized firmware window and SDIO data-mode transition. */
	int (*write_window)(struct rtw88_softc *, uint32_t, const uint8_t *, size_t);
	int (*set_data_mode)(struct rtw88_softc *, bool);
};

extern const struct rtw88_hci_ops rtw88_pci_hci_ops;
int rtw88_hci_valid(const struct rtw88_softc *);
int rtw88_pci_fw_packet(struct rtw88_softc *, const uint8_t *, size_t);
/* The consumer must copy borrowed bytes before this call returns. */
void rtw88_rx_deliver(struct rtw88_softc *, const uint8_t *, size_t, uint64_t);
void rtw88_rx_deliver_host(struct rtw88_softc *, const uint8_t *, size_t,
    uint64_t, const struct rtw88_async_epoch *);
int rtw88_rx_epoch_check(struct rtw88_softc *, const struct rtw88_async_epoch *);
int rtw88_rx_deliver_epoch(struct rtw88_softc *, const uint8_t *, size_t,
    uint64_t, const struct rtw88_async_epoch *);

#endif
