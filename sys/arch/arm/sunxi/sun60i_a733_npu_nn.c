/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * Copyright (c) 2024 Pengutronix, Philipp Zabel
 * Copyright (c) 2026 EmberBSD contributors.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Origin: EmberBSD, AI-assisted. The finite v8 coefficient representation
 * follows Mesa 26.2.4 etnaviv_ml_nn_v8.c. No general Huffman encoder is used.
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <arm/sunxi/sun60i_a733_npu_nn.h>

_Static_assert(sizeof(struct sun60i_a733_npu_nn_image) ==
    A733_NPU_NN_IMAGE_BYTES, "The fixed byte-array image has no padding");

static const int8_t nn_weights[8][4] = {
	{ 1, 0, 0, 0 }, { 0, 1, 0, 0 },
	{ 0, 0, 1, 0 }, { 0, 0, 0, 1 },
	{ 1, -1, 0, 0 }, { 0, 1, -1, 0 },
	{ 0, 0, 1, -1 }, { -1, 0, 0, 1 }
};

static void
nn_store(uint8_t *out, uint32_t value)
{
	for (unsigned byte = 0; byte < 4; byte++)
		out[byte] = (uint8_t)(value >> (byte * 8));
}

static void
nn_stream(uint8_t *out, unsigned core)
{
	unsigned symbols[16] = { 1, 0 };

	/* Kernel count1, four differences, five zeros, pad, four flush codes. */
	for (unsigned channel = 0; channel < 4; channel++)
		symbols[channel + 2] = nn_weights[core][channel] < 0 ?
		    4 : (unsigned)nn_weights[core][channel];
	for (unsigned i = 12; i < 16; i++)
		symbols[i] = 4;
	/* Codes 0, 1 and 4 need no suffix or variable bits in the v8 pipeline. */
	for (unsigned i = 0; i < 16; i++) {
		for (unsigned bit = 0; bit < 3; bit++) {
			if ((symbols[i] & (1U << bit)) != 0)
				out[(i * 3 + bit) / 8] |=
				    (uint8_t)(1U << ((i * 3 + bit) % 8));
		}
	}
}

int
sun60i_a733_npu_nn_tiny_image(void *out, size_t bytes)
{
	/* Fixed v8.3 profile: plane strides64, unit float exponent127. */
	static const uint32_t config[48] = {
		0xa0100106, 0x00400200, 0xf8000000, 0x00400208,
		0x08204008, 0x00040084, 0x01003000, 0x01004000,
		0x00401000, 0, 0, 0, 0, 0, 0, 0x30000200,
		0x00400008, 8, 0, 0x03ffffff, 0, 0x03ffffff, 0x00008080, 0,
		0, 0, 0, 0x00025000, 0, 0x7f800000, 0xff800000, 0,
		0, 0, 0, 0x07ffffff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};
	struct sun60i_a733_npu_nn_image *image = out;
	uint8_t *coefficients;

	if (out == NULL || bytes < A733_NPU_NN_IMAGE_BYTES ||
	    A733_NPU_NN_IMAGE_BYTES > UINTPTR_MAX - (uintptr_t)out)
		return EINVAL;
	memset(image, 0, sizeof(*image));
	for (unsigned i = 0; i < 48; i++)
		nn_store(image->constants + i * 4, config[i]);
	coefficients = image->constants + 256;
	coefficients[0] = 0x10;
	nn_store(coefficients + 20, 0x76543210);
	for (unsigned core = 0; core < 8; core++) {
		nn_store(coefficients + 28 + core * 4, 48);
		nn_stream(coefficients + 64 + core * 64, core);
		nn_store(coefficients + 576 + core * 4, core + 1);
	}
	for (unsigned pixel = 0; pixel < 64; pixel++) {
		int values[4];

		for (unsigned channel = 0; channel < 4; channel++) {
			values[channel] =
			    (int)((pixel * (channel + 1) + 3 * channel) % 11) - 5;
			image->input[channel * 64 + pixel] =
			    (uint8_t)(values[channel] + 128);
		}
		for (unsigned core = 0; core < 8; core++) {
			int value = (int)core + 1;

			for (unsigned channel = 0; channel < 4; channel++)
				value += nn_weights[core][channel] * values[channel];
			image->expected[core * 64 + pixel] = (uint8_t)(value + 128);
		}
	}
	return 0;
}
