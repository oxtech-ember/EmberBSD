/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright 2024 Fiona Klute. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: Linux rtw88 adc218676eef25575469234709c2d87185ca223a; native adaptation. */
/* Pinned Linux adc218676eef25575469234709c2d87185ca223a rtw88:
 * rtw8723d.c IQK TX/RX path and one-shot programming; rtw8723x.h/reg.h.
 * Native fallible/cancellable sequencing and bounded cleanup, AI-assisted. */
#include <dev/ic/rtw8723d_iqk.h>
#include <dev/ic/rtw8723d_iqk_guard.h>
#include <dev/ic/rtw8723d_boot.h>
#include <dev/ic/rtw8723d_rf.h>
#include <dev/ic/rtw88_io.h>

struct rtw8723d_iqk_path_config {
	uint32_t select, tx_pi, wlint, wlsel, trigger;
	uint8_t lutwe, padlut, gaintx, bspad;
};

static const struct rtw8723d_iqk_path_config rtw8723d_iqk_paths[2] = {
	{ 0x99000000, 0x8214019f, 0xe0d, 0x60d, 0xfa000000,
	    0xef, 0xdf, 0x56, 0x54 }, /* S1 antenna switch */
	{ 0x99000280, 0x8214018a, 0xe6d, 0x66d, 0xf9000000,
	    0xee, 0xde, 0x66, 0x64 }  /* S0 antenna switch */
};

static int
rtw8723d_iqk_path_check(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops)
{
	if (io->error == 0 && ops->cancelled(ops->cookie))
		io->error = ECANCELED;
	return io->error;
}

static void
rtw8723d_iqk_path_write(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops, uint32_t addr, uint32_t value)
{
	if (rtw8723d_iqk_path_check(io, ops) == 0)
		rtw88_io_wr(io, addr, 4, value);
}

static void
rtw8723d_iqk_path_rf(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops, unsigned addr, uint32_t mask,
    uint32_t value)
{
	if (rtw8723d_iqk_path_check(io, ops) == 0)
		(void)rtw8723d_rf_write(io, 0, addr, mask, value);
}

static void
rtw8723d_iqk_path_reset(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops, bool settle)
{
	if (rtw8723d_iqk_path_check(io, ops) != 0)
		return;
	rtw88_io_wr_mask(io, 0x0e28, 0xffffff00, 0);
	/* Observe settling even when the host cannot confirm the write. */
	if (settle)
		ops->wait_ms(ops->cookie, 1);
}

static int
rtw8723d_iqk_path_shot(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops,
    const struct rtw8723d_iqk_path_config *cfg, bool rx_trigger, bool rx_report,
    struct rtw8723d_iqk_result *result)
{
	if (rtw8723d_iqk_path_check(io, ops) != 0)
		return io->error;
	rtw88_io_wr_mask(io, 0x0e28, 0xffffff00, 0x808000);
	rtw8723d_iqk_path_write(io, ops, 0x07c4, 0x0000ff00);
	rtw8723d_iqk_path_write(io, ops, 0x07c0, 0xc0020038);
	if (rtw8723d_iqk_path_check(io, ops) == 0)
		rtw88_io_wr_mask(io, 0x0070, UINT32_C(1) << 26, 1);
	if (rtw8723d_iqk_path_check(io, ops) != 0)
		return io->error;
	/* Preserve the donor's indirect-query settling before the trigger. */
	rtw88_io_wr(io, 0x07c0, 4, 0x800f0054);
	ops->wait_ms(ops->cookie, 1);
	rtw8723d_iqk_path_write(io, ops, 0x0e48,
	    rx_trigger ? 0xf9000000 : cfg->trigger);
	rtw8723d_iqk_path_write(io, ops, 0x0e48, 0xf8000000);
	if (rtw8723d_iqk_path_check(io, ops) != 0)
		return io->error;
	return rtw8723d_iqk_result_read(io, ops, rx_report, result);
}

static void
rtw8723d_iqk_path_cleanup(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops,
    const struct rtw8723d_iqk_path_config *cfg,
    struct rtw8723d_iqk_guard *guard)
{
	struct rtw88_io cleanup;
	int first;

	(void)rtw8723d_iqk_guard_release(io, guard);
	first = io->error;
	rtw88_io_init(&cleanup, io->ops, io->cookie);
	rtw88_io_wr_mask(&cleanup, 0x0e28, 0xffffff00, 0);
	ops->wait_ms(ops->cookie, 1);
	if (first == 0)
		first = cleanup.error;
#define CLEAR_RF(address, mask) do { \
	rtw88_io_init(&cleanup, io->ops, io->cookie); \
	(void)rtw8723d_rf_write(&cleanup, 0, (address), (mask), 0); \
	if (first == 0) \
		first = cleanup.error; \
} while (0)
	/* A failed/uncertain LUT write must not leave table programming enabled. */
	CLEAR_RF(cfg->lutwe, 0x80000);
	CLEAR_RF(cfg->padlut, 0x800);
	CLEAR_RF(0x01, 1);
	CLEAR_RF(0x02, 1);
#undef CLEAR_RF
	io->error = first;
}

int
rtw8723d_iqk_path_measure(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops, unsigned path, bool receive,
    struct rtw8723d_iqk_result *out)
{
	struct rtw88_io validation;
	struct rtw8723d_iqk_guard guard = { 0 };
	struct rtw8723d_iqk_result result;
	const struct rtw8723d_iqk_path_config *cfg;
	int error;

	if (io == NULL)
		return EINVAL;
	if (io->error != 0)
		return io->error;
	if (ops == NULL || ops->wait_ms == NULL || ops->cancelled == NULL ||
	    out == NULL || path >= 2)
		return io->error = EINVAL;
	rtw88_io_init(&validation, io->ops, io->cookie);
	if (validation.error != 0)
		return io->error = validation.error;
	cfg = &rtw8723d_iqk_paths[path];
	error = rtw8723d_iqk_guard_acquire(io, ops, &guard);
	if (error != 0)
		return error;

#define W(addr, value) rtw8723d_iqk_path_write(io, ops, (addr), (value))
#define R(addr, mask, value) \
	rtw8723d_iqk_path_rf(io, ops, (addr), (mask), (value))
	W(0x0948, cfg->select);
	rtw8723d_iqk_path_reset(io, ops, !receive);
	if (receive) {
		W(0x0e40, 0x01007c00);
		W(0x0e44, 0x01004800);
		W(0x0e30, 0x18008c1c);
		W(0x0e34, 0x38008c1c);
		W(0x0e50, 0x38008c1c);
		W(0x0e54, 0x38008c1c);
		W(0x0e38, 0x82160000);
		W(0x0e3c, 0x28160000);
		W(0x0e4c, 0x0046a911);
	}
	R(cfg->lutwe, 0xfffff, 0x80000);
	R(0x33, 0xfffff, receive ? 6 : 4);
	R(0x3e, 0xfffff, receive ? 0x5f : 0x5d);
	R(0x3f, 0xfffff, receive ? 0xa7ffb : 0xbffe0);
	R(cfg->lutwe, 0xfffff, 0);
	if (!receive) {
		W(0x0e30, 0x08008c0c);
		W(0x0e34, 0x38008c1c);
		W(0x0e38, cfg->tx_pi);
		W(0x0e3c, 0x28160200);
		W(0x0e40, 0x01007c00);
		W(0x0e44, 0x01004800);
		W(0x0e4c, 0x00462911);
	}
	R(cfg->padlut, 0x800, 1);
	R(cfg->gaintx, 0x600, 0);
	if (!receive) {
		R(cfg->gaintx, 0x1e0, 3);
		R(0x8d, 0x1f, 0xf);
		R(cfg->lutwe, 0x10, 1);
		R(cfg->bspad, 1, 1);
	}
	R(0x01, 0xfffff, cfg->wlint);
	R(0x02, 0xfffff, cfg->wlsel);
	/* RX step one measures TX too, using the RX-specific trigger value. */
	error = rtw8723d_iqk_path_shot(io, ops, cfg, receive, false, &result);
	if (receive && error == 0) {
		W(0x0e40, 0x80007c00 | ((uint32_t)result.x << 16) | result.y);
		W(0x0e44, 0x01004800);
		W(0x0e30, 0x38008c1c);
		W(0x0e34, 0x18008c1c);
		W(0x0e50, 0x38008c1c);
		W(0x0e54, 0x38008c1c);
		W(0x0e38, 0x82170000);
		W(0x0e3c, 0x28171400);
		W(0x0e4c, 0x0046a8d1);
		rtw8723d_iqk_path_reset(io, ops, true);
		R(cfg->lutwe, 0x80000, 1);
		R(0x33, 0xfffff, 7);
		R(0x3e, 0xfffff, 0x5f);
		R(0x3f, 0xfffff, 0xb3fdb);
		R(cfg->lutwe, 0xfffff, 0);
		error = rtw8723d_iqk_path_shot(io, ops, cfg, true, true, &result);
	}
#undef R
#undef W
	rtw8723d_iqk_path_cleanup(io, ops, cfg, &guard);
	if (rtw8723d_iqk_path_check(io, ops) != 0)
		return io->error;
	if (error != 0)
		return error;
	*out = result;
	return 0;
}
