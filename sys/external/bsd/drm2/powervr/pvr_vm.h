/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _PVR_VM_H_
#define _PVR_VM_H_

#include <sys/types.h>
#include <sys/bus.h>

struct pvr_vm;

/*
 * CPU-owned Rogue VM, with 40-bit VA, 36-bit PA and 4 KiB GPU pages.
 * The caller serializes all access from a sleepable context. The caller
 * retains the DMA tag and all mapped RAM until unmap or destruction.
 * The caller owns GEM references, heap policy and physical RAM ownership.
 * Never publish this VM to firmware or hardware. GPU ownership, concurrent
 * GPU use, TLB invalidation and context lifetime are not implemented here.
 *
 * Return positive native errno. Create requires an initially NULL slot.
 * Map failures preserve all existing table bytes and range ownership.
 * Ranges cannot overlap. Unmap must exactly match one successful map.
 * Leaf flags are the A733_GPU_MMU_PTE_* options in the native MMU encoder.
 */
int pvr_vm_create(bus_dma_tag_t, struct pvr_vm **);
int pvr_vm_map(struct pvr_vm *, uint64_t, uint64_t, uint64_t, uint64_t);
int pvr_vm_unmap(struct pvr_vm *, uint64_t, uint64_t);
void pvr_vm_destroy(struct pvr_vm *);

/* Kernel-only address accessor. It does not transfer or publish ownership. */
uint64_t pvr_vm_root_pa(const struct pvr_vm *);

#endif /* _PVR_VM_H_ */
