/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD clean-room implementation of the interface used by
 * MIT-licensed consumers; written without access to the GPL original.
 *
 * Device-class registration and change notification of DRM devices.  On
 * EmberBSD these end in the device monitor (drm618_sysfs.c), not in sysfs.
 */

#ifndef _DRM618_DRM_SYSFS_H_
#define _DRM618_DRM_SYSFS_H_

struct device;
struct drm_connector;
struct drm_device;
struct drm_property;

int drm_class_device_register(struct device *dev);
void drm_class_device_unregister(struct device *dev);

/* The set of connected outputs of a device changed. */
void drm_sysfs_hotplug_event(struct drm_device *dev);

/* The state of one connector changed. */
void drm_sysfs_connector_hotplug_event(struct drm_connector *connector);

/* A property of a connector changed. */
void drm_sysfs_connector_property_event(struct drm_connector *connector,
    struct drm_property *property);

#endif	/* _DRM618_DRM_SYSFS_H_ */
