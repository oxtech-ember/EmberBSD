/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_SDMMC_RTW88_SDIO_RX_PUBLISH_H_
#define _DEV_SDMMC_RTW88_SDIO_RX_PUBLISH_H_
#include <dev/sdmmc/rtw88_sdio_rx.h>
struct rtw88_sdio_bus;
struct rtw88_sdio_rx_context {
	struct rtw88_sdio_bus *bus;
	uint8_t *packet, *scratch;
	size_t packet_cap, scratch_cap;
	struct rtw88_sdio_rx_batch *batch;
	uint8_t counter;
};

/* One consumed logical FIFO batch, no transfer padding. Sole hardware actor
 * owns core, immutable bytes and separate preallocated batch storage; none
 * overlaps core. Snapshot id on entry; check takes the external interlock
 * and validates all three identity fields, including between records.
 * Entire batch validates before publication. Reserve host sequences for the
 * consumed batch with an overflow check before any record; discarded tails
 * never reuse identity. Final data admission/copy checks the captured identity
 * again inside the netif lock. Callback bytes/metadata expire on return.
 * This performs no FIFO I/O, IRQ acknowledgement or TX-report retirement.
 * Success means classifier calls, not accepted frames or firmware completion.
 * Fatal format/overflow errors require owner IRQ recovery, never blind replay.
 */
int rtw88_sdio_rx_publish(struct rtw88_softc *, const uint8_t *, size_t,
    struct rtw88_sdio_rx_batch *, const struct rtw88_async_epoch *,
    int (*)(void *, const struct rtw88_async_epoch *), void *);

/* Operational IRQ owner supplies its already read logical RX_REQ_LEN and
 * captured identity. Same sole-actor contract. All buffers/context/core/bus
 * metadata are disjoint and pinned; provision them before enabling IRQs.
 * One FIFO read (including its counter/error drain policy), complete batch
 * validation, then epoch-bound core/netif publication. No IRQ ack/unmask or
 * completion inference. Caller handles an error before processing another
 * hardware cause; never replay an uncertain FIFO transaction. */
int rtw88_sdio_rx_receive(struct rtw88_sdio_rx_context *, struct rtw88_softc *,
    size_t, const struct rtw88_async_epoch *,
    int (*)(void *, const struct rtw88_async_epoch *), void *);

/* Discard one old FIFO batch while the sole actor establishes an RX fence.
 * The caller has disabled hardware RX, not merely masked its interrupts,
 * and keeps RX closed until its FIFO-empty check and epoch commit complete.
 * Validate the batch and retire its host sequence range without data/event
 * publication. No current peer/key epoch is assigned to these old bytes.
 * All context/buffer/core/output storage is disjoint and pinned as above.
 * frontier is unchanged on error. Consumed malformed bytes or an overflow
 * latch FIFO failure; no retry or claimed fence after uncertain framing.
 * This is one consumed batch, not an atomic hardware frontier operation:
 * the IRQ owner must read actual RX length, drain to empty within its budget,
 * recheck generation and commit/reopen RX in the correct order. */
int rtw88_sdio_rx_drain(struct rtw88_sdio_rx_context *, struct rtw88_softc *,
    size_t, uint32_t, uint64_t *);
#endif
