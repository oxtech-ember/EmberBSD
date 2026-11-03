/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: Linux rtw88 adc218676eef25575469234709c2d87185ca223a; native adaptation. */
/* Register protocol: pinned Linux rtw88/mac.c legacy allocator, util.c
 * check_hw_ready, and rtw8723d.c SDIO table row0, commit
 * adc218676eef25575469234709c2d87185ca223a. Native fallible sequencing,
 * cancellation and closed-TX error handling are AI-assisted. */
#include <dev/ic/rtw8723d_trx.h>
#include <dev/ic/rtw88_io.h>

static bool
rtw8723d_trx_live(struct rtw88_io *io, int (*check)(void *), void *cookie)
{

	if (io->error == 0 && check != NULL)
		io->error = check(cookie);
	return io->error == 0;
}

int
rtw8723d_trx_init(struct rtw88_io *io, int (*check)(void *), void *cookie)
{
	struct rtw88_io validation, cleanup;
	unsigned poll;
	int error;
	uint32_t value;

	if (io == NULL)
		return EINVAL;
	if (io->error != 0)
		return io->error;
	rtw88_io_init(&validation, io->ops, io->cookie);
	if (validation.error != 0)
		return io->error = validation.error;
	if (!rtw8723d_trx_live(io, check, cookie))
		return io->error;

#define WRITE(addr, width, data) do { \
	if (rtw8723d_trx_live(io, check, cookie)) \
		rtw88_io_wr(io, (addr), (width), (data)); \
} while (0)
	/* Native owner keeps hardware TX closed across the initialization block. */
	rtw88_io_wr(io, 0x522, 1, 0xff);
	/* SDIO row0: VO/VI NORMAL, BE/BK LOW, MG EXTRA, HI HIGH.
	 * Management descriptors use HIGH QSEL because EXTRA has zero pages. */
	WRITE(0x10c, 2, 0xc5a0);
	WRITE(0x100, 1, 0);
	WRITE(0x100, 1, 0xff);
	if (rtw8723d_trx_live(io, check, cookie))
		(void)rtw88_io_rd(io, 0x10250020, 4);
	WRITE(0x10250000, 4, 0);
	/* 32768/128=256 TX pages, eight driver-reserved pages: boundary248.
	 * HQ12 + NQ2 + LQ2 + EXTRA0 + gap1 + public231 =248. */
	WRITE(0x214, 4, 0x00000002);
	WRITE(0x200, 4, 0x80e7020c);
	WRITE(0x114, 1, 0xf8);
	/* 16KiB RX FIFO minus 128-byte report region and terminal byte. */
	WRITE(0x116, 2, 0x3f7f);
	WRITE(0x209, 1, 0xf8);
	WRITE(0x424, 1, 0xf8);
	WRITE(0x425, 1, 0xf8);
	WRITE(0x45d, 1, 0xf8);
	if (rtw8723d_trx_live(io, check, cookie))
		rtw88_io_set(io, 0x224, 4, 0x10000);
	for (poll = 0; poll < 1000 &&
	    rtw8723d_trx_live(io, check, cookie); poll++) {
		value = rtw88_io_rd(io, 0x224, 4);
		if (!rtw8723d_trx_live(io, check, cookie))
			break;
		if ((value & 0x10000) == 0)
			break;
		rtw88_io_delay(io, 10);
	}
	if (io->error == 0 && poll == 1000)
		io->error = ETIMEDOUT;
	(void)rtw8723d_trx_live(io, check, cookie);
	if (io->error != 0) {
		error = io->error;
		rtw88_io_init(&cleanup, io->ops, io->cookie);
		rtw88_io_wr(&cleanup, 0x522, 1, 0xff);
		io->error = error;
	}
#undef WRITE
	return io->error;
}

/* Invoked after the separate chip MAC table, in the pinned init order. */
int
rtw8723d_trx_post_mac(struct rtw88_io *io, int (*check)(void *), void *cookie)
{
	struct rtw88_io validation, cleanup;
	uint32_t value;
	int error;

	if (io == NULL)
		return EINVAL;
	if (io->error != 0)
		return io->error;
	rtw88_io_init(&validation, io->ops, io->cookie);
	if (validation.error != 0)
		return io->error = validation.error;
	if (!rtw8723d_trx_live(io, check, cookie))
		return io->error;
	/* Native core removes exactly four FCS bytes before net80211 delivery.
	 * Unlike Linux's donor default RCR, require APP_FCS as well as PHYSTS.
	 * Later table/PHY/station RCR writes preserve this high-byte policy. */
	rtw88_io_wr(io, 0x60f, 1, 4);
	if (rtw8723d_trx_live(io, check, cookie))
		rtw88_io_set(io, 0x608, 4, 0x90000000);
	if (rtw8723d_trx_live(io, check, cookie))
		rtw88_io_clr(io, 0x7d4, 4, 0x300);
	if (rtw8723d_trx_live(io, check, cookie))
		(void)rtw88_io_rd(io, 0x10250020, 4);
	if (rtw8723d_trx_live(io, check, cookie)) {
		value = rtw88_io_rd(io, 0x10250000, 4);
		if (rtw8723d_trx_live(io, check, cookie))
			rtw88_io_wr(io, 0x10250000, 4, value & 0xfff8);
	}
	(void)rtw8723d_trx_live(io, check, cookie);
	if (io->error != 0) {
		error = io->error;
		rtw88_io_init(&cleanup, io->ops, io->cookie);
		rtw88_io_wr(&cleanup, 0x522, 1, 0xff);
		io->error = error;
	}
	return io->error;
}
