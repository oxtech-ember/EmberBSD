/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#ifndef _ARM_A733_NPU_TILE_H_
#define _ARM_A733_NPU_TILE_H_

#include <sys/ioccom.h>
#include <sys/types.h>

#define A733_NPU_TILE_PIXELS	64
#define A733_NPU_TILE_INPUTS	4
#define A733_NPU_TILE_OUTPUTS	8
#define A733_NPU_TILE_ZERO	128
#define A733_NPU_TILE_MIN	123
#define A733_NPU_TILE_MAX	133

struct a733_npu_tile {
	uint8_t input[A733_NPU_TILE_INPUTS][A733_NPU_TILE_PIXELS];
	uint8_t output[A733_NPU_TILE_OUTPUTS][A733_NPU_TILE_PIXELS];
};

#define A733_NPU_TILE_RUN	_IOWR('N', 0, struct a733_npu_tile)

#endif /* _ARM_A733_NPU_TILE_H_ */
