/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native FIFO/error handling, AI-assisted. Protocol reference:
 * Linux adc218676eef25575469234709c2d87185ca223a rtw88/sdio.c read/write_port.
 * No Linux implementation is embedded. */
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <string.h>
#endif
#include "rtw88_sdio_fifo.h"
#include "sdmmc_io_status.h"

static int
rtw88_fifo_ready(const struct rtw88_sdio_bus *bus)
{
	if (bus == NULL || bus->ops == NULL || bus->ops->command53 == NULL)
		return EINVAL;
	if (bus->fifo_failed)
		return EIO;
	if (!bus->function_claimed || !bus->function_ready ||
	    !bus->cmd53_enabled || bus->block_size != RTW88_SDIO_FIFO_BLOCK)
		return EPROTO;
	return 0;
}

static bool
rtw88_fifo_overlap(const void *a, size_t alen, const void *b, size_t blen)
{
	uintptr_t first = (uintptr_t)a, second = (uintptr_t)b;

	return first <= second ? second - first < alen : first - second < blen;
}

static void
rtw88_fifo_failed(struct rtw88_sdio_bus *bus)
{
	bus->fifo_failed = true;
	bus->cmd53_enabled = false;
}

static int
rtw88_fifo_command(struct rtw88_sdio_bus *bus, uint32_t port, void *data,
    size_t length, bool write)
{
	uint32_t arg = 0x14000000U | (port << 9), response = 0;
	int error;

	if (write)
		arg |= 0x80000000U;
	if (length >= RTW88_SDIO_FIFO_BLOCK) {
		arg |= 0x08000000U;
		arg |= (uint32_t)(length / RTW88_SDIO_FIFO_BLOCK);
	} else {
		arg |= (uint32_t)length;
	}
	error = bus->ops->command53(bus->ops->cookie, arg, data, length,
	    &response);
	return error != 0 ? error : sdmmc_io_r5_error(response);
}

int
rtw88_sdio_fifo_write(struct rtw88_sdio_bus *bus, uint32_t port,
    const void *data, size_t logical, size_t transfer,
    enum rtw88_tx_progress *progress)
{
	uint8_t *scratch;
	int error;

	if (progress == NULL)
		return EINVAL;
	*progress = RTW88_TX_NOT_STARTED;
	error = rtw88_fifo_ready(bus);
	if (error != 0)
		return error;
	if (data == NULL || logical == 0 || logical > RTW88_SDIO_FIFO_MAX_TX ||
	    transfer > RTW88_SDIO_FIFO_MAX_TX ||
	    transfer != ((logical + 511) & ~(size_t)511) ||
	    (port >> 13) < 4 || (port >> 13) > 7 ||
	    (port & 0x1fffU) != (logical + 3) / 4)
		return EINVAL;
	scratch = bus->read_scratch;
	if (scratch == NULL || bus->read_scratch_len < transfer ||
	    rtw88_fifo_overlap(data, logical, scratch, transfer))
		return EINVAL;
	memcpy(scratch, data, logical);
	memset(scratch + logical, 0, transfer - logical);
	*progress = RTW88_TX_MAY_HAVE_WRITTEN;
	error = rtw88_fifo_command(bus, port, scratch, transfer, true);
	if (error != 0)
		rtw88_fifo_failed(bus);
	else
		*progress = RTW88_TX_ACCEPTED;
	return error;
}

int
rtw88_sdio_fifo_read(struct rtw88_sdio_bus *bus, uint8_t *counter,
    void *out, size_t logical, uint8_t *scratch, size_t scratch_len)
{
	uint32_t port;
	size_t transfer, done, chunk;
	int error;

	error = rtw88_fifo_ready(bus);
	if (error != 0)
		return error;
	if (counter == NULL || out == NULL || scratch == NULL || logical == 0 ||
	    logical > RTW88_SDIO_FIFO_MAX_RX)
		return EINVAL;
	transfer = logical <= RTW88_SDIO_FIFO_BLOCK ?
	    (logical + 3) & ~(size_t)3 : (logical + 511) & ~(size_t)511;
	if (scratch_len < transfer ||
	    rtw88_fifo_overlap(out, logical, scratch, transfer) ||
	    rtw88_fifo_overlap(counter, 1, scratch, transfer) ||
	    rtw88_fifo_overlap(counter, 1, out, logical))
		return EINVAL;
	port = 0xe000U | (*counter & 3U);
	(*counter)++;
	for (done = 0; done < transfer; done += chunk) {
		chunk = transfer - done;
		if (chunk > RTW88_SDIO_FIFO_MAX_REQUEST)
			chunk = RTW88_SDIO_FIFO_MAX_REQUEST;
		error = rtw88_fifo_command(bus, port, scratch + done, chunk, false);
		if (error != 0) {
			rtw88_fifo_failed(bus);
			return error;
		}
	}
	memcpy(out, scratch, logical);
	return 0;
}
