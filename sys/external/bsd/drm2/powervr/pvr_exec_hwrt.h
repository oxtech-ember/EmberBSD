/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted native interface. */
#ifndef _PVR_EXEC_HWRT_H_
#define _PVR_EXEC_HWRT_H_
#include <sys/types.h>
struct drm_device;
struct drm_file;
struct pvr_hwrt;
struct pvr_hwrt_target;
struct pvr_file_vm;
struct idr;

/*
 * HWRT dataset ownership: two free-list references keep their VM and all
 * mappings alive. All holds are released only after the firmware cleanup
 * acknowledgement. A failed cleanup retains the owner, its VM and every
 * dependency through device orphan retention. Requires the device and
 * backend gates for every entry point except target's read of a held ref.
 */
struct pvr_exec_hwrt *pvr_exec_hwrt_get(struct drm_file *, uint32_t);
void pvr_exec_hwrt_put(struct drm_device *, struct pvr_exec_hwrt *);
struct pvr_file_vm *pvr_exec_hwrt_vm(const struct pvr_exec_hwrt *);
int pvr_exec_hwrt_target(struct pvr_exec_hwrt *, unsigned,
    struct pvr_hwrt_target *);

int pvr_create_hwrt_dataset(struct drm_device *, void *, struct drm_file *);
int pvr_destroy_hwrt_dataset(struct drm_device *, void *, struct drm_file *);
#endif /* _PVR_EXEC_HWRT_H_ */
