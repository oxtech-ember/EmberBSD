/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
/* Native deferred SDIO RX to core/netif identity binding, AI-assisted. */
#ifdef _KERNEL
#include <sys/errno.h>
#else
#include <errno.h>
#endif
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw88_executor.h>
#include <dev/sdmmc/rtw88_sdio_rx_publish.h>
#include <dev/sdmmc/rtw88_sdio_fifo.h>

int
rtw88_sdio_rx_publish(struct rtw88_softc *sc, const uint8_t *data, size_t length,
    struct rtw88_sdio_rx_batch *batch, const struct rtw88_async_epoch *input,
    int (*check)(void *, const struct rtw88_async_epoch *), void *cookie)
{
	struct rtw88_async_epoch id;
	const struct rtw88_sdio_rx_record *record;
	uint64_t base;
	size_t span;
	unsigned i, count;
	int error;

	if (input == NULL || check == NULL)
		return EINVAL;
	id = *input;
	error = rtw88_rx_epoch_check(sc, &id);
	if (error != 0)
		return error;
	error = check(cookie, &id);
	if (error != 0)
		return error;
	error = rtw88_sdio_rx_validate(sc, data, length, batch);
	if (error != 0)
		return error;
	batch->id = id;
	count = batch->count;
	base = sc->sc_rx_sequence;
	if ((uint64_t)count > UINT64_MAX - base)
		return EOVERFLOW;
	/* The FIFO read consumed all records, even if an epoch fence below
	 * discards a suffix. Do not reuse those host identities. */
	sc->sc_rx_sequence = base + count;
	for (i = 0; i < count; i++) {
		error = check(cookie, &id);
		if (error != 0)
			return error;
		record = &batch->records[i];
		span = (size_t)record->payload_offset + record->length -
		    record->descriptor_offset;
		error = rtw88_rx_deliver_epoch(sc,
		    data + record->descriptor_offset, span, base + i + 1, &id);
		if (error != 0)
			return error;
	}
	return 0;
}

int
rtw88_sdio_rx_receive(struct rtw88_sdio_rx_context *ctx, struct rtw88_softc *sc,
    size_t logical, const struct rtw88_async_epoch *input,
    int (*check)(void *, const struct rtw88_async_epoch *), void *cookie)
{
	struct rtw88_async_epoch id;
	int error;

	if (ctx == NULL || ctx->bus == NULL || input == NULL || check == NULL)
		return EINVAL;
	if (ctx->packet == NULL || ctx->scratch == NULL || ctx->batch == NULL)
		return ENOMEM;
	if (logical == 0 || logical > RTW88_SDIO_RX_BATCH_MAX ||
	    logical > ctx->packet_cap)
		return EMSGSIZE;
	id = *input;
	error = rtw88_rx_epoch_check(sc, &id);
	if (error != 0)
		return error;
	error = check(cookie, &id);
	if (error != 0)
		return error;
	error = rtw88_sdio_fifo_read(ctx->bus, &ctx->counter, ctx->packet,
	    logical, ctx->scratch, ctx->scratch_cap);
	if (error != 0)
		return error;
	return rtw88_sdio_rx_publish(sc, ctx->packet, logical, ctx->batch,
	    &id, check, cookie);
}

int
rtw88_sdio_rx_drain(struct rtw88_sdio_rx_context *ctx, struct rtw88_softc *sc,
    size_t logical, uint32_t generation, uint64_t *frontier)
{
	uint64_t next;
	int error;

	if (ctx == NULL || ctx->bus == NULL || sc == NULL || frontier == NULL ||
	    sc->sc_chip == NULL || sc->sc_chip->rx_desc == NULL ||
	    sc->sc_chip->caps.rx_desc_size != 24)
		return EINVAL;
	if (!rtw88_executor_owned(sc))
		return EPERM;
	if (generation != sc->sc_generation)
		return ESTALE;
	if (!sc->sc_resources_ready || !sc->sc_powered)
		return ENXIO;
	if (ctx->packet == NULL || ctx->scratch == NULL || ctx->batch == NULL)
		return ENOMEM;
	if (logical == 0 || logical > RTW88_SDIO_RX_BATCH_MAX ||
	    logical > ctx->packet_cap)
		return EMSGSIZE;
	error = rtw88_sdio_fifo_read(ctx->bus, &ctx->counter, ctx->packet,
	    logical, ctx->scratch, ctx->scratch_cap);
	if (error != 0)
		return error;
	if (generation != sc->sc_generation)
		return ESTALE;
	error = rtw88_sdio_rx_validate(sc, ctx->packet, logical, ctx->batch);
	if (error == 0 &&
	    (uint64_t)ctx->batch->count > UINT64_MAX - sc->sc_rx_sequence)
		error = EOVERFLOW;
	if (error != 0) {
		/* Bytes are gone, but no trustworthy sequence frontier exists. */
		ctx->bus->fifo_failed = true;
		ctx->bus->cmd53_enabled = false;
		return error;
	}
	if (generation != sc->sc_generation)
		return ESTALE;
	next = sc->sc_rx_sequence + ctx->batch->count;
	sc->sc_rx_sequence = next;
	*frontier = next;
	return 0;
}
