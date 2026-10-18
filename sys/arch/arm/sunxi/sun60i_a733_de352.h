/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; independent DE3.52 data encoding. */
#ifndef _SUN60I_A733_DE352_H
#define _SUN60I_A733_DE352_H

#define A733_DE352_RCQ_DESCRIPTOR_BYTES 16

struct sun60i_a733_de352_rgb {
	uint32_t width, height, pitch;
	/* Device-visible address and mapped bytes, not a CPU physical pointer. */
	uint64_t dma, mapped_bytes;
};

struct sun60i_a733_de352_rgb_words {
	/* Values only. The caller owns each register and preserves other fields. */
	uint32_t attribute, size, coordinate, pitch;
	uint32_t address_low, address_high_byte;
	uint32_t bottom_low, bottom_high_byte, fill_color;
	uint32_t channel_size, coarse_horizontal, coarse_vertical;
	uint64_t required_bytes;
};

struct sun60i_a733_de352_rcq_block {
	uint64_t dma;
	uint32_t bytes;
	/* Register offset from DE base, not an absolute CPU address. */
	uint32_t destination;
	bool dirty;
};

/* No function below performs I/O, starts DMA or proves resource ownership. */
int sun60i_a733_de352_rgb_encode(const struct sun60i_a733_de352_rgb *,
    struct sun60i_a733_de352_rgb_words *);
int sun60i_a733_de352_rcq_encode(const struct sun60i_a733_de352_rcq_block *,
    uint8_t[A733_DE352_RCQ_DESCRIPTOR_BYTES]);
/* The final unused descriptor, if present, must be zero-filled by the caller. */
int sun60i_a733_de352_rcq_list_size(uint32_t, uint32_t *);

#endif
