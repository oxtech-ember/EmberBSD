/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_SDMMC_RTW88_SDIO_RX_H_
#define _DEV_SDMMC_RTW88_SDIO_RX_H_
#include <dev/ic/rtw88_async.h>
#include <dev/ic/rtw88_hw.h>

#define RTW88_SDIO_RX_BATCH_MAX 16384U
#define RTW88_SDIO_RX_RECORDS_MAX 512U

struct rtw88_softc;
struct rtw88_sdio_rx_record {
	uint16_t descriptor_offset, payload_offset, length;
	struct rtw88_rx_info info;
};
struct rtw88_sdio_rx_batch {
	struct rtw88_async_epoch id;
	uint16_t count;
	struct rtw88_sdio_rx_record records[RTW88_SDIO_RX_RECORDS_MAX];
};

/* Caller owns an immutable logical FIFO batch and a separate, preallocated
 * metadata buffer (never put records[512] on the kernel stack). id is the
 * caller's captured full epoch and is preserved. Selected rx_desc must be
 * pure, deterministic, and stable during both passes. No allocation, I/O,
 * frame delivery or event dispatch occurs here. With separate output storage,
 * count is zero on any error; an invalid input/output alias is not modified.
 * Records are published by setting count only after complete validation.
 * Caller must revalidate the full epoch immediately before every subsequent
 * delivery, including between records, and stamp host identity then.
 * Input length excludes block-transfer padding. Final record may omit its
 * trailing alignment bytes; a truncated following descriptor is rejected. */
/* Bounded CRC/ICV-bad DATA records remain in the batch for the core to drop;
 * they do not invalidate good neighbours. Corrupt C2H reports are rejected
 * before any event dispatch. A successful parse is not frame admission. */
int rtw88_sdio_rx_validate(struct rtw88_softc *, const uint8_t *, size_t,
    struct rtw88_sdio_rx_batch *);

#endif /* _DEV_SDMMC_RTW88_SDIO_RX_H_ */
