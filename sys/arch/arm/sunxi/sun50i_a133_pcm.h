/* Origin: EmberBSD - validate A133 PCM formats and transfer geometry. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _SUN50I_A133_PCM_H_
#define _SUN50I_A133_PCM_H_

#include <sys/types.h>
#ifndef _KERNEL
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

#define A133_PCM_PLAY	1
#define A133_PCM_RECORD	2

struct a133_pcm_format {
	unsigned frame_bytes;
	uint32_t dac_fifoc;
	uint32_t adc_fifoc;
};

/* Encoding/precision validation belongs to the audio_hw_if adapter. */
int a133_pcm_format(unsigned, unsigned, unsigned, struct a133_pcm_format *);

struct a133_pcm_ring {
	size_t size;
	size_t block;
	size_t offset;
	size_t remaining;
	unsigned frame_bytes;
};

/* The caller serializes ring access and copies before advancing. */
int a133_pcm_ring_init(struct a133_pcm_ring *, size_t, size_t, unsigned);
size_t a133_pcm_ring_chunk(const struct a133_pcm_ring *, size_t);
/* Completion releases source/destination memory, not hardware FIFO samples. */
int a133_pcm_ring_advance(struct a133_pcm_ring *, size_t, bool *);

#endif /* _SUN50I_A133_PCM_H_ */
