/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* GPU dependency query only. This does not grant shared-cache access. */
#include <sys/param.h>
#include <sys/cpu.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/systm.h>

#include <dev/acpi/acpivar.h>
#include <arm/broadcom/bcm2712_display_acpi.h>
#include <arm/broadcom/bcm2712_iommuc.h>

static int
bcm2712_iommuc_dependency_context(void)
{

	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	return 0;
}

int
bcm2712_iommuc_acpi_dependency(ACPI_HANDLE gpu, ACPI_HANDLE *cache,
    struct bcm2712_iommuc_contract *out)
{
	struct bcm2712_display_contract display;
	struct bcm2712_iommuc_contract decoded;
	ACPI_HANDLE expected, handle, seen[BCM2712_IOMMUC_MAX_DEPENDENCIES];
	ACPI_OBJECT_TYPE type;
	ACPI_BUFFER buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	ACPI_OBJECT *package, *element;
	ACPI_STATUS status;
	unsigned int i, j, matches = 0;
	int error;

	if (gpu == NULL || cache == NULL || out == NULL)
		return EINVAL;
	if ((error = bcm2712_iommuc_dependency_context()) != 0)
		return error;
	status = AcpiGetHandle(NULL, "\\_SB.GPU0", &handle);
	if (ACPI_FAILURE(status))
		return status == AE_NOT_FOUND ? EOPNOTSUPP : EIO;
	if (gpu != handle)
		return EINVAL;
	if (ACPI_FAILURE(AcpiGetType(gpu, &type)))
		return EIO;
	if (type != ACPI_TYPE_DEVICE)
		return EINVAL;
	error = bcm2712_display_acpi_resources(NULL, gpu, &display);
	if (error != 0)
		return error;
	status = AcpiGetHandle(NULL, BCM2712_IOMMUC_PATH, &expected);
	if (status == AE_NOT_FOUND)
		return EOPNOTSUPP;
	if (ACPI_FAILURE(status))
		return EIO;
	status = AcpiEvaluateObjectTyped(gpu, "_DEP", NULL, &buffer,
	    ACPI_TYPE_PACKAGE);
	if (ACPI_FAILURE(status))
		return status == AE_NOT_FOUND ? EOPNOTSUPP : EIO;
	package = buffer.Pointer;
	error = EINVAL;
	if (package == NULL || package->Type != ACPI_TYPE_PACKAGE ||
	    package->Package.Count == 0 ||
	    package->Package.Count > BCM2712_IOMMUC_MAX_DEPENDENCIES ||
	    package->Package.Elements == NULL)
		goto done;
	for (i = 0; i < package->Package.Count; i++) {
		element = &package->Package.Elements[i];
		/* The generic helper also accepts ANY and STRING; this ABI does not. */
		if (element->Type != ACPI_TYPE_LOCAL_REFERENCE ||
		    element->Reference.ActualType != ACPI_TYPE_DEVICE ||
		    element->Reference.Handle == NULL)
			goto done;
		if (ACPI_FAILURE(acpi_eval_reference_handle(element, &handle)) ||
		    ACPI_FAILURE(AcpiGetType(handle, &type)) || type != ACPI_TYPE_DEVICE)
			goto done;
		for (j = 0; j < i; j++) {
			if (seen[j] == handle)
				goto done;
		}
		seen[i] = handle;
		if (handle == expected)
			matches++;
	}
	if (matches != 1)
		goto done;
	error = bcm2712_iommuc_acpi_resources(expected, &decoded);
	if (error == 0) {
		*cache = expected;
		*out = decoded;
	}
done:
	ACPI_FREE(buffer.Pointer);
	return error;
}
