/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; A733 compatibility adapter. */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _SUN60I_A733_GPU_FWCOMPUTE_VM_H
#define _SUN60I_A733_GPU_FWCOMPUTE_VM_H
#include "../../../dev/powervr/pvr_fwcompute_vm.h"
#define A733_GPU_FWCOMPUTE_VM_WINDOWS PVR_FWCOMPUTE_VM_WINDOWS
#define A733_GPU_FWCOMPUTE_VM_PAGE PVR_FWCOMPUTE_VM_PAGE
#define A733_GPU_FWCOMPUTE_VM_WINDOW_VA PVR_FWCOMPUTE_VM_WINDOW_VA
#define A733_GPU_FWCOMPUTE_VM_RO PVR_FWCOMPUTE_VM_RO
#define A733_GPU_FWCOMPUTE_VM_BYPASS PVR_FWCOMPUTE_VM_BYPASS
#define a733_gpu_fwcompute_vm_region pvr_fwcompute_vm_region
#define a733_gpu_fwcompute_vm_slot pvr_fwcompute_vm_slot
#define a733_gpu_fwcompute_vm_summary pvr_fwcompute_vm_summary
int sun60i_a733_gpu_fwcompute_vm_encode(
    const struct a733_gpu_fwcompute_vm_region *, uint32_t,
    void *page_catalog,
    void * const directory[A733_GPU_FWCOMPUTE_VM_WINDOWS],
    void * const table[A733_GPU_FWCOMPUTE_VM_WINDOWS],
    const uint64_t directory_pa[A733_GPU_FWCOMPUTE_VM_WINDOWS],
    const uint64_t table_pa[A733_GPU_FWCOMPUTE_VM_WINDOWS],
    uint64_t page_catalog_pa, uint8_t fw_context[32],
    struct a733_gpu_fwcompute_vm_summary *);
#endif
