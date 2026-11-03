/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright 2024 Fiona Klute. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: Linux adc218676eef25575469234709c2d87185ca223a rtw88/phy.c,
 * rtw8723x.c and rtw8723d.c; native error handling is AI-assisted. */
#include <dev/ic/rtw8723d_rf.h>
#include <dev/ic/rtw88_io.h>

#define RTW8723D_RF_MASK 0x000fffffU
#define RTW8723D_RF_READ_ADDR 0x7f800000U
#define RTW8723D_RF_READ_EDGE 0x80000000U

static const struct {
	uint32_t hssi1, hssi2, read, read_pi, write;
} rtw8723d_rf_ports[] = {
	{ 0x820, 0x824, 0x8a0, 0x8b8, 0x840 },
	{ 0x828, 0x82c, 0x8a4, 0x8bc, 0x844 }
};

static int
rtw8723d_rf_prepare(struct rtw88_io *io, unsigned path, unsigned addr,
    uint32_t mask)
{
	struct rtw88_io validation;

	if (io == NULL)
		return EINVAL;
	if (io->error != 0)
		return io->error;
	if (path >= 2 || addr > 0xff || mask == 0 ||
	    (mask & ~RTW8723D_RF_MASK) != 0) {
		io->error = EINVAL;
		return io->error;
	}
	rtw88_io_init(&validation, io->ops, io->cookie);
	io->error = validation.error;
	return io->error;
}

int
rtw8723d_rf_read(struct rtw88_io *io, unsigned path, unsigned addr,
    uint32_t mask, uint32_t *out)
{
	uint32_t value, port;
	int error;

	error = rtw8723d_rf_prepare(io, path, addr, mask);
	if (error != 0)
		return error;
	if (out == NULL) {
		io->error = EINVAL;
		return io->error;
	}
	value = rtw88_io_rd(io, rtw8723d_rf_ports[path].hssi2, 4);
	value = (value & ~RTW8723D_RF_READ_ADDR) | ((uint32_t)addr << 23);
	rtw88_io_wr(io, rtw8723d_rf_ports[path].hssi2, 4, value);
	/* The read trigger belongs to path A for both physical RF paths. */
	value = rtw88_io_rd(io, rtw8723d_rf_ports[0].hssi2, 4);
	if (io->error == 0) {
		rtw88_io_wr(io, rtw8723d_rf_ports[0].hssi2, 4,
		    value & ~RTW8723D_RF_READ_EDGE);
		rtw88_io_wr(io, rtw8723d_rf_ports[0].hssi2, 4,
		    value | RTW8723D_RF_READ_EDGE);
		/* A failed write may have reached the device. Keep settling time
		 * after an edge attempt, but never perform another scalar access. */
		io->ops->delay_us(io->cookie, 120);
	}
	value = rtw88_io_rd(io, rtw8723d_rf_ports[path].hssi1, 4);
	port = (value & 0x100U) != 0 ? rtw8723d_rf_ports[path].read_pi :
	    rtw8723d_rf_ports[path].read;
	value = rtw88_io_rd(io, port, 4);
	if (io->error == 0)
		*out = (value & mask) >> __builtin_ctz(mask);
	return io->error;
}

int
rtw8723d_rf_write(struct rtw88_io *io, unsigned path, unsigned addr,
    uint32_t mask, uint32_t data)
{
	uint32_t value, command;
	int error;

	error = rtw8723d_rf_prepare(io, path, addr, mask);
	if (error != 0)
		return error;
	if (mask == RTW8723D_RF_MASK) {
		value = data & mask;
	} else {
		error = rtw8723d_rf_read(io, path, addr, RTW8723D_RF_MASK,
		    &value);
		if (error != 0)
			return error;
		value = (value & ~mask) | ((data << __builtin_ctz(mask)) & mask);
	}
	command = ((uint32_t)addr << 20) | value;
	rtw88_io_wr(io, rtw8723d_rf_ports[path].write, 4, command);
	/* Preserve the command settling time even when the write reports an
	 * error after a possible device-side effect. No later I/O follows. */
	io->ops->delay_us(io->cookie, 13);
	return io->error;
}
