/* Origin: EmberBSD private BCM2712 renderer attachment. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _ARM_BROADCOM_BCM2712_V3D_RENDER_H_
#define _ARM_BROADCOM_BCM2712_V3D_RENDER_H_

#include <sys/bus.h>
#include <sys/device.h>
#include <dev/acpi/acpivar.h>

struct bcmv3d_bootfb_owner;
/* The opaque boot owner is borrowed for the ACPI attachment lifetime.
 * It does not grant display access, console closure or a writer drain. */
int bcmv3d_render_attach(device_t, bus_space_tag_t, bus_dma_tag_t, ACPI_HANDLE,
    const struct bcmv3d_bootfb_owner *);

#endif
