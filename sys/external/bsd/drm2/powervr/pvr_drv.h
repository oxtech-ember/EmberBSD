/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _PVR_DRV_H_
#define _PVR_DRV_H_

#include <sys/bus.h>
#include <sys/device.h>

struct drm_device;
struct pvr_query_input;
struct drm_file;
struct drm_gem_object;
struct file;
struct uvm_object;

/*
 * Call with metadata from prepared firmware, from a serialized sleepable context.
 * Do not hold the sysctl tree lock. Pass an initially NULL owner slot.
 * Success retains one device reference in that slot until reboot.
 * Failure leaves the slot unchanged. No hot removal is implemented.
 */
int pvr_render_attach(device_t, bus_dma_tag_t, uint64_t,
    const struct pvr_query_input *, struct drm_device **);
int pvr_create_bo(struct drm_device *, void *, struct drm_file *);
int pvr_get_bo_mmap_offset(struct drm_device *, void *, struct drm_file *);
/* The caller retains a GEM reference. Return positive native errno. */
int pvr_bo_mapping(struct drm_gem_object *, uint64_t, uint64_t,
    uint64_t *, uint64_t *);
int pvr_file_open(struct drm_device *, struct drm_file *);
void pvr_file_close(struct drm_device *, struct drm_file *);
int pvr_create_vm_context(struct drm_device *, void *, struct drm_file *);
int pvr_destroy_vm_context(struct drm_device *, void *, struct drm_file *);
int pvr_vm_map_ioctl(struct drm_device *, void *, struct drm_file *);
int pvr_vm_unmap_ioctl(struct drm_device *, void *, struct drm_file *);
int pvr_mmap_object(struct drm_device *, off_t, size_t, int,
    struct uvm_object **, voff_t *, struct file *);

#endif /* _PVR_DRV_H_ */
