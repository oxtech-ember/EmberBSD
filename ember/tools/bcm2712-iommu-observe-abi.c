/* SPDX-License-Identifier: BSD-2-Clause */
/* AI-assisted layout sample. Compare selected kernel and module ELF sections.
 * It does not prove export, source, power, mapping or runtime compatibility.
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
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <libfdt.h>
#include "drm_internal.h"
#define OFFSET(t, m) __builtin_offsetof(struct t, m)
const uint64_t bcm2712_iommu_observe_abi[]
    __attribute__((__used__, __section__(".rodata.bcm2712_iommu_observe_abi"))) = {
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
	sizeof(struct drm_minor), _Alignof(struct drm_minor),
	OFFSET(drm_minor, type), OFFSET(drm_minor, dev),
	sizeof(struct drm_device), _Alignof(struct drm_device),
	OFFSET(drm_device, dev), OFFSET(drm_device, driver),
	OFFSET(drm_device, registered), OFFSET(drm_device, render),
	sizeof(struct drm_driver), _Alignof(struct drm_driver), OFFSET(drm_driver, name),
	sizeof(struct acpi_devnode), _Alignof(struct acpi_devnode), OFFSET(acpi_devnode, ad_device),
	sizeof(struct bcm2712_display_memory), _Alignof(struct bcm2712_display_memory),
	OFFSET(bcm2712_display_memory, address),
	OFFSET(bcm2712_display_memory, length),
	sizeof(struct bcm2712_display_contract), _Alignof(struct bcm2712_display_contract),
	OFFSET(bcm2712_display_contract, memory),
	sizeof(struct bcm2712_display_clock), _Alignof(struct bcm2712_display_clock),
	OFFSET(bcm2712_display_clock, id), OFFSET(bcm2712_display_clock, state),
	OFFSET(bcm2712_display_clock, rate),
	sizeof(struct bcm2712_display_clocks), _Alignof(struct bcm2712_display_clocks),
	OFFSET(bcm2712_display_clocks, clock),
	sizeof(struct bcm2712_display_clock_failure), _Alignof(struct bcm2712_display_clock_failure),
	OFFSET(bcm2712_display_clock_failure, clock),
	OFFSET(bcm2712_display_clock_failure, check_tag),
	OFFSET(bcm2712_display_clock_failure, check),
	OFFSET(bcm2712_display_clock_failure, valid),
	sizeof(enum bcm2712_display_clock_check),
	sizeof(struct bus_space), _Alignof(struct bus_space), OFFSET(bus_space, bs_cookie),
	OFFSET(bus_space, bs_map), OFFSET(bus_space, bs_unmap),
	OFFSET(bus_space, bs_r_4),
	sizeof(bus_space_handle_t), _Alignof(bus_space_handle_t),
	sizeof(bus_addr_t), _Alignof(bus_addr_t),
	sizeof(bus_size_t), _Alignof(bus_size_t),
	sizeof(deviter_t), _Alignof(deviter_t),
	sizeof(bool), sizeof(void *)
};
