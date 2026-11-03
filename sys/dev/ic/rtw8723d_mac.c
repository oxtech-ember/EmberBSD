/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright 2024 Fiona Klute */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: Linux adc218676eef25575469234709c2d87185ca223a,
 * rtw88/rtw8723x.c __rtw8723x_mac_init and reg.h/rtw8723x.h.
 * Native fallible register sequencing is AI-assisted. */
#include <dev/ic/rtw8723d_mac.h>
#include <dev/ic/rtw8723d_trx.h>
#include <dev/ic/rtw88_io.h>

static const struct {
	uint32_t address, value;
	unsigned width;
} rtw8723d_mac_setup[] = {
	/* FWHW_TXQ_CTRL + 1: report enable, then TCR configuration. */
	{ 0x0421, 0x1f, 1 },
	{ 0x0604, 0x00003200, 4 },
	/* RXFLTMAP0/1/2 and RCR; unchanged donor filters. */
	{ 0x06a0, 0xffff, 2 },
	{ 0x06a2, 0x0400, 2 },
	{ 0x06a4, 0xffff, 2 },
	{ 0x0608, 0x700060ce, 4 },
	/* INT_MIG, MCUTST_1, MISC_CTRL and 2ND_CCA_CTRL. */
	{ 0x0304, 0, 4 },
	{ 0x01c0, 0, 4 },
	{ 0x0577, 0x03, 1 },
	{ 0x0976, 0, 1 }
};

int
rtw8723d_mac_init(struct rtw88_io *io, int (*check)(void *), void *cookie)
{
	struct rtw88_io validation;
	size_t i;

	if (io == NULL)
		return EINVAL;
	if (io->error != 0)
		return io->error;
	rtw88_io_init(&validation, io->ops, io->cookie);
	if (validation.error != 0)
		return io->error = validation.error;
	for (i = 0; i < sizeof(rtw8723d_mac_setup) /
	    sizeof(rtw8723d_mac_setup[0]); i++) {
		if (check != NULL) {
			io->error = check(cookie);
			if (io->error != 0)
				return io->error;
		}
		rtw88_io_wr(io, rtw8723d_mac_setup[i].address,
		    rtw8723d_mac_setup[i].width, rtw8723d_mac_setup[i].value);
		if (io->error != 0)
			return io->error;
	}
	return 0;
}

int
rtw8723d_mac_configure(struct rtw88_io *io, int (*check)(void *), void *cookie)
{
	struct rtw88_io cleanup;
	int error;

	error = rtw8723d_trx_init(io, check, cookie);
	if (error != 0)
		return error;
	error = rtw8723d_mac_init(io, check, cookie);
	if (error == 0)
		return rtw8723d_trx_post_mac(io, check, cookie);
	/* The table failed between the two TRX stages. Do not resume traffic. */
	rtw88_io_init(&cleanup, io->ops, io->cookie);
	rtw88_io_wr(&cleanup, 0x522, 1, 0xff);
	io->error = error;
	return error;
}
