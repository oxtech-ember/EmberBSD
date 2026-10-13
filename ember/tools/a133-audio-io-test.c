/* Origin: EmberBSD - test actual codec register sequencing and FIFO service. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sun50i_a133_codec_io.h"

struct bank {
	uint32_t regs[3][1024];
	uint32_t tx[256], rx[128];
	unsigned reads, writes, delay, ntx, nrx, irx;
	unsigned space_writes[3];
	unsigned resets;
	bool lock;
};

static uint32_t
rd(void *cookie, unsigned space, unsigned reg)
{
	struct bank *b = cookie;
	assert(space < 3 && reg < 4096 && reg % 4 == 0);
	b->reads++;
	if (space == A133_CODEC_CCU && reg == 0x78 && b->lock &&
	    (b->regs[space][reg / 4] & 0x20000000))
		return b->regs[space][reg / 4] | 0x10000000;
	if (space == A133_CODEC_REG && reg == 0x40) {
		assert(b->irx < b->nrx);
		return b->rx[b->irx++];
	}
	return b->regs[space][reg / 4];
}

static void
wr(void *cookie, unsigned space, unsigned reg, uint32_t value)
{
	struct bank *b = cookie;
	assert(space < 3 && reg < 4096 && reg % 4 == 0);
	b->writes++;
	b->space_writes[space]++;
	/* Bus reset destroys state outside the driver's owned register list. */
	if (space == A133_CODEC_CCU && reg == 0xa5c &&
	    (b->regs[space][reg / 4] & 0x10000) != 0 && (value & 0x10000) == 0) {
		b->resets++;
		b->regs[A133_CODEC_REG][0x320 / 4] = 0x80;
		b->regs[A133_CODEC_REG][0x100 / 4] = 0;
		b->regs[A133_CODEC_REG][0x324 / 4] = 0x80800c44;
		b->regs[A133_CODEC_REG][0x310 / 4] = 0;
	}
	/* PLL lock status is read-only, unlike the software-owned control bits. */
	if (space == A133_CODEC_CCU && reg == 0x78)
		value &= ~0x10000000U;
	if (space == A133_CODEC_REG && reg == 0x20) {
		assert(b->ntx < 256);
		b->tx[b->ntx++] = value;
	}
	b->regs[space][reg / 4] = value;
}

static void
wait_us(void *cookie, unsigned us)
{
	struct bank *b = cookie;
	b->delay += us;
}

static void
fixture(struct bank *b)
{
	memset(b, 0, sizeof(*b));
	b->lock = true;
	b->regs[A133_CODEC_CCU][0x78 / 4] = 0x88145500;
	b->regs[A133_CODEC_CCU][0xa50 / 4] = 0x00402010;
	b->regs[A133_CODEC_CCU][0xa5c / 4] = 0x00204000;
	b->regs[A133_CODEC_REG][0x324 / 4] = 0x80800c44;
	b->regs[A133_CODEC_REG][0x310 / 4] = 0x00150000;
	b->regs[A133_CODEC_REG][0x320 / 4] = 0x91;
	b->regs[A133_CODEC_REG][0x100 / 4] = 0x12345678;
	b->regs[A133_CODEC_PIO][0xb4 / 4] = 0x07373733;
	b->regs[A133_CODEC_PIO][0xc4 / 4] = 0xa5318300;
}

static void
lifecycle(void)
{
	struct bank b, before;
	struct a133_codec_lease lease = { 0 };
	struct a133_codec_io io = { &b, rd, wr, wait_us };
	unsigned writes;

	fixture(&b);
	/* Reserved DAC bits must survive route setup, not only final restore. */
	b.regs[A133_CODEC_REG][0x310 / 4] |= 0x04000020;
	before = b;
	assert(a133_codec_prepare(&io, &lease) == 0 && lease.prepared);
	assert(b.resets == 0);
	assert((b.regs[A133_CODEC_CCU][0x78 / 4] & 0xa93fff03) == 0xa9042702);
	assert(b.regs[A133_CODEC_CCU][0x178 / 4] == 0xc001eb85);
	assert(b.regs[A133_CODEC_CCU][0xa50 / 4] == 0x80402013);
	assert(b.regs[A133_CODEC_CCU][0xa5c / 4] == 0x00214001);
	assert(b.regs[A133_CODEC_PIO][0xb4 / 4] == 0x07373733);
	writes = b.writes;
	assert(a133_codec_prepare(&io, &lease) == EBUSY && b.writes == writes);
	assert(a133_codec_route(&io, &lease, A133_PCM_PLAY, true) == 0);
	assert((b.regs[A133_CODEC_REG][0x310 / 4] & 0x04000020) == 0x04000020);
	assert(b.regs[A133_CODEC_REG][0x324 / 4] == 0x80808f8c);
	assert((b.regs[A133_CODEC_PIO][0xb4 / 4] & 0x07000000) == 0x01000000);
	assert((b.regs[A133_CODEC_PIO][0xc4 / 4] & 0x40) == 0);
	assert(a133_codec_route(&io, &lease, A133_PCM_RECORD, true) == 0);
	assert((b.regs[A133_CODEC_REG][0x300 / 4] & 0x80001f00) == 0x80001000);
	assert(a133_codec_route(&io, &lease, A133_PCM_PLAY, false) == 0);
	assert((b.regs[A133_CODEC_PIO][0xc4 / 4] & 0x40) != 0);
	/* Another owner changes an unrelated GPIO bit while the lease is held. */
	b.regs[A133_CODEC_PIO][0xc4 / 4] ^= 0x100;
	a133_codec_restore(&io, &lease);
	assert(b.resets == 0);
	assert(!lease.saved && !lease.prepared && lease.routes == 0);
	assert(memcmp(b.regs[A133_CODEC_REG], before.regs[A133_CODEC_REG],
	    sizeof(b.regs[A133_CODEC_REG])) == 0);
	/* A released reset stays released: reasserting it would erase trims. */
	before.regs[A133_CODEC_CCU][0xa5c / 4] |= 0x10000;
	assert(memcmp(b.regs[A133_CODEC_CCU], before.regs[A133_CODEC_CCU],
	    sizeof(b.regs[A133_CODEC_CCU])) == 0);
	assert(b.regs[A133_CODEC_PIO][0xb4 / 4] == 0x07373733);
	assert(b.regs[A133_CODEC_PIO][0xc4 / 4] == (0xa5318300 ^ 0x100));
	writes = b.writes;
	a133_codec_restore(&io, &lease);
	assert(b.writes == writes);

	fixture(&b); b.regs[A133_CODEC_CCU][0xa5c / 4] |= 0x10000;
	before = b;
	assert(a133_codec_prepare(&io, &lease) == 0);
	assert(a133_codec_route(&io, &lease, A133_PCM_PLAY, true) == 0);
	assert(b.regs[A133_CODEC_REG][0x320 / 4] == 0x91);
	assert(b.regs[A133_CODEC_REG][0x100 / 4] == 0x12345678);
	assert(b.regs[A133_CODEC_REG][0x324 / 4] == 0x80808f8c);
	assert((b.regs[A133_CODEC_REG][0x310 / 4] & 0x00ff0000) == 0x00150000);
	a133_codec_restore(&io, &lease);
	assert(b.resets == 0 && memcmp(b.regs, before.regs, sizeof(b.regs)) == 0);

	fixture(&b); b.lock = false; before = b;
	assert(a133_codec_prepare(&io, &lease) == ETIMEDOUT);
	assert(b.space_writes[A133_CODEC_REG] == 0);
	assert(!lease.saved && !lease.prepared && b.delay <= 112000);
	assert(memcmp(b.regs, before.regs, sizeof(b.regs)) == 0);
	fixture(&b); b.regs[A133_CODEC_REG][0] = 0x80000000;
	assert(a133_codec_prepare(&io, &lease) == EBUSY && b.writes == 0);
	fixture(&b); b.regs[A133_CODEC_CCU][0xa10 / 4] = 0x80000000;
	assert(a133_codec_prepare(&io, &lease) == EBUSY && b.writes == 0);
}

static void
fifo(void)
{
	struct bank b;
	struct a133_codec_io io = { &b, rd, wr, wait_us };
	struct a133_codec_lease lease = { 0 };
	struct a133_pcm_ring ring;
	uint8_t pcm[1026];
	size_t bytes;
	bool done;
	unsigned reads;

	fixture(&b); memset(pcm, 0xa5, sizeof(pcm));
	assert(a133_pcm_ring_init(&ring, 1024, 512, 4) == 0);
	bytes = 99; done = true; reads = b.reads;
	assert(a133_codec_transfer(&io, &lease, A133_PCM_PLAY, pcm + 1,
	    &ring, 128, &bytes, &done) == EACCES);
	assert(bytes == 99 && done && b.reads == reads && b.writes == 0);
	lease.prepared = true; lease.routes = A133_PCM_PLAY;
	pcm[1] = 0x34; pcm[2] = 0x12; pcm[3] = 0x00; pcm[4] = 0x80;
	b.regs[A133_CODEC_REG][0x14 / 4] = 0x00800308;
	assert(a133_codec_transfer(&io, &lease, A133_PCM_PLAY, pcm + 1,
	    &ring, 128, &bytes, &done) == 0);
	assert(bytes == 4 && !done && b.ntx == 2);
	assert(b.tx[0] == 0x1234 && b.tx[1] == 0x8000 && ring.offset == 4);
	b.regs[A133_CODEC_REG][0x14 / 4] = 0x00808008;
	assert(a133_codec_transfer(&io, &lease, A133_PCM_PLAY, pcm + 1,
	    &ring, 999, &bytes, &done) == 0 && bytes == 256 && !done);
	b.ntx = 0;
	assert(a133_codec_transfer(&io, &lease, A133_PCM_PLAY, pcm + 1,
	    &ring, 128, &bytes, &done) == 0 && bytes == 252 && done);
	assert(ring.offset == 512 && ring.remaining == 512);
	b.regs[A133_CODEC_REG][0x14 / 4] = 0;
	assert(a133_codec_transfer(&io, &lease, A133_PCM_PLAY, pcm + 1,
	    &ring, 128, &bytes, &done) == 0 && bytes == 0 && !done);
	assert(pcm[0] == 0xa5 && pcm[1025] == 0xa5);

	assert(a133_pcm_ring_init(&ring, 32, 16, 2) == 0);
	lease.routes = A133_PCM_RECORD;
	b.regs[A133_CODEC_REG][0x38 / 4] = 0x00800308;
	b.rx[0] = 0xffff8000; b.rx[1] = 0x00007fff; b.rx[2] = 0x00001234;
	b.nrx = 3;
	assert(a133_codec_transfer(&io, &lease, A133_PCM_RECORD, pcm + 1,
	    &ring, 128, &bytes, &done) == 0 && bytes == 6 && !done);
	assert(memcmp(pcm + 1, "\x00\x80\xff\x7f\x34\x12", 6) == 0);
	/* RXA guarantees one readable sample even if its count snapshot is zero. */
	b.regs[A133_CODEC_REG][0x38 / 4] = 0x00800008;
	b.rx[3] = 0xfffffff0; b.nrx = 4;
	assert(a133_codec_transfer(&io, &lease, A133_PCM_RECORD, pcm + 1,
	    &ring, 128, &bytes, &done) == 0 && bytes == 2);
	assert(pcm[7] == 0xf0 && pcm[8] == 0xff);
	reads = b.reads; bytes = 99; done = true;
	assert(a133_codec_transfer(&io, &lease, 3, pcm + 1,
	    &ring, 128, &bytes, &done) == EINVAL);
	assert(bytes == 99 && done && b.reads == reads);
}

int
main(void)
{
	lifecycle();
	fifo();
	puts("A133 codec lease, silent guards and bounded FIFO service passed");
	return 0;
}
