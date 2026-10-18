/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted finite read-only observer. It never authorizes HVS DMA. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/bus.h>
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

MODULE(MODULE_CLASS_MISC, bcm2712_iommu_observe, NULL);

extern struct bus_space arm_generic_bs_tag;

/* Register facts: pinned bcm2712-iommu.c. See the observer guide. */
static const bus_size_t iommu_observe_offsets[] = {
	0x38, 0x00, 0x04, 0x14, 0x1c, 0x20, 0x24
};
#define IOMMU_OBSERVE_WORDS 7
#define IOMMU_OBSERVE_BOUND (UINT64_C(1) << 36)

struct iommu_observe_sample {
	uint32_t word[2][IOMMU_OBSERVE_WORDS];
	unsigned int valid[2];
	struct bcm2712_display_clocks clocks[2];
	bool clocks_valid[2];
	struct bcm2712_display_clock_failure failure;
	const char *stage;
	uint64_t bypass_end, cap_end;
	bool candidate;
};

/* A synchronous callback. The clock reader does not retain it or its argument. */
static int
iommu_observe_exchange(void *arg, uint32_t *request)
{
	uint32_t response;

	(void)arg;
	return bcmmbox_request(BCMMBOX_CHANARM2VC, request,
	    8 * sizeof(*request), &response);
}

static bool
iommu_observe_supported(uint32_t debug)
{

	return (debug & 0xfU) >= 4 && ((debug >> 4) & 0xfU) >= 6 &&
	    ((debug >> 8) & 0xfU) >= 6 && (debug & (1U << 29)) != 0;
}

/* Equal snapshots can describe this narrow pattern, never a DMA permission. */
static bool
iommu_observe_classify(struct iommu_observe_sample *sample)
{
	const uint32_t *word = sample->word[0];
	uint64_t end, cap;
	unsigned int shift;

	if (!iommu_observe_supported(word[0]) ||
	    (word[1] & ~0x0f1f1f8fU) != 0 || (word[1] & 1U) == 0 ||
	    (word[1] & ((1U << 27) | (1U << 20) | (1U << 12) |
	    (1U << 8) | (1U << 7) | (1U << 3) | (1U << 2))) != 0 ||
	    (word[6] & (1U << 31)) != 0 || word[4] != 0 ||
	    (word[5] & (1U << 31)) == 0 || (word[3] & (1U << 31)) == 0)
		return false;
	shift = (word[0] & (1U << 28)) != 0 ? 22 : 28;
	end = (uint64_t)(word[5] & 0x7fffffffU) << shift;
	cap = ((uint64_t)(word[3] & 0x7fffffffU) + 1) << 28;
	if (end == 0 || end > cap || cap > IOMMU_OBSERVE_BOUND)
		return false;
	sample->bypass_end = end;
	sample->cap_end = cap;
	return true;
}

static int
iommu_observe_cycle(device_t dev, ACPI_HANDLE handle,
    struct iommu_observe_sample *sample)
{
	struct bcm2712_display_contract contract;
	bus_space_handle_t mapping;
	bool mapped = false;
	unsigned int pass, i;
	int error;

	sample->stage = "resources";
	error = bcm2712_display_acpi_resources(dev, handle, &contract);
	if (error != 0)
		return error;
	if (contract.memory[4].address != UINT64_C(0x1000005200) ||
	    contract.memory[4].length != 0x80)
		return EINVAL;
	sample->stage = "pre-clocks";
	error = bcm2712_display_read_clocks_report(iommu_observe_exchange, NULL,
	    &sample->clocks[0], &sample->failure);
	if (error != 0)
		return error;
	sample->clocks_valid[0] = true;
	sample->stage = "map";
	error = bus_space_map(&arm_generic_bs_tag, contract.memory[4].address,
	    contract.memory[4].length, 0, &mapping);
	if (error != 0)
		return error;
	mapped = true;
	sample->stage = "registers";
	aprint_normal("IOMMU4 first resource4 MMIO: read-only; native "
	    "bus_space stall/abort is not recoverable\n");
	for (pass = 0; pass < 2; pass++) {
		for (i = 0; i < IOMMU_OBSERVE_WORDS; i++) {
			sample->word[pass][i] = bus_space_read_4(&arm_generic_bs_tag,
			    mapping, iommu_observe_offsets[i]);
			sample->valid[pass]++;
			if (pass == 0 && i == 0 &&
			    !iommu_observe_supported(sample->word[0][0])) {
				error = EOPNOTSUPP;
				goto out;
			}
		}
	}
	bus_space_unmap(&arm_generic_bs_tag, mapping, contract.memory[4].length);
	mapped = false;
	sample->stage = "post-clocks";
	error = bcm2712_display_read_clocks_report(iommu_observe_exchange, NULL,
	    &sample->clocks[1], &sample->failure);
	if (error != 0)
		return error;
	sample->clocks_valid[1] = true;
	sample->stage = "compare";
	if (memcmp(sample->word[0], sample->word[1],
	    sizeof(sample->word[0])) != 0)
		return EAGAIN;
	for (i = 0; i < BCM2712_DISPLAY_NCLOCK; i++) {
		if (sample->clocks[0].clock[i].id != sample->clocks[1].clock[i].id ||
		    sample->clocks[0].clock[i].state !=
		    sample->clocks[1].clock[i].state ||
		    sample->clocks[0].clock[i].rate != sample->clocks[1].clock[i].rate)
			return EAGAIN;
	}
	sample->candidate = iommu_observe_classify(sample);
	sample->stage = "complete";
	error = 0;
out:
	if (mapped)
		bus_space_unmap(&arm_generic_bs_tag, mapping,
		    contract.memory[4].length);
	return error;
}

/* Hold the registered native render object during resources, GETs and MMIO. */
static int
iommu_observe_device(struct drm_minor **out, ACPI_HANDLE *handle)
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
iommu_observe_framebuffer(void)
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

/* The raw evidence and completion line follow unmap and render-ref release. */
static void
iommu_observe_log(const struct iommu_observe_sample *sample, int error)
{
	unsigned int pass, i;

	for (pass = 0; pass < 2; pass++) {
		if (sample->clocks_valid[pass]) {
			for (i = 0; i < BCM2712_DISPLAY_NCLOCK; i++)
				aprint_normal("IOMMU4 clocks %s index %u: id %u "
				    "state %#x rate %u Hz\n", pass == 0 ? "pre" :
				    "post", i, sample->clocks[pass].clock[i].id,
				    sample->clocks[pass].clock[i].state,
				    sample->clocks[pass].clock[i].rate);
		}
		for (i = 0; i < sample->valid[pass]; i++)
			aprint_normal("IOMMU4 registers pass %u index %u offset "
			    "%#x: %#x\n", pass, i,
			    (unsigned int)iommu_observe_offsets[i], sample->word[pass][i]);
	}
	if (sample->failure.valid)
		aprint_normal("IOMMU4 validated clock failure at %s: id %u "
		    "state %#x rate %u Hz check %u tag %#x\n", sample->stage,
		    sample->failure.clock.id, sample->failure.clock.state,
		    sample->failure.clock.rate, sample->failure.check,
		    sample->failure.check_tag);
	if (error != 0) {
		aprint_error("IOMMU4 observation stopped at %s: error %d "
		    "valid words %u/%u candidate 0; no DMA authorization\n",
		    sample->stage, error, sample->valid[0], sample->valid[1]);
		return;
	}
	aprint_normal("IOMMU4 snapshot complete: candidate %u bypass_end "
	    "%#llx cap_end %#llx; no bus_dma tag or mapping validated; "
	    "no DMA authorization or lifetime proof\n", sample->candidate,
	    (unsigned long long)sample->bypass_end,
	    (unsigned long long)sample->cap_end);
}

static int
bcm2712_iommu_observe_modcmd(modcmd_t command, void *argument)
{
	static const char * const conflicts[] = {
		"clock-mode", "dvp-mode", "hd-mode", "hpd-mode", "ddc-mode",
		"phy-mode"
	};
	prop_dictionary_t properties = argument;
	struct iommu_observe_sample sample;
	struct drm_minor *minor;
	ACPI_HANDLE handle;
	const char *expected, *mode;
	bool cold_headless = false;
	size_t length;
	unsigned int i;
	int error;

	if (command == MODULE_CMD_FINI)
		return 0;
	if (command != MODULE_CMD_INIT)
		return ENOTTY;
	if (properties == NULL ||
	    !prop_dictionary_get_bool(properties, "cold-headless", &cold_headless) ||
	    !cold_headless || !prop_dictionary_get_cstring_nocopy(properties,
	    "expected-buildinfo", &expected) ||
	    !prop_dictionary_get_cstring_nocopy(properties, "iommu-mode", &mode) ||
	    strcmp(mode, "observe4") != 0)
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
	error = iommu_observe_framebuffer();
	if (error != 0)
		return error;
	error = iommu_observe_device(&minor, &handle);
	if (error != 0)
		return error;
	memset(&sample, 0, sizeof(sample));
	error = iommu_observe_cycle(minor->dev->dev, handle, &sample);
	drm_minor_release(minor);
	iommu_observe_log(&sample, error);
	return error;
}
