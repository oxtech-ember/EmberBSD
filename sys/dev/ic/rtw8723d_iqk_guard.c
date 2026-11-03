/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright 2024 Fiona Klute. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: Linux rtw88 adc218676eef25575469234709c2d87185ca223a; native adaptation. */
/* Pinned Linux adc218676eef25575469234709c2d87185ca223a:
 * rtw8723x.h IQK path/LTE backup/config/restore; rtw8723d.c IQK grant and
 * final BB selection restore. Native fallible ownership, AI-assisted. */
#include <dev/ic/rtw8723d_iqk_guard.h>
#include <dev/ic/rtw8723d_boot.h>
#include <dev/ic/rtw88_io.h>

static int
rtw8723d_iqk_guard_check(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops)
{
	if (io->error == 0 && ops->cancelled(ops->cookie))
		io->error = ECANCELED;
	return io->error;
}

static int
rtw8723d_iqk_restore(struct rtw88_io *io, uint32_t address,
    unsigned width, uint32_t value)
{
	struct rtw88_io cleanup;

	rtw88_io_init(&cleanup, io->ops, io->cookie);
	rtw88_io_wr(&cleanup, address, width, value);
	return cleanup.error;
}

int
rtw8723d_iqk_guard_release(struct rtw88_io *io,
    struct rtw8723d_iqk_guard *guard)
{
	int first = 0, error, original;

	if (io == NULL || guard == NULL)
		return EINVAL;
	if (!guard->held)
		return io->error;
	original = io->error;
	/* Do not commit stale WRITE_DATA after a failed prerequisite write. */
	error = rtw8723d_iqk_restore(io, 0x07c4, 4, guard->lte_grant);
	if (error == 0)
		error = rtw8723d_iqk_restore(io, 0x07c0, 4, 0xc00f0038);
	first = error;
	error = rtw8723d_iqk_restore(io, 0x0070, 4, guard->lte_path);
	if (first == 0)
		first = error;
	error = rtw8723d_iqk_restore(io, 0x0067, 1, guard->btg_select);
	if (first == 0)
		first = error;
	error = rtw8723d_iqk_restore(io, 0x0948, 4, guard->bb_select);
	if (first == 0)
		first = error;
	if (first == 0)
		guard->held = false;
	io->error = original != 0 ? original : first;
	return io->error;
}

int
rtw8723d_iqk_guard_acquire(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops, struct rtw8723d_iqk_guard *guard)
{
	struct rtw88_io validation;
	struct rtw8723d_iqk_guard saved = { 0 };

	if (io == NULL)
		return EINVAL;
	if (io->error != 0)
		return io->error;
	if (guard == NULL || ops == NULL || ops->wait_ms == NULL ||
	    ops->cancelled == NULL)
		return io->error = EINVAL;
	if (guard->held)
		return io->error = EBUSY;
	rtw88_io_init(&validation, io->ops, io->cookie);
	if (validation.error != 0)
		return io->error = validation.error;
	if (rtw8723d_iqk_guard_check(io, ops) != 0)
		return io->error;
	saved.btg_select = rtw88_io_rd(io, 0x0067, 1);
	if (rtw8723d_iqk_guard_check(io, ops) != 0)
		return io->error;
	saved.bb_select = rtw88_io_rd(io, 0x0948, 4);
	if (rtw8723d_iqk_guard_check(io, ops) != 0)
		return io->error;
	saved.lte_path = rtw88_io_rd(io, 0x0070, 4);
	if (rtw8723d_iqk_guard_check(io, ops) != 0)
		return io->error;
	rtw88_io_wr(io, 0x07c0, 4, 0x800f0038);
	/* CTRL is a command trigger, not saved configuration: replaying an
	 * old value could issue another indirect command. This read command
	 * changes neither LTE grant nor path selection. Even an uncertain host
	 * write or cancellation must observe its bounded settling interval. */
	ops->wait_ms(ops->cookie, 1);
	if (rtw8723d_iqk_guard_check(io, ops) != 0)
		return io->error;
	saved.lte_grant = rtw88_io_rd(io, 0x07c8, 4);
	if (rtw8723d_iqk_guard_check(io, ops) != 0)
		return io->error;
	saved.held = true;
	*guard = saved; /* Publish restoration ownership before the first change. */
	rtw88_io_wr_mask(io, 0x0064, UINT32_C(1) << 31, 1);
	if (rtw8723d_iqk_guard_check(io, ops) == 0)
		rtw88_io_wr(io, 0x07c4, 4, 0x0000ff00);
	if (rtw8723d_iqk_guard_check(io, ops) == 0)
		rtw88_io_wr(io, 0x07c0, 4, 0xc0020038);
	if (rtw8723d_iqk_guard_check(io, ops) == 0)
		rtw88_io_wr_mask(io, 0x0070, UINT32_C(1) << 26, 1);
	if (rtw8723d_iqk_guard_check(io, ops) != 0)
		return rtw8723d_iqk_guard_release(io, guard);
	return 0;
}
