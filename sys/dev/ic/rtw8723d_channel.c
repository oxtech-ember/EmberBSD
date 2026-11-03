/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright 2024 Fiona Klute. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: Linux rtw88 adc218676eef25575469234709c2d87185ca223a; native adaptation. */
/* Linux adc218676eef25575469234709c2d87185ca223a rtw88/rtw8723d.c
 * set_channel_rf/bb, spur_cal/cfg_notch; rtw8723x.h/reg.h constants.
 * mac.c rtw_set_channel_mac selected 11N/20MHz branch from pinned upstream
 * blob (web source, not a locally hashed donor file). Native bounded
 * cancellation/PSD control restoration, AI-assisted. */
#include <dev/ic/rtw8723d_channel.h>
#include <dev/ic/rtw8723d_boot.h>
#include <dev/ic/rtw8723d_rf.h>
#include <dev/ic/rtw88_io.h>

static bool
rtw8723d_channel_live(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops)
{
	if (io->error == 0 && ops->cancelled(ops->cookie))
		io->error = ECANCELED;
	return io->error == 0;
}

static void
rtw8723d_channel_write(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops, uint32_t address, uint32_t value)
{
	if (rtw8723d_channel_live(io, ops))
		rtw88_io_wr(io, address, 4, value);
}

static void
rtw8723d_channel_mask(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops, uint32_t address,
    uint32_t mask, uint32_t value)
{
	if (rtw8723d_channel_live(io, ops))
		rtw88_io_wr_mask(io, address, mask, value);
}

static void
rtw8723d_channel_psd_restore(struct rtw88_io *io, uint32_t control,
    uint32_t analog)
{
	struct rtw88_io cleanup;
	int first = io->error;

	/* Stop the measurement before re-enabling the saved three-wire state.
	 * Failed stop must not suppress the independent analog restore attempt. */
	rtw88_io_init(&cleanup, io->ops, io->cookie);
	rtw88_io_wr(&cleanup, 0x808, 4, control);
	if (first == 0)
		first = cleanup.error;
	rtw88_io_init(&cleanup, io->ops, io->cookie);
	rtw88_io_wr(&cleanup, 0x88c, 4, analog);
	if (first == 0)
		first = cleanup.error;
	io->error = first;
}

int
rtw8723d_channel20(struct rtw88_io *io,
    const struct rtw8723d_boot_ops *ops, unsigned channel)
{
	struct rtw88_io validation;
	uint32_t rf_a, rf_b, analog = 0, psd_control = 0, report = 0;
	bool notch = false;
	unsigned i;

	if (io == NULL)
		return EINVAL;
	if (io->error != 0)
		return io->error;
	if (ops == NULL || ops->wait_ms == NULL || ops->cancelled == NULL)
		return io->error = EINVAL;
	if (channel < 1 || channel > 13)
		return io->error = EOPNOTSUPP;
	rtw88_io_init(&validation, io->ops, io->cookie);
	if (validation.error != 0)
		return io->error = validation.error;
	if (!rtw8723d_channel_live(io, ops))
		return io->error;
	if (channel == 13) {
		analog = rtw88_io_rd(io, 0x88c, 4);
		if (!rtw8723d_channel_live(io, ops))
			return io->error;
		psd_control = rtw88_io_rd(io, 0x808, 4);
		if (!rtw8723d_channel_live(io, ops))
			return io->error;
		if ((psd_control & 0x400000) != 0)
			return io->error = EBUSY;
	}
	(void)rtw8723d_rf_read(io, 0, 0x18, 0xfffff, &rf_a);
	if (!rtw8723d_channel_live(io, ops))
		return io->error;
	(void)rtw8723d_rf_read(io, 1, 0x18, 0xfffff, &rf_b);
	if (!rtw8723d_channel_live(io, ops))
		return io->error;
	/* Preserve the donor's asymmetric bandwidth programming: path A only. */
	rf_a = (rf_a & ~UINT32_C(0xcff)) | 0xc00 | channel;
	rf_b = (rf_b & ~UINT32_C(0xff)) | channel;
	(void)rtw8723d_rf_write(io, 0, 0x18, 0xfffff, rf_a);
	if (rtw8723d_channel_live(io, ops))
		(void)rtw8723d_rf_write(io, 1, 0x18, 0xfffff, rf_b);
	if (!rtw8723d_channel_live(io, ops))
		return io->error;

#define W(addr, value) rtw8723d_channel_write(io, ops, (addr), (value))
#define M(addr, mask, value) rtw8723d_channel_mask(io, ops, (addr), (mask), (value))
	if (channel == 13) {
		/* From this first attempted write on, restore saved controls even
		 * when the bus cannot confirm whether a command was committed. */
		rtw88_io_wr(io, 0x88c, 4, 0xccf000c0);
		W(0x808, 0xfccd);
		W(0x808, 0x400000 | 0xfccd);
		if (rtw8723d_channel_live(io, ops)) {
			ops->wait_ms(ops->cookie, 30);
			if (rtw8723d_channel_live(io, ops))
				report = rtw88_io_rd(io, 0x8b4, 4);
		}
		rtw8723d_channel_psd_restore(io, psd_control, analog);
		if (!rtw8723d_channel_live(io, ops))
			return io->error;
		notch = report >= 0x16;
	}
	M(0xc40, 0x1f000000, notch ? 0xb : 0x1f);
	M(0xc40, 0x200, notch ? 1 : 0);
	for (i = 0; i < 4; i++)
		W(0xd40 + i * 4, notch && i == 0 ? 0x04000000 : 0);
	M(0xd2c, 0x10000000, notch ? 1 : 0);
	/* 11N MAC: no 8822C clock/timebase or 5GHz CCK controls. */
	if (rtw8723d_channel_live(io, ops))
		rtw88_io_wr(io, 0x483, 1, 0);
	M(0x668, 0x180, 0);
	W(0xa24, 0x64b80c1c);
	W(0xa28, 0x00008810);
	W(0xaac, 0x01235667);
	M(0x800, 1, 0);
	M(0x900, 1, 0);
	M(0x954, 0x80000, 1);
	M(0x954, 0x0f000000, 0xa);
#undef M
#undef W
	(void)rtw8723d_channel_live(io, ops);
	return io->error;
}
