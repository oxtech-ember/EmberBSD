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
 * Origin: EmberBSD, AI-assisted; fixed diagnostic data and hardware commands.
 * The host fixture generator owns model preparation and the numerical oracle.
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <arm/sunxi/sun60i_a733_npu_nn.h>
#include "sun60i_a733_npu_nn_data.h"

_Static_assert(sizeof(struct sun60i_a733_npu_nn_image) ==
    A733_NPU_NN_IMAGE_BYTES, "The fixed byte-array image has no padding");
_Static_assert(sizeof(nn_tiny_image) == A733_NPU_NN_IMAGE_BYTES,
    "The diagnostic fixture must fill the fixed image");

static void
nn_store(uint8_t *out, uint32_t value)
{
	for (unsigned byte = 0; byte < 4; byte++)
		out[byte] = (uint8_t)(value >> (byte * 8));
}

int
sun60i_a733_npu_nn_tiny_image(void *out, size_t bytes)
{

	if (out == NULL || bytes < A733_NPU_NN_IMAGE_BYTES ||
	    A733_NPU_NN_IMAGE_BYTES > UINTPTR_MAX - (uintptr_t)out)
		return EINVAL;
	memcpy(out, nn_tiny_image, sizeof(nn_tiny_image));
	return 0;
}

int
sun60i_a733_npu_nn_task_commands(void *out, size_t bytes)
{
	/* Mesa NN dispatch plus the non-BLT kernel completion tail. */
	static const uint32_t words[36] = {
		0x08010061, 0xffffff7f, 0x08010e02, 0x30000701,
		0x48000000, 0x30000701, 0x0801028a, 0x11,
		0x08010e13, 2, 0x08010e4f, 0, 0x08010e50, 0,
		0x08010e4c, 0x10, 0x08010428, 0x01002000,
		0x08010429, 0, 0x08010e03, 0xc23, 0x08010e03, 0xc23,
		0x08010e03, 0x23, 0x08010594, 1, 0x08010e02, 0x701,
		0x48000000, 0x701, 0x08010e01, 0x44, 0x10000000, 0
	};
	uint8_t *p = out;

	if (out == NULL || bytes < A733_NPU_NN_COMMAND_BYTES ||
	    A733_NPU_NN_COMMAND_BYTES > UINTPTR_MAX - (uintptr_t)out)
		return EINVAL;
	for (unsigned i = 0; i < 36; i++)
		nn_store(p + i * 4, words[i]);
	return 0;
}
