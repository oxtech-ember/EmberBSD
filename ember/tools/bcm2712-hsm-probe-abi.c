/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted configuration and layout sample. Compile it with the selected
 * kernel and module flags. Compare the dedicated ELF sections. This checks
 * three configuration definitions and the layouts below;
 * it does not establish symbol, source, option, or runtime compatibility.
 */
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <prop/proplib.h>
#include <dev/acpi/acpivar.h>
#include <dev/fdt/fdtvar.h>
#include <arm/fdt/arm_simplefb.h>
#include <arm/broadcom/bcm2712_display_acpi.h>
#include <arm/broadcom/bcm2712_display_dvp.h>
#include <arm/broadcom/bcm2712_display_hsm.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <libfdt.h>
#include "drm_internal.h"

#define OFFSET(t, m)	__builtin_offsetof(struct t, m)

const uint64_t bcm2712_hsm_probe_abi[]
    __attribute__((__used__, __section__(".rodata.bcm2712_hsm_probe_abi"))) = {
#ifdef CONFIG_ARM64
	1,
#else
	0,
#endif
#ifdef CONFIG_ARM
	1,
#else
	0,
#endif
#ifdef CONFIG_DRM_LEGACY
	1,
#else
	0,
#endif
	sizeof(struct drm_minor),
	OFFSET(drm_minor, type),
	OFFSET(drm_minor, dev),
	sizeof(struct drm_device),
	OFFSET(drm_device, dev),
	OFFSET(drm_device, driver),
	OFFSET(drm_device, registered),
	OFFSET(drm_device, render),
	sizeof(struct drm_driver),
	OFFSET(drm_driver, name),
	sizeof(struct acpi_devnode),
	OFFSET(acpi_devnode, ad_device),
	sizeof(struct bcm2712_hsm_status),
	OFFSET(bcm2712_hsm_status, minimum),
	OFFSET(bcm2712_hsm_status, rate),
	OFFSET(bcm2712_hsm_status, state),
	OFFSET(bcm2712_hsm_status, leases),
	OFFSET(bcm2712_hsm_status, fault),
	OFFSET(bcm2712_hsm_status, mutation_attempted),
	sizeof(struct bcm2712_dvp_status),
	OFFSET(bcm2712_dvp_status, gates),
	OFFSET(bcm2712_dvp_status, resets),
	OFFSET(bcm2712_dvp_status, leases),
	OFFSET(bcm2712_dvp_status, fault),
	OFFSET(bcm2712_dvp_status, mutation_attempted),
	sizeof(struct bus_space),
	OFFSET(bus_space, bs_cookie),
	OFFSET(bus_space, bs_map),
	OFFSET(bus_space, bs_unmap),
	OFFSET(bus_space, bs_barrier),
	OFFSET(bus_space, bs_r_4),
	OFFSET(bus_space, bs_w_4),
	sizeof(bus_space_handle_t),
	sizeof(bus_addr_t),
	sizeof(bus_size_t),
	sizeof(bool),
	sizeof(void *)
};
