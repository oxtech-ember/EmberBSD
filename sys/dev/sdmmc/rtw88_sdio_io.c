/* Origin: EmberBSD RTL8723DS diagnostics generalized as checked SDIO helpers. */
/* SPDX-License-Identifier: BSD-3-Clause */
/*-
 * Copyright (c) 2021 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 * Copyright (c) 2021 Jernej Skrabec <jernej.skrabec@gmail.com>
 * Copyright (c) 2018-2019 Realtek Corporation
 * Copyright (c) 2026 Anton and EmberBSD contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <string.h>
#endif
#include "rtw88_sdio_io.h"

static int
rtw88_sdio_r5(uint32_t response)
{
	if ((response & 0x8000) != 0)
		return EILSEQ;
	return (response & 0x4b00) != 0 ? EIO : 0;
}

int
rtw88_sdio_cmd52(const struct rtw88_sdio_ops *ops, unsigned int function,
    uint32_t address, bool write, uint8_t *value)
{
	uint32_t arg, response;
	int error;

	if (ops == NULL || ops->command52 == NULL || value == NULL ||
	    function > 1 || address > 0x1ffff)
		return EINVAL;
	arg = (function << 28) | (address << 9);
	if (write)
		arg |= 0x80000000U | *value;
	error = ops->command52(ops->cookie, arg, &response);
	if (error == 0)
		error = rtw88_sdio_r5(response);
	if (error == 0 && !write)
		*value = response & 0xff;
	return error;
}

int
rtw88_sdio_set_cmd53(struct rtw88_sdio_bus *bus, bool enabled)
{
	if (bus == NULL)
		return EINVAL;
	if (!enabled) {
		bus->cmd53_enabled = false;
		return 0;
	}
	if (!bus->function_claimed || !bus->function_ready ||
	    bus->block_size == 0 || bus->block_size > RTW88_SDIO_MAX_TRANSFER ||
	    bus->ops == NULL || bus->ops->command53 == NULL)
		return EPROTO;
	bus->cmd53_enabled = true;
	return 0;
}

static bool
rtw88_sdio_overlap(const void *a, const void *b, size_t len)
{
	uintptr_t first = (uintptr_t)a, second = (uintptr_t)b;

	return (first <= second ? second - first : first - second) < len;
}

int
rtw88_sdio_transfer(struct rtw88_sdio_bus *bus, enum rtw88_sdio_domain domain,
    uint32_t offset, void *data, size_t len, bool write)
{
	uint32_t address, limit;
	uint8_t *buffer;
	size_t done;
	int error;

	if (bus == NULL || bus->ops == NULL || bus->ops->command52 == NULL ||
	    data == NULL || len == 0 || len > RTW88_SDIO_MAX_TRANSFER ||
	    bus->max_request == 0 || bus->max_request > RTW88_SDIO_MAX_REQUEST)
		return EINVAL;
	switch (domain) {
	case RTW88_SDIO_LOCAL:
		limit = 0xfff;
		address = 0;
		break;
	case RTW88_SDIO_MAC:
		limit = 0xffff;
		address = 0x10000;
		break;
	default:
		return EINVAL;
	}
	if (offset > limit || len > limit - offset + 1)
		return EINVAL;
	address |= offset;
	if (!write && (bus->read_scratch == NULL || bus->read_scratch_len < len ||
	    rtw88_sdio_overlap(data, bus->read_scratch, len)))
		return EINVAL;
	buffer = write ? data : bus->read_scratch;
	if (!bus->cmd53_enabled) {
		for (done = 0; done < len; done++) {
			error = rtw88_sdio_cmd52(bus->ops, 1, address + done,
			    write, &buffer[done]);
			if (error != 0)
				return error;
		}
	} else {
		size_t chunk;

		if (!bus->function_claimed || !bus->function_ready ||
		    bus->block_size == 0 || bus->block_size > RTW88_SDIO_MAX_TRANSFER ||
		    bus->ops->command53 == NULL)
			return EPROTO;
		for (done = 0; done < len; done += chunk) {
			uint32_t response, arg;
			size_t count, blocks;

			chunk = len - done;
			if (chunk > bus->max_request)
				chunk = bus->max_request;
			blocks = chunk / bus->block_size;
			arg = 0x14000000U | ((address + done) << 9);
			if (write)
				arg |= 0x80000000U;
			if (blocks != 0) {
				/* Never encode the unbounded block-count-zero form. */
				if (blocks > 511)
					blocks = 511;
				chunk = blocks * bus->block_size;
				arg |= 0x08000000U;
				count = blocks;
			} else {
				count = chunk == 512 ? 0 : chunk;
			}
			arg |= count;
			error = bus->ops->command53(bus->ops->cookie, arg,
			    &buffer[done], chunk, &response);
			if (error == 0)
				error = rtw88_sdio_r5(response);
			if (error != 0)
				return error;
		}
	}
	if (!write)
		memcpy(data, buffer, len);
	return 0;
}

int
rtw88_sdio_function_acquire(struct rtw88_sdio_bus *bus,
    struct rtw88_sdio_function_state *state, bool allow_adopt)
{
	int error;

	if (bus == NULL || bus->ops == NULL || bus->ops->command52 == NULL ||
	    bus->ops->wait_ms == NULL || state == NULL)
		return EINVAL;
	if (bus->function_claimed)
		return EBUSY;
	memset(state, 0, sizeof(*state));
	bus->function_ready = bus->cmd53_enabled = false;
	error = rtw88_sdio_cmd52(bus->ops, 0, 2, false, &state->original);
	if (error != 0)
		return error;
	if ((state->original & 2) != 0 && !allow_adopt)
		return EBUSY;
	bus->function_claimed = true;
	if ((state->original & 2) == 0) {
		uint8_t value, want;

		want = state->original | 2;
		value = want;
		/* A failed write may have reached the card; release must still run. */
		state->changed = state->owns_enable = true;
		error = rtw88_sdio_cmd52(bus->ops, 0, 2, true, &value);
		if (error != 0)
			return error;
		error = rtw88_sdio_cmd52(bus->ops, 0, 2, false, &value);
		if (error != 0)
			return error;
		if (value != want)
			return EIO;
	}
	for (unsigned int attempt = 0; attempt <= 100; attempt++) {
		error = rtw88_sdio_cmd52(bus->ops, 0, 3, false, &state->ready);
		if (error != 0)
			return error;
		if ((state->ready & 2) != 0) {
			bus->function_ready = true;
			return 0;
		}
		if (attempt < 100)
			bus->ops->wait_ms(bus->ops->cookie, 10);
	}
	return ETIMEDOUT;
}

int
rtw88_sdio_function_release(struct rtw88_sdio_bus *bus,
    struct rtw88_sdio_function_state *state, enum rtw88_sdio_restore_policy policy)
{
	uint8_t value, want;
	int error;

	if (bus == NULL || bus->ops == NULL || bus->ops->command52 == NULL ||
	    state == NULL || (policy != RTW88_SDIO_RESTORE_OWNED_BIT &&
	    policy != RTW88_SDIO_RESTORE_DIAGNOSTIC_SNAPSHOT))
		return EINVAL;
	bus->cmd53_enabled = bus->function_ready = false;
	if (!state->changed || !state->owns_enable) {
		bus->function_claimed = false;
		return 0;
	}
	if (policy == RTW88_SDIO_RESTORE_DIAGNOSTIC_SNAPSHOT) {
		want = state->original;
	} else {
		error = rtw88_sdio_cmd52(bus->ops, 0, 2, false, &value);
		if (error != 0)
			return error;
		want = value & ~2;
		if ((value & 2) == 0)
			goto released;
	}
	value = want;
	error = rtw88_sdio_cmd52(bus->ops, 0, 2, true, &value);
	if (error != 0)
		return error;
	error = rtw88_sdio_cmd52(bus->ops, 0, 2, false, &value);
	if (error != 0)
		return error;
	if (policy == RTW88_SDIO_RESTORE_DIAGNOSTIC_SNAPSHOT ?
	    value != want : (value & 2) != 0)
		return EIO;
released:
	state->changed = state->owns_enable = false;
	bus->function_claimed = false;
	return 0;
}
