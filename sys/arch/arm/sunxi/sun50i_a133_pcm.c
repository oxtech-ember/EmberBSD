/* Origin: EmberBSD - validate A133 PCM formats and transfer geometry. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/param.h>
#include <sys/errno.h>

#include "sun50i_a133_pcm.h"

int
a133_pcm_format(unsigned mode, unsigned rate, unsigned channels,
    struct a133_pcm_format *out)
{
	struct a133_pcm_format f = { 0 };
	uint32_t fs;

	if (out == NULL || (mode != A133_PCM_PLAY && mode != A133_PCM_RECORD) ||
	    (rate != 16000 && rate != 48000) || channels < 1 || channels > 2 ||
	    (mode == A133_PCM_RECORD && channels != 1))
		return EINVAL;

	/* Selector 3 is the observed 16 kHz setting in both native and Linux. */
	fs = rate == 16000 ? (3U << 29) : 0;
	f.frame_bytes = channels * 2;
	if (mode == A133_PCM_PLAY) {
		/* Low 16 bits, TX threshold 64; mono duplicates left into right. */
		f.dac_fifoc = fs | (1U << 24) | (64U << 8);
		if (channels == 1)
			f.dac_fifoc |= 1U << 6;
	} else {
		/* Low 16 bits, left ADC only, RX threshold 32. */
		f.adc_fifoc = fs | (1U << 24) | (1U << 12) | (32U << 4);
	}
	*out = f;
	return 0;
}

static bool
a133_pcm_geometry(size_t size, size_t block, unsigned frame)
{

	return block != 0 && (frame == 2 || frame == 4) &&
	    block % frame == 0 && size % block == 0 && size / block >= 2;
}

static bool
a133_pcm_ring_valid(const struct a133_pcm_ring *r)
{

	return r != NULL && a133_pcm_geometry(r->size, r->block,
	    r->frame_bytes) && r->offset < r->size &&
	    r->offset % r->frame_bytes == 0 &&
	    r->remaining == r->block - r->offset % r->block;
}

int
a133_pcm_ring_init(struct a133_pcm_ring *r, size_t size, size_t block,
    unsigned frame)
{
	struct a133_pcm_ring fresh = { 0 };

	if (r == NULL || !a133_pcm_geometry(size, block, frame))
		return EINVAL;
	fresh.size = size;
	fresh.block = block;
	fresh.remaining = block;
	fresh.frame_bytes = frame;
	*r = fresh;
	return 0;
}

size_t
a133_pcm_ring_chunk(const struct a133_pcm_ring *r, size_t limit)
{

	if (!a133_pcm_ring_valid(r))
		return 0;
	if (limit > r->remaining)
		limit = r->remaining;
	return limit - limit % r->frame_bytes;
}

int
a133_pcm_ring_advance(struct a133_pcm_ring *r, size_t bytes, bool *completed)
{

	if (completed == NULL || !a133_pcm_ring_valid(r) || bytes == 0 ||
	    bytes > r->remaining || bytes % r->frame_bytes != 0)
		return EINVAL;
	/* Valid geometry guarantees bytes <= size - offset, without overflow. */
	r->offset += bytes;
	if (r->offset == r->size)
		r->offset = 0;
	r->remaining -= bytes;
	*completed = r->remaining == 0;
	if (*completed)
		r->remaining = r->block;
	return 0;
}
