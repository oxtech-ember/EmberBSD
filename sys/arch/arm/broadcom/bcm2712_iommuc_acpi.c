/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Descriptor queries only. The shared cache has no access grant here. */
#include <sys/param.h>
#include <sys/cpu.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/systm.h>

#include <dev/acpi/acpivar.h>
#include <arm/broadcom/bcm2712_iommuc.h>

struct bcm2712_iommuc_walk {
	unsigned int memory;
	unsigned int end;
};

static ACPI_STATUS
bcm2712_iommuc_resource(ACPI_RESOURCE *resource, void *arg)
{
	struct bcm2712_iommuc_walk *walk = arg;
	ACPI_RESOURCE_ADDRESS64 *address;

	if (walk->end != 0)
		return AE_BAD_DATA;
	switch (resource->Type) {
	case ACPI_RESOURCE_TYPE_ADDRESS64:
		address = &resource->Data.Address64;
		if (++walk->memory != 1 ||
		    address->ResourceType != ACPI_MEMORY_RANGE ||
		    address->ProducerConsumer != ACPI_CONSUMER ||
		    address->Decode != ACPI_POS_DECODE ||
		    address->MinAddressFixed != ACPI_ADDRESS_FIXED ||
		    address->MaxAddressFixed != ACPI_ADDRESS_FIXED ||
		    address->Info.Mem.WriteProtect != ACPI_READ_WRITE_MEMORY ||
		    address->Info.Mem.Caching != ACPI_NON_CACHEABLE_MEMORY ||
		    address->Info.Mem.RangeType != 0 ||
		    address->Info.Mem.Translation != 0 ||
		    address->ResourceSource.StringLength != 0 ||
		    address->Address.Granularity != 0 ||
		    address->Address.TranslationOffset != 0 ||
		    address->Address.Minimum != BCM2712_IOMMUC_ADDRESS ||
		    address->Address.Maximum != BCM2712_IOMMUC_ADDRESS +
		    BCM2712_IOMMUC_LENGTH - 1 ||
		    address->Address.AddressLength != BCM2712_IOMMUC_LENGTH)
			return AE_BAD_DATA;
		return AE_OK;
	case ACPI_RESOURCE_TYPE_END_TAG:
		walk->end++;
		return walk->memory == 1 ? AE_OK : AE_BAD_DATA;
	default:
		return AE_BAD_DATA;
	}
}

static int
bcm2712_iommuc_context(void)
{

	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	return 0;
}

int
bcm2712_iommuc_acpi_resources(ACPI_HANDLE handle,
    struct bcm2712_iommuc_contract *out)
{
	struct bcm2712_iommuc_walk walk = { 0 };
	struct bcm2712_iommuc_contract decoded;
	ACPI_DEVICE_INFO *info;
	ACPI_INTEGER value;
	ACPI_HANDLE expected;
	ACPI_OBJECT_TYPE type;
	ACPI_STATUS status;
	char *compatible;
	int error;

	if (handle == NULL || out == NULL)
		return EINVAL;
	if ((error = bcm2712_iommuc_context()) != 0)
		return error;
	status = AcpiGetHandle(NULL, BCM2712_IOMMUC_PATH, &expected);
	if (status == AE_NOT_FOUND)
		return EOPNOTSUPP;
	if (ACPI_FAILURE(status))
		return EIO;
	if (handle != expected)
		return EINVAL;
	if (ACPI_FAILURE(AcpiGetType(handle, &type)))
		return EIO;
	if (type != ACPI_TYPE_DEVICE)
		return EINVAL;
	if (ACPI_FAILURE(AcpiGetObjectInfo(handle, &info)))
		return EIO;
	error = 0;
	if (info->Type != ACPI_TYPE_DEVICE ||
	    (info->Valid & ACPI_VALID_HID) == 0 ||
	    info->HardwareId.String == NULL ||
	    strcmp(info->HardwareId.String, "PRP0001") != 0)
		error = EINVAL;
	ACPI_FREE(info);
	if (error != 0)
		return error;
	status = acpi_dsd_string(handle, "compatible", &compatible);
	if (ACPI_FAILURE(status))
		return EINVAL;
	error = strcmp(compatible, "brcm,bcm2712-iommuc") == 0 ? 0 : EINVAL;
	kmem_strfree(compatible);
	if (error != 0)
		return error;
	status = acpi_dsd_integer(handle, BCM2712_IOMMUC_PROPERTY, &value);
	if (status == AE_NOT_FOUND)
		return EOPNOTSUPP;
	if (ACPI_FAILURE(status))
		return EIO;
	if (value != BCM2712_IOMMUC_VERSION)
		return EOPNOTSUPP;
	if (ACPI_FAILURE(acpi_eval_integer(handle, "_UID", &value)) || value != 0 ||
	    ACPI_FAILURE(acpi_eval_integer(handle, "_STA", &value)) || value != 0x0f)
		return EINVAL;
	status = AcpiWalkResources(handle, "_CRS", bcm2712_iommuc_resource, &walk);
	if (ACPI_FAILURE(status))
		return status == AE_BAD_DATA ? EINVAL : EIO;
	if (walk.memory != 1 || walk.end != 1)
		return EINVAL;
	memset(&decoded, 0, sizeof(decoded));
	decoded.version = BCM2712_IOMMUC_VERSION;
	decoded.address = BCM2712_IOMMUC_ADDRESS;
	decoded.length = BCM2712_IOMMUC_LENGTH;
	*out = decoded;
	return 0;
}
