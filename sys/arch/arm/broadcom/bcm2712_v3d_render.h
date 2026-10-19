/* Origin: EmberBSD private BCM2712 renderer attachment. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _ARM_BROADCOM_BCM2712_V3D_RENDER_H_
#define _ARM_BROADCOM_BCM2712_V3D_RENDER_H_

#include <sys/bus.h>
#include <sys/device.h>
#include <dev/acpi/acpivar.h>

struct bcmv3d_bootfb_owner;
struct bcmv3d_device;
/* The opaque boot owner is borrowed for the ACPI attachment lifetime.
 * It does not grant display access, console closure or a writer drain.
 * Return the actual registered renderer through disjoint output storage only
 * on success. The borrowed renderer remains resident until reboot.
 * Failure preserves the output. No device/minor lookup substitutes for it. */
int bcmv3d_render_attach(device_t, bus_space_tag_t, bus_dma_tag_t, ACPI_HANDLE,
    const struct bcmv3d_bootfb_owner *, struct bcmv3d_device **);
/* Compare immutable attachment fields only. No lookup, allocation or I/O. */
int bcmv3d_render_bootfb_validate(const struct bcmv3d_device *, device_t,
    bus_space_tag_t, bus_dma_tag_t, ACPI_HANDLE,
    const struct bcmv3d_bootfb_owner *);

#endif
