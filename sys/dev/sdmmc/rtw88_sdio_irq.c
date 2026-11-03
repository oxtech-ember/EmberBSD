/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
/* Original native bounded actor implementation, AI-assisted. Register and
 * ordering facts: Linux adc218676eef25575469234709c2d87185ca223a
 * rtw88/sdio.h and sdio.c:rtw_sdio_rx_isr/rtw_sdio_handle_interrupt.
 * rtw8723d_hw_spec selects WCPU_11N, RX FIFO16384 and RX descriptor24.
 * No Linux implementation is embedded. */
#ifdef _KERNEL
#include <sys/errno.h>
#else
#include <errno.h>
#endif
#include <dev/ic/rtw88var.h>
#include <dev/sdmmc/rtw88_sdio_io.h>
#include <dev/sdmmc/rtw88_sdio_irq.h>

#define RTWSDIO_IRQ_HIMR 0x14U
#define RTWSDIO_IRQ_HISR 0x18U
#define RTWSDIO_IRQ_RX_LEN 0x1cU
#define RTWSDIO_IRQ_RX_REQUEST 1U
#define RTWSDIO_IRQ_BYTE_BUDGET 65536U
#define RTWSDIO_IRQ_BATCH_BUDGET 16U

static int
rtw88_sdio_irq_read(struct rtw88_sdio_bus *bus, unsigned offset,
    unsigned width, uint32_t *value)
{
	struct rtw88_sdio_bus access = *bus;
	uint8_t bytes[4];
	int error;

	/* Narrow legacy registers use CMD52, as in native scalar I/O. */
	if (width != 4)
		access.cmd53_enabled = false;
	error = rtw88_sdio_transfer(&access, RTW88_SDIO_LOCAL, offset,
	    bytes, width, false);
	if (error != 0)
		return error;
	*value = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8;
	if (width == 4)
		*value |= (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
	return 0;
}

static int
rtw88_sdio_irq_write_mask(struct rtw88_sdio_bus *bus, uint32_t value)
{
	uint8_t bytes[4] = { value, value >> 8, value >> 16, value >> 24 };

	if (bus == NULL)
		return EINVAL;
	return rtw88_sdio_transfer(bus, RTW88_SDIO_LOCAL, RTWSDIO_IRQ_HIMR,
	    bytes, sizeof(bytes), true);
}

int
rtw88_sdio_irq_mask(struct rtw88_sdio_bus *bus)
{

	return rtw88_sdio_irq_write_mask(bus, 0);
}

static int
rtw88_sdio_irq_service_mode(struct rtw88_sdio_rx_context *ctx, struct rtw88_softc *sc,
    const struct rtw88_async_epoch *input,
    int (*check)(void *, const struct rtw88_async_epoch *), void *cookie,
    bool *again, bool rearm)
{
	struct rtw88_async_epoch id;
	uint32_t status, length;
	unsigned batches = 0, bytes = 0;
	int error;

	if (ctx == NULL || ctx->bus == NULL || input == NULL || check == NULL ||
	    again == NULL)
		return EINVAL;
	id = *input;
	*again = false;
	error = rtw88_rx_epoch_check(sc, &id);
	if (error != 0)
		return error;
	if (!sc->sc_resources_ready || !sc->sc_powered)
		return ENXIO;
	if (sc->sc_chip == NULL || sc->sc_chip->rx_desc == NULL ||
	    sc->sc_chip->caps.rx_desc_size != 24)
		return EINVAL;
	if (ctx->packet == NULL || ctx->scratch == NULL || ctx->batch == NULL ||
	    ctx->packet_cap < RTW88_SDIO_RX_BATCH_MAX ||
	    ctx->scratch_cap < RTW88_SDIO_RX_BATCH_MAX)
		return ENOMEM;
	error = rtw88_sdio_irq_mask(ctx->bus);
	if (error != 0)
		return error;
	for (;;) {
		error = check(cookie, &id);
		if (error != 0)
			return error;
		error = rtw88_rx_epoch_check(sc, &id);
		if (error != 0)
			return error;
		error = rtw88_sdio_irq_read(ctx->bus, RTWSDIO_IRQ_HISR, 4, &status);
		if (error != 0)
			return error;
		/* Length alone may describe a buffer HW/FW is still filling. */
		if ((status & RTWSDIO_IRQ_RX_REQUEST) == 0)
			break;
		error = rtw88_sdio_irq_read(ctx->bus, RTWSDIO_IRQ_RX_LEN, 2, &length);
		if (error != 0)
			return error;
		if (length == 0) {
			*again = true;
			return EAGAIN;
		}
		if (length > RTW88_SDIO_RX_BATCH_MAX)
			return EMSGSIZE;
		if (batches == RTWSDIO_IRQ_BATCH_BUDGET ||
		    length > RTWSDIO_IRQ_BYTE_BUDGET - bytes) {
			*again = true;
			return 0;
		}
		error = rtw88_sdio_rx_receive(ctx, sc, length, &id, check, cookie);
		if (error != 0)
			return error;
		bytes += length;
		batches++;
	}
	error = check(cookie, &id);
	if (error == 0)
		error = rtw88_rx_epoch_check(sc, &id);
	if (error != 0)
		return error;
	if (!rearm)
		return 0;
	/* Only RX_REQUEST is enabled. No unsupported cause is acknowledged. */
	error = rtw88_sdio_irq_write_mask(ctx->bus, RTWSDIO_IRQ_RX_REQUEST);
	if (error == 0)
		error = check(cookie, &id);
	if (error == 0)
		error = rtw88_rx_epoch_check(sc, &id);
	if (error == 0)
		error = rtw88_sdio_irq_read(ctx->bus, RTWSDIO_IRQ_HISR, 4, &status);
	if (error == 0)
		error = check(cookie, &id);
	if (error == 0)
		error = rtw88_rx_epoch_check(sc, &id);
	if (error != 0) {
		/* Independent close after a possibly effective enable; first error
		 * wins. A failed close requires the owner's retained-resource path. */
		(void)rtw88_sdio_irq_mask(ctx->bus);
		return error;
	}
	if ((status & RTWSDIO_IRQ_RX_REQUEST) != 0) {
		error = rtw88_sdio_irq_mask(ctx->bus);
		if (error != 0)
			return error;
		*again = true;
	}
	return 0;
}

int
rtw88_sdio_irq_service(struct rtw88_sdio_rx_context *ctx, struct rtw88_softc *sc,
    const struct rtw88_async_epoch *id,
    int (*check)(void *, const struct rtw88_async_epoch *), void *cookie,
    bool *again)
{

	return rtw88_sdio_irq_service_mode(ctx, sc, id, check, cookie, again, true);
}

int
rtw88_sdio_irq_service_masked(struct rtw88_sdio_rx_context *ctx,
    struct rtw88_softc *sc, const struct rtw88_async_epoch *id,
    int (*check)(void *, const struct rtw88_async_epoch *), void *cookie,
    bool *again)
{

	return rtw88_sdio_irq_service_mode(ctx, sc, id, check, cookie, again, false);
}

int
rtw88_sdio_irq_rearm(struct rtw88_sdio_bus *bus, bool *pending)
{
	uint32_t status;
	int error;

	if (pending == NULL)
		return EINVAL;
	error = rtw88_sdio_irq_write_mask(bus, RTWSDIO_IRQ_RX_REQUEST);
	if (error == 0)
		error = rtw88_sdio_irq_read(bus, RTWSDIO_IRQ_HISR, 4, &status);
	if (error != 0) {
		(void)rtw88_sdio_irq_mask(bus);
		return error;
	}
	if ((status & RTWSDIO_IRQ_RX_REQUEST) != 0) {
		error = rtw88_sdio_irq_mask(bus);
		if (error != 0)
			return error;
	}
	*pending = (status & RTWSDIO_IRQ_RX_REQUEST) != 0;
	return 0;
}
