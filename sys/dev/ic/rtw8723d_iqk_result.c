/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright 2024 Fiona Klute. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: Linux rtw88 adc218676eef25575469234709c2d87185ca223a; native adaptation. */
/* Pinned Linux adc218676eef25575469234709c2d87185ca223a rtw88:
 * util.c check_hw_ready, rtw8723d.c IQK TX/RX checks, rtw8723x.h limits.
 * Native bounded/cancellable completion and error propagation, AI-assisted. */
#include <dev/ic/rtw8723d_iqk.h>
#include <dev/ic/rtw8723d_boot.h>
#include <dev/ic/rtw88_io.h>

static int
rtw8723d_iqk_result_check(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops)
{
	if (io->error == 0 && ops->cancelled(ops->cookie))
		io->error = ECANCELED;
	return io->error;
}

int
rtw8723d_iqk_result_read(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops, bool receive,
    struct rtw8723d_iqk_result *out)
{
	struct rtw88_io validation;
	struct rtw8723d_iqk_result result;
	uint32_t status = 0;
	unsigned poll;
	int signed_y;

	if (io == NULL)
		return EINVAL;
	if (io->error != 0)
		return io->error;
	if (ops == NULL || ops->cancelled == NULL || out == NULL)
		return io->error = EINVAL;
	rtw88_io_init(&validation, io->ops, io->cookie);
	if (validation.error != 0)
		return io->error = validation.error;
	for (poll = 0; poll < 1000; poll++) {
		if (rtw8723d_iqk_result_check(io, ops) != 0)
			return io->error;
		status = rtw88_io_rd(io, 0x0eac, 4);
		if (io->error != 0)
			return io->error;
		if ((status & (UINT32_C(1) << 26)) != 0)
			break;
		rtw88_io_delay(io, 10);
		if (rtw8723d_iqk_result_check(io, ops) != 0)
			return io->error;
	}
	if (poll == 1000)
		return io->error = ETIMEDOUT;
	if (rtw8723d_iqk_result_check(io, ops) != 0)
		return io->error;
	result.x = (rtw88_io_rd(io, receive ? 0x0ea4 : 0x0e94, 4) >> 16) & 0x3ff;
	if (rtw8723d_iqk_result_check(io, ops) != 0)
		return io->error;
	if (receive)
		result.y = (status >> 16) & 0x3ff;
	else
		result.y = (rtw88_io_rd(io, 0x0e9c, 4) >> 16) & 0x3ff;
	if (rtw8723d_iqk_result_check(io, ops) != 0)
		return io->error;
	if (receive) {
		/* Explicit sign extension avoids signed shifts/overflow. */
		signed_y = result.y >= 0x200 ? (int)result.y - 0x400 : result.y;
		if (signed_y < 0)
			signed_y = -signed_y;
		if ((status & (UINT32_C(1) << 27)) != 0 ||
		    result.x <= 0x0e6 || result.x >= 0x11a || signed_y >= 0x1a)
			return EAGAIN;
	} else if ((status & (UINT32_C(1) << 28)) != 0 ||
	    /* The pinned matrix installers skip TX entirely for zero X. */
	    result.x == 0 || result.x == 0x142 || result.y == 0x042)
		return EAGAIN;
	*out = result;
	return 0;
}
