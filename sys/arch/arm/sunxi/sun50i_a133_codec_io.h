/* Origin: EmberBSD - own the verified A133 codec registers and FIFO service. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _SUN50I_A133_CODEC_IO_H_
#define _SUN50I_A133_CODEC_IO_H_
#include "sun50i_a133_pcm.h"

#define A133_CODEC_REG 0
#define A133_CODEC_CCU 1
#define A133_CODEC_PIO 2

struct a133_codec_io {
	void *cookie;
	uint32_t (*read)(void *, unsigned, unsigned);
	void (*write)(void *, unsigned, unsigned, uint32_t);
	void (*delay_us)(void *, unsigned);
};

struct a133_codec_lease {
	uint32_t ccu[6];
	uint32_t codec[13];
	uint32_t pio_cfg, pio_data;
	unsigned routes;
	bool saved, prepared, pio_owned, codec_touched;
};

int a133_codec_prepare(const struct a133_codec_io *, struct a133_codec_lease *);
void a133_codec_restore(const struct a133_codec_io *, struct a133_codec_lease *);
int a133_codec_route(const struct a133_codec_io *, struct a133_codec_lease *,
    unsigned, bool);
int a133_codec_transfer(const struct a133_codec_io *,
    const struct a133_codec_lease *, unsigned, uint8_t *,
    struct a133_pcm_ring *, unsigned, size_t *, bool *);
#endif
