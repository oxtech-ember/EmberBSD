/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
/* Register meanings/widths: dual-BSD Linux rtw88/sec.c enable sequence and
 * rtw8723d.c phy_set_param tail,
 * commit adc218676eef25575469234709c2d87185ca223a. Native Ember software
 * crypto requires clearing these enables instead of the donor's set path.
 * Fallible sequencing/cleanup, AI-assisted. */
#include <dev/ic/rtw8723d_startup.h>
#include <dev/ic/rtw88_io.h>

static bool
rtw8723d_startup_live(struct rtw88_io *io, int (*check)(void *), void *cookie)
{

	if (io->error == 0 && check != NULL)
		io->error = check(cookie);
	return io->error == 0;
}

struct rtw8723d_startup_field {
	uint32_t addr, mask, data;
	unsigned width;
};

static int
rtw8723d_startup_fields(struct rtw88_io *io,
    const struct rtw8723d_startup_field *fields, size_t count,
    int (*check)(void *), void *cookie)
{
	struct rtw88_io validation, cleanup;
	uint32_t value;
	size_t i;
	int error;

	if (io == NULL)
		return EINVAL;
	if (io->error != 0)
		return io->error;
	rtw88_io_init(&validation, io->ops, io->cookie);
	if (validation.error != 0)
		return io->error = validation.error;
	if (!rtw8723d_startup_live(io, check, cookie))
		return io->error;
	rtw88_io_wr(io, 0x522, 1, 0xff);
	for (i = 0; i < count &&
	    rtw8723d_startup_live(io, check, cookie); i++) {
		value = rtw88_io_rd(io, fields[i].addr, fields[i].width);
		if (rtw8723d_startup_live(io, check, cookie))
			rtw88_io_wr(io, fields[i].addr, fields[i].width,
			    (value & ~fields[i].mask) | fields[i].data);
	}
	(void)rtw8723d_startup_live(io, check, cookie);
	if (io->error != 0) {
		error = io->error;
		rtw88_io_init(&cleanup, io->ops, io->cookie);
		rtw88_io_wr(&cleanup, 0x522, 1, 0xff);
		io->error = error;
	}
	return io->error;
}

int
rtw8723d_swcrypto_configure(struct rtw88_io *io,
    int (*check)(void *), void *cookie)
{
	static const struct rtw8723d_startup_field fields[] = {
		/* CR SECURITY_EN, SEC_CONFIG TX/RX/default-key enables. */
		{ 0x100, 0x200, 0, 2 },
		{ 0x680, 0xcf, 0, 2 }
	};

	return rtw8723d_startup_fields(io, fields,
	    sizeof(fields) / sizeof(fields[0]), check, cookie);
}

int
rtw8723d_dm_before_lck(struct rtw88_io *io,
    int (*check)(void *), void *cookie)
{
	static const struct rtw8723d_startup_field fields[] = {
		{ 0x20c, 0x200, 0x200, 2 }
	};

	return rtw8723d_startup_fields(io, fields,
	    sizeof(fields) / sizeof(fields[0]), check, cookie);
}

int
rtw8723d_dm_after_lck(struct rtw88_io *io,
    int (*check)(void *), void *cookie)
{
	static const struct rtw8723d_startup_field fields[] = {
		/* Ordered BB low-byte writes, preserving bits31:8. */
		{ 0xc50, 0xff, 0x50, 4 },
		{ 0xc50, 0xff, 0x20, 4 }
	};

	return rtw8723d_startup_fields(io, fields,
	    sizeof(fields) / sizeof(fields[0]), check, cookie);
}
