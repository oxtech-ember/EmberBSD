/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* A resource and firmware-clock observer. It never touches display MMIO. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <dev/acpi/acpivar.h>
#include <arm/broadcom/bcm2712_display_acpi.h>
#include <arm/broadcom/bcm2835_mbox.h>
#include <arch/evbarm/rpi/vcio.h>

struct bcm2712_display_walk {
	struct bcm2712_display_memory memory[BCM2712_DISPLAY_NMEM];
	uint32_t irq[BCM2712_DISPLAY_NIRQ];
	size_t nmem;
	size_t nirq;
};

static ACPI_STATUS
bcm2712_display_resource(ACPI_RESOURCE *resource, void *arg)
{
	struct bcm2712_display_walk *walk = arg;
	ACPI_RESOURCE_ADDRESS64 *address;
	ACPI_RESOURCE_EXTENDED_IRQ *irq;
	uint64_t base, length;
	unsigned int i;

	switch (resource->Type) {
	case ACPI_RESOURCE_TYPE_ADDRESS64:
		address = &resource->Data.Address64;
		base = address->Address.Minimum;
		length = address->Address.AddressLength;
		if (walk->nmem == BCM2712_DISPLAY_NMEM ||
		    address->ResourceType != ACPI_MEMORY_RANGE ||
		    address->ProducerConsumer != ACPI_CONSUMER ||
		    address->Decode != ACPI_POS_DECODE ||
		    address->MinAddressFixed != ACPI_ADDRESS_FIXED ||
		    address->MaxAddressFixed != ACPI_ADDRESS_FIXED ||
		    address->Info.Mem.WriteProtect != ACPI_READ_WRITE_MEMORY ||
		    address->Info.Mem.Caching != ACPI_NON_CACHEABLE_MEMORY ||
		    address->Info.Mem.RangeType != 0 || address->Info.Mem.Translation != 0 ||
		    address->ResourceSource.StringLength != 0 ||
		    address->Address.Granularity != 0 ||
		    address->Address.TranslationOffset != 0 || length == 0 ||
		    base > UINT64_MAX - (length - 1) ||
		    address->Address.Maximum != base + length - 1)
			return AE_BAD_DATA;
		walk->memory[walk->nmem].address = base;
		walk->memory[walk->nmem++].length = length;
		return AE_OK;
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		irq = &resource->Data.ExtendedIrq;
		if (irq->ProducerConsumer != ACPI_CONSUMER ||
		    irq->Triggering != ACPI_LEVEL_SENSITIVE ||
		    irq->Polarity != ACPI_ACTIVE_HIGH ||
		    irq->Shareable != ACPI_EXCLUSIVE || irq->WakeCapable != 0 ||
		    irq->ResourceSource.StringLength != 0 || irq->InterruptCount == 0 ||
		    irq->InterruptCount > BCM2712_DISPLAY_NIRQ - walk->nirq)
			return AE_BAD_DATA;
		for (i = 0; i < irq->InterruptCount; i++)
			walk->irq[walk->nirq++] = irq->Interrupts[i];
		return AE_OK;
	case ACPI_RESOURCE_TYPE_END_TAG:
		return AE_OK;
	default:
		return AE_BAD_DATA;
	}
}

static int
bcm2712_display_exchange(void *arg, uint32_t *request)
{
	uint32_t response;

	(void)arg;
	return bcmmbox_request(BCMMBOX_CHANARM2VC, request,
	    8 * sizeof(*request), &response);
}

int
bcm2712_display_acpi_resources(device_t dev, ACPI_HANDLE handle,
    struct bcm2712_display_contract *contract)
{
	struct bcm2712_display_walk walk;
	struct bcm2712_display_contract decoded;
	ACPI_INTEGER contract_version, sta, cca;
	ACPI_STATUS status;
	int error;

	(void)dev;
	if (handle == NULL || contract == NULL)
		return EINVAL;
	status = acpi_dsd_integer(handle, BCM2712_DISPLAY_PROPERTY, &contract_version);
	if (status == AE_NOT_FOUND)
		return EOPNOTSUPP;
	if (ACPI_FAILURE(status))
		return EIO;
	if (contract_version != BCM2712_DISPLAY_VERSION)
		return EOPNOTSUPP;
	if (ACPI_FAILURE(acpi_eval_integer(handle, "_STA", &sta)) ||
	    ACPI_FAILURE(acpi_eval_integer(handle, "_CCA", &cca)))
		return EIO;
	if (sta != 0x0f || cca != 0)
		return EINVAL;
	memset(&walk, 0, sizeof(walk));
	status = AcpiWalkResources(handle, "_CRS", bcm2712_display_resource,
	    &walk);
	if (ACPI_FAILURE(status))
		return status == AE_BAD_DATA ? EINVAL : EIO;
	error = bcm2712_display_decode(contract_version, walk.memory, walk.nmem,
	    walk.irq, walk.nirq, &decoded);
	if (error != 0)
		return error;
	*contract = decoded;
	return 0;
}

int
bcm2712_display_acpi_query(device_t dev, ACPI_HANDLE handle,
    struct bcm2712_display_contract *contract,
    struct bcm2712_display_clocks *clocks)
{
	struct bcm2712_display_contract decoded;
	struct bcm2712_display_clocks observed;
	int error;

	if (contract == NULL || clocks == NULL)
		return EINVAL;
	error = bcm2712_display_acpi_resources(dev, handle, &decoded);
	if (error != 0)
		return error;
	error = bcm2712_display_read_clocks(bcm2712_display_exchange, NULL,
	    &observed);
	if (error != 0)
		return error;
	*contract = decoded;
	*clocks = observed;
	return 0;
}

void
bcm2712_display_acpi_observe(device_t dev, ACPI_HANDLE handle)
{
	struct bcm2712_display_contract contract;
	struct bcm2712_display_clocks clocks;
	struct bcm2712_display_clock_failure failure;
	int error;

	error = bcm2712_display_acpi_resources(dev, handle, &contract);
	if (error != 0) {
		aprint_normal_dev(dev,
		    "display resources unavailable: error %d; no display MMIO\n",
		    error);
		return;
	}
	aprint_normal_dev(dev,
	    "display resources v%u validated: %u MMIO, %u IRQ; "
	    "no display MMIO\n", contract.version,
	    BCM2712_DISPLAY_NMEM, BCM2712_DISPLAY_NIRQ);
	error = bcm2712_display_read_clocks_report(bcm2712_display_exchange,
	    NULL, &clocks, &failure);
	if (error != 0) {
		if (failure.valid) {
			const char *check;

			check = failure.check == BCM2712_DISPLAY_CLOCK_STATE ?
			    "state != 1" :
			    failure.check == BCM2712_DISPLAY_CLOCK_RATE ?
			    "rate == 0" : "rate below HSM minimum";
			aprint_normal_dev(dev,
			    "firmware clock snapshot unavailable: error %d; "
			    "validated clock %u check tag %#x (%s), "
			    "state %#x rate %u Hz; no display MMIO\n",
			    error, failure.clock.id, failure.check_tag, check,
			    failure.clock.state, failure.clock.rate);
		} else {
			aprint_normal_dev(dev,
			    "firmware clock snapshot unavailable: error %d; "
			    "no complete validated response; no display MMIO\n", error);
		}
		return;
	}
	aprint_normal_dev(dev,
	    "firmware clock snapshot validated; no display MMIO, HPD or KMS\n");
}
