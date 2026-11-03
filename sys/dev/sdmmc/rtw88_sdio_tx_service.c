/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
/* Native serialized descriptor/credit/FIFO transaction, AI-assisted. */
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <string.h>
#endif
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8723d_tx.h>
#include <dev/sdmmc/rtw88_sdio_fifo.h>
#include <dev/sdmmc/rtw88_sdio_tx.h>
#include <dev/sdmmc/rtw88_sdio_tx_service.h>

#define RTW8723D_SDIO_FREE_TXPG 0x20U
_Static_assert(RTW8723D_TX_PACKET_DESC_SIZE == RTW8723D_SDIO_TX_DESC_SIZE,
    "descriptor and FIFO layout sizes must agree");

static bool
rtw88_sdio_tx_overlap(const void *a, size_t an, const void *b, size_t bn)
{
	uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;

	return x <= y ? y - x < an : x - y < bn;
}

int
rtw88_sdio_tx_progress_init(struct rtw88_sdio_tx_context *ctx,
    const uint8_t *frame, size_t len, const struct rtw88_tx_info *info,
    enum rtw88_tx_progress *progress)
{
	if (progress == NULL)
		return EINVAL;
	/* Reject output aliases before publishing even NOT_STARTED. */
	if ((frame != NULL && rtw88_sdio_tx_overlap(progress, sizeof(*progress),
	    frame, len)) || (info != NULL && rtw88_sdio_tx_overlap(progress,
	    sizeof(*progress), info, sizeof(*info))) ||
	    (ctx != NULL && rtw88_sdio_tx_overlap(progress, sizeof(*progress),
	    ctx, sizeof(*ctx))) || (ctx != NULL && ctx->packet != NULL &&
	    rtw88_sdio_tx_overlap(progress, sizeof(*progress), ctx->packet,
	    ctx->packet_cap)))
		return EINVAL;
	if (ctx != NULL && ctx->bus != NULL &&
	    (rtw88_sdio_tx_overlap(progress, sizeof(*progress), ctx->bus,
	    sizeof(*ctx->bus)) || (ctx->bus->read_scratch != NULL &&
	    rtw88_sdio_tx_overlap(progress, sizeof(*progress),
	    ctx->bus->read_scratch, ctx->bus->read_scratch_len))))
		return EINVAL;
	*progress = RTW88_TX_NOT_STARTED;
	return 0;
}

int
rtw88_sdio_tx_submit(struct rtw88_sdio_tx_context *ctx, unsigned q,
    const uint8_t *frame, size_t len, const struct rtw88_tx_info *info,
    enum rtw88_tx_progress *progress)
{
	struct rtw88_sdio_tx_layout layout;
	struct rtw88_tx_info snapshot;
	struct rtw88_wme_map mapping;
	struct rtw88_sdio_bus *bus;
	uint8_t desc[RTW8723D_TX_PACKET_DESC_SIZE], raw[4];
	uint32_t free_pages;
	unsigned pages;
	int error;

	error = rtw88_sdio_tx_progress_init(ctx, frame, len, info, progress);
	if (error != 0)
		return error;
	if (ctx == NULL || ctx->bus == NULL || frame == NULL || info == NULL)
		return EINVAL;
	error = rtw88_sdio_tx_layout(q, len, &layout);
	if (error != 0)
		return error;
	bus = ctx->bus;
	if (bus->ops == NULL || bus->ops->command52 == NULL ||
	    bus->ops->command53 == NULL)
		return EINVAL;
	if (ctx->packet == NULL || ctx->packet_cap < layout.transfer ||
	    bus->read_scratch == NULL || bus->read_scratch_len < layout.transfer)
		return ENOMEM;
	if (rtw88_sdio_tx_overlap(ctx->packet, layout.transfer,
	    bus->read_scratch, layout.transfer) ||
	    rtw88_sdio_tx_overlap(ctx->packet, layout.transfer, frame, len) ||
	    rtw88_sdio_tx_overlap(ctx->packet, layout.transfer, info, sizeof(*info)) ||
	    rtw88_sdio_tx_overlap(bus->read_scratch, layout.transfer, frame, len) ||
	    rtw88_sdio_tx_overlap(bus->read_scratch, layout.transfer, info,
	    sizeof(*info)))
		return EINVAL;
	if (bus->fifo_failed)
		return EIO;
	if (!bus->function_claimed || !bus->function_ready ||
	    !bus->cmd53_enabled || bus->block_size != 512)
		return EPROTO;
	snapshot = *info;
	if (snapshot.packet_size != len)
		return EINVAL;
	if (q == RTW88_TX_RING_MGMT)
		snapshot.queue = 17;
	else {
		error = rtw88_wme_map(snapshot.queue, &mapping);
		if (error != 0)
			return error;
		if (mapping.ring != q)
			return EINVAL;
	}
	error = rtw8723d_tx_encode(desc, sizeof(desc), &snapshot);
	if (error != 0)
		return error;
	/* SDIO LOCAL FREE_TXPG is high/normal/low/public, read before submission. */
	error = rtw88_sdio_transfer(bus, RTW88_SDIO_LOCAL,
	    RTW8723D_SDIO_FREE_TXPG, raw,
	    sizeof(raw), false);
	if (error != 0)
		return error;
	free_pages = (uint32_t)raw[0] | (uint32_t)raw[1] << 8 |
	    (uint32_t)raw[2] << 16 | (uint32_t)raw[3] << 24;
	error = rtw88_sdio_tx_credit(free_pages, q, layout.transfer, &pages);
	if (error != 0)
		return error;
	memcpy(ctx->packet, desc, sizeof(desc));
	memcpy(ctx->packet + sizeof(desc), frame, len);
	return rtw88_sdio_fifo_write(bus, layout.port, ctx->packet,
	    layout.logical, layout.transfer, progress);
}
