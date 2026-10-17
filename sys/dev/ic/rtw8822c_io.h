/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD RTL8822C register sequence helpers, AI-assisted. */
/*
 * Register helpers for long reference sequences. The first bus error is
 * kept in io->error and every later access becomes a no-op, so a failed
 * read never feeds a computed write.
 */
#ifndef _DEV_IC_RTW8822C_IO_H_
#define _DEV_IC_RTW8822C_IO_H_

struct rtw8822c_io {
	struct rtw88_softc *sc;
	int error;
};

static inline uint32_t
rtw8822c_io_rd(struct rtw8822c_io *io, uint32_t addr, unsigned size)
{
	const struct rtw88_bus_ops *ops = io->sc->sc_ops;
	uint32_t v32 = 0;
	uint16_t v16 = 0;
	uint8_t v8 = 0;

	if (io->error != 0)
		return 0;
	if (size == 1) {
		io->error = ops->read_1(io->sc->sc_bus, addr, &v8);
		return v8;
	}
	if (size == 2) {
		io->error = ops->read_2(io->sc->sc_bus, addr, &v16);
		return v16;
	}
	io->error = ops->read_4(io->sc->sc_bus, addr, &v32);
	return v32;
}

static inline void
rtw8822c_io_wr(struct rtw8822c_io *io, uint32_t addr, unsigned size,
    uint32_t v)
{
	const struct rtw88_bus_ops *ops = io->sc->sc_ops;

	if (io->error != 0)
		return;
	if (size == 1)
		io->error = ops->write_1(io->sc->sc_bus, addr, (uint8_t)v);
	else if (size == 2)
		io->error = ops->write_2(io->sc->sc_bus, addr, (uint16_t)v);
	else
		io->error = ops->write_4(io->sc->sc_bus, addr, v);
}

static inline void
rtw8822c_io_delay(struct rtw8822c_io *io, unsigned us)
{

	if (io->error == 0)
		io->sc->sc_ops->delay_us(io->sc->sc_bus, us);
}

/* rtw_write{8,16,32}_set and _clr. */
static inline void
rtw8822c_io_set(struct rtw8822c_io *io, uint32_t addr, unsigned size,
    uint32_t bits)
{
	uint32_t v = rtw8822c_io_rd(io, addr, size);

	rtw8822c_io_wr(io, addr, size, v | bits);
}

static inline void
rtw8822c_io_clr(struct rtw8822c_io *io, uint32_t addr, unsigned size,
    uint32_t bits)
{
	uint32_t v = rtw8822c_io_rd(io, addr, size);

	rtw8822c_io_wr(io, addr, size, v & ~bits);
}

/* rtw_read32_mask and rtw_write32_mask: data is shifted to the mask. */
static inline uint32_t
rtw8822c_io_rd_mask(struct rtw8822c_io *io, uint32_t addr, uint32_t mask)
{

	return (rtw8822c_io_rd(io, addr, 4) & mask) >> __builtin_ctz(mask);
}

static inline void
rtw8822c_io_wr_mask(struct rtw8822c_io *io, uint32_t addr, uint32_t mask,
    uint32_t data)
{
	uint32_t v = rtw8822c_io_rd(io, addr, 4);

	rtw8822c_io_wr(io, addr, 4,
	    (v & ~mask) | ((data << __builtin_ctz(mask)) & mask));
}

#endif /* _DEV_IC_RTW8822C_IO_H_ */
