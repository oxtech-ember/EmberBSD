/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
/* Native RTL8723D SDIO HCI lifecycle, AI-assisted. Parent resource/IRQ
 * ownership remains in the checked SDMMC frontend; no PCI DMA state is set.
 * The admitted frontend requires explicit radio policy for networking. */
#include <dev/ic/rtw88_io.h>
#include <dev/ic/rtw88_executor.h>
#include <dev/ic/rtw8723d_runtime.h>
#include <dev/sdmmc/rtw88_sdio_io.h>
#include <dev/sdmmc/rtw88_sdio_hci.h>

static struct rtw88_sdio_hci *
rtw88_sdio_hci_context(struct rtw88_softc *sc)
{
	struct rtw88_sdio_hci *ctx;

	if (sc == NULL || (ctx = sc->sc_hci_private) == NULL || ctx->core != sc)
		return NULL;
	return ctx;
}

static int
rtw88_sdio_hci_owner(struct rtw88_sdio_hci *ctx, bool cleanup)
{
	int error;

	if (ctx == NULL)
		return EINVAL;
	if (!rtw88_executor_owned(ctx->core))
		return EPERM;
	if ((error = ctx->parent.status(ctx->parent.cookie)) != 0)
		return error;
	if (!cleanup && (ctx->core->sc_stop_requested ||
	    ctx->parent.revoked(ctx->parent.cookie)))
		return ECANCELED;
	return 0;
}

static bool
rtw88_sdio_hci_offline(struct rtw88_sdio_hci *ctx)
{
	struct rtw88_softc *sc = ctx->core;

	/* Only a quiescent caller after actor drain may sample this in process
	 * context. An attempted acquire owns cleanup even before resources_ready. */
	return !ctx->bus->fifo_failed && !ctx->acquired && !ctx->tx_active &&
	    !sc->sc_resources_ready &&
	    !sc->sc_started && !sc->sc_running && !sc->sc_powered &&
	    !sc->sc_power_dirty && !ctx->bus->function_claimed &&
	    !ctx->bus->function_ready;
}

static int
rtw88_sdio_hci_buffers(struct rtw88_sdio_hci *ctx)
{

	if (ctx == NULL)
		return EINVAL;
	if (ctx->tx == NULL || ctx->rx == NULL || ctx->tx->bus != ctx->bus ||
	    ctx->rx->bus != ctx->bus || ctx->tx->packet == NULL ||
	    ctx->tx->packet_cap < RTW88_SDIO_MAX_TRANSFER ||
	    ctx->rx->packet == NULL || ctx->rx->scratch == NULL ||
	    ctx->rx->batch == NULL ||
	    ctx->rx->packet_cap < RTW88_SDIO_RX_BATCH_MAX ||
	    ctx->rx->scratch_cap < RTW88_SDIO_RX_BATCH_MAX)
		return ENOMEM;
	return 0;
}

static int
rtw88_sdio_hci_attach(struct rtw88_softc *sc)
{
	struct rtw88_sdio_hci *ctx = rtw88_sdio_hci_context(sc);
	int error;

	if ((error = rtw88_sdio_hci_buffers(ctx)) != 0)
		return error;
	if (sc->sc_resources_ready || sc->sc_running || sc->sc_started ||
	    sc->sc_powered || sc->sc_power_dirty || ctx->tx_active)
		return EBUSY;
	/* Quiescent process-context binding only. The frontend explicitly
	 * routes prepare + identity through its executor before netif attach. */
	return 0;
}

static int
rtw88_sdio_hci_prepare(struct rtw88_softc *sc)
{
	struct rtw88_sdio_hci *ctx = rtw88_sdio_hci_context(sc);
	int error;

	if ((error = rtw88_sdio_hci_owner(ctx, false)) != 0)
		return error;
	if (sc->sc_running || ctx->tx_active)
		return EBUSY;
	if (ctx->bus->fifo_failed)
		return EBUSY;
	if ((error = rtw88_sdio_hci_buffers(ctx)) != 0)
		return error;
	ctx->acquired = true; /* Retain even an uncertain partial acquire. */
	ctx->quiesced = false;
	error = ctx->parent.acquire(ctx->parent.cookie);
	if (error != 0)
		return error;
	if ((error = rtw88_sdio_hci_owner(ctx, false)) != 0)
		return error;
	if (!ctx->bus->function_claimed || !ctx->bus->function_ready)
		return ENXIO;
	sc->sc_resources_ready = 1;
	ctx->quiesced = false;
	return 0;
}

static int
rtw88_sdio_hci_setup(struct rtw88_softc *sc)
{
	struct rtw88_sdio_hci *ctx = rtw88_sdio_hci_context(sc);
	struct rtw8723d_runtime *chip = sc != NULL ? sc->sc_chip_private : NULL;
	int error;

	if ((error = rtw88_sdio_hci_owner(ctx, false)) != 0)
		return error;
	if (chip == NULL || chip->boot.core != sc || !chip->boot.owned ||
	    !chip->boot.firmware_booted || chip->boot.failed ||
	    chip->boot.generation != sc->sc_generation || sc->sc_generation == 0 ||
	    !sc->sc_powered || !sc->sc_resources_ready || sc->sc_running ||
	    ctx->tx_active)
		return ENXIO;
	if (ctx->setup_generation >= sc->sc_generation)
		return ctx->setup_generation == sc->sc_generation ? EALREADY : ESTALE;
	/* Chip firmware boot alone does not prove SDIO FIFO/function resync.
	 * Keep uncertain framing latched until a separately verified recovery.
	 * The pinned donor also retains rx_addr across normal HCI start/stop. */
	if (ctx->bus->fifo_failed)
		return EBUSY;
	ctx->setup_generation = sc->sc_generation;
	ctx->error = 0;
	ctx->hold_irq = ctx->delayed_rx = false;
	ctx->parent.retry_reset(ctx->parent.cookie);
	return 0;
}

static int
rtw88_sdio_hci_start(struct rtw88_softc *sc)
{
	struct rtw88_sdio_hci *ctx = rtw88_sdio_hci_context(sc);
	int error;

	if ((error = rtw88_sdio_hci_owner(ctx, false)) != 0)
		return error;
	if (!sc->sc_powered || !sc->sc_resources_ready || sc->sc_generation == 0 ||
	    ctx->setup_generation != sc->sc_generation || ctx->bus->fifo_failed)
		return ENXIO;
	error = rtw88_sdio_irq_mask(ctx->bus);
	if (error == 0)
		error = ctx->parent.irq_start(ctx->parent.cookie);
	if (error == 0)
		error = rtw88_sdio_hci_owner(ctx, false);
	if (error == 0)
		sc->sc_running = 1;
	/* The owner poll will arm RX only with a committed full netif identity,
	 * after chip start/control publication. No RX epoch is invented here. */
	return error;
}

static int
rtw88_sdio_hci_quiesce(struct rtw88_softc *sc)
{
	struct rtw88_sdio_hci *ctx = rtw88_sdio_hci_context(sc);
	int error;

	if (ctx != NULL && rtw88_sdio_hci_offline(ctx)) {
		ctx->quiesced = true;
		return 0; /* Already released, process-context core_detach is safe. */
	}
	if ((error = rtw88_sdio_hci_owner(ctx, true)) != 0)
		return error;
	if (ctx->tx_active)
		return EBUSY;
	if (sc->sc_resources_ready) {
		error = rtw88_sdio_irq_mask(ctx->bus);
		if (error != 0)
			return error;
	}
	sc->sc_running = 0;
	ctx->quiesced = true;
	return 0;
}

static int
rtw88_sdio_hci_stop(struct rtw88_softc *sc)
{
	struct rtw88_sdio_hci *ctx = rtw88_sdio_hci_context(sc);
	struct rtw8723d_runtime *chip = sc != NULL ? sc->sc_chip_private : NULL;
	int error;

	/* CMD52 cleanup cannot prove an ambiguous FIFO was resynchronised.
	 * Keep the HCI and parent resources owned until explicit recovery. */
	if (ctx != NULL && ctx->bus->fifo_failed)
		return EIO;
	if (ctx != NULL && rtw88_sdio_hci_offline(ctx))
		return 0;
	if ((error = rtw88_sdio_hci_owner(ctx, true)) != 0)
		return error;
	if (!ctx->quiesced || ctx->tx_active || sc->sc_running ||
	    sc->sc_powered || sc->sc_power_dirty ||
	    (chip != NULL && chip->boot.owned))
		return EBUSY;
	error = ctx->parent.release(ctx->parent.cookie);
	if (error != 0)
		return error;
	if (ctx->bus->function_claimed || ctx->bus->function_ready)
		return EBUSY;
	sc->sc_resources_ready = 0;
	ctx->acquired = false;
	ctx->error = 0;
	return 0;
}

static int
rtw88_sdio_hci_detach(struct rtw88_softc *sc)
{
	struct rtw88_sdio_hci *ctx = rtw88_sdio_hci_context(sc);

	if (ctx == NULL)
		return EINVAL;
	/* Process context after netif STOP/drain; no hardware access here. */
	return rtw88_sdio_hci_offline(ctx) ? 0 : EBUSY;
}

static int
rtw88_sdio_hci_ready(struct rtw88_sdio_hci *ctx, bool tx)
{
	struct rtw88_softc *sc;
	int error;

	if ((error = rtw88_sdio_hci_owner(ctx, false)) != 0)
		return error;
	sc = ctx->core;
	if (ctx->bus->fifo_failed)
		return EIO;
	if (ctx->error != 0)
		return ctx->error;
	if (sc->sc_generation == 0 || ctx->setup_generation != sc->sc_generation)
		return ESTALE;
	if (ctx->quiesced || !sc->sc_resources_ready || !sc->sc_powered ||
	    !sc->sc_running || !sc->sc_started ||
	    !ctx->bus->function_claimed || !ctx->bus->function_ready ||
	    !ctx->bus->cmd53_enabled || (tx && !sc->sc_rf_enabled))
		return ENXIO;
	return 0;
}

static unsigned
rtw88_sdio_hci_space(struct rtw88_softc *sc, unsigned queue)
{
	struct rtw88_sdio_hci *ctx = rtw88_sdio_hci_context(sc);

	return queue < RTW88_NTXQ && rtw88_sdio_hci_ready(ctx, true) == 0 &&
	    !ctx->tx_active ? 1 : 0;
}

static int
rtw88_sdio_hci_drain(struct rtw88_softc *sc, unsigned *pending)
{
	struct rtw88_sdio_hci *ctx = rtw88_sdio_hci_context(sc);

	if (pending == NULL || ctx == NULL)
		return EINVAL;
	if (!rtw88_executor_owned(sc))
		return EPERM;
	*pending = ctx->tx_active ? 1 : 0;
	/* Host staging ownership only; no physical MAC-idle or air-ACK claim. */
	return rtw88_sdio_hci_ready(ctx, false);
}

static int
rtw88_sdio_hci_xmit(struct rtw88_softc *sc, unsigned queue,
    const uint8_t *frame, size_t size, const struct rtw88_tx_info *info,
    enum rtw88_tx_progress *progress)
{
	static const unsigned ring[RTW88_NTXQ] = {
		RTW88_TX_RING_MGMT, RTW88_TX_RING_BE, RTW88_TX_RING_BK,
		RTW88_TX_RING_VI, RTW88_TX_RING_VO
	};
	struct rtw88_sdio_hci *ctx = rtw88_sdio_hci_context(sc);
	int error, host_error;

	if (ctx == NULL)
		return EINVAL;
	error = rtw88_sdio_tx_progress_init(ctx->tx, frame, size, info, progress);
	if (error != 0)
		return error;
	if (queue >= RTW88_NTXQ)
		return EINVAL;
	if ((error = rtw88_sdio_hci_ready(ctx, true)) != 0)
		return error;
	if (ctx->tx_active)
		return EBUSY;
	ctx->tx_active = true;
	error = rtw88_sdio_tx_submit(ctx->tx, ring[queue], frame, size,
	    info, progress);
	/* A synchronous completion retires the host buffer; a raw host error
	 * retains it because checked reset/IDMA quiescence is not established. */
	host_error = ctx->parent.status(ctx->parent.cookie);
	if (host_error == 0)
		ctx->tx_active = false;
	else if (error == 0)
		error = host_error;
	if (error == 0) {
		sc->sc_stats.tx_done++;
		if (sc->sc_tx_complete != NULL)
			sc->sc_tx_complete(sc->sc_net_cookie, 1);
	}
	return error;
}

int
rtw88_sdio_hci_poll(struct rtw88_softc *sc)
{
	struct rtw88_sdio_hci *ctx = rtw88_sdio_hci_context(sc);
	struct rtw88_async_epoch id;
	bool again = false;
	int error;

	if ((error = rtw88_sdio_hci_ready(ctx, false)) != 0)
		return error;
	if (ctx->delayed_rx && !ctx->parent.retry_ready(ctx->parent.cookie))
		return 0;
	error = rtw88_sdio_irq_mask(ctx->bus);
	if (error != 0)
		return error;
	error = ctx->parent.rx_epoch(ctx->parent.cookie, &id);
	if (error == ENXIO) {
		ctx->hold_irq = true;
		return 0; /* Existing STOP/closed owner already has the wakeup. */
	}
	if (error == ESTALE || error == EAGAIN) {
		/* INIT/key/peer publication is still owned by a control request.
		 * Keep IRQ closed and wait for that owner reason; never self-spin
		 * or turn a normal fence into a fatal chip/transport fault. */
		ctx->hold_irq = true;
		ctx->delayed_rx = true;
		ctx->parent.again(ctx->parent.cookie, true);
		return 0;
	}
	if (error != 0)
		return error;
	ctx->hold_irq = ctx->delayed_rx = false;
	error = rtw88_sdio_irq_service_masked(ctx->rx, sc, &id,
	    ctx->parent.rx_current, ctx->parent.cookie, &again);
	if (again && (error == 0 || error == EAGAIN)) {
		ctx->hold_irq = true;
		ctx->delayed_rx = error == EAGAIN;
		ctx->parent.again(ctx->parent.cookie, error == EAGAIN);
		return 0;
	}
	if (!ctx->bus->fifo_failed &&
	    ctx->parent.status(ctx->parent.cookie) == 0 &&
	    (error == ESTALE || error == EAGAIN)) {
		ctx->hold_irq = true;
		ctx->delayed_rx = true;
		ctx->parent.again(ctx->parent.cookie, true);
		return 0;
	}
	if (error == 0)
		ctx->parent.retry_reset(ctx->parent.cookie);
	return error;
}

int
rtw88_sdio_hci_reopen(struct rtw88_softc *sc, bool permitted)
{
	struct rtw88_sdio_hci *ctx = rtw88_sdio_hci_context(sc);
	bool pending;
	int error;

	if (!permitted)
		return 0;
	if ((error = rtw88_sdio_hci_ready(ctx, false)) != 0)
		return error;
	if (ctx->hold_irq)
		return 0;
	error = rtw88_sdio_irq_rearm(ctx->bus, &pending);
	if (error == 0)
		error = rtw88_sdio_hci_owner(ctx, false);
	if (error != 0) {
		(void)rtw88_sdio_irq_mask(ctx->bus);
		return error;
	}
	if (pending) {
		ctx->hold_irq = true;
		ctx->parent.again(ctx->parent.cookie, false);
	}
	return 0;
}

static int
rtw88_sdio_hci_intr(struct rtw88_softc *sc)
{
	struct rtw88_sdio_hci *ctx = rtw88_sdio_hci_context(sc);
	int error;

	if (ctx == NULL || !sc->sc_running || !sc->sc_started)
		return 0;
	error = rtw88_sdio_hci_poll(sc);
	if (error != 0) {
		ctx->error = error;
		ctx->parent.fault(ctx->parent.cookie, error);
		return 0;
	}
	return 1;
}

static int
rtw88_sdio_hci_window(struct rtw88_softc *sc, uint32_t address,
    const uint8_t *data, size_t size)
{
	struct rtw88_sdio_hci *ctx = rtw88_sdio_hci_context(sc);
	int error;

	if ((error = rtw88_sdio_hci_owner(ctx, false)) != 0)
		return error;
	return rtw88_sdio_transfer(ctx->bus, RTW88_SDIO_MAC, address,
	    (void *)(uintptr_t)data, size, true);
}

static int
rtw88_sdio_hci_data_mode(struct rtw88_softc *sc, bool enable)
{
	struct rtw88_sdio_hci *ctx = rtw88_sdio_hci_context(sc);
	int error;

	if ((error = rtw88_sdio_hci_owner(ctx, !enable)) != 0)
		return error;
	if (enable && (!sc->sc_powered || !sc->sc_resources_ready))
		return ENXIO;
	return rtw88_sdio_set_cmd53(ctx->bus, enable);
}

static int
rtw88_sdio_hci_frontier(struct rtw88_softc *sc __unused,
    uint32_t generation __unused, uint64_t *out __unused)
{

	/* No supported DFS/RFK frontier in the initial 2.4GHz legacy profile. */
	return EOPNOTSUPP;
}

static int
rtw88_sdio_hci_fw_section(struct rtw88_softc *sc __unused,
    uint32_t generation __unused, const uint8_t *data __unused,
    size_t size __unused, const struct rtw88_fw_section *section __unused)
{

	/* RTL8723D's validated legacy loader uses write_window, never PCI DDMA. */
	return EOPNOTSUPP;
}

const struct rtw88_hci_ops rtw88_sdio_hci_ops = {
	.attach = rtw88_sdio_hci_attach,
	.detach = rtw88_sdio_hci_detach,
	.prepare = rtw88_sdio_hci_prepare,
	.setup = rtw88_sdio_hci_setup,
	.start = rtw88_sdio_hci_start,
	.quiesce = rtw88_sdio_hci_quiesce,
	.stop = rtw88_sdio_hci_stop,
	.intr = rtw88_sdio_hci_intr,
	.space = rtw88_sdio_hci_space,
	.drain = rtw88_sdio_hci_drain,
	.frontier = rtw88_sdio_hci_frontier,
	.fw_section = rtw88_sdio_hci_fw_section,
	.xmit = rtw88_sdio_hci_xmit,
	.no_host_dma = true,
	.write_window = rtw88_sdio_hci_window,
	.set_data_mode = rtw88_sdio_hci_data_mode
};

int
rtw88_sdio_hci_bind(struct rtw88_sdio_hci *ctx, struct rtw88_softc *sc,
    struct rtw88_sdio_bus *bus, struct rtw88_sdio_tx_context *tx,
    struct rtw88_sdio_rx_context *rx, const struct rtw88_sdio_hci_parent *parent)
{

	if (ctx == NULL || sc == NULL || bus == NULL || tx == NULL || rx == NULL ||
	    parent == NULL || parent->acquire == NULL || parent->irq_start == NULL ||
	    parent->release == NULL || parent->status == NULL ||
	    parent->revoked == NULL || parent->rx_epoch == NULL ||
	    parent->rx_current == NULL || parent->retry_ready == NULL ||
	    parent->retry_reset == NULL ||
	    parent->again == NULL ||
	    parent->fault == NULL || sc->sc_defer_irq == NULL ||
	    sc->sc_hci_private != NULL || sc->sc_hci != NULL || ctx->core != NULL ||
	    sc->sc_started || sc->sc_running || sc->sc_powered || sc->sc_power_dirty ||
	    sc->sc_resources_ready || ctx->acquired || ctx->tx_active ||
	    ctx->setup_generation != 0 || ctx->error != 0 ||
	    ctx->hold_irq || ctx->delayed_rx || tx->bus != bus || rx->bus != bus)
		return EINVAL;
	ctx->core = sc;
	ctx->bus = bus;
	ctx->tx = tx;
	ctx->rx = rx;
	ctx->parent = *parent;
	sc->sc_hci_private = ctx;
	sc->sc_hci = &rtw88_sdio_hci_ops;
	return 0;
}
