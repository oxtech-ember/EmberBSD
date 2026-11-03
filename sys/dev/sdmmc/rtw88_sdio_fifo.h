/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD checked FIFO transport, AI-assisted. */
#ifndef _DEV_SDMMC_RTW88_SDIO_FIFO_H_
#define _DEV_SDMMC_RTW88_SDIO_FIFO_H_

#include <dev/ic/rtw88_hci.h>
#include <dev/sdmmc/rtw88_sdio_io.h>

#define RTW88_SDIO_FIFO_BLOCK 512U
#define RTW88_SDIO_FIFO_MAX_REQUEST 4096U
#define RTW88_SDIO_FIFO_MAX_TX RTW88_SDIO_FIFO_MAX_REQUEST
#define RTW88_SDIO_FIFO_MAX_RX 16384U

/* Caller serializes bus, scratch and generation lifetime. TX borrows the bus
 * read_scratch as a shared staging buffer; it must cover transfer and must not
 * overlap the immutable logical input. transfer is exactly logical rounded to
 * 512 bytes; port includes the TX queue and ceil(logical/4) offset.
 * progress is separate caller storage. ACCEPTED means FIFO submission only,
 * never an over-the-air acknowledgement. No command is retried. */
int rtw88_sdio_fifo_write(struct rtw88_sdio_bus *, uint32_t, const void *,
    size_t, size_t, enum rtw88_tx_progress *);

/* RX uses one port/counter for the entire batch. Scratch covers logical rounded
 * to 4 bytes for a byte-mode read, or 512 bytes above one block. Destination,
 * scratch and counter must be disjoint. Counter advances once after admission,
 * including on error. Output changes only after all reads succeed. After an
 * error all remaining chunks are attempted once to drain the batch, preserving
 * the first errno. Any FIFO error latches fifo_failed and disables CMD53; the
 * owner must resynchronize chip/function and start a new generation before reuse.
 * bus.max_request remains the register-helper limit, not the FIFO request size. */
int rtw88_sdio_fifo_read(struct rtw88_sdio_bus *, uint8_t *, void *, size_t,
    uint8_t *, size_t);

#endif /* _DEV_SDMMC_RTW88_SDIO_FIFO_H_ */
