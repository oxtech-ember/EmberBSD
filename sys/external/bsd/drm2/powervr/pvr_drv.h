/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _PVR_DRV_H_
#define _PVR_DRV_H_

#include <sys/bus.h>
#include <sys/device.h>

struct drm_device;
struct drm_file;
struct file;
struct uvm_object;

/*
 * Call after hardware identification, from a serialized sleepable context.
 * Do not hold the sysctl tree lock. Pass an initially NULL owner slot.
 * Success retains one device reference in that slot until reboot.
 * Failure leaves the slot unchanged. No hot removal is implemented.
 */
int pvr_render_attach(device_t, bus_dma_tag_t, uint64_t, struct drm_device **);
int pvr_create_bo(struct drm_device *, void *, struct drm_file *);
int pvr_get_bo_mmap_offset(struct drm_device *, void *, struct drm_file *);
int pvr_mmap_object(struct drm_device *, off_t, size_t, int,
    struct uvm_object **, voff_t *, struct file *);

#endif /* _PVR_DRV_H_ */
