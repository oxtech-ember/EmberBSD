/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; A733 compatibility adapter. */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _SUN60I_A733_GPU_MMU_H
#define _SUN60I_A733_GPU_MMU_H
#include "../../../dev/powervr/pvr_mmu.h"
#define A733_GPU_MMU_PAGE_SIZE PVR_MMU_PAGE_SIZE
#define A733_GPU_MMU_PC_ENTRIES PVR_MMU_PC_ENTRIES
#define A733_GPU_MMU_PD_ENTRIES PVR_MMU_PD_ENTRIES
#define A733_GPU_MMU_PT_ENTRIES PVR_MMU_PT_ENTRIES
#define A733_GPU_MMU_PA_BITS PVR_MMU_PA_BITS
#define A733_GPU_MMU_VA_BITS PVR_MMU_VA_BITS
#define A733_GPU_MMU_PTE_READ_ONLY PVR_MMU_PTE_READ_ONLY
#define A733_GPU_MMU_PTE_COHERENT PVR_MMU_PTE_COHERENT
#define A733_GPU_MMU_PTE_SLC_BYPASS PVR_MMU_PTE_SLC_BYPASS
#define A733_GPU_MMU_PTE_PM_FW_PROTECT PVR_MMU_PTE_PM_FW_PROTECT
struct sun60i_a733_gpu_mmu_indices { uint16_t pc, pd, pt, offset; };
int sun60i_a733_gpu_mmu_indices(uint64_t,
    struct sun60i_a733_gpu_mmu_indices *);
int sun60i_a733_gpu_mmu_pce(uint64_t, uint32_t *);
int sun60i_a733_gpu_mmu_pde(uint64_t, uint64_t *);
int sun60i_a733_gpu_mmu_pte(uint64_t, uint64_t, uint64_t *);
#endif
