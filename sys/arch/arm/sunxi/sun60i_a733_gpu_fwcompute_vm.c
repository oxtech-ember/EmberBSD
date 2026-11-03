/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; A733 compatibility adapter. */
/* Copyright (c) 2026 EmberBSD contributors. */
#include "sun60i_a733_gpu_fwcompute_vm.h"

int
sun60i_a733_gpu_fwcompute_vm_encode(
    const struct a733_gpu_fwcompute_vm_region *regions, uint32_t count,
    void *catalog, void * const directory[A733_GPU_FWCOMPUTE_VM_WINDOWS],
    void * const table[A733_GPU_FWCOMPUTE_VM_WINDOWS],
    const uint64_t directory_pa[A733_GPU_FWCOMPUTE_VM_WINDOWS],
    const uint64_t table_pa[A733_GPU_FWCOMPUTE_VM_WINDOWS],
    uint64_t catalog_pa, uint8_t context[32],
    struct a733_gpu_fwcompute_vm_summary *summary)
{
	return pvr_fwcompute_vm_encode(regions, count, catalog, directory,
	    table, directory_pa, table_pa, catalog_pa, context, summary);
}
