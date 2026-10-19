/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted finite read-only observer. It grants no ownership or drain. */
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

MODULE(MODULE_CLASS_MISC, bcm2712_hvs_observe, NULL);

extern struct bus_space arm_generic_bs_tag;

/* Facts: pinned vc4_regs.h and C0/D0 readable dump sets. See the guide. */
#define HVS_OBSERVE_WORDS 8
static const bus_size_t hvs_observe_offsets[2][HVS_OBSERVE_WORDS] = {
	{ 0x00, 0x20, 0x30, 0x44, 0x50, 0x64, 0x70, 0x84 },
	{ 0x00, 0x20, 0x100, 0x118, 0x140, 0x158, 0x180, 0x198 }
};

struct hvs_observe_sample {
	uint32_t word[2][HVS_OBSERVE_WORDS];
	unsigned int valid[2], layout;
	struct bcm2712_display_clocks clocks[2];
	bool clocks_valid[2], initial_inactive_snapshot;
	struct bcm2712_display_clock_failure failure;
	const char *stage;
	int post_error;
};

/* Synchronous only. The existing reader does not retain this callback. */
static int
hvs_observe_exchange(void *arg, uint32_t *request)
{
	uint32_t response;

	(void)arg;
	return bcmmbox_request(BCMMBOX_CHANARM2VC, request,
	    8 * sizeof(*request), &response);
}

static int
hvs_observe_layout(uint32_t version)
{

	switch (version & 0xffU) {
	case 0x53:
		return 0;
	case 0x54:
		return 1;
	default:
		return -1;
	}
}

/* This strict sampled pattern is not an ownership, power or drain predicate. */
static bool
hvs_observe_inactive(const uint32_t *word)
{
	unsigned int i;

	if ((word[1] & (1U << 31)) != 0)
		return false;
	for (i = 2; i < HVS_OBSERVE_WORDS; i += 2) {
		if ((word[i] & ((1U << 31) | (1U << 30))) != 0 ||
		    (word[i + 1] & 0x6000U) != 0)
			return false;
	}
	return true;
}

static int
hvs_observe_cycle(device_t dev, ACPI_HANDLE handle,
    struct hvs_observe_sample *sample)
{
	struct bcm2712_display_contract contract;
	bus_space_handle_t mapping;
	unsigned int pass, i;
	int error, layout;

	sample->stage = "resources";
	error = bcm2712_display_acpi_resources(dev, handle, &contract);
	if (error != 0)
		return error;
	if (contract.memory[3].address != UINT64_C(0x107c580000) ||
	    contract.memory[3].length != 0x1a000)
		return EINVAL;
	sample->stage = "pre-clocks";
	error = bcm2712_display_read_clocks_report(hvs_observe_exchange, NULL,
	    &sample->clocks[0], &sample->failure);
	if (error != 0)
		return error;
	sample->clocks_valid[0] = true;
	sample->stage = "map";
	error = bus_space_map(&arm_generic_bs_tag, contract.memory[3].address,
	    contract.memory[3].length, 0, &mapping);
	if (error != 0)
		return error;
	sample->stage = "registers";
	aprint_normal("HVS first resource3 MMIO: read-only; native "
	    "bus_space stall/abort is not recoverable\n");
	for (pass = 0; pass < 2; pass++) {
		for (i = 0; i < HVS_OBSERVE_WORDS; i++) {
			sample->word[pass][i] = bus_space_read_4(&arm_generic_bs_tag,
			    mapping, hvs_observe_offsets[sample->layout][i]);
			sample->valid[pass]++;
			if (i == 0 && pass == 0) {
				layout = hvs_observe_layout(sample->word[0][0]);
				if (layout < 0) {
					error = EOPNOTSUPP;
					goto read_done;
				}
				sample->layout = (unsigned int)layout;
			} else if (i == 0 && (sample->word[1][0] & 0xffU) !=
			    (sample->word[0][0] & 0xffU)) {
				error = EAGAIN;
				goto read_done;
			}
		}
	}
read_done:
	/* Preserve the already collected observation result before post GETs. */
	if (error == 0) {
		sample->stage = "compare";
		if (memcmp(sample->word[0], sample->word[1],
		    sizeof(sample->word[0])) != 0)
			error = EAGAIN;
		else if (!hvs_observe_inactive(sample->word[0])) {
			sample->stage = "strict-pattern";
			error = EBUSY;
		}
	}
	bus_space_unmap(&arm_generic_bs_tag, mapping, contract.memory[3].length);
	/* A later clock error cannot replace a primary observation errno. */
	sample->post_error = bcm2712_display_read_clocks_report(
	    hvs_observe_exchange, NULL, &sample->clocks[1], &sample->failure);
	if (sample->post_error == 0)
		sample->clocks_valid[1] = true;
	if (error != 0)
		return error;
	if (sample->post_error != 0) {
		sample->stage = "post-clocks";
		return sample->post_error;
	}
	sample->stage = "compare";
	for (i = 0; i < BCM2712_DISPLAY_NCLOCK; i++) {
		if (sample->clocks[0].clock[i].id != sample->clocks[1].clock[i].id ||
		    sample->clocks[0].clock[i].state !=
		    sample->clocks[1].clock[i].state ||
		    sample->clocks[0].clock[i].rate != sample->clocks[1].clock[i].rate)
			return EAGAIN;
	}
	sample->initial_inactive_snapshot = true;
	sample->stage = "complete";
	return 0;
}

/* Hold the registered native render object during resources, GETs and MMIO. */
static int
hvs_observe_device(struct drm_minor **out, ACPI_HANDLE *handle)
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
hvs_observe_framebuffer(void)
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

/* Evidence and the completion line follow unmap and render-reference release. */
static void
hvs_observe_log(const struct hvs_observe_sample *sample, int error)
{
	unsigned int pass, i;

	for (pass = 0; pass < 2; pass++) {
		if (sample->clocks_valid[pass]) {
			for (i = 0; i < BCM2712_DISPLAY_NCLOCK; i++)
				aprint_normal("HVS clocks %s index %u: id %u "
				    "state %#x rate %u Hz\n", pass == 0 ? "pre" :
				    "post", i, sample->clocks[pass].clock[i].id,
				    sample->clocks[pass].clock[i].state,
				    sample->clocks[pass].clock[i].rate);
		}
		for (i = 0; i < sample->valid[pass]; i++)
			aprint_normal("HVS registers pass %u index %u offset "
			    "%#x: %#x\n", pass, i,
			    (unsigned int)hvs_observe_offsets[sample->layout][i],
			    sample->word[pass][i]);
	}
	if (sample->failure.valid)
		aprint_normal("HVS validated clock failure at %s: id %u "
		    "state %#x rate %u Hz check %u tag %#x\n",
		    sample->clocks_valid[0] ? "post-clocks" : "pre-clocks",
		    sample->failure.clock.id, sample->failure.clock.state,
		    sample->failure.clock.rate, sample->failure.check,
		    sample->failure.check_tag);
	if (error != 0) {
		aprint_error("HVS observation stopped at %s: error %d "
		    "post_error %d valid words %u/%u initial_inactive_snapshot 0; "
		    "no ownership or drain grant\n", sample->stage, error,
		    sample->post_error, sample->valid[0], sample->valid[1]);
		return;
	}
	aprint_normal("HVS snapshot complete: initial_inactive_snapshot %u; "
	    "no ownership, power, DMA or stop/drain grant\n",
	    sample->initial_inactive_snapshot);
}

static int
bcm2712_hvs_observe_modcmd(modcmd_t command, void *argument)
{
	static const char * const conflicts[] = {
		"clock-mode", "dvp-mode", "hd-mode", "hpd-mode", "ddc-mode",
		"phy-mode", "iommu-mode", "disp-mode", "prepare-only"
	};
	prop_dictionary_t properties = argument;
	struct hvs_observe_sample sample;
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
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	if (properties == NULL ||
	    !prop_dictionary_get_bool(properties, "cold-headless", &cold_headless) ||
	    !cold_headless || !prop_dictionary_get_cstring_nocopy(properties,
	    "expected-buildinfo", &expected) ||
	    !prop_dictionary_get_cstring_nocopy(properties, "hvs-mode", &mode) ||
	    strcmp(mode, "observe0") != 0)
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
	error = hvs_observe_framebuffer();
	if (error != 0)
		return error;
	error = hvs_observe_device(&minor, &handle);
	if (error != 0)
		return error;
	memset(&sample, 0, sizeof(sample));
	error = hvs_observe_cycle(minor->dev->dev, handle, &sample);
	drm_minor_release(minor);
	hvs_observe_log(&sample, error);
	return error;
}
