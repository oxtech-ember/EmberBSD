/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted finite acceptance probe. Display MMIO needs an explicit mode. */
#include "opt_bcm2712_ddc.h"

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
#ifdef BCM2712_DISPLAY_DDC
#include <arm/broadcom/bcm2712_display_ddc.h>
#endif
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
	HSM_PROBE_GATE0,
	HSM_PROBE_HPD0,
	HSM_PROBE_DDC0,
	HSM_PROBE_EDID0
};
struct hsm_probe_ddc {
	bus_space_handle_t bsc_handle, handle;
	bool bsc_mapped, mapped, bsc_valid, valid;
	uint32_t status, mask, ctl, enable, ctlhi;
#ifdef BCM2712_DISPLAY_DDC
	struct bcm2712_ddc_provider *owner;
	struct bcm2712_ddc_status sample;
	uint8_t edid[BCM2712_DDC_EDID_LENGTH];
	int operation_error;
	bool sample_valid, edid_valid;
#endif
};
struct hsm_probe_dvp {
	struct bcm2712_dvp_provider *owner;
	bus_space_handle_t handle;
	bool mapped, hpd_mapped, hpd_valid;
	bus_space_handle_t hpd_handle;
	uint32_t hpd_raw;
	struct hsm_probe_ddc ddc;
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

/* HSM and DVP leases remain held until the controller mapping is released. */
static int
hsm_probe_hpd0(device_t dev, const struct bcm2712_display_contract *contract,
    struct hsm_probe_dvp *dvp)
{
	const struct bcm2712_display_memory *memory = &contract->memory[16];
	int error;

	dvp->stage = "HPD0-map";
	error = bus_space_map(&arm_generic_bs_tag, memory->address,
	    memory->length, 0, &dvp->hpd_handle);
	if (error != 0)
		return error;
	dvp->hpd_mapped = true;
	dvp->stage = "HPD0-read";
	aprint_normal_dev(dev, "HPD0 probe: normal-active-SoC prior-art "
	    "assumption; first per-controller resource16 MMIO: one read at "
	    "+0x1c8; bus_space stall/abort is not recoverable; no ACPI power "
	    "or display-lifetime claim\n");
	/* BCM2712 uses the VC6 HDMI core table, not the VC5 register offset. */
	dvp->hpd_raw = bus_space_read_4(&arm_generic_bs_tag, dvp->hpd_handle,
	    0x1c8);
	dvp->hpd_valid = true;
	aprint_normal_dev(dev, "HPD0 probe sample: raw %#x connected %u\n",
	    dvp->hpd_raw, dvp->hpd_raw & 1U);
	bus_space_unmap(&arm_generic_bs_tag, dvp->hpd_handle, memory->length);
	dvp->hpd_mapped = false;
	dvp->stage = "HPD0-unmapped";
	return 0;
}

static void
hsm_probe_ddc_snapshot(device_t dev, const char *stage,
    const struct hsm_probe_ddc *ddc)
{
	bool candidate;

	if (ddc->bsc_valid)
		aprint_normal_dev(dev, "DDC0 probe %s: BSC status %#x mask %#x\n",
		    stage, ddc->status, ddc->mask);
	if (!ddc->valid)
		return;
	candidate = (ddc->mask & 2U) != 0 && (ddc->status & 2U) == 0 &&
	    (ddc->ctl & 0x40U) == 0 && (ddc->enable & 3U) == 0;
	aprint_normal_dev(dev, "DDC0 probe %s: ctl %#x enable %#x ctlhi %#x "
	    "candidate_quiescent %u; non-atomic snapshot only; no SCL/SDA "
	    "or free-bus guarantee\n", stage, ddc->ctl, ddc->enable,
	    ddc->ctlhi, candidate);
}

/* Sampling does not start a transfer or acquire the BSC interrupt domain. */
static int
hsm_probe_ddc0(device_t dev, const struct bcm2712_display_contract *contract,
    struct hsm_probe_dvp *dvp)
{
	const struct bcm2712_display_memory *bsc = &contract->memory[13];
	const struct bcm2712_display_memory *memory = &contract->memory[14];
	struct hsm_probe_ddc *ddc = &dvp->ddc;
	int error;

	dvp->stage = "BSC-map";
	error = bus_space_map(&arm_generic_bs_tag, bsc->address, bsc->length,
	    0, &ddc->bsc_handle);
	if (error != 0)
		return error;
	ddc->bsc_mapped = true;
	dvp->stage = "BSC-read";
	aprint_normal_dev(dev, "DDC0 probe observe0: normal-active-SoC prior-art "
	    "assumption; first BSC resource13 MMIO; bus_space stall/abort "
	    "is not recoverable; no ACPI power or display-lifetime claim\n");
	ddc->status = bus_space_read_4(&arm_generic_bs_tag, ddc->bsc_handle, 0);
	ddc->mask = bus_space_read_4(&arm_generic_bs_tag, ddc->bsc_handle, 4);
	ddc->bsc_valid = true;
	dvp->stage = "DDC0-map";
	error = bus_space_map(&arm_generic_bs_tag, memory->address,
	    memory->length, 0, &ddc->handle);
	if (error != 0)
		return error;
	ddc->mapped = true;
	dvp->stage = "DDC0-read";
	aprint_normal_dev(dev, "DDC0 probe: first controller resource14 MMIO; "
	    "three raw control reads only; bus_space stall/abort is not "
	    "recoverable\n");
	ddc->ctl = bus_space_read_4(&arm_generic_bs_tag, ddc->handle, 0x28);
	ddc->enable = bus_space_read_4(&arm_generic_bs_tag, ddc->handle, 0x2c);
	ddc->ctlhi = bus_space_read_4(&arm_generic_bs_tag, ddc->handle, 0x50);
	ddc->valid = true;
	hsm_probe_ddc_snapshot(dev, "sample", ddc);
	bus_space_unmap(&arm_generic_bs_tag, ddc->handle, memory->length);
	ddc->mapped = false;
	bus_space_unmap(&arm_generic_bs_tag, ddc->bsc_handle, bsc->length);
	ddc->bsc_mapped = false;
	dvp->stage = "DDC0-unmapped";
	return 0;
}

#ifdef BCM2712_DISPLAY_DDC
/* Native callbacks and retained storage belong to the kernel provider. */
static int
hsm_probe_edid0(device_t dev, const struct bcm2712_display_contract *contract,
    struct hsm_probe_dvp *dvp)
{
	const struct bcm2712_display_memory *bsc = &contract->memory[13];
	const struct bcm2712_display_memory *memory = &contract->memory[14];
	struct hsm_probe_ddc *ddc = &dvp->ddc;
	int error;

	dvp->stage = "EDID0-BSC-map";
	error = bus_space_map(&arm_generic_bs_tag, bsc->address, bsc->length,
	    0, &ddc->bsc_handle);
	if (error != 0)
		goto known_error;
	ddc->bsc_mapped = true;
	dvp->stage = "EDID0-map";
	error = bus_space_map(&arm_generic_bs_tag, memory->address,
	    memory->length, 0, &ddc->handle);
	if (error != 0)
		goto known_error;
	ddc->mapped = true;
	dvp->stage = "EDID0-create";
	error = bcm2712_display_ddc_create(dev, contract, bsc,
	    ddc->bsc_handle, memory, ddc->handle, &arm_generic_bs_tag,
	    &ddc->owner);
	if (error != 0) {
		if (ddc->owner != NULL)
			return error;
		goto known_error;
	}
	if (ddc->owner == NULL)
		return EIO;
	dvp->stage = "EDID0-read";
	ddc->operation_error = bcm2712_display_ddc_read_edid(ddc->owner,
	    ddc->edid, sizeof(ddc->edid));
	dvp->stage = "EDID0-status";
	error = bcm2712_display_ddc_status(ddc->owner, &ddc->sample);
	if (error != 0)
		return error;
	ddc->sample_valid = true;
	aprint_normal_dev(dev, "EDID0 cached: native_fault %d core_fault %d "
	    "active %u commands %u polls %u mutation_attempted %u "
	    "BSC_status %#x status_valid %u BSC_mask %#x mask_valid %u\n",
	    ddc->sample.fault, ddc->sample.core.fault,
	    ddc->sample.core.active, ddc->sample.core.commands,
	    ddc->sample.core.polls, ddc->sample.core.mutation_attempted,
	    ddc->sample.bsc_status, ddc->sample.status_valid,
	    ddc->sample.bsc_mask, ddc->sample.mask_valid);
	if (!ddc->sample.status_valid || !ddc->sample.mask_valid)
		return EIO;
	if (ddc->sample.fault != 0 || ddc->sample.core.fault != 0 ||
	    ddc->sample.core.active)
		return ddc->sample.fault != 0 ? ddc->sample.fault :
		    ddc->sample.core.fault != 0 ? ddc->sample.core.fault : EBUSY;
	dvp->stage = "EDID0-destroy";
	error = bcm2712_display_ddc_destroy(ddc->owner);
	if (error != 0)
		return error;
	ddc->owner = NULL;
	ddc->edid_valid = ddc->operation_error == 0;
	goto unmap;
known_error:
	/* No native owner or controller write exists on this path. */
	ddc->operation_error = error;
unmap:
	if (ddc->mapped) {
		bus_space_unmap(&arm_generic_bs_tag, ddc->handle, memory->length);
		ddc->mapped = false;
	}
	if (ddc->bsc_mapped) {
		bus_space_unmap(&arm_generic_bs_tag, ddc->bsc_handle, bsc->length);
		ddc->bsc_mapped = false;
	}
	dvp->stage = "EDID0-unmapped";
	return 0;
}

/* Publish only after every provider and mapping has been released. */
static void
hsm_probe_edid0_payload(device_t dev, const uint8_t *edid)
{
	unsigned int i;

	aprint_normal_dev(dev, "BCM2712_EDID0 begin length=128\n");
	for (i = 0; i < BCM2712_DDC_EDID_LENGTH; i += 16)
		aprint_normal_dev(dev, "BCM2712_EDID0 data %02x: "
		    "%02x %02x %02x %02x %02x %02x %02x %02x "
		    "%02x %02x %02x %02x %02x %02x %02x %02x\n", i,
		    edid[i], edid[i + 1], edid[i + 2], edid[i + 3],
		    edid[i + 4], edid[i + 5], edid[i + 6], edid[i + 7],
		    edid[i + 8], edid[i + 9], edid[i + 10], edid[i + 11],
		    edid[i + 12], edid[i + 13], edid[i + 14], edid[i + 15]);
	aprint_normal_dev(dev, "BCM2712_EDID0 end length=128\n");
}
#endif

/* The caller holds a verified HSM lease for this entire DVP/controller cycle. */
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
	    mode == HSM_PROBE_OBSERVE ? "observe" :
	    mode == HSM_PROBE_HPD0 ? "HPD0" :
	    mode == HSM_PROBE_DDC0 ? "DDC0" :
	    mode == HSM_PROBE_EDID0 ? "EDID0" : "gate0");
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
	/* Do not combine first controller access with an unaccepted gate write. */
	if (mode == HSM_PROBE_HPD0 && (initial.gates & (1U << 3)) != 0)
		return EBUSY;
	if (mode == HSM_PROBE_DDC0 && (initial.gates & (1U << 3)) != 0)
		return EBUSY;
	if (mode == HSM_PROBE_EDID0 && (initial.gates & (1U << 3)) != 0)
		return EBUSY;
	if (mode == HSM_PROBE_GATE0 || mode == HSM_PROBE_HPD0 ||
	    mode == HSM_PROBE_DDC0 || mode == HSM_PROBE_EDID0) {
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
		if (mode == HSM_PROBE_HPD0 || mode == HSM_PROBE_DDC0 ||
		    mode == HSM_PROBE_EDID0) {
			/* Acquire can write if an external owner changed the gate. */
			if (status.mutation_attempted)
				return EIO;
			if (mode == HSM_PROBE_HPD0)
				error = hsm_probe_hpd0(dev, &contract, dvp);
			else if (mode == HSM_PROBE_DDC0)
				error = hsm_probe_ddc0(dev, &contract, dvp);
			else {
#ifdef BCM2712_DISPLAY_DDC
				error = hsm_probe_edid0(dev, &contract, dvp);
#else
				error = EOPNOTSUPP;
#endif
			}
			if (error != 0)
				return error;
		}
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
#ifdef BCM2712_DISPLAY_DDC
	if (mode == HSM_PROBE_EDID0) {
		if (dvp.ddc.operation_error != 0) {
			aprint_error_dev(dev, "EDID0 cleanup complete; operation "
			    "error %d; payload withheld; no retry\n",
			    dvp.ddc.operation_error);
			return dvp.ddc.operation_error;
		}
		if (!dvp.ddc.edid_valid)
			return EIO;
		hsm_probe_edid0_payload(dev, dvp.ddc.edid);
	}
#endif
	aprint_normal_dev(dev, "HSM probe destroy succeeded; finite cycle "
	    "complete; %s; no reset or clock-lifetime claim\n",
	    mode == HSM_PROBE_CLOCK ? "no display MMIO or HPD" :
	    mode == HSM_PROBE_HPD0 ? "one HPD0 sample" :
	    mode == HSM_PROBE_DDC0 ? "DDC0 snapshot complete; no idle or transfer claim" :
	    mode == HSM_PROBE_EDID0 ? "EDID0 base block complete" :
	    "shared DVP resource10 only; no HPD");
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
		if (mode == HSM_PROBE_HPD0)
			aprint_error_dev(dev, "HPD0 probe resource16 mapping "
			    "retained %u sample valid %u; cached raw %#x connected "
			    "%u; no current connector-state claim\n", dvp.hpd_mapped,
			    dvp.hpd_valid, dvp.hpd_raw, dvp.hpd_raw & 1U);
		if (mode == HSM_PROBE_DDC0) {
			aprint_error_dev(dev, "DDC0 probe resource13 mapping retained "
			    "%u resource14 mapping retained %u; BSC sample valid %u "
			    "DDC sample valid %u; cached values only\n",
			    dvp.ddc.bsc_mapped, dvp.ddc.mapped,
			    dvp.ddc.bsc_valid, dvp.ddc.valid);
			hsm_probe_ddc_snapshot(dev, "retained cached", &dvp.ddc);
		}
#ifdef BCM2712_DISPLAY_DDC
		if (mode == HSM_PROBE_EDID0) {
			aprint_error_dev(dev, "EDID0 probe resource13 mapping retained "
			    "%u resource14 mapping retained %u native owner retained "
			    "%u operation error %d cached status valid %u; payload "
			    "withheld\n", dvp.ddc.bsc_mapped, dvp.ddc.mapped,
			    dvp.ddc.owner != NULL, dvp.ddc.operation_error,
			    dvp.ddc.sample_valid);
			if (dvp.ddc.sample_valid)
				aprint_error_dev(dev, "EDID0 cached native fault %d "
				    "core fault %d active %u; no current-state claim\n",
				    dvp.ddc.sample.fault, dvp.ddc.sample.core.fault,
				    dvp.ddc.sample.core.active);
		}
#endif
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
	const char *expected, *dvp_mode, *hpd_mode, *ddc_mode;
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
	if (prop_dictionary_get(properties, "ddc-mode") != NULL) {
		if (prop_dictionary_get(properties, "hpd-mode") != NULL ||
		    prop_dictionary_get(properties, "dvp-mode") != NULL ||
		    !prop_dictionary_get_cstring_nocopy(properties, "ddc-mode",
		    &ddc_mode))
			return EINVAL;
		if (strcmp(ddc_mode, "observe0") == 0)
			mode = HSM_PROBE_DDC0;
		else if (strcmp(ddc_mode, "edid0") == 0) {
#ifndef BCM2712_DISPLAY_DDC
			return EOPNOTSUPP;
#else
			mode = HSM_PROBE_EDID0;
#endif
		} else
			return EINVAL;
	} else if (prop_dictionary_get(properties, "hpd-mode") != NULL) {
		if (prop_dictionary_get(properties, "dvp-mode") != NULL ||
		    !prop_dictionary_get_cstring_nocopy(properties, "hpd-mode",
		    &hpd_mode) || strcmp(hpd_mode, "sample0") != 0)
			return EINVAL;
		mode = HSM_PROBE_HPD0;
	} else if (prop_dictionary_get(properties, "dvp-mode") != NULL) {
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
		aprint_error("HSM probe failed: %d; see reported stages; no retry\n", error);
	return error;
}
