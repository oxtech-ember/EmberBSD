/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD contributors (AI-assisted). */
/*
 * Actual drm_pci_native_identity(). It fills the version 1 native PCI
 * record from the Linux pci_dev shim. drm_dev_register() later adds
 * the minor numbers and the primary/render flags.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drm_native_identity.h"

static unsigned checks;
#define CHECK(e) do { checks++; if (!(e)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); exit(1); \
} } while (0)

typedef int pci_chipset_tag_t;
struct pci_attach_args {
	pci_chipset_tag_t pa_pc;
	int pa_bus, pa_device, pa_function;
};
struct pci_dev {
	struct pci_attach_args pd_pa;
	uint16_t vendor, device, subsystem_vendor, subsystem_device;
	uint8_t revision;
};
struct drm_device { struct drm_native_pci_record native_pci; };

static pci_chipset_tag_t segment_pc;
static unsigned
pci_get_segment(pci_chipset_tag_t pc)
{
	segment_pc = pc;
	return 3;
}

#include "identity-body.h"

int
main(void)
{
	struct pci_dev pdev = {
		.pd_pa = { .pa_pc = 17, .pa_bus = 0, .pa_device = 2,
		    .pa_function = 0 },
		.vendor = 0x8086, .device = 0x4e55,
		.subsystem_vendor = 0x1d05, .subsystem_device = 0x1121,
		.revision = 0x01,
	};
	struct drm_device dev;
	const struct drm_native_pci_record *r = &dev.native_pci;

	/* Every byte is written, including fields for drm_dev_register(). */
	memset(&dev, 0xa5, sizeof(dev));
	drm_pci_native_identity(&dev, &pdev);
	CHECK(sizeof(*r) == 68);
	CHECK(r->version == DRM_NATIVE_PCI_VERSION && r->length == 68);
	CHECK(r->flags == DRM_NATIVE_REVISION);
	CHECK(r->bus_type == DRM_NATIVE_BUS_PCI);
	CHECK(segment_pc == 17 && r->domain == 3);
	CHECK(r->bus == 0 && r->device == 2 && r->function == 0);
	CHECK(r->vendor == 0x8086 && r->product == 0x4e55);
	CHECK(r->subvendor == 0x1d05 && r->subproduct == 0x1121);
	CHECK(r->revision == 0x01);
	CHECK(r->primary_major == 0 && r->primary_minor == 0);
	CHECK(r->render_major == 0 && r->render_minor == 0);

	/* Revision zero is a valid revision, not an absent field. */
	pdev.revision = 0;
	pdev.pd_pa.pa_bus = 255;
	pdev.pd_pa.pa_device = 31;
	pdev.pd_pa.pa_function = 7;
	drm_pci_native_identity(&dev, &pdev);
	CHECK(r->flags == DRM_NATIVE_REVISION && r->revision == 0);
	CHECK(r->bus == 255 && r->device == 31 && r->function == 7);

	printf("DRM PCI identity contract: %u checks passed\n", checks);
	return 0;
}
