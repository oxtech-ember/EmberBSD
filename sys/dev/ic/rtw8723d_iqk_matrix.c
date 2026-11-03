/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright 2024 Fiona Klute. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: Linux rtw88 adc218676eef25575469234709c2d87185ca223a; native adaptation. */
/* Pinned Linux adc218676eef25575469234709c2d87185ca223a rtw88/rtw8723d.c
 * IQK fill_s1_matrix/fill_s0_matrix and rtw8723x.h fixed-point helpers.
 * Native preflight and defined signed arithmetic, AI-assisted. */
#include <dev/ic/rtw8723d_iqk.h>
#include <dev/ic/rtw88_io.h>

static int
rtw8723d_iqk_signed(uint16_t value)
{
	return value >= 0x200 ? (int)value - 0x400 : value;
}

static uint32_t
rtw8723d_iqk_product(uint16_t value, uint32_t swing, uint32_t *extra)
{
	int product = rtw8723d_iqk_signed(value) * (int)swing;
	int quotient = product / 256;

	/* Emulate the donor's arithmetic shift, including negative fractions,
	 * without shifting signed negatives or overflowing a signed left shift. */
	if (product < 0 && product % 256 != 0)
		quotient--;
	*extra = ((uint32_t)product >> 7) & 1;
	return (uint32_t)quotient;
}

static int
rtw8723d_iqk_matrix_check(struct rtw88_io *io, int (*check)(void *), void *cookie)
{
	if (io->error == 0 && check != NULL)
		io->error = check(cookie);
	return io->error;
}

static void
rtw8723d_iqk_matrix_field(struct rtw88_io *io, uint32_t address,
    uint32_t mask, uint32_t value, int (*check)(void *), void *cookie)
{
	if (rtw8723d_iqk_matrix_check(io, check, cookie) == 0)
		rtw88_io_wr_mask(io, address, mask, value);
}

int
rtw8723d_iqk_matrix_apply(struct rtw88_io *io,
    const struct rtw8723d_iqk_matrix *matrix, int (*check)(void *), void *cookie)
{
	struct rtw88_io validation;
	struct rtw8723d_iqk_matrix saved;
	uint32_t swing[2], a[2], c[2], a_extra[2], c_extra[2];
	unsigned i;
	int y;

	if (io == NULL)
		return EINVAL;
	if (io->error != 0)
		return io->error;
	if (matrix == NULL)
		return io->error = EINVAL;
	rtw88_io_init(&validation, io->ops, io->cookie);
	if (validation.error != 0)
		return io->error = validation.error;
	saved = *matrix;
	for (i = 0; i < 2; i++) {
		if (saved.tx[i].x > 0x3ff || saved.tx[i].y > 0x3ff ||
		    saved.rx[i].x > 0x3ff || saved.rx[i].y > 0x3ff)
			return io->error = EINVAL;
		y = rtw8723d_iqk_signed(saved.rx[i].y);
		if (y < 0)
			y = -y;
		if (saved.tx[i].x == 0 || saved.tx[i].x == 0x142 ||
		    saved.tx[i].y == 0x042 || saved.rx[i].x <= 0x0e6 ||
		    saved.rx[i].x >= 0x11a || y >= 0x1a)
			return io->error = EINVAL;
	}
	if (rtw8723d_iqk_matrix_check(io, check, cookie) != 0)
		return io->error;
	swing[0] = rtw88_io_rd_mask(io, 0x0c80, 0xffc00000);
	if (rtw8723d_iqk_matrix_check(io, check, cookie) != 0)
		return io->error;
	swing[1] = rtw88_io_rd_mask(io, 0x0cd4, 0x007fe000);
	if (rtw8723d_iqk_matrix_check(io, check, cookie) != 0)
		return io->error;
	if (swing[0] == 0 || swing[1] == 0)
		return io->error = EPROTO;
	for (i = 0; i < 2; i++) {
		a[i] = rtw8723d_iqk_product(saved.tx[i].x, swing[i], &a_extra[i]);
		c[i] = rtw8723d_iqk_product(saved.tx[i].y, swing[i], &c_extra[i]);
	}

#define FIELD(addr, mask, value) \
	rtw8723d_iqk_matrix_field(io, (addr), (mask), (value), check, cookie)
	/* Antenna path S1. Preserve independent register fields. */
	FIELD(0x0c80, 0x000003ff, a[0]);
	FIELD(0x0c4c, 0x80000000, a_extra[0]);
	FIELD(0x0c94, 0xf0000000, (c[0] & 0x3c0) >> 6);
	FIELD(0x0c80, 0x003f0000, c[0] & 0x3f);
	FIELD(0x0c4c, 0x20000000, c_extra[0]);
	FIELD(0x0c14, 0x000003ff, saved.rx[0].x);
	FIELD(0x0c14, 0x0000fc00, saved.rx[0].y & 0x3f);
	FIELD(0x0ca0, 0xf0000000, (saved.rx[0].y >> 6) & 15);
	/* Antenna path S0; not a second transmitted spatial stream. */
	FIELD(0x0cd0, 0x000007fe, a[1]);
	FIELD(0x0cd0, 0x00000001, a_extra[1]);
	FIELD(0x0cd4, 0x000007fe, c[1]);
	FIELD(0x0cd4, 0x00000001, c_extra[1]);
	FIELD(0x0cd8, 0x000003ff, saved.rx[1].x);
	FIELD(0x0cd8, 0x003ff000, saved.rx[1].y);
#undef FIELD
	return rtw8723d_iqk_matrix_check(io, check, cookie);
}
