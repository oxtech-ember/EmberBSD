/* Origin: EmberBSD - exercise A133 PCM formats and buffer boundaries. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sun50i_a133_pcm.h"

static void
formats(void)
{
	static const struct {
		unsigned mode, rate, channels, frame;
		uint32_t tx, rx;
	} good[] = {
		{ A133_PCM_PLAY, 16000, 1, 2, 0x61004040, 0 },
		{ A133_PCM_PLAY, 16000, 2, 4, 0x61004000, 0 },
		{ A133_PCM_PLAY, 48000, 1, 2, 0x01004040, 0 },
		{ A133_PCM_PLAY, 48000, 2, 4, 0x01004000, 0 },
		{ A133_PCM_RECORD, 16000, 1, 2, 0, 0x61001200 },
		{ A133_PCM_RECORD, 48000, 1, 2, 0, 0x01001200 },
	};
	static const struct {
		unsigned mode, rate, channels;
	} bad[] = {
		{ 0, 16000, 1 }, { 3, 16000, 1 }, { 4, 16000, 1 },
		{ A133_PCM_PLAY, 0, 1 }, { A133_PCM_PLAY, 44100, 1 },
		{ A133_PCM_PLAY, 96000, 2 }, { A133_PCM_PLAY, 48000, 0 },
		{ A133_PCM_PLAY, 48000, 3 },
		{ A133_PCM_RECORD, 16000, 2 },
	};
	struct a133_pcm_format f, saved;
	size_t i;

	for (i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
		memset(&f, 0xa5, sizeof(f));
		assert(a133_pcm_format(good[i].mode, good[i].rate,
		    good[i].channels, &f) == 0);
		assert(f.frame_bytes == good[i].frame);
		assert(f.dac_fifoc == good[i].tx);
		assert(f.adc_fifoc == good[i].rx);
		/* Neither direction starts streams, DMA or IRQs. */
		assert((f.dac_fifoc & 0x1f) == 0);
		assert((f.adc_fifoc & 0x1000000f) == 0);
	}
	memset(&f, 0xa5, sizeof(f));
	saved = f;
	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		assert(a133_pcm_format(bad[i].mode, bad[i].rate,
		    bad[i].channels, &f) == EINVAL);
		assert(memcmp(&f, &saved, sizeof(f)) == 0);
	}
	assert(a133_pcm_format(A133_PCM_PLAY, 16000, 1, NULL) == EINVAL);
}

static void
ring_geometry(void)
{
	static const struct {
		size_t size, block;
		unsigned frame;
	} bad[] = {
		{ 0, 16, 2 }, { 64, 0, 2 }, { 64, 64, 2 },
		{ 64, 16, 0 }, { 64, 16, 3 }, { 64, 15, 2 },
		{ 64, 30, 2 }, { 64, 2, 4 },
		/* Naive 2 * block overflows, admitting a single block. */
		{ SIZE_MAX - 1, SIZE_MAX - 1, 2 },
	};
	struct a133_pcm_ring r = { 0 }, saved;
	bool done;
	size_t i;

	assert(a133_pcm_ring_chunk(&r, 256) == 0);
	assert(a133_pcm_ring_chunk(NULL, 256) == 0);
	assert(a133_pcm_ring_init(NULL, 64, 16, 2) == EINVAL);
	assert(a133_pcm_ring_init(&r, 64, 16, 2) == 0);
	saved = r;
	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		assert(a133_pcm_ring_init(&r, bad[i].size,
		    bad[i].block, bad[i].frame) == EINVAL);
		assert(memcmp(&r, &saved, sizeof(r)) == 0);
	}
	assert(a133_pcm_ring_advance(NULL, 2, &done) == EINVAL);
	assert(a133_pcm_ring_advance(&r, 2, NULL) == EINVAL);
	assert(memcmp(&r, &saved, sizeof(r)) == 0);
	for (i = 0; i < 3; i++) {
		static const size_t wrong[] = { 0, 3, 18 };
		done = true;
		assert(a133_pcm_ring_advance(&r, wrong[i], &done) == EINVAL);
		assert(done);
		assert(memcmp(&r, &saved, sizeof(r)) == 0);
	}
	assert(a133_pcm_ring_chunk(&r, 0) == 0);
	assert(a133_pcm_ring_chunk(&r, 1) == 0);
	assert(a133_pcm_ring_chunk(&r, 3) == 2);
	assert(a133_pcm_ring_chunk(&r, SIZE_MAX) == 16);
	assert(a133_pcm_ring_advance(&r, 14, &done) == 0 && !done);
	assert(r.offset == 14 && r.remaining == 2);
	assert(a133_pcm_ring_chunk(&r, 256) == 2);
	assert(a133_pcm_ring_advance(&r, 2, &done) == 0 && done);
	assert(r.offset == 16 && r.remaining == 16);
	assert(a133_pcm_ring_init(&r, 64, 16, 4) == 0);
	assert(a133_pcm_ring_chunk(&r, 3) == 0);
	assert(a133_pcm_ring_chunk(&r, 7) == 4);

	/* Even a valid SIZE_MAX-adjacent ring must wrap without addition overflow. */
	assert(a133_pcm_ring_init(&r, SIZE_MAX - 3, (SIZE_MAX - 3) / 2, 2) == 0);
	assert(a133_pcm_ring_advance(&r, r.block, &done) == 0 && done);
	assert(a133_pcm_ring_advance(&r, r.block, &done) == 0 && done);
	assert(r.offset == 0 && r.remaining == r.block);
}

static void
ring_copy(void)
{
	uint8_t source[64], dest[194];
	struct a133_pcm_ring r;
	struct a133_pcm_format f;
	size_t i, copied, n, blocks = 0;
	bool done;

	for (i = 0; i < sizeof(source); i++)
		source[i] = (uint8_t)i;
	memset(dest, 0xa5, sizeof(dest));
	assert(a133_pcm_format(A133_PCM_PLAY, 48000, 2, &f) == 0);
	assert(a133_pcm_ring_init(&r, 64, 16, f.frame_bytes) == 0);
	for (copied = 0; copied < 192; copied += n) {
		n = a133_pcm_ring_chunk(&r, 11);
		assert(n == 8);
		memcpy(dest + 1 + copied, source + r.offset, n);
		assert(a133_pcm_ring_advance(&r, n, &done) == 0);
		if (done)
			blocks++;
	}
	assert(blocks == 12 && r.offset == 0 && r.remaining == 16);
	assert(dest[0] == 0xa5 && dest[193] == 0xa5);
	for (i = 0; i < 192; i++)
		assert(dest[i + 1] == i % 64);
}

int
main(void)
{
	formats();
	ring_geometry();
	ring_copy();
	puts("A133 PCM formats and ring boundaries passed");
	return 0;
}
