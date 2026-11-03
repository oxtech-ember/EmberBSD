/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright 2024 Fiona Klute. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: Linux adc218676eef25575469234709c2d87185ca223a,
 * rtw88/efuse.c, rtw8723x.c and reg.h; native error handling AI-assisted. */
#include <dev/ic/rtw8723d.h>
#include <dev/ic/rtw88_io.h>
#ifdef _KERNEL
#include <sys/systm.h>
#else
#include <string.h>
#endif

#define RTW8723D_REG_SYS_FUNC_EN 0x0002U
#define RTW8723D_REG_SYS_CLKR 0x0008U
#define RTW8723D_REG_EFUSE_CTRL 0x0030U
#define RTW8723D_REG_LDO_EFUSE_CTRL 0x0034U
#define RTW8723D_REG_EFUSE_ACCESS 0x00cfU
#define RTW8723D_EFUSE_ACCESS_ON 0x69U
#define RTW8723D_EFUSE_ELDR 0x1000U
#define RTW8723D_EFUSE_CLOCKS 0x0022U
#define RTW8723D_EFUSE_BANK 0x00000300U
#define RTW8723D_EFUSE_LDO25 0x80U
#define RTW8723D_EFUSE_ADDRESS 0x0003ff00U
#define RTW8723D_EFUSE_FLAG 0x80000000U

/* Share one dump-wide budget instead of renewing a million polls per byte.
 * Each poll delays one microsecond. Bus transaction time is additional. */
#define RTW8723D_EFUSE_POLL_BUDGET 1000000U

int
rtw8723d_efuse_read(struct rtw88_io *io,
    uint8_t out[RTW8723D_EFUSE_PHYSICAL_SIZE])
{
	struct rtw88_io cleanup;
	uint8_t physical[RTW8723D_EFUSE_PHYSICAL_SIZE];
	uint32_t control;
	unsigned address, budget = RTW8723D_EFUSE_POLL_BUDGET;
	int error;

	if (io == NULL)
		return EINVAL;
	if (io->error != 0)
		return io->error;
	if (out == NULL) {
		io->error = EINVAL;
		return io->error;
	}
	/* Validate scalar callbacks before taking any device ownership. */
	rtw88_io_init(&cleanup, io->ops, io->cookie);
	if (cleanup.error != 0) {
		io->error = cleanup.error;
		return io->error;
	}
	/* Even a failed grant write may have taken effect. Every path after
	 * this attempt must revoke access through a fresh error context. */
	rtw88_io_wr(io, RTW8723D_REG_EFUSE_ACCESS, 1, RTW8723D_EFUSE_ACCESS_ON);
	rtw88_io_set(io, RTW8723D_REG_SYS_FUNC_EN, 2, RTW8723D_EFUSE_ELDR);
	rtw88_io_set(io, RTW8723D_REG_SYS_CLKR, 2, RTW8723D_EFUSE_CLOCKS);
	rtw88_io_clr(io, RTW8723D_REG_LDO_EFUSE_CTRL, 4, RTW8723D_EFUSE_BANK);
	rtw88_io_clr(io, RTW8723D_REG_LDO_EFUSE_CTRL + 3, 1,
	    RTW8723D_EFUSE_LDO25);
	control = rtw88_io_rd(io, RTW8723D_REG_EFUSE_CTRL, 4);
	for (address = 0; address < RTW8723D_EFUSE_PHYSICAL_SIZE &&
	    io->error == 0; address++) {
		control &= ~(RTW8723D_EFUSE_ADDRESS | RTW8723D_EFUSE_FLAG | 0xffU);
		control |= address << 8;
		rtw88_io_wr(io, RTW8723D_REG_EFUSE_CTRL, 4, control);
		while (io->error == 0) {
			if (budget == 0) {
				io->error = ETIMEDOUT;
				break;
			}
			budget--;
			rtw88_io_delay(io, 1);
			control = rtw88_io_rd(io, RTW8723D_REG_EFUSE_CTRL, 4);
			if ((control & RTW8723D_EFUSE_FLAG) != 0)
				break;
		}
		if (io->error == 0)
			physical[address] = (uint8_t)control;
	}
	error = io->error;
	/* Preserve the first read/setup error while always trying grant-off.
	 * A cleanup error also prevents publication and requires owner unwind. */
	rtw88_io_wr(&cleanup, RTW8723D_REG_EFUSE_ACCESS, 1, 0);
	io->error = error != 0 ? error : cleanup.error;
	if (io->error == 0)
		memcpy(out, physical, sizeof(physical));
	return io->error;
}
