/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD RTL8822C register helpers, generalized for native rtw88. */
/*
 * Register sequence helpers. The caller serializes access and keeps the bus
 * callbacks and cookie alive. The first error stops every later access.
 * Only scalar callbacks are required; no DMA or interrupt operations are used.
 */
#ifndef _DEV_IC_RTW88_IO_H_
#define _DEV_IC_RTW88_IO_H_

#ifdef _KERNEL
#include <sys/errno.h>
#else
#include <errno.h>
#endif
#include <dev/ic/rtw88var.h>

struct rtw88_io {
	const struct rtw88_bus_ops *ops;
	void *cookie;
	int error;
};

static inline void
rtw88_io_init(struct rtw88_io *io, const struct rtw88_bus_ops *ops, void *cookie)
{

	io->ops = ops;
	io->cookie = cookie;
	io->error = 0;
	if (ops == NULL || ops->read_1 == NULL || ops->read_2 == NULL ||
	    ops->read_4 == NULL || ops->write_1 == NULL || ops->write_2 == NULL ||
	    ops->write_4 == NULL || ops->delay_us == NULL)
		io->error = EINVAL;
}

static inline uint32_t
rtw88_io_rd(struct rtw88_io *io, uint32_t addr, unsigned size)
{
	uint32_t v32 = 0;
	uint16_t v16 = 0;
	uint8_t v8 = 0;

	if (io->error != 0)
		return 0;
	switch (size) {
	case 1:
		io->error = io->ops->read_1(io->cookie, addr, &v8);
		return io->error == 0 ? v8 : 0;
	case 2:
		io->error = io->ops->read_2(io->cookie, addr, &v16);
		return io->error == 0 ? v16 : 0;
	case 4:
		io->error = io->ops->read_4(io->cookie, addr, &v32);
		return io->error == 0 ? v32 : 0;
	default:
		io->error = EINVAL;
		return 0;
	}
}

static inline void
rtw88_io_wr(struct rtw88_io *io, uint32_t addr, unsigned size, uint32_t value)
{

	if (io->error != 0)
		return;
	switch (size) {
	case 1:
		io->error = io->ops->write_1(io->cookie, addr, (uint8_t)value);
		break;
	case 2:
		io->error = io->ops->write_2(io->cookie, addr, (uint16_t)value);
		break;
	case 4:
		io->error = io->ops->write_4(io->cookie, addr, value);
		break;
	default:
		io->error = EINVAL;
		break;
	}
}

static inline void
rtw88_io_delay(struct rtw88_io *io, unsigned us)
{

	if (io->error == 0)
		io->ops->delay_us(io->cookie, us);
}

static inline void
rtw88_io_set(struct rtw88_io *io, uint32_t addr, unsigned size, uint32_t bits)
{
	uint32_t value = rtw88_io_rd(io, addr, size);

	rtw88_io_wr(io, addr, size, value | bits);
}

static inline void
rtw88_io_clr(struct rtw88_io *io, uint32_t addr, unsigned size, uint32_t bits)
{
	uint32_t value = rtw88_io_rd(io, addr, size);

	rtw88_io_wr(io, addr, size, value & ~bits);
}

/* Masked data is shifted to the first set bit, as in rtw88 reference code. */
static inline uint32_t
rtw88_io_rd_mask(struct rtw88_io *io, uint32_t addr, uint32_t mask)
{

	if (io->error != 0)
		return 0;
	if (mask == 0) {
		io->error = EINVAL;
		return 0;
	}
	return (rtw88_io_rd(io, addr, 4) & mask) >> __builtin_ctz(mask);
}

static inline void
rtw88_io_wr_mask(struct rtw88_io *io, uint32_t addr, uint32_t mask, uint32_t data)
{
	uint32_t value;

	if (io->error != 0)
		return;
	if (mask == 0) {
		io->error = EINVAL;
		return;
	}
	value = rtw88_io_rd(io, addr, 4);
	rtw88_io_wr(io, addr, 4,
	    (value & ~mask) | ((data << __builtin_ctz(mask)) & mask));
}

#endif /* _DEV_IC_RTW88_IO_H_ */
