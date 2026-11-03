/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_SDMMC_RTW88_SDIO_TX_H_
#define _DEV_SDMMC_RTW88_SDIO_TX_H_
#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stddef.h>
#include <stdint.h>
#endif

#define RTW8723D_SDIO_TX_DESC_SIZE 40U
#define RTW8723D_SDIO_TX_MAX_TRANSFER 4096U

enum rtw88_sdio_credit_class {
	RTW88_SDIO_CREDIT_HIGH,
	RTW88_SDIO_CREDIT_NORMAL,
	RTW88_SDIO_CREDIT_LOW
};

struct rtw88_sdio_tx_layout {
	size_t logical, transfer;
	uint32_t port;
	unsigned credit_class, pages;
};

/* q uses the common rtw88_tx_ring_index, not descriptor QSEL or a TID.
 * One immutable frame plus the 40-byte descriptor fits one <=4096-byte
 * padded FIFO command. Empty and oversized frames fail before publication.
 * Output remains unchanged on every error. No card access or allocation. */
int rtw88_sdio_tx_layout(unsigned, size_t, struct rtw88_sdio_tx_layout *);

/* FREE_TXPG bytes are high/normal/low/public; MGMT uses high+public although
 * its FIFO port is EXTRA. Account for the full padded transfer in 128-byte
 * pages. EBUSY means insufficient capacity before any FIFO write; it is not
 * a submission or a completion. The serialized owner must recheck credits
 * immediately before writing. Output remains unchanged on error. */
int rtw88_sdio_tx_credit(uint32_t, unsigned, size_t, unsigned *);

#endif /* _DEV_SDMMC_RTW88_SDIO_TX_H_ */
