/* SPDX-License-Identifier: BSD-2-Clause */
/* AI-assisted layout sample. Compare selected kernel and module ELF sections.
 * New DISP types are private source exceptions. This is not runtime proof.
 */
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <prop/proplib.h>
#include <dev/acpi/acpivar.h>
#include <dev/fdt/fdtvar.h>
#include <arm/fdt/arm_simplefb.h>
#include <arm/broadcom/bcm2712_display_acpi.h>
#include <arm/broadcom/bcm2712_display_clockvar.h>
#include <arm/broadcom/bcm2712_display_hsm.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <libfdt.h>
#include "drm_internal.h"
#include "../../sys/arch/arm/broadcom/bcm2712_display_disp.h"
#define OFFSET(t, m) __builtin_offsetof(struct t, m)
const uint64_t bcm2712_disp_probe_abi[]
    __attribute__((__used__, __section__(".rodata.bcm2712_disp_probe_abi"))) = {
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
	sizeof(struct drm_driver), _Alignof(struct drm_driver),
	OFFSET(drm_driver, name),
	sizeof(struct acpi_devnode), _Alignof(struct acpi_devnode),
	OFFSET(acpi_devnode, ad_device),
	sizeof(struct bcm2712_display_memory), _Alignof(struct bcm2712_display_memory),
	OFFSET(bcm2712_display_memory, address), OFFSET(bcm2712_display_memory, length),
	sizeof(struct bcm2712_display_contract), _Alignof(struct bcm2712_display_contract),
	OFFSET(bcm2712_display_contract, memory),
	sizeof(struct bcm2712_clock_status), _Alignof(struct bcm2712_clock_status),
	OFFSET(bcm2712_clock_status, minimum), OFFSET(bcm2712_clock_status, floor),
	OFFSET(bcm2712_clock_status, rate), OFFSET(bcm2712_clock_status, state),
	OFFSET(bcm2712_clock_status, leases), OFFSET(bcm2712_clock_status, fault),
	OFFSET(bcm2712_clock_status, mutation_attempted),
	sizeof(struct bcm2712_clock), _Alignof(struct bcm2712_clock),
	OFFSET(bcm2712_clock, exchange), OFFSET(bcm2712_clock, arg),
	OFFSET(bcm2712_clock, status), OFFSET(bcm2712_clock, profile),
	OFFSET(bcm2712_clock, initial_state), OFFSET(bcm2712_clock, initialized),
	OFFSET(bcm2712_clock, rate_changed), OFFSET(bcm2712_clock, state_changed),
	sizeof(enum bcm2712_clock_profile),
	BCM2712_CLOCK_HSM, BCM2712_CLOCK_BVB,
	sizeof(struct bcm2712_hsm_status), _Alignof(struct bcm2712_hsm_status),
	OFFSET(bcm2712_hsm_status, minimum), OFFSET(bcm2712_hsm_status, rate),
	OFFSET(bcm2712_hsm_status, state), OFFSET(bcm2712_hsm_status, leases),
	OFFSET(bcm2712_hsm_status, fault), OFFSET(bcm2712_hsm_status, mutation_attempted),
	sizeof(struct bcm2712_disp_status), _Alignof(struct bcm2712_disp_status),
	OFFSET(bcm2712_disp_status, minimum), OFFSET(bcm2712_disp_status, floor),
	OFFSET(bcm2712_disp_status, rate), OFFSET(bcm2712_disp_status, state),
	OFFSET(bcm2712_disp_status, leases), OFFSET(bcm2712_disp_status, fault),
	OFFSET(bcm2712_disp_status, mutation_attempted),
	sizeof(deviter_t), _Alignof(deviter_t),
	sizeof(kmutex_t), _Alignof(kmutex_t),
	sizeof(bool), sizeof(void *)
};
