/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original native implementation, AI-assisted. */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/ic/cixvpu_resources.h>

__CTASSERT(CIXVPU_ACPI_FIXED_MEMORY32 == ACPI_RESOURCE_TYPE_FIXED_MEMORY32);
__CTASSERT(CIXVPU_ACPI_EXTENDED_IRQ == ACPI_RESOURCE_TYPE_EXTENDED_IRQ);
__CTASSERT(CIXVPU_ACPI_END_TAG == ACPI_RESOURCE_TYPE_END_TAG);

static const struct device_compatible_entry cixvpu_acpi_compat[] = {
	{ .compat = "CIXH3010" },
	DEVICE_COMPAT_EOL
};

static int
cixvpu_acpi_match(device_t parent, cfdata_t cf, void *aux)
{

	return acpi_compatible_match(aux, cixvpu_acpi_compat);
}

static ACPI_STATUS
cixvpu_acpi_resource_check(ACPI_RESOURCE *resource, void *context)
{

	if (!cixvpu_acpi_resource_type_allowed(resource->Type)) {
		device_t self = context;

		aprint_error_dev(self, "unsupported _CRS descriptor type %u\n",
		    resource->Type);
		return AE_BAD_DATA;
	}
	return AE_OK;
}

static void
cixvpu_acpi_attach(device_t parent, device_t self, void *aux)
{
	static const char * const reasons[] = {
		"valid",
		"expected two fixed memory resources and one interrupt",
		"invalid memory geometry or address overflow",
		"overlapping memory resources",
		"expected a level-sensitive standard GIC SPI",
		"_CCA is absent or is not zero or one"
	};
	const struct acpi_attach_args *aa = aux;
	const ACPI_HANDLE handle = aa->aa_node->ad_handle;
	struct acpi_resources resources;
	struct cixvpu_resources description = { 0 };
	const struct acpi_mem *mem;
	const struct acpi_irq *irq;
	ACPI_INTEGER cca;
	ACPI_STATUS rv;
	enum cixvpu_resource_status status;
	unsigned int i;

	aprint_naive(": CIX P1 video codec (disabled)\n");
	aprint_normal(": CIX P1 video codec (diagnostic attachment)\n");

	/* The common parser ignores some descriptor types. Reject those first. */
	rv = AcpiWalkResources(handle, "_CRS", cixvpu_acpi_resource_check, self);
	if (ACPI_FAILURE(rv)) {
		aprint_error_dev(self, "_CRS descriptor validation failed: %s\n",
		    AcpiFormatException(rv));
		goto disabled;
	}
	/* Parse firmware descriptions only. Do not map or activate the block. */
	rv = acpi_resource_parse(self, handle, "_CRS", &resources,
	    &acpi_resource_parse_ops_quiet);
	if (ACPI_FAILURE(rv))
		goto done;
	description.nmem = resources.ar_nmem;
	description.nirq = resources.ar_nirq;
	description.other = resources.ar_nio != 0 ||
	    resources.ar_niorange != 0 || resources.ar_nmemrange != 0 ||
	    resources.ar_ndrq != 0;
	for (i = 0; i < __arraycount(description.mem); i++) {
		mem = acpi_res_mem(&resources, i);
		if (mem == NULL)
			break;
		description.mem[i].base = mem->ar_base;
		description.mem[i].translated = mem->ar_xbase;
		description.mem[i].length = mem->ar_length;
	}
	irq = acpi_res_irq(&resources, 0);
	if (irq != NULL) {
		description.irq = irq->ar_irq;
		description.irq_trigger = irq->ar_type;
	}
	rv = acpi_eval_integer(handle, "_CCA", &cca);
	if (ACPI_SUCCESS(rv)) {
		description.cca_present = 1;
		description.cca = cca;
	} else {
		aprint_error_dev(self, "_CCA unavailable: %s\n",
		    AcpiFormatException(rv));
	}
	status = cixvpu_resources_validate(&description);
	if (status != CIXVPU_RESOURCE_OK) {
		aprint_error_dev(self,
		    "resource description rejected: %s\n", reasons[status]);
		goto done;
	}
	for (i = 0; i < __arraycount(description.mem); i++) {
		aprint_normal_dev(self,
		    "MEM%u bus %#" PRIx64 " CPU %#" PRIx64 " size %#" PRIx64
		    " (not mapped)\n", i, description.mem[i].base,
		    description.mem[i].translated, description.mem[i].length);
	}
	aprint_normal_dev(self, "IRQ %u level, _CCA %" PRIu64
	    " (not activated)\n", description.irq, description.cca);
done:
	/* The parser initializes its lists before any failure; free them too. */
	acpi_resource_cleanup(&resources);
disabled:
	aprint_error_dev(self,
	    "codec disabled: resource activation, DMA and firmware are not enabled\n");
}

CFATTACH_DECL_NEW(cixvpu_acpi, 0, cixvpu_acpi_match, cixvpu_acpi_attach,
    NULL, NULL);
