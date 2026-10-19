/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted finite CPU/DMA-map preparation. No HVS hardware access. */
#include <sys/param.h>
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
#include <linux/err.h>
#include "drm_internal.h"
#include "bcmv3d_drv.h"
#include "bcmv3d_hvs_prepare.h"
#include "bcmv3d_mmu4_backing.h"
#include <arm/broadcom/bcm2712_mmu4_corevar.h>

MODULE(MODULE_CLASS_MISC, bcmv3d_hvs_prepare, NULL);

/* A failed private cleanup must not discard the render reference or code. */
static struct bcmv3d_mmu4_backing *hvs_retained_backing;
static struct drm_minor *hvs_retained_minor;

static int
hvs_prepare_device(struct drm_minor **out, ACPI_HANDLE *handle)
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
static int
hvs_prepare_framebuffer(void)
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

/* Pattern policy belongs to this finite module, not to the memory helper. */
static uint32_t
hvs_prepare_pixel(size_t index)
{

	return (uint32_t)((index * 2654435761U) ^ 0x005aa55aU) & 0x00ffffffU;
}

static int
hvs_prepare_cycle(struct bcmv3d_device *sc)
{
	struct bcmv3d_bo_stats before, during, after;
	struct bcmv3d_hvs_frame *frame;
	volatile uint32_t *pixels;
	size_t i, count;
	int error;

	bcmv3d_bo_snapshot(sc, &before);
	/* Current GPU0 tag only. This is not an HVS/IOMMU address-path proof. */
	error = bcmv3d_hvs_prepare(sc, sc->dmat, &frame);
	if (error != 0)
		return error;
	pixels = frame->vaddr;
	count = frame->size / sizeof(*pixels);
	for (i = 0; i < count; i++)
		pixels[i] = hvs_prepare_pixel(i);
	for (i = 0; i < count; i++) {
		if (pixels[i] != hvs_prepare_pixel(i)) {
			error = EIO;
			goto destroy;
		}
	}
	bcmv3d_hvs_prewrite(frame);
	bcmv3d_bo_snapshot(sc, &during);
	aprint_normal_dev(sc->dev, "HVS preparation: %ux%u XRGB8888 pitch "
	    "%u logical %u backing %ju consumer DMA %#jx; pattern verified "
	    "and all backing bytes PREWRITE; no publication/address-path proof\n",
	    BCMV3D_HVS_WIDTH, BCMV3D_HVS_HEIGHT, frame->pitch, BCMV3D_HVS_BYTES,
	    (uintmax_t)frame->size, (uintmax_t)frame->address);
	if (during.reserved_bytes < before.reserved_bytes ||
	    during.reserved_bytes - before.reserved_bytes != frame->size)
		error = EBUSY;
destroy:
	/* No descriptor, LPTR, job, IRQ or other consumer has seen this frame. */
	bcmv3d_hvs_destroy(frame);
	bcmv3d_bo_snapshot(sc, &after);
	aprint_normal_dev(sc->dev, "HVS preparation accounting: before %ju "
	    "after %ju; no concurrent GPU clients is an external prerequisite\n",
	    (uintmax_t)before.reserved_bytes, (uintmax_t)after.reserved_bytes);
	if (after.reserved_bytes != before.reserved_bytes && error == 0)
		error = EBUSY;
	return error;
}

/* The supplied DEBUG is an encoder profile, not a live MMIO observation. */
static int
hvs_prepare_mmu4_cycle(struct bcmv3d_device *sc, uint32_t debug)
{
	struct bcmv3d_mmu4_backing *backing = NULL;
	struct bcmv3d_mmu4_backing_status status;
	struct bcm2712_mmu4_config cfg;
	struct bcmv3d_bo_stats before, during, after;
	uint64_t fragments;
	int error, close_error;

	bcmv3d_bo_snapshot(sc, &before);
	error = bcmv3d_mmu4_backing_create(sc, debug, &backing);
	if (error != 0)
		goto account;
	error = bcmv3d_mmu4_backing_get_config(backing, &cfg);
	if (error == 0)
		error = bcmv3d_mmu4_backing_prepare(backing, &cfg);
	if (error == 0)
		error = bcmv3d_mmu4_backing_status(backing, &status);
	if (error == 0) {
		fragments = (status.frame_bytes + 4 * status.control_bytes) / 4096;
		if (status.frame_bytes != (((size_t)BCMV3D_HVS_BYTES + PAGE_SIZE - 1) &
		    ~((size_t)PAGE_SIZE - 1)) || status.control_bytes != PAGE_SIZE ||
		    !status.prepare_started || !status.prepared || status.sealed ||
		    status.mapping_uncertain ||
		    status.stage != BCMV3D_MMU4_BACKING_PREPARED ||
		    status.first_error != 0 || status.syncs != 5 ||
		    status.fragments != fragments ||
		    status.pmap_calls != 2 * fragments)
			error = EIO;
		bcmv3d_bo_snapshot(sc, &during);
		if (error == 0 && (during.reserved_bytes < before.reserved_bytes ||
		    during.reserved_bytes - before.reserved_bytes != status.frame_bytes))
			error = EBUSY;
		aprint_normal_dev(sc->dev, "MMU4 preparation: error %d, frame %ju, "
		    "control %ju, fragments %u, pmap calls %u, syncs %u; "
		    "supplied DEBUG profile %#x; no MMIO or address publication\n",
		    error, (uintmax_t)status.frame_bytes,
		    (uintmax_t)status.control_bytes, status.fragments,
		    status.pmap_calls, status.syncs, debug);
	}
	close_error = bcmv3d_mmu4_backing_destroy_unexposed(backing);
	if (close_error != 0) {
		hvs_retained_backing = backing;
		aprint_error_dev(sc->dev, "MMU4 preparation cleanup failed: %d; "
		    "retaining private backing until reboot\n", close_error);
		return error != 0 ? error : close_error;
	}
account:
	bcmv3d_bo_snapshot(sc, &after);
	aprint_normal_dev(sc->dev, "MMU4 preparation accounting: before %ju "
	    "after %ju; no concurrent GPU clients is an external prerequisite\n",
	    (uintmax_t)before.reserved_bytes, (uintmax_t)after.reserved_bytes);
	if (after.reserved_bytes != before.reserved_bytes && error == 0)
		error = EBUSY;
	return error;
}

static int
bcmv3d_hvs_prepare_modcmd(modcmd_t command, void *argument)
{
	prop_dictionary_t properties = argument;
	struct bcm2712_display_contract contract;
	struct drm_minor *minor;
	struct bcmv3d_device *sc;
	ACPI_HANDLE handle;
	const char *expected;
	bool prepare_only = false, mmu4_prepare = false;
	uint32_t debug = 0;
	size_t length;
	int error;

	if (command == MODULE_CMD_FINI)
		return hvs_retained_backing != NULL || hvs_retained_minor != NULL ?
		    EBUSY : 0;
	if (command != MODULE_CMD_INIT)
		return ENOTTY;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	if (properties == NULL ||
	    prop_dictionary_get(properties, "clock-mode") != NULL ||
	    prop_dictionary_get(properties, "dvp-mode") != NULL ||
	    prop_dictionary_get(properties, "hd-mode") != NULL ||
	    prop_dictionary_get(properties, "hpd-mode") != NULL ||
	    prop_dictionary_get(properties, "ddc-mode") != NULL ||
	    prop_dictionary_get(properties, "phy-mode") != NULL ||
	    prop_dictionary_get(properties, "iommu-mode") != NULL ||
	    prop_dictionary_get(properties, "disp-mode") != NULL ||
	    !prop_dictionary_get_bool(properties,
	    "prepare-only", &prepare_only) || !prepare_only ||
	    !prop_dictionary_get_cstring_nocopy(properties,
	    "expected-buildinfo", &expected))
		return EINVAL;
	if (hvs_retained_backing != NULL || hvs_retained_minor != NULL)
		return EBUSY;
	if (prop_dictionary_get(properties, "mmu4-prepare-only") != NULL &&
	    (!prop_dictionary_get_bool(properties,
	    "mmu4-prepare-only", &mmu4_prepare) ||
	    !mmu4_prepare))
		return EINVAL;
	if (mmu4_prepare) {
		if (!prop_dictionary_get_uint32(properties,
		    "expected-mmu4-debug", &debug))
			return EINVAL;
	} else if (prop_dictionary_get(properties, "expected-mmu4-debug") != NULL) {
		return EINVAL;
	}
	length = strlen(buildinfo);
	if (length != 0 && buildinfo[length - 1] == '\n')
		length--;
	if (length == 0 || strlen(expected) != length ||
	    memcmp(expected, buildinfo, length) != 0)
		return EPROGMISMATCH;
	error = hvs_prepare_framebuffer();
	if (error != 0)
		return error;
	error = hvs_prepare_device(&minor, &handle);
	if (error != 0)
		return error;
	sc = minor->dev->dev_private;
	if (sc == NULL || sc->drm != minor->dev || sc->dev != minor->dev->dev ||
	    sc->handle != handle || sc->dmat == NULL)
		error = ENODEV;
	else
		error = bcm2712_display_acpi_resources(sc->dev, handle, &contract);
	if (error == 0)
		error = mmu4_prepare ? hvs_prepare_mmu4_cycle(sc, debug) :
		    hvs_prepare_cycle(sc);
	if (hvs_retained_backing != NULL) {
		hvs_retained_minor = minor;
		aprint_error("MMU4 preparation failed: %d; retaining module and "
		    "render reference for failed cleanup, not successful preparation\n",
		    error);
		return 0;
	}
	/* All unexposed maps/GEM refs are gone before the render reference. */
	drm_minor_release(minor);
	if (error != 0)
		aprint_error("HVS preparation failed: %d; no hardware exposure\n", error);
	return error;
}
