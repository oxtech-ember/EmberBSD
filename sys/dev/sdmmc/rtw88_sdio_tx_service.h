/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_SDMMC_RTW88_SDIO_TX_SERVICE_H_
#define _DEV_SDMMC_RTW88_SDIO_TX_SERVICE_H_
#include <dev/ic/rtw88_hci.h>
#ifndef _KERNEL
#include <stddef.h>
#endif

struct rtw88_sdio_bus;
struct rtw88_sdio_tx_context {
	struct rtw88_sdio_bus *bus;
	uint8_t *packet;
	size_t packet_cap;
};

/* Validate output aliasing before a lifecycle wrapper publishes NOT_STARTED.
 * Does not validate readiness or issue I/O. submit repeats this preflight. */
int rtw88_sdio_tx_progress_init(struct rtw88_sdio_tx_context *,
    const uint8_t *, size_t, const struct rtw88_tx_info *,
    enum rtw88_tx_progress *);

/* Internal transport operation, called only by the serialized current-
 * generation hardware owner after chip/channel/key admission. Context and
 * both staging buffers belong to that owner and are disjoint from context
 * metadata. packet must not overlap the bus FIFO scratch, input frame,
 * tx_info or progress. Input frame, tx_info and progress must also be
 * disjoint from bus FIFO scratch; progress is separate from borrowed inputs.
 * No allocation here.
 * Resources are provisioned by prepare; unavailable storage returns ENOMEM
 * before any card access. q is a software ring; DATA uses tx_info.queue TID,
 * MGMT forces the RTL8723D SDIO high QSEL 17. frame is already encrypted.
 * EBUSY is capacity before write; retry only under the core's bounded policy
 * with the same immutable bytes/PN. Any attempted FIFO error is uncertain,
 * not requeueable. ACCEPTED is FIFO submission, never an air acknowledgement.
 * This is not the net80211/HCI lifecycle binding or a radio readiness check. */
int rtw88_sdio_tx_submit(struct rtw88_sdio_tx_context *, unsigned,
    const uint8_t *, size_t, const struct rtw88_tx_info *,
    enum rtw88_tx_progress *);

#endif /* _DEV_SDMMC_RTW88_SDIO_TX_SERVICE_H_ */
