/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright 2024 Fiona Klute. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: Linux rtw88 adc218676eef25575469234709c2d87185ca223a; native adaptation. */
/* Linux adc218676eef25575469234709c2d87185ca223a rtw88/rtw8723d.c
 * IQK round/preconfiguration and rtw8723x.c backup/restore/tolerance.
 * Native fallible ownership; reject incomplete/default/hybrid results.
 * AI-assisted. */
#include <dev/ic/rtw8723d_iqk_cal.h>
#include <dev/ic/rtw8723d_boot.h>
#include <dev/ic/rtw8723d_rf.h>
#include <dev/ic/rtw88_io.h>

static const uint32_t rtw8723d_iqk_adda[] = {
	0x85c, 0xe6c, 0xe70, 0xe74, 0xe78, 0xe7c, 0xe80, 0xe84,
	0xe88, 0xe8c, 0xed0, 0xed4, 0xed8, 0xedc, 0xee0, 0xeec
};
static const uint32_t rtw8723d_iqk_bb[] = {
	0xc04, 0xc08, 0x874, 0xb68, 0xb6c, 0x870, 0x860, 0x864, 0xa04
};
static const uint32_t rtw8723d_iqk_mac8[] = { 0x522, 0x550, 0x551 };
struct rtw8723d_iqk_saved {
	uint32_t adda[16], bb[9], mac32;
	uint8_t mac8[3], igi[2];
};

static int
rtw8723d_iqk_cal_check(void *cookie)
{
	const struct rtw8723d_boot_ops * const *owner_ops = cookie;
	const struct rtw8723d_boot_ops *ops = *owner_ops;

	return ops->cancelled(ops->cookie) ? ECANCELED : 0;
}

static bool
rtw8723d_iqk_cal_live(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops)
{
	if (io->error == 0)
		io->error = rtw8723d_iqk_cal_check(&ops);
	return io->error == 0;
}

static int
rtw8723d_iqk_cal_save(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops, struct rtw8723d_iqk_saved *saved)
{
	unsigned i;

	for (i = 0; i < 16 && rtw8723d_iqk_cal_live(io, ops); i++)
		saved->adda[i] = rtw88_io_rd(io, rtw8723d_iqk_adda[i], 4);
	for (i = 0; i < 3 && rtw8723d_iqk_cal_live(io, ops); i++)
		saved->mac8[i] = rtw88_io_rd(io, rtw8723d_iqk_mac8[i], 1);
	if (rtw8723d_iqk_cal_live(io, ops))
		saved->mac32 = rtw88_io_rd(io, 0x40, 4);
	for (i = 0; i < 9 && rtw8723d_iqk_cal_live(io, ops); i++)
		saved->bb[i] = rtw88_io_rd(io, rtw8723d_iqk_bb[i], 4);
	for (i = 0; i < 2 && rtw8723d_iqk_cal_live(io, ops); i++)
		saved->igi[i] = rtw88_io_rd_mask(io, i == 0 ? 0xc50 : 0xc58, 0xff);
	(void)rtw8723d_iqk_cal_live(io, ops);
	return io->error;
}

static void
rtw8723d_iqk_cal_restore(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops, const struct rtw8723d_iqk_saved *saved)
{
	struct rtw88_io cleanup;
	int first = io->error;
	unsigned i;

	/* The original pause command can itself have an uncertain outcome.
	 * Try to keep TX closed before restoring other state, independently. */
	rtw88_io_init(&cleanup, io->ops, io->cookie);
	rtw88_io_wr(&cleanup, 0x522, 1, 0xff);
	if (first == 0)
		first = cleanup.error;

	/* Also covers a failed prepath before a measurement took ownership. */
	rtw88_io_init(&cleanup, io->ops, io->cookie);
	rtw88_io_wr_mask(&cleanup, 0xe28, 0xffffff00, 0);
	ops->wait_ms(ops->cookie, 1);
	if (first == 0)
		first = cleanup.error;

/* Independent bounded attempts; cancellation never suppresses cleanup. */
#define RESTORE(addr, width, value) do { \
	rtw88_io_init(&cleanup, io->ops, io->cookie); \
	rtw88_io_wr(&cleanup, (addr), (width), (value)); \
	if (first == 0) first = cleanup.error; \
} while (0)
	for (i = 0; i < 16; i++)
		RESTORE(rtw8723d_iqk_adda[i], 4, saved->adda[i]);
	/* TXPAUSE is deliberately restored only after matrix success. */
	for (i = 1; i < 3; i++)
		RESTORE(rtw8723d_iqk_mac8[i], 1, saved->mac8[i]);
	RESTORE(0x40, 4, saved->mac32);
	for (i = 0; i < 9; i++)
		RESTORE(rtw8723d_iqk_bb[i], 4, saved->bb[i]);
	for (i = 0; i < 2; i++) {
		/* Keep each IGI strobe and restore ordered: failed setup must not
		 * prevent a separate attempt to return the original field. */
		rtw88_io_init(&cleanup, io->ops, io->cookie);
		rtw88_io_wr_mask(&cleanup, i == 0 ? 0xc50 : 0xc58, 0xff, 0x50);
		if (first == 0) first = cleanup.error;
		rtw88_io_init(&cleanup, io->ops, io->cookie);
		rtw88_io_wr_mask(&cleanup, i == 0 ? 0xc50 : 0xc58, 0xff, saved->igi[i]);
		if (first == 0) first = cleanup.error;
	}
	RESTORE(0xe30, 4, 0x01008c00);
	RESTORE(0xe34, 4, 0x01008c00);
#undef RESTORE
	io->error = first;
}

static void
rtw8723d_iqk_cal_write(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops, uint32_t addr, uint32_t value)
{
	if (rtw8723d_iqk_cal_live(io, ops))
		rtw88_io_wr(io, addr, 4, value);
}

static void
rtw8723d_iqk_cal_adda_on(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops)
{
	unsigned i;

	for (i = 0; i < 16; i++)
		rtw8723d_iqk_cal_write(io, ops, rtw8723d_iqk_adda[i], 0x03c00016);
}

static void
rtw8723d_iqk_cal_standby(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops, unsigned rf_path)
{
	if (!rtw8723d_iqk_cal_live(io, ops))
		return;
	rtw88_io_wr_mask(io, 0xe28, 0xffffff00, 0);
	ops->wait_ms(ops->cookie, 1);
	if (rtw8723d_iqk_cal_live(io, ops))
		(void)rtw8723d_rf_write(io, rf_path, 0, 0xfffff, 0x10000);
	if (rtw8723d_iqk_cal_live(io, ops))
		rtw88_io_wr_mask(io, 0xe28, 0xffffff00, 0x808000);
}

static void
rtw8723d_iqk_cal_prepath(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops, unsigned path)
{
	if (path == 1) {
		rtw8723d_iqk_cal_standby(io, ops, 0);
		rtw8723d_iqk_cal_adda_on(io, ops);
	}
	if (rtw8723d_iqk_cal_live(io, ops))
		rtw88_io_wr_mask(io, 0xe28, 0xffffff00, 0x808000);
	rtw8723d_iqk_cal_write(io, ops, 0xe40, 0x01007c00);
	rtw8723d_iqk_cal_write(io, ops, 0xe44, 0x01004800);
	if (path == 0) {
		rtw8723d_iqk_cal_standby(io, ops, 1);
		rtw8723d_iqk_cal_adda_on(io, ops);
	}
}

static int
rtw8723d_iqk_cal_round(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops, struct rtw8723d_iqk_matrix *result)
{
	unsigned path, receive, attempt;
	bool complete = true;
	int error = 0;

	if (rtw8723d_iqk_cal_live(io, ops))
		rtw88_io_wr(io, 0x522, 1, 0xff);
	rtw8723d_iqk_cal_adda_on(io, ops);
	if (rtw8723d_iqk_cal_live(io, ops))
		rtw88_io_wr_mask(io, 0xa04, 0x0f000000, 0xf);
	rtw8723d_iqk_cal_write(io, ops, 0xc04, 0x03a05611);
	rtw8723d_iqk_cal_write(io, ops, 0xc08, 0x000800e4);
	rtw8723d_iqk_cal_write(io, ops, 0x874, 0x25204200);
	for (path = 0; path < 2 && io->error == 0; path++) {
		rtw8723d_iqk_cal_prepath(io, ops, path);
		for (receive = 0; receive < 2 && io->error == 0; receive++) {
			for (attempt = 0; attempt < 2; attempt++) {
				error = rtw8723d_iqk_path_measure(io, ops, path,
				    receive != 0, receive ? &result->rx[path] : &result->tx[path]);
				if (error != EAGAIN)
					break;
			}
			if (error != 0 && error != EAGAIN)
				return error;
			if (error == EAGAIN)
				complete = false;
		}
	}
	if (io->error != 0)
		return io->error;
	return complete ? 0 : EAGAIN;
}

static bool
rtw8723d_iqk_cal_similar(const struct rtw8723d_iqk_matrix *a,
    const struct rtw8723d_iqk_matrix *b)
{
	unsigned path, receive, component;
	const struct rtw8723d_iqk_result *p, *q;
	int x, y, diff;

	for (path = 0; path < 2; path++) {
		for (receive = 0; receive < 2; receive++) {
			p = receive ? &a->rx[path] : &a->tx[path];
			q = receive ? &b->rx[path] : &b->tx[path];
			for (component = 0; component < 2; component++) {
				x = component ? p->y : p->x;
				y = component ? q->y : q->x;
				if (x >= 0x200) x -= 0x400;
				if (y >= 0x200) y -= 0x400;
				diff = x - y;
				if (diff < -5 || diff > 5)
					return false;
			}
		}
	}
	return true;
}

int
rtw8723d_iqk_calibrate(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops, struct rtw8723d_iqk_matrix *out)
{
	struct rtw88_io validation;
	struct rtw8723d_iqk_saved saved;
	struct rtw8723d_iqk_matrix rounds[3] = { 0 };
	bool complete[3] = { false, false, false };
	unsigned i, j, selected = 3;
	int error;

	if (io == NULL)
		return EINVAL;
	if (io->error != 0)
		return io->error;
	if (ops == NULL || ops->wait_ms == NULL || ops->cancelled == NULL || out == NULL)
		return io->error = EINVAL;
	rtw88_io_init(&validation, io->ops, io->cookie);
	if (validation.error != 0)
		return io->error = validation.error;
	if (rtw8723d_iqk_cal_save(io, ops, &saved) != 0)
		return io->error;
	for (i = 0; i < 3; i++) {
		error = rtw8723d_iqk_cal_round(io, ops, &rounds[i]);
		complete[i] = error == 0;
		if (error != 0 && error != EAGAIN && io->error == 0)
			io->error = error;
		/* Unlike the donor's first-round shortcut, restore each round.
		 * This also covers partial preconfiguration and aborted attempts. */
		rtw8723d_iqk_cal_restore(io, ops, &saved);
		if (!rtw8723d_iqk_cal_live(io, ops))
			return io->error;
		if (error != 0 && error != EAGAIN)
			return io->error = error;
		if (!complete[i])
			continue;
		for (j = 0; j < i; j++) {
			if (complete[j] && rtw8723d_iqk_cal_similar(&rounds[j], &rounds[i])) {
				selected = j;
				break;
			}
		}
		if (selected != 3)
			break;
	}
	if (selected == 3)
		return EAGAIN; /* TX remains paused; no fabricated coefficients. */
	error = rtw8723d_iqk_matrix_apply(io, &rounds[selected],
	    rtw8723d_iqk_cal_check, &ops);
	if (error != 0 || !rtw8723d_iqk_cal_live(io, ops))
		return io->error != 0 ? io->error : error;
	rtw88_io_wr(io, 0x522, 1, saved.mac8[0]);
	if (io->error != 0)
		return io->error;
	*out = rounds[selected];
	return 0;
}
