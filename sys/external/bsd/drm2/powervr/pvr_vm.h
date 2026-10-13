/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _PVR_VM_H_
#define _PVR_VM_H_

#include <sys/types.h>
#include <sys/bus.h>

struct pvr_vm;

/*
 * Rogue VM, with 40-bit VA, 36-bit PA and 4 KiB GPU pages.
 * The caller serializes all access from a sleepable context. The caller
 * retains the DMA tag and all mapped RAM until unmap or destruction.
 * The caller owns GEM references, heap policy and physical RAM ownership.
 * A new VM is CPU-owned. Before firmware receives its root address, install
 * the invalidate callback through publish(). The owner excludes concurrent
 * GPU jobs from every table mutation. Flush before each later GPU submission.
 * An invalidation failure poisons the VM and retains its tables and mappings.
 * The caller must then retain every GEM reference until a proven GPU reset.
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

/*
 * Publish installs a synchronous device-invalidation callback and flushes.
 * The owner serializes these calls with mutation, firmware submission and
 * cleanup. Unmap defers table reclamation until this callback succeeds.
 * Quiesce requires all jobs and firmware references to be retired first.
 * It flushes once more and returns the VM to CPU ownership on success.
 * Destroy is legal only for a CPU-owned, nonfailed VM. No reset is implied.
 */
int pvr_vm_publish(struct pvr_vm *, int (*)(void *), void *);
int pvr_vm_flush(struct pvr_vm *);
int pvr_vm_quiesce(struct pvr_vm *);

/* Kernel-only address accessor. It does not transfer or publish ownership. */
uint64_t pvr_vm_root_pa(const struct pvr_vm *);

#endif /* _PVR_VM_H_ */
