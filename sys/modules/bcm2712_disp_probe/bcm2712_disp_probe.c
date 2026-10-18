/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted finite DISP16 lease gate. No HVS activation or MMIO access. */
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
#include <arm/broadcom/bcm2835_mbox.h>
#include <arch/evbarm/rpi/vcio.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <libfdt.h>
#include <linux/err.h>
#include "drm_internal.h"

#include "../../arch/arm/broadcom/bcm2712_display_disp.h"
#include "../../arch/arm/broadcom/bcm2712_display_dispvar.h"

MODULE(MODULE_CLASS_MISC, bcm2712_disp_probe, NULL);

/* Hold the registered native render object throughout clock ownership. */
static int
disp_probe_device(struct drm_minor **out, ACPI_HANDLE *handle)
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

/* Keep the accepted descriptor guard even when no framebuffer driver attached. */
static int
disp_probe_framebuffer(void)
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

struct disp_probe_context {
	struct bcm2712_disp_provider *owner;
	struct drm_minor *minor;
	struct bcm2712_disp_status sample;
	const char *stage;
	int operation_error;
	bool adapter_live, sample_valid, held;
};
static struct disp_probe_context disp_probe;

static void
disp_probe_sample(const char *label)
{
	const struct bcm2712_disp_status *status = &disp_probe.sample;

	aprint_normal("DISP16 %s: cached floor %u state %#x rate %u Hz "
	    "minima %u/%u leases %#x fault %d mutation %u\n", label,
	    status->floor, status->state, status->rate, status->minimum[0],
	    status->minimum[1], status->leases, status->fault,
	    status->mutation_attempted);
}

/* No automatic release exists after a hold or an uncertain SET. */
static int
disp_probe_hold(ACPI_HANDLE handle)
{
	struct disp_probe_context *ctx = &disp_probe;
	struct bcm2712_disp_status *status = &ctx->sample;
	const char *failed_stage;
	uint32_t floor;
	int error, operation_error;

	ctx->stage = "adapter-init";
	error = bcm2712_disp_adapter_init();
	if (error != 0) {
		if (error == EWOULDBLOCK)
			goto known_error;
		goto retained;
	}
	ctx->adapter_live = true;
	ctx->stage = "create";
	error = bcm2712_display_disp_create(ctx->minor->dev->dev, handle,
	    &ctx->owner);
	if (error != 0) {
		if (ctx->owner != NULL)
			goto retained;
		goto known_error;
	}
	if (ctx->owner == NULL) {
		error = EIO;
		goto retained;
	}
	ctx->stage = "initial-status";
	error = bcm2712_display_disp_status(ctx->owner, status);
	if (error != 0)
		goto retained;
	ctx->sample_valid = true;
	if (status->floor == 0 || status->leases != 0 || status->fault != 0 ||
	    status->mutation_attempted || status->minimum[0] != 0 ||
	    status->minimum[1] != 0) {
		error = EIO;
		goto retained;
	}
	floor = status->floor;
	ctx->stage = "acquire-floor";
	error = bcm2712_display_disp_acquire(ctx->owner, 0, floor);
	if (error != 0) {
		operation_error = error;
		if (bcm2712_display_disp_status(ctx->owner, status) != 0 ||
		    status->fault != 0 || status->mutation_attempted ||
		    status->leases != 0) {
			error = operation_error;
			goto retained;
		}
		error = operation_error;
		goto known_error;
	}
	ctx->stage = "acquired-status";
	error = bcm2712_display_disp_status(ctx->owner, status);
	if (error != 0)
		goto retained;
	if (status->floor != floor || status->state != 1 || status->rate < floor ||
	    status->leases != 1 || status->minimum[0] != floor ||
	    status->minimum[1] != 0 || status->fault != 0) {
		error = EIO;
		goto retained;
	}
	ctx->held = true;
	disp_probe_sample("hold verified");
	aprint_normal("DISP16 lease/module retained until reboot; no power, "
	    "MMIO, DMA, HVS activation or stop/drain proof\n");
	return 0;
known_error:
	operation_error = error;
	failed_stage = ctx->stage;
	ctx->operation_error = operation_error;
	if (ctx->owner != NULL) {
		ctx->stage = "destroy";
		error = bcm2712_display_disp_destroy(ctx->owner);
		if (error != 0)
			goto retained;
		ctx->owner = NULL;
	}
	if (ctx->adapter_live) {
		ctx->stage = "adapter-fini";
		error = bcm2712_disp_adapter_fini();
		if (error != 0)
			goto retained;
		ctx->adapter_live = false;
	}
	drm_minor_release(ctx->minor);
	ctx->minor = NULL;
	aprint_error("DISP16 hold refused at %s: error %d; provider, mutex "
	    "and render reference released; no hold success\n", failed_stage,
	    operation_error);
	return operation_error;
retained:
	ctx->held = true;
	if (ctx->operation_error == 0)
		ctx->operation_error = error;
	if (ctx->sample_valid)
		disp_probe_sample("last cached before retained failure");
	aprint_error("DISP16 retained fault at %s: error %d operation_error %d; "
	    "no hold success, retry or restore; module/owner/lock/reference "
	    "retained until reboot\n", ctx->stage, error, ctx->operation_error);
	return 0;
}

static int
bcm2712_disp_probe_modcmd(modcmd_t command, void *argument)
{
	static const char * const conflicts[] = {
		"clock-mode", "dvp-mode", "hd-mode", "hpd-mode", "ddc-mode",
		"phy-mode", "iommu-mode"
	};
	prop_dictionary_t properties = argument;
	struct disp_probe_context *ctx = &disp_probe;
	ACPI_HANDLE handle;
	const char *expected, *mode;
	bool cold_headless = false;
	size_t length;
	unsigned int i;
	int error;

	if (command == MODULE_CMD_FINI)
		return ctx->held || ctx->adapter_live || ctx->owner != NULL ||
		    ctx->minor != NULL ? EBUSY : 0;
	if (command != MODULE_CMD_INIT)
		return ENOTTY;
	if (ctx->held || ctx->adapter_live || ctx->owner != NULL ||
	    ctx->minor != NULL)
		return EBUSY;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	if (properties == NULL ||
	    !prop_dictionary_get_bool(properties, "cold-headless", &cold_headless) ||
	    !cold_headless || !prop_dictionary_get_cstring_nocopy(properties,
	    "expected-buildinfo", &expected) ||
	    !prop_dictionary_get_cstring_nocopy(properties, "disp-mode", &mode) ||
	    strcmp(mode, "hold") != 0)
		return EINVAL;
	for (i = 0; i < __arraycount(conflicts); i++) {
		if (prop_dictionary_get(properties, conflicts[i]) != NULL)
			return EINVAL;
	}
	length = strlen(buildinfo);
	if (length != 0 && buildinfo[length - 1] == '\n')
		length--;
	if (length == 0 || strlen(expected) != length ||
	    memcmp(expected, buildinfo, length) != 0)
		return EPROGMISMATCH;
	error = disp_probe_framebuffer();
	if (error != 0)
		return error;
	memset(ctx, 0, sizeof(*ctx));
	error = disp_probe_device(&ctx->minor, &handle);
	if (error != 0)
		return error;
	return disp_probe_hold(handle);
}
