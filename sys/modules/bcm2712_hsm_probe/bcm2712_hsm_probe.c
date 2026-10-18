/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted finite native-owner acceptance probe. No display MMIO. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <prop/proplib.h>
#include <dev/acpi/acpivar.h>
#include <dev/fdt/fdtvar.h>
#include <arm/fdt/arm_simplefb.h>
#include <arm/broadcom/bcm2712_display_acpi.h>
#include <arm/broadcom/bcm2712_display_hsm.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <libfdt.h>
#include <linux/err.h>
#include "drm_internal.h"

MODULE(MODULE_CLASS_MISC, bcm2712_hsm_probe, NULL);

/* The reference protects the registered render object throughout this cycle. */
static int
hsm_probe_device(struct drm_minor **out, ACPI_HANDLE *handle)
{
	const struct acpi_devnode *node;
	struct drm_minor *minor;
	device_t dev;

	dev = device_find_by_xname("bcmv3d0");
	if (dev == NULL || !device_is_a(dev, "bcmv3d"))
		return ENODEV;
	minor = drm_minor_acquire(128);
	if (IS_ERR(minor))
		return ENODEV;
	if (minor->type != DRM_MINOR_RENDER || minor->dev->dev != dev ||
	    !minor->dev->registered || minor->dev->render != minor ||
	    strcmp(minor->dev->driver->name, "v3d") != 0 ||
	    ACPI_FAILURE(AcpiGetHandle(NULL, "\\_SB.GPU0", handle)))
		goto fail;
	node = acpi_match_node(*handle);
	if (node == NULL || node->ad_device != dev)
		goto fail;
	*out = minor;
	return 0;
fail:
	drm_minor_release(minor);
	return ENODEV;
}

/* A descriptor is a refusal even if a driver has not attached to it. */
static int
hsm_probe_framebuffer(void)
{
	const void *fdt = fdtbus_get_data();
	deviter_t iterator;
	device_t dev;
	int offset, next, depth = -1;

	if (arm_simplefb_physaddr() != 0)
		return EBUSY;
	deviter_init(&iterator, DEVITER_F_ROOT_FIRST);
	while ((dev = deviter_next(&iterator)) != NULL) {
		if (device_is_a(dev, "genfb") || device_is_a(dev, "wsdisplay")) {
			deviter_release(&iterator);
			return EBUSY;
		}
	}
	deviter_release(&iterator);
	if (fdt == NULL || fdt_check_full(fdt, fdt_totalsize(fdt)) != 0)
		return EIO;
	for (offset = -1;; offset = next) {
		const char *name;
		int compatible, length;

		next = fdt_next_node(fdt, offset, &depth);
		if (next < 0 || next <= offset)
			return EIO;
		if (depth < 0) {
			uint32_t tag;

			/* libfdt's full check permits multiple top-level nodes. */
			do {
				tag = fdt_next_tag(fdt, next, &next);
				if (next < 0)
					return EIO;
			} while (tag == FDT_NOP);
			if (tag != FDT_END || (fdt_version(fdt) >= 17 &&
			    (uint32_t)next != fdt_size_dt_struct(fdt)))
				return EIO;
			return 0;
		}
		name = fdt_get_name(fdt, next, &length);
		if (name == NULL || length < 0)
			return EIO;
		if (strncmp(name, "framebuffer", 11) == 0 &&
		    (name[11] == 0 || name[11] == '@'))
			return EBUSY;
		compatible = fdt_node_check_compatible(fdt, next,
		    "simple-framebuffer");
		if (compatible == 0)
			return EBUSY;
		if (compatible < 0 && compatible != -FDT_ERR_NOTFOUND)
			return EIO;
	}
}

static void
hsm_probe_snapshot(device_t dev, const char *stage,
    const struct bcm2712_hsm_status *status)
{

	aprint_normal_dev(dev, "HSM probe %s: last verified state %#x "
	    "rate %u Hz leases %#x fault %d mutation %u\n", stage,
	    status->state, status->rate, status->leases, status->fault,
	    status->mutation_attempted);
}

static int
hsm_probe_cycle(device_t dev, ACPI_HANDLE handle)
{
	struct bcm2712_hsm_provider *provider;
	struct bcm2712_hsm_status status;
	bool entered_lease = false;
	int error, cleanup;

	error = bcm2712_display_hsm_create(dev, handle, &provider);
	if (error != 0)
		return error;
	error = bcm2712_display_hsm_status(provider, &status);
	if (error != 0)
		goto retained;
	hsm_probe_snapshot(dev, "initial", &status);
	if (status.leases != 0 || status.fault != 0 || status.mutation_attempted) {
		error = EIO;
		goto stop;
	}
	error = bcm2712_display_hsm_acquire(provider, 0,
	    BCM2712_DISPLAY_HSM_MIN_RATE);
	if (error != 0)
		goto stop;
	entered_lease = true;
	error = bcm2712_display_hsm_status(provider, &status);
	if (error != 0)
		goto retained;
	hsm_probe_snapshot(dev, "acquired", &status);
	if (status.leases != 1 || status.state != 1 ||
	    status.rate < BCM2712_DISPLAY_HSM_MIN_RATE || status.fault != 0) {
		error = EIO;
		goto stop;
	}
	error = bcm2712_display_hsm_release(provider, 0);
	if (error != 0)
		goto stop;
	error = bcm2712_display_hsm_status(provider, &status);
	if (error != 0)
		goto retained;
	hsm_probe_snapshot(dev, "released (before destroy)", &status);
	if (status.leases != 0 || status.fault != 0 || status.state != 1 ||
	    status.rate < BCM2712_DISPLAY_HSM_MIN_RATE) {
		error = EIO;
		goto stop;
	}
	error = bcm2712_display_hsm_destroy(provider);
	if (error != 0)
		goto stop;
	/* Successful destroy invalidates provider. Do not read it again. */
	aprint_normal_dev(dev, "HSM probe destroy succeeded; finite cycle "
	    "complete; no display MMIO, HPD, reset or clock-lifetime claim\n");
	return 0;
stop:
	if (bcm2712_display_hsm_status(provider, &status) == 0) {
		hsm_probe_snapshot(dev, "stopped", &status);
		if (!entered_lease && !status.mutation_attempted &&
		    status.leases == 0 && status.fault == 0) {
			cleanup = bcm2712_display_hsm_destroy(provider);
			aprint_normal_dev(dev,
			    "HSM probe empty unmutated owner cleanup: %d\n", cleanup);
			if (cleanup == 0)
				return error;
		}
	}
retained:
	/* Native storage has no module callback and keeps exclusive ownership. */
	aprint_error_dev(dev, "HSM probe owner retained; no retry or restore; "
	    "controlled reboot required\n");
	return error;
}

static int
bcm2712_hsm_probe_modcmd(modcmd_t command, void *argument)
{
	prop_dictionary_t properties = argument;
	struct drm_minor *minor;
	ACPI_HANDLE handle;
	const char *expected;
	bool cold_headless = false;
	size_t length;
	int error;

	if (command == MODULE_CMD_FINI)
		return 0;
	if (command != MODULE_CMD_INIT)
		return ENOTTY;
	if (properties == NULL ||
	    !prop_dictionary_get_bool(properties, "cold-headless", &cold_headless) ||
	    !cold_headless || !prop_dictionary_get_cstring_nocopy(properties,
	    "expected-buildinfo", &expected))
		return EINVAL;
	/* This selection guard does not prove matching headers or module ABI. */
	length = strlen(buildinfo);
	if (length != 0 && buildinfo[length - 1] == '\n')
		length--;
	if (length == 0 || strlen(expected) != length ||
	    memcmp(expected, buildinfo, length) != 0)
		return EPROGMISMATCH;
	error = hsm_probe_framebuffer();
	if (error != 0) {
		aprint_error("HSM probe refused: firmware framebuffer/console "
		    "present or absence unverified: %d; zero HSM calls\n", error);
		return error;
	}
	error = hsm_probe_device(&minor, &handle);
	if (error != 0)
		return error;
	error = hsm_probe_cycle(minor->dev->dev, handle);
	drm_minor_release(minor);
	if (error != 0)
		aprint_error("HSM probe failed: %d; no display MMIO\n", error);
	return error;
}
