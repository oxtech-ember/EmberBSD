/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_SDMMC_RTW88_SDIO_IRQ_H_
#define _DEV_SDMMC_RTW88_SDIO_IRQ_H_
#include <dev/sdmmc/rtw88_sdio_rx_publish.h>

/* Minimal always-awake RTL8723D RX path. Power-save/CPWM, register C2H,
 * TX error recovery and other causes are not enabled or acknowledged. */
int rtw88_sdio_irq_mask(struct rtw88_sdio_bus *);

/* Sole actor, current committed RX identity, powered resources and pinned,
 * disjoint RX buffers. Caller establishes its old-FIFO/epoch fence before
 * enabling runtime RX; this function does not create that hardware fence.
 * Masks HIMR, consumes only RX_REQUEST-ready batches, then rearms RX_REQUEST
 * and checks for a lost wakeup. It never W1C-acknowledges the RX request.
 * At most16 batches/64KiB per pass, each within the chip's16KiB RX FIFO.
 * again=true means IRQ stays masked: enqueue another owner pass. EAGAIN
 * with again=true means a request had zero length: use a bounded delayed
 * retry, not a busy loop. No FIFO read occurred for that zero-length request.
 * Preflight errors perform no I/O. After successful initial masking, other
 * errors keep IRQ closed (or require retained resources if a later masking
 * failed), and the owner must handle the error before another pass. Do not
 * retry an uncertain FIFO transaction. The caller supplies the same full
 * identity check used by rx_receive, without holding its admission lock.
 * Success is transport progress, not netif/radio or power-save readiness. */
int rtw88_sdio_irq_service(struct rtw88_sdio_rx_context *, struct rtw88_softc *,
    const struct rtw88_async_epoch *,
    int (*)(void *, const struct rtw88_async_epoch *), void *, bool *);
/* Same bounded receive processing, but always leaves HIMR masked. Native
 * SDMMC owners use this before granting a Giant preparation window. */
int rtw88_sdio_irq_service_masked(struct rtw88_sdio_rx_context *,
    struct rtw88_softc *, const struct rtw88_async_epoch *,
    int (*)(void *, const struct rtw88_async_epoch *), void *, bool *);
/* Sole transport owner with pinned resources; no child interlock across
 * this SDMMC I/O. Caller has ended its masked netif I/O window and may rearm
 * only if that end did not grant Giant preparation or pending STOP. No
 * classifier/core access is performed here. Unmask then recheck RX_REQUEST;
 * a pending cause closes HIMR again and must enqueue the same owner. Output
 * is unchanged on error; an uncertain close retains resources. This does not
 * acknowledge HISR, admit old FIFO bytes, or grant any hardware RX fence. */
int rtw88_sdio_irq_rearm(struct rtw88_sdio_bus *, bool *);
#endif
