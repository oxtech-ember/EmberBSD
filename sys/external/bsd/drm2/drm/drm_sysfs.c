/* Origin: EmberBSD native DRM device-change notifications. */
/*	$NetBSD: drm_sysfs.c,v 1.9 2021/12/19 12:30:31 riastradh Exp $	*/

/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Taylor R. Campbell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: drm_sysfs.c,v 1.9 2021/12/19 12:30:31 riastradh Exp $");

#include <sys/cpu.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <prop/proplib.h>

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_connector.h>
#include <drm/drm_sysfs.h>

#include "../dist/drm/drm_internal.h"

int
drm_sysfs_connector_add(struct drm_connector *connector)
{
	connector->kdev = connector->dev->dev; /* XXX */
	return 0;
}

void
drm_sysfs_connector_remove(struct drm_connector *connector)
{
}

/*
 * Publish an advisory primary-node change through the native device monitor.
 * Callers retain the DRM device and stop notification work before teardown.
 * This function can sleep. IRQ handlers must defer to process context.
 */
static void
drm_sysfs_device_change(struct drm_device *dev)
{
	prop_dictionary_t event;
	char path[32];
	int length;

	if (dev == NULL || !dev->registered || dev->primary == NULL ||
	    dev->dev == NULL || dev->primary->index < 0)
		return;
	if (cpu_intr_p() || cpu_softintr_p()) {
		KASSERTMSG(false, "DRM hotplug requires process context");
		return;
	}

	length = snprintf(path, sizeof(path), "dri/card%d", dev->primary->index);
	if (length < 0 || (size_t)length >= sizeof(path))
		return;
	event = prop_dictionary_create();
	if (event == NULL)
		return;
	if (!prop_dictionary_set_string(event, "device", path) ||
	    !prop_dictionary_set_string(event, "parent", device_xname(dev->dev))) {
		prop_object_release(event);
		return;
	}

	/* Success consumes event, including the no-listener case. */
	if ((*devmon_insert_vec)("device-change", event) != 0)
		prop_object_release(event);
}

void
drm_sysfs_hotplug_event(struct drm_device *dev)
{
	drm_sysfs_device_change(dev);
}

void
drm_sysfs_connector_status_event(struct drm_connector *connector,
    struct drm_property *prop)
{
	if (connector != NULL)
		drm_sysfs_device_change(connector->dev);
}
