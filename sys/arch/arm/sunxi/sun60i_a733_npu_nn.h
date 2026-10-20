/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; fixed CPU-only NN image interface. */
#ifndef _SUN60I_A733_NPU_NN_H
#define _SUN60I_A733_NPU_NN_H
#include <sys/types.h>

#define A733_NPU_NN_CONSTANT_BYTES 864
#define A733_NPU_NN_INPUT_BYTES 256
#define A733_NPU_NN_EXPECTED_BYTES 512
#define A733_NPU_NN_IMAGE_BYTES 1632

struct sun60i_a733_npu_nn_image {
	uint8_t constants[A733_NPU_NN_CONSTANT_BYTES];
	uint8_t input[A733_NPU_NN_INPUT_BYTES];
	uint8_t expected[A733_NPU_NN_EXPECTED_BYTES];
};

/*
 * Emit only the fixed 8x8x4 to 8x8x8 unsigned-storage 1x1 convolution.
 * The trusted kernel output span must contain at least IMAGE_BYTES bytes.
 * Failure preserves output. Success writes IMAGE_BYTES and preserves its tail.
 * Byte arrays support unaligned output. The config uses fixed device VAs.
 * This CPU mechanism allocates nothing and does not access DMA or hardware.
 * Expected bytes are a CPU oracle. Never copy them into a device output page.
 * This function does not prove NN dispatch or silicon format acceptance.
 */
int sun60i_a733_npu_nn_tiny_image(void *, size_t);
#endif
