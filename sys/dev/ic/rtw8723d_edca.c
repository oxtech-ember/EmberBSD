/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: RTL8723D register addresses confirmed by pinned dual-BSD rtw88/reg.h
 * adc218676eef25575469234709c2d87185ca223a. Uses the shared EmberBSD WME
 * encoder; bounded fallible owner programming is AI-assisted. */
#include <dev/ic/rtw8723d_edca.h>
#include <dev/ic/rtw88_hw.h>
#include <dev/ic/rtw88_io.h>

static bool
rtw8723d_edca_live(struct rtw88_io *io, int (*check)(void *), void *cookie)
{
	if (io->error == 0 && check != NULL)
		io->error = check(cookie);
	return io->error == 0;
}

int
rtw8723d_edca_apply(struct rtw88_io *io,
    const struct rtw88_edca_params *params, unsigned slot,
    int (*check)(void *), void *cookie)
{
	static const uint32_t regs[RTW88_WME_NAC] = {
		0x508, 0x50c, 0x504, 0x500
	};
	struct rtw88_io validation, cleanup;
	uint32_t words[RTW88_WME_NAC];
	uint8_t pause;
	unsigned i;
	int error;

	if (io == NULL)
		return EINVAL;
	if (io->error != 0)
		return io->error;
	error = rtw88_edca_encode(params, slot, 10, words);
	if (error != 0)
		return io->error = error;
	rtw88_io_init(&validation, io->ops, io->cookie);
	if (validation.error != 0)
		return io->error = validation.error;
	if (!rtw8723d_edca_live(io, check, cookie))
		return io->error;
	pause = rtw88_io_rd(io, 0x522, 1);
	if (!rtw8723d_edca_live(io, check, cookie))
		return io->error;
	rtw88_io_wr(io, 0x522, 1, 0xff);
	for (i = 0; i < RTW88_WME_NAC &&
	    rtw8723d_edca_live(io, check, cookie); i++)
		rtw88_io_wr(io, regs[i], 4, words[i]);
	if (rtw8723d_edca_live(io, check, cookie))
		rtw88_io_wr(io, 0x522, 1, pause);
	if (!rtw8723d_edca_live(io, check, cookie)) {
		error = io->error;
		rtw88_io_init(&cleanup, io->ops, io->cookie);
		rtw88_io_wr(&cleanup, 0x522, 1, 0xff);
		io->error = error;
	}
	return io->error;
}
