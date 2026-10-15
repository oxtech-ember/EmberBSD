/* Origin: EmberBSD - own the verified A133 codec registers and FIFO service. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/param.h>
#include <sys/errno.h>
#include "sun50i_a133_codec_io.h"

static const unsigned ccu_regs[] = { 0x78, 0x178, 0xa50, 0xa54, 0xa58, 0xa5c };
static const uint32_t ccu_masks[] = {
	0xa93fff03, 0xffffffff, 0x8300000f, 0x8300000f, 0x8300000f, 0x00010001
};
static const unsigned codec_regs[] = {
	0, 4, 0x10, 0x30, 0x34, 0xf0, 0xf8, 0x300, 0x304, 0x310,
	0x318, 0x324, 0x328
};

static bool
io_valid(const struct a133_codec_io *io)
{

	return io != NULL && io->read != NULL && io->write != NULL &&
	    io->delay_us != NULL;
}

static void
update(const struct a133_codec_io *io, unsigned space, unsigned reg,
    uint32_t mask, uint32_t value)
{
	uint32_t old = io->read(io->cookie, space, reg);

	io->write(io->cookie, space, reg, (old & ~mask) | (value & mask));
}

void
a133_codec_restore(const struct a133_codec_io *io, struct a133_codec_lease *l)
{
	unsigned i;

	if (!io_valid(io) || l == NULL || !l->saved)
		return;
	if (l->pio_owned)
		update(io, A133_CODEC_PIO, 0xc4, 0x40, 0x40);
	if (l->codec_touched) {
		/* No saved stream ran; remove IRQ/digital enables before restore. */
		update(io, A133_CODEC_REG, 0x10, 0x1e, 0);
		update(io, A133_CODEC_REG, 0x30, 0x1000000e, 0);
		update(io, A133_CODEC_REG, 0, 0x80000000, 0);
		for (i = 13; i > 0; i--)
			io->write(io->cookie, A133_CODEC_REG, codec_regs[i - 1], l->codec[i - 1]);
	}
	/* Stop module outputs before changing PLL factors/pattern back. */
	for (i = 2; i < 5; i++)
		update(io, A133_CODEC_CCU, ccu_regs[i], 0x80000000, 0);
	update(io, A133_CODEC_CCU, 0x78, 0x80000000, 0);
	io->write(io->cookie, A133_CODEC_CCU, 0x178, l->ccu[1]);
	update(io, A133_CODEC_CCU, 0x78, ccu_masks[0], l->ccu[0]);
	for (i = 2; i < 6; i++) {
		/* Never reassert a released reset: it erases unowned trims/DAP. */
		uint32_t mask = i == 5 && l->codec_touched ? 1 : ccu_masks[i];
		update(io, A133_CODEC_CCU, ccu_regs[i], mask, l->ccu[i]);
	}
	if (l->pio_owned) {
		update(io, A133_CODEC_PIO, 0xb4, 0x07000000, l->pio_cfg);
		update(io, A133_CODEC_PIO, 0xc4, 0x40, l->pio_data);
	}
	l->saved = l->prepared = l->pio_owned = false;
	l->codec_touched = false;
	l->routes = 0;
}

int
a133_codec_prepare(const struct a133_codec_io *io, struct a133_codec_lease *l)
{
	static const unsigned consumers[] = { 0xa10, 0xa14, 0xa18, 0xa1c, 0xa20, 0xa40 };
	unsigned i;

	if (!io_valid(io) || l == NULL)
		return EINVAL;
	if (l->saved || l->prepared)
		return EBUSY;
	if ((io->read(io->cookie, A133_CODEC_REG, 0) & 0x80000000) ||
	    (io->read(io->cookie, A133_CODEC_REG, 0x30) & 0x10000000))
		return EBUSY;
	for (i = 0; i < sizeof(consumers) / sizeof(consumers[0]); i++)
		if (io->read(io->cookie, A133_CODEC_CCU, consumers[i]) & 0x80000000)
			return EBUSY;
	for (i = 0; i < 6; i++)
		l->ccu[i] = io->read(io->cookie, A133_CODEC_CCU, ccu_regs[i]);
	for (i = 0; i < 13; i++)
		l->codec[i] = io->read(io->cookie, A133_CODEC_REG, codec_regs[i]);
	l->pio_cfg = io->read(io->cookie, A133_CODEC_PIO, 0xb4);
	l->pio_data = io->read(io->cookie, A133_CODEC_PIO, 0xc4);
	l->saved = true;
	update(io, A133_CODEC_CCU, 0x78, 0x80000000, 0);
	io->delay_us(io->cookie, 1000);
	io->write(io->cookie, A133_CODEC_CCU, 0x178, 0xc001eb85);
	update(io, A133_CODEC_CCU, 0x78, ccu_masks[0], 0x09042702);
	update(io, A133_CODEC_CCU, 0x78, ccu_masks[0], 0x29042702);
	update(io, A133_CODEC_CCU, 0x78, ccu_masks[0], 0xa9042702);
	for (i = 0; i < 1000; i++) {
		if (io->read(io->cookie, A133_CODEC_CCU, 0x78) & 0x10000000)
			break;
		io->delay_us(io->cookie, 100);
	}
	if (i == 1000) {
		a133_codec_restore(io, l);
		return ETIMEDOUT;
	}
	update(io, A133_CODEC_CCU, 0xa50, ccu_masks[2], 0x80000003);
	update(io, A133_CODEC_CCU, 0xa54, ccu_masks[3], 0x80000003);
	update(io, A133_CODEC_CCU, 0xa58, ccu_masks[4], 0x80000000);
	/* Release reset once; do not pulse or later reassert the bus reset. */
	update(io, A133_CODEC_CCU, 0xa5c, ccu_masks[5], 0x10001);
	l->codec_touched = true;
	io->delay_us(io->cookie, 10000);
	/* Headset detection and DSP paths are not part of this board route. */
	update(io, A133_CODEC_REG, 0x328, 7, 0);
	io->write(io->cookie, A133_CODEC_REG, 0xf0, 0);
	io->write(io->cookie, A133_CODEC_REG, 0xf8, 0);
	io->write(io->cookie, A133_CODEC_REG, 0x10, 0);
	io->write(io->cookie, A133_CODEC_REG, 0x30, 0);
	l->prepared = true;
	return 0;
}

int
a133_codec_route(const struct a133_codec_io *io, struct a133_codec_lease *l,
    unsigned mode, bool enable)
{
	if (!io_valid(io) || l == NULL ||
	    (mode != A133_PCM_PLAY && mode != A133_PCM_RECORD))
		return EINVAL;
	if (!l->prepared)
		return EACCES;
	if (mode == A133_PCM_PLAY) {
		update(io, A133_CODEC_PIO, 0xc4, 0x40, 0x40);
		update(io, A133_CODEC_PIO, 0xb4, 0x07000000, 0x01000000);
		l->pio_owned = true;
		if (enable) {
			io->write(io->cookie, A133_CODEC_REG, 4, 0x1a0a0);
			/* Preserve analog bias trims; bit27 is the observed shared rail. */
			update(io, A133_CODEC_REG, 0x310, 0x7b00f05f, 0x1b00d05a);
			update(io, A133_CODEC_REG, 0x324, 0xffff, 0x8f8c);
			io->delay_us(io->cookie, 120000);
			update(io, A133_CODEC_PIO, 0xc4, 0x40, 0);
		} else {
			update(io, A133_CODEC_REG, 0, 0x80000000, 0);
			update(io, A133_CODEC_REG, 0x324, 0x8000, 0);
			update(io, A133_CODEC_REG, 0x310, 0xc000, 0);
		}
	} else if (enable) {
		update(io, A133_CODEC_REG, 0x300, 0x80001f00, 0x80001000);
		update(io, A133_CODEC_REG, 0x318, 0x80, 0x80);
		io->write(io->cookie, A133_CODEC_REG, 0x34, 0xa0a0);
		io->delay_us(io->cookie, 200000);
	} else {
		update(io, A133_CODEC_REG, 0x30, 0x10000000, 0);
		update(io, A133_CODEC_REG, 0x300, 0x80000000, 0);
		update(io, A133_CODEC_REG, 0x318, 0x80, 0);
	}
	if (enable)
		l->routes |= mode;
	else
		l->routes &= ~mode;
	return 0;
}

int
a133_codec_transfer(const struct a133_codec_io *io,
    const struct a133_codec_lease *l, unsigned mode, uint8_t *buffer,
    struct a133_pcm_ring *ring, unsigned budget, size_t *bytes, bool *done)
{
	uint32_t status, sample;
	size_t n, i, offset;
	unsigned words;

	if (!io_valid(io) || l == NULL || buffer == NULL || ring == NULL ||
	    bytes == NULL || done == NULL ||
	    (mode != A133_PCM_PLAY && mode != A133_PCM_RECORD) ||
	    (mode == A133_PCM_RECORD && ring->frame_bytes != 2) ||
	    a133_pcm_ring_chunk(ring, (size_t)-1) == 0)
		return EINVAL;
	if (!l->prepared || (l->routes & mode) == 0)
		return EACCES;
	if (budget > 128)
		budget = 128;
	*bytes = 0;
	*done = false;
	if (budget == 0)
		return 0;
	status = io->read(io->cookie, A133_CODEC_REG, mode == A133_PCM_PLAY ? 0x14 : 0x38);
	words = mode == A133_PCM_PLAY ? (status >> 8) & 0x7fff : (status >> 8) & 0x7f;
	if (mode == A133_PCM_RECORD && words == 0 && (status & 0x800000))
		words = 1;
	if (words > 128)
		return EIO;
	if (words > budget)
		words = budget;
	n = a133_pcm_ring_chunk(ring, words * 2);
	if (n == 0)
		return 0;
	offset = ring->offset;
	for (i = 0; i < n; i += 2) {
		if (mode == A133_PCM_PLAY) {
			sample = buffer[offset + i] | ((uint32_t)buffer[offset + i + 1] << 8);
			io->write(io->cookie, A133_CODEC_REG, 0x20, sample);
		} else {
			sample = io->read(io->cookie, A133_CODEC_REG, 0x40);
			buffer[offset + i] = sample & 0xff;
			buffer[offset + i + 1] = (sample >> 8) & 0xff;
		}
	}
	*bytes = n;
	return a133_pcm_ring_advance(ring, n, done);
}
