/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright 2024 Fiona Klute */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: Linux adc218676eef25575469234709c2d87185ca223a,
 * rtw88/rtw8723x.c __rtw8723x_lck and rtw8723x.h/reg.h.
 * Bounded, cancellable STA-only sequencing is AI-assisted. */
#include <dev/ic/rtw8723d_lck.h>
#include <dev/ic/rtw8723d_boot.h>
#include <dev/ic/rtw8723d_rf.h>
#include <dev/ic/rtw88_io.h>

#define RTW8723D_LCK_CTX 0x0d03U
#define RTW8723D_LCK_CTX_TYPE 0x70U
#define RTW8723D_LCK_TXPAUSE 0x0522U
#define RTW8723D_LCK_CFGCH 0x18U
#define RTW8723D_LCK_BIT 0x8000U
#define RTW8723D_LCK_RF_MASK 0xfffffU

static int
rtw8723d_lck_check(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops)
{
	if (io->error == 0 && ops->cancelled(ops->cookie))
		io->error = ECANCELED;
	return io->error;
}

int
rtw8723d_lck(struct rtw88_io *io, const struct rtw8723d_boot_ops *ops)
{
	struct rtw88_io validation, cleanup;
	uint32_t context, pause, original, busy;
	unsigned poll;
	int error;

	if (io == NULL)
		return EINVAL;
	if (io->error != 0)
		return io->error;
	if (ops == NULL || ops->wait_ms == NULL || ops->cancelled == NULL)
		return io->error = EINVAL;
	rtw88_io_init(&validation, io->ops, io->cookie);
	if (validation.error != 0)
		return io->error = validation.error;
	if (rtw8723d_lck_check(io, ops) != 0)
		return io->error;
	context = rtw88_io_rd(io, RTW8723D_LCK_CTX, 1);
	if (io->error != 0)
		return io->error;
	/* Continuous TX is a manufacturing mode, outside this STA API. */
	if ((context & RTW8723D_LCK_CTX_TYPE) != 0)
		return io->error = EBUSY;
	pause = rtw88_io_rd(io, RTW8723D_LCK_TXPAUSE, 1);
	if (rtw8723d_lck_check(io, ops) != 0)
		return io->error;
	rtw88_io_wr(io, RTW8723D_LCK_TXPAUSE, 1, 0xff);
	if (rtw8723d_lck_check(io, ops) != 0)
		return io->error;
	error = rtw8723d_rf_read(io, 0, RTW8723D_LCK_CFGCH,
	    RTW8723D_LCK_RF_MASK, &original);
	if (error != 0)
		return error;
	if ((original & RTW8723D_LCK_BIT) != 0)
		return io->error = EBUSY;
	if (rtw8723d_lck_check(io, ops) != 0)
		return io->error;
	/* From this point a failed command may have started calibration. */
	(void)rtw8723d_rf_write(io, 0, RTW8723D_LCK_CFGCH,
	    RTW8723D_LCK_RF_MASK, original | RTW8723D_LCK_BIT);
	for (poll = 0; poll < 100 && io->error == 0; poll++) {
		if (rtw8723d_lck_check(io, ops) != 0)
			break;
		error = rtw8723d_rf_read(io, 0, RTW8723D_LCK_CFGCH,
		    RTW8723D_LCK_BIT, &busy);
		if (error != 0 || busy == 0)
			break;
		if (poll == 99) {
			io->error = ETIMEDOUT;
			break;
		}
		if (rtw8723d_lck_check(io, ops) != 0)
			break;
		ops->wait_ms(ops->cookie, 10);
	}
	(void)rtw8723d_lck_check(io, ops);
	if (io->error != 0) {
		error = io->error;
		/* Cancellation must not suppress a bounded attempt to abort LCK.
		 * Do not resume TX after failure. Caller still unwinds chip power. */
		rtw88_io_init(&cleanup, io->ops, io->cookie);
		(void)rtw8723d_rf_write(&cleanup, 0, RTW8723D_LCK_CFGCH,
		    RTW8723D_LCK_RF_MASK, original);
		io->error = error;
		return error;
	}
	(void)rtw8723d_rf_write(io, 0, RTW8723D_LCK_CFGCH,
	    RTW8723D_LCK_RF_MASK, original);
	if (rtw8723d_lck_check(io, ops) == 0)
		rtw88_io_wr(io, RTW8723D_LCK_TXPAUSE, 1, pause);
	return io->error;
}
