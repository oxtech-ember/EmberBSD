/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD A133 GPU explicit boot ownership, AI-assisted. */
#ifndef _SUN50I_A133_GPU_OWNER_H
#define _SUN50I_A133_GPU_OWNER_H
#include <sys/types.h>
#ifndef _KERNEL
#include <stdbool.h>
#include <stdint.h>
#endif
int a133_gpu_effective_hz(uint64_t, uint32_t, uint64_t *);
bool a133_gpu_opp_valid(uint64_t, uint32_t);
/* Numeric diagnostics report the operation entered, including a failed call. */
enum a133_gpu_stage {
 A133_GPU_STAGE_IDLE, A133_GPU_STAGE_BINDING, A133_GPU_STAGE_ACQUIRE,
 A133_GPU_STAGE_PREFLIGHT_ENABLED, A133_GPU_STAGE_PREFLIGHT_VOLTAGE,
 A133_GPU_STAGE_MAP_GPU, A133_GPU_STAGE_MAP_DFS, A133_GPU_STAGE_FW_OPEN,
 A133_GPU_STAGE_FW_SIZE, A133_GPU_STAGE_FW_ALLOC, A133_GPU_STAGE_FW_READ,
 A133_GPU_STAGE_FW_PARSE, A133_GPU_STAGE_FW_CLOSE, A133_GPU_STAGE_PLL_ENABLE,
 A133_GPU_STAGE_CORE_RATE, A133_GPU_STAGE_RESET, A133_GPU_STAGE_BUS_ENABLE,
 A133_GPU_STAGE_CORE_ENABLE, A133_GPU_STAGE_PPU_ENABLE, A133_GPU_STAGE_PPU_STATE,
 A133_GPU_STAGE_POWER_ENABLED, A133_GPU_STAGE_POWER_VOLTAGE,
 A133_GPU_STAGE_POWER_CLOCK, A133_GPU_STAGE_LIVE_PPU,
 A133_GPU_STAGE_LIVE_ENABLED, A133_GPU_STAGE_LIVE_VOLTAGE,
 A133_GPU_STAGE_LIVE_CLOCK, A133_GPU_STAGE_BVNC, A133_GPU_STAGE_DFS,
 A133_GPU_STAGE_OPP, A133_GPU_STAGE_DMA, A133_GPU_STAGE_FEATURES,
 A133_GPU_STAGE_INTERFACE, A133_GPU_STAGE_EFFECTIVE_RECHECK,
 A133_GPU_STAGE_FW_BOOT, A133_GPU_STAGE_COMPLETE,
 A133_GPU_STAGE_SET_VOLTAGE, A133_GPU_STAGE_VOLTAGE_READBACK
};
struct a133_gpu_diagnostics {
 int stage, last_error, detail_error, attempted, retained;
 int regulator_enabled, microvolts, ppu_enabled, core_hz, pll_hz;
 int effective_hz, dfs;
 int64_t bvnc;
};
struct a133_gpu_owner {
	bool attempted, retain_resources;
};
struct a133_gpu_owner_ops {
	int (*preflight)(void *);
	int (*activate)(void *);
	int (*prepare)(void *);
	int (*boot)(void *);
};
/* The caller serializes the full call. A failed activation is uncertain. */
int a133_gpu_owner_boot(struct a133_gpu_owner *,
    const struct a133_gpu_owner_ops *, void *);
#endif
