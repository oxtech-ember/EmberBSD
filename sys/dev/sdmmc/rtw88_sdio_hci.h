/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_SDMMC_RTW88_SDIO_HCI_H_
#define _DEV_SDMMC_RTW88_SDIO_HCI_H_
#include <dev/sdmmc/rtw88_sdio_irq.h>
#include <dev/sdmmc/rtw88_sdio_tx_service.h>

/* Native frontend resource hooks, not another hardware actor. All hardware
 * hooks run on the core executor, outside the child admission interlock.
 * acquire owns F1/FBR with the existing checked parent APIs; irq_start owns
 * the parent interrupt cookie/INTEN while chip HIMR is masked. release runs
 * only after confirmed chip-off and retains all unresolved ownership on an
 * error. It must not perform a second chip power sequence. status reports
 * the parent's persistent host-error latch; it never clears/retries it.
 * RX identity callbacks briefly take the existing netif interlock inside a
 * successful net_io_begin/end window. again schedules one owner reason,
 * with a bounded delayed retry for zero RX length or a transient host-epoch
 * fence. It owns at most one timer, enforces a finite retry budget/deadline,
 * and faults/schedules STOP on exhaustion. retry_ready never allows an early
 * retry; retry_reset cancels/reset this budget after a completed good pass or
 * new firmware setup. release drains timer/callback lifetime before returning
 * ownership. fault closes net admission
 * and schedules that same actor's STOP; it must not free resources inline. */
struct rtw88_sdio_hci_parent {
	int (*acquire)(void *);
	int (*irq_start)(void *);
	int (*release)(void *);
	int (*status)(void *);
	bool (*revoked)(void *);
	int (*rx_epoch)(void *, struct rtw88_async_epoch *);
	int (*rx_current)(void *, const struct rtw88_async_epoch *);
	bool (*retry_ready)(void *);
	void (*retry_reset)(void *);
	void (*again)(void *, bool);
	void (*fault)(void *, int);
	void *cookie;
};

struct rtw88_sdio_hci {
	struct rtw88_softc *core;
	struct rtw88_sdio_bus *bus;
	struct rtw88_sdio_tx_context *tx;
	struct rtw88_sdio_rx_context *rx;
	struct rtw88_sdio_hci_parent parent;
	uint32_t setup_generation;
	bool acquired, tx_active, quiesced, hold_irq, delayed_rx;
	int error;
};

struct rtw88_hci_ops;
extern const struct rtw88_hci_ops rtw88_sdio_hci_ops;
/* Fresh zeroed and disjoint storage, existing scalar bus ops/deferred IRQ
 * callback already installed. Buffers are allocated/pinned by the frontend.
 * This only binds callbacks; it does not acquire hardware or attach a netif. */
int rtw88_sdio_hci_bind(struct rtw88_sdio_hci *, struct rtw88_softc *,
    struct rtw88_sdio_bus *, struct rtw88_sdio_tx_context *,
    struct rtw88_sdio_rx_context *, const struct rtw88_sdio_hci_parent *);
/* Sole owner within the netif I/O window. Errno-returning frontend entry,
 * unlike the legacy .intr callback's handled/not-handled return value. */
int rtw88_sdio_hci_poll(struct rtw88_softc *);
/* After masked poll, without child lock, pass the actual net_io_end result.
 * The sole actor cannot grant a new Giant window during this call; a new
 * prep request waits for its next mask-before-io_begin pass. Pending status
 * schedules that same owner. No frame is classified outside the I/O window. */
int rtw88_sdio_hci_reopen(struct rtw88_softc *, bool);
#endif
