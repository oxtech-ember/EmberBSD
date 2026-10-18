/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted finite acceptance probe. Only explicit modes access shared DVP. */
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
#include <arm/broadcom/bcm2712_display_dvp.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <libfdt.h>
#include <linux/err.h>
#include "drm_internal.h"

MODULE(MODULE_CLASS_MISC, bcm2712_hsm_probe, NULL);

extern struct bus_space arm_generic_bs_tag;

enum hsm_probe_mode {
	HSM_PROBE_CLOCK,
	HSM_PROBE_OBSERVE,
	HSM_PROBE_GATE0
};
struct hsm_probe_dvp {
	struct bcm2712_dvp_provider *owner;
	bus_space_handle_t handle;
	bool mapped;
	const char *stage;
	struct bcm2712_dvp_status sample;
	const char *sample_stage;
};

static void
hsm_probe_dvp_snapshot(device_t dev, const char *stage,
    const struct bcm2712_dvp_status *status)
{

	aprint_normal_dev(dev, "DVP probe %s: last verified gates %#x resets "
	    "%#x leases %#x fault %d mutation %u\n", stage, status->gates,
	    status->resets, status->leases, status->fault,
	    status->mutation_attempted);
}

/* The caller holds a verified HSM lease for this entire shared-resource cycle. */
static int
hsm_probe_dvp_cycle(device_t dev, ACPI_HANDLE handle, enum hsm_probe_mode mode,
    struct hsm_probe_dvp *dvp)
{
	struct bcm2712_display_contract contract;
	struct bcm2712_dvp_status initial, status;
	int error;

	dvp->stage = "resources";
	error = bcm2712_display_acpi_resources(dev, handle, &contract);
	if (error != 0)
		return error;
	dvp->stage = "map";
	error = bus_space_map(&arm_generic_bs_tag,
	    contract.memory[BCM2712_DVP_RESOURCE].address,
	    contract.memory[BCM2712_DVP_RESOURCE].length, 0, &dvp->handle);
	if (error != 0)
		return error;
	dvp->mapped = true;
	aprint_normal_dev(dev, "DVP probe %s: normal-active-SoC prior-art "
	    "assumption; first shared resource10 MMIO; bus_space stall/abort "
	    "is not recoverable; no ACPI power or display-lifetime claim\n",
	    mode == HSM_PROBE_OBSERVE ? "observe" : "gate0");
	dvp->stage = "create";
	error = bcm2712_display_dvp_create(dev, &contract,
	    &contract.memory[BCM2712_DVP_RESOURCE], &arm_generic_bs_tag,
	    dvp->handle, &dvp->owner);
	if (error != 0)
		return error;
	dvp->stage = "initial-status";
	error = bcm2712_display_dvp_status(dvp->owner, &initial);
	if (error != 0)
		return error;
	dvp->sample = initial;
	dvp->sample_stage = dvp->stage;
	hsm_probe_dvp_snapshot(dev, "initial", &initial);
	if (initial.leases != 0 || initial.fault != 0 || initial.mutation_attempted)
		return EIO;
	if ((initial.resets & (1U << 1)) != 0)
		return EBUSY;
	if (mode == HSM_PROBE_GATE0) {
		dvp->stage = "acquire0";
		error = bcm2712_display_dvp_acquire(dvp->owner, 0);
		if (error != 0)
			return error;
		dvp->stage = "acquired-status";
		error = bcm2712_display_dvp_status(dvp->owner, &status);
		if (error != 0)
			return error;
		dvp->sample = status;
		dvp->sample_stage = dvp->stage;
		hsm_probe_dvp_snapshot(dev, "acquired", &status);
		if (status.leases != 1 || status.fault != 0 ||
		    status.gates != (initial.gates & ~(1U << 3)) ||
		    status.resets != initial.resets)
			return EIO;
		dvp->stage = "release0";
		error = bcm2712_display_dvp_release(dvp->owner, 0);
		if (error != 0)
			return error;
		dvp->stage = "released-status";
		error = bcm2712_display_dvp_status(dvp->owner, &status);
		if (error != 0)
			return error;
		dvp->sample = status;
		dvp->sample_stage = dvp->stage;
		hsm_probe_dvp_snapshot(dev, "released (before destroy)", &status);
		if (status.leases != 0 || status.fault != 0 ||
		    status.gates != initial.gates || status.resets != initial.resets)
			return EIO;
	}
	dvp->stage = "destroy";
	error = bcm2712_display_dvp_destroy(dvp->owner);
	if (error != 0)
		return error;
	dvp->owner = NULL;
	bus_space_unmap(&arm_generic_bs_tag, dvp->handle,
	    contract.memory[BCM2712_DVP_RESOURCE].length);
	dvp->mapped = false;
	dvp->stage = "unmapped";
	return 0;
}

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
hsm_probe_cycle(device_t dev, ACPI_HANDLE handle, enum hsm_probe_mode mode)
{
	struct bcm2712_hsm_provider *provider;
	struct bcm2712_hsm_status status, sample = { 0 };
	struct hsm_probe_dvp dvp = { .stage = "not-entered" };
	const char *stage, *sample_stage = NULL;
	bool entered_lease = false;
	int error, cleanup;

	error = bcm2712_display_hsm_create(dev, handle, &provider);
	if (error != 0)
		return error;
	stage = "initial-status";
	error = bcm2712_display_hsm_status(provider, &status);
	if (error != 0)
		goto retained;
	sample = status;
	sample_stage = stage;
	hsm_probe_snapshot(dev, "initial", &status);
	if (status.leases != 0 || status.fault != 0 || status.mutation_attempted) {
		error = EIO;
		goto stop;
	}
	stage = "acquire0";
	error = bcm2712_display_hsm_acquire(provider, 0,
	    BCM2712_DISPLAY_HSM_MIN_RATE);
	if (error != 0)
		goto stop;
	entered_lease = true;
	stage = "acquired-status";
	error = bcm2712_display_hsm_status(provider, &status);
	if (error != 0)
		goto retained;
	sample = status;
	sample_stage = stage;
	hsm_probe_snapshot(dev, "acquired", &status);
	if (status.leases != 1 || status.state != 1 ||
	    status.rate < BCM2712_DISPLAY_HSM_MIN_RATE || status.fault != 0) {
		error = EIO;
		goto stop;
	}
	if (mode != HSM_PROBE_CLOCK) {
		stage = "DVP-cycle";
		error = hsm_probe_dvp_cycle(dev, handle, mode, &dvp);
		if (error != 0)
			goto retained;
	}
	stage = "release0";
	error = bcm2712_display_hsm_release(provider, 0);
	if (error != 0)
		goto stop;
	stage = "released-status";
	error = bcm2712_display_hsm_status(provider, &status);
	if (error != 0)
		goto retained;
	sample = status;
	sample_stage = stage;
	hsm_probe_snapshot(dev, "released (before destroy)", &status);
	if (status.leases != 0 || status.fault != 0 || status.state != 1 ||
	    status.rate < BCM2712_DISPLAY_HSM_MIN_RATE) {
		error = EIO;
		goto stop;
	}
	stage = "destroy";
	error = bcm2712_display_hsm_destroy(provider);
	if (error != 0)
		goto stop;
	/* Successful destroy invalidates provider. Do not read it again. */
	aprint_normal_dev(dev, "HSM probe destroy succeeded; finite cycle "
	    "complete; %s; no HPD, reset or clock-lifetime claim\n",
	    mode == HSM_PROBE_CLOCK ? "no display MMIO" : "shared DVP resource10 only");
	return 0;
stop:
	if (bcm2712_display_hsm_status(provider, &status) == 0) {
		sample = status;
		sample_stage = "stopped-status";
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
	if (mode != HSM_PROBE_CLOCK) {
		aprint_error_dev(dev, "DVP probe stopped at %s: error %d "
		    "mapping retained %u native owner retained %u\n", dvp.stage,
		    error, dvp.mapped, dvp.owner != NULL);
		aprint_error_dev(dev, "DVP probe sample valid %u at %s; "
		    "pre-failure cached values only; current status unknown\n",
		    dvp.sample_stage != NULL,
		    dvp.sample_stage != NULL ? dvp.sample_stage : "none");
		if (dvp.sample_stage != NULL)
			hsm_probe_dvp_snapshot(dev, "retained cached", &dvp.sample);
	}
	aprint_error_dev(dev, "HSM probe sample valid %u at %s; cached "
	    "values only; current status unknown\n", sample_stage != NULL,
	    sample_stage != NULL ? sample_stage : "none");
	if (sample_stage != NULL)
		hsm_probe_snapshot(dev, "retained cached", &sample);
	aprint_error_dev(dev, "HSM probe owner retained at %s; no retry or "
	    "restore; controlled reboot required\n", stage);
	return error;
}

static int
bcm2712_hsm_probe_modcmd(modcmd_t command, void *argument)
{
	prop_dictionary_t properties = argument;
	struct drm_minor *minor;
	ACPI_HANDLE handle;
	const char *expected, *dvp_mode;
	enum hsm_probe_mode mode = HSM_PROBE_CLOCK;
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
	if (prop_dictionary_get(properties, "dvp-mode") != NULL) {
		if (!prop_dictionary_get_cstring_nocopy(properties, "dvp-mode", &dvp_mode))
			return EINVAL;
		if (strcmp(dvp_mode, "observe") == 0)
			mode = HSM_PROBE_OBSERVE;
		else if (strcmp(dvp_mode, "gate0") == 0)
			mode = HSM_PROBE_GATE0;
		else
			return EINVAL;
	}
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
	error = hsm_probe_cycle(minor->dev->dev, handle, mode);
	drm_minor_release(minor);
	if (error != 0)
		aprint_error("HSM probe failed: %d; no per-controller MMIO\n", error);
	return error;
}
