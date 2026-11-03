/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
/* Native bounded accounting, AI-assisted. Protocol facts: pinned Linux
 * adc218676eef25575469234709c2d87185ca223a rtw88/sdio.c
 * get_tx_addr/check_free_txpg. No Linux implementation is embedded. */
#ifdef _KERNEL
#include <sys/errno.h>
#else
#include <errno.h>
#endif
#include <dev/ic/rtw88var.h>
#include <dev/sdmmc/rtw88_sdio_tx.h>

static int
rtw88_sdio_queue(unsigned q, unsigned *port, unsigned *credit)
{
	switch (q) {
	case RTW88_TX_RING_BK:
	case RTW88_TX_RING_BE:
		*port = 6;
		*credit = RTW88_SDIO_CREDIT_LOW;
		return 0;
	case RTW88_TX_RING_VI:
	case RTW88_TX_RING_VO:
		*port = 5;
		*credit = RTW88_SDIO_CREDIT_NORMAL;
		return 0;
	case RTW88_TX_RING_MGMT:
		*port = 7;
		*credit = RTW88_SDIO_CREDIT_HIGH;
		return 0;
	default:
		return EOPNOTSUPP;
	}
}

int
rtw88_sdio_tx_layout(unsigned q, size_t frame_len,
    struct rtw88_sdio_tx_layout *out)
{
	struct rtw88_sdio_tx_layout layout;
	unsigned port;
	int error;

	if (out == NULL || frame_len == 0 ||
	    frame_len > RTW8723D_SDIO_TX_MAX_TRANSFER - RTW8723D_SDIO_TX_DESC_SIZE)
		return EINVAL;
	error = rtw88_sdio_queue(q, &port, &layout.credit_class);
	if (error != 0)
		return error;
	layout.logical = RTW8723D_SDIO_TX_DESC_SIZE + frame_len;
	layout.transfer = (layout.logical + 511) & ~(size_t)511;
	layout.port = (uint32_t)(port << 13) |
	    (uint32_t)((layout.logical + 3) / 4);
	layout.pages = (unsigned)(layout.transfer / 128);
	*out = layout;
	return 0;
}

int
rtw88_sdio_tx_credit(uint32_t free_txpg, unsigned q, size_t transfer_len,
    unsigned *pages)
{
	unsigned port, credit, needed, available;
	int error;

	if (pages == NULL || transfer_len == 0 ||
	    transfer_len > RTW8723D_SDIO_TX_MAX_TRANSFER ||
	    (transfer_len & 511) != 0)
		return EINVAL;
	error = rtw88_sdio_queue(q, &port, &credit);
	if (error != 0)
		return error;
	needed = (unsigned)(transfer_len / 128);
	available = ((free_txpg >> (credit * 8)) & 255) + (free_txpg >> 24);
	if (available < needed)
		return EBUSY;
	*pages = needed;
	return 0;
}
