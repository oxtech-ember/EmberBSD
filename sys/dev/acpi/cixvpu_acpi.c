/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original native implementation, AI-assisted. */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/sha2.h>
#include <sys/systm.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/cixvpu_acpi_control.h>
#include <dev/firmload.h>
#include <dev/ic/cixvpu_dma.h>
#include <dev/ic/cixvpu_resources.h>

__CTASSERT(CIXVPU_ACPI_FIXED_MEMORY32 == ACPI_RESOURCE_TYPE_FIXED_MEMORY32);
__CTASSERT(CIXVPU_ACPI_EXTENDED_IRQ == ACPI_RESOURCE_TYPE_EXTENDED_IRQ);
__CTASSERT(CIXVPU_ACPI_END_TAG == ACPI_RESOURCE_TYPE_END_TAG);

struct cixvpu_acpi_softc {
	device_t sc_dev;
	struct acpi_attach_args sc_aa;
	struct cixvpu_resources sc_resources;
	struct cixvpu_acpi_control sc_control;
	/* CPU-only owner. Retain it if cleanup unexpectedly fails. */
	struct cixvpu_dma *sc_dma;
	unsigned int sc_mode;
	bool sc_probe_ran;
};

static void
cixvpu_acpi_allocation(device_t self)
{
	static const uint8_t digest_expected[SHA256_DIGEST_LENGTH] = {
		0x2f, 0x74, 0x53, 0x94, 0xcc, 0x87, 0x00, 0x23,
		0x34, 0x48, 0xd7, 0x4c, 0xf4, 0xfb, 0x14, 0xd1,
		0x8e, 0xba, 0x54, 0x7d, 0x22, 0x59, 0xde, 0x5e,
		0x0b, 0xbf, 0x82, 0x5a, 0x1e, 0xfe, 0xf5, 0x6d
	};
	struct cixvpu_acpi_softc *sc = device_private(self);
	struct cixvpu_fw_plan plan;
	struct cixvpu_dma_page page;
	firmware_handle_t firmware;
	SHA256_CTX hash;
	uint8_t digest[SHA256_DIGEST_LENGTH];
	unsigned int roles[CIXVPU_FW_PLAN_LEAF + 1] = { 0 };
	unsigned int i, j, pages = 0, checked = 0;
	off_t extent;
	size_t size = 0;
	void *image = NULL;
	int error, close_error = 0, cleanup_error = 0;

	if (sc->sc_probe_ran)
		return;
	sc->sc_probe_ran = true;
	/* One-core allocation fixture; no VPU power, clock, reset or MMIO. */
	error = firmware_open("cixvpu", "hevcenc.fwb", &firmware);
	if (error != 0)
		goto out;
	extent = firmware_get_size(firmware);
	if (extent < CIXVPU_FW_HEADER_SIZE || extent > 1048576 ||
	    extent != 420736) {
		error = EINVAL;
	} else {
		size = (size_t)extent;
		image = firmware_malloc(size);
		error = image == NULL ? ENOMEM :
		    firmware_read(firmware, 0, image, size);
	}
	/* firmware_close consumes its handle even on failure. */
	close_error = firmware_close(firmware);
	if (error != 0 || close_error != 0)
		goto out;
	(void)SHA256_Init(&hash);
	(void)SHA256_Update(&hash, image, size);
	(void)SHA256_Final(digest, &hash);
	if (memcmp(digest, digest_expected, sizeof(digest)) != 0) {
		error = EINVAL;
		goto out;
	}
	error = cixvpu_fw_plan_build(image, size, 1, &plan);
	if (error != 0)
		goto out;
	firmware_free(image, size);
	image = NULL;
	pages = plan.backing_pages;
	error = cixvpu_dma_alloc(sc->sc_aa.aa_dmat, sc->sc_aa.aa_dmat64,
	    &plan, &sc->sc_dma);
	if (error != 0)
		goto out;
	for (i = 0; i < pages; i++) {
		error = cixvpu_dma_page(sc->sc_dma, i, &page);
		if (error != 0)
			break;
		if (page.kva == NULL || page.plan_page.backing_index != i ||
		    (unsigned int)page.plan_page.role >= __arraycount(roles)) {
			error = EIO;
			break;
		}
		for (j = 0; j < CIXVPU_FW_PAGE_SIZE; j++) {
			if (((const uint8_t *)page.kva)[j] != 0) {
				error = EIO;
				break;
			}
		}
		if (error != 0)
			break;
		roles[page.plan_page.role]++;
		checked++;
	}
out:
	if (image != NULL)
		firmware_free(image, size);
	if (sc->sc_dma != NULL)
		cleanup_error = cixvpu_dma_free(&sc->sc_dma);
	aprint_normal_dev(self,
	    "one-core allocation fixture: pages %u checked %u, owner retained %s\n",
	    pages, checked, sc->sc_dma != NULL ? "yes" : "no");
	for (i = 0; i < __arraycount(roles); i++) {
		if (roles[i] != 0)
			aprint_normal_dev(self, "allocation role %u pages %u\n",
			    i, roles[i]);
	}
	aprint_normal_dev(self,
	    "allocation result %d, primary %d close %d cleanup %d\n",
	    error != 0 ? error : close_error != 0 ? close_error : cleanup_error,
	    error, close_error, cleanup_error);
}

static void
cixvpu_acpi_deferred(device_t self)
{
	struct cixvpu_acpi_softc *sc = device_private(self);
	const struct cixvpu_acpi_report *r = &sc->sc_control.report;
	int error;

	if (sc->sc_probe_ran)
		return;
	sc->sc_probe_ran = true;
	if (sc->sc_mode == 3)
		error = cixvpu_acpi_control_probe_native(&sc->sc_control, self,
		    &sc->sc_aa, &sc->sc_resources);
	else
		error = cixvpu_acpi_control_probe(&sc->sc_control, self, &sc->sc_aa,
		    &sc->sc_resources, sc->sc_mode == 2);
	aprint_normal_dev(self,
	    "%s: rate query attempted %s, rate valid %s\n",
	    sc->sc_mode == 1 ? "resource query" :
	    sc->sc_mode == 3 ? "native identity probe" : "identity probe",
	    r->rate_query_attempted ? "yes" : "no",
	    r->rate_valid ? "yes" : "no");
	if (r->rate_valid)
		aprint_normal_dev(self, "clock rate %" PRIu64 "\n", r->rate);
	aprint_normal_dev(self,
	    "ID %#x cores %u slots %u SVN %#x, identity valid %s, released %s\n",
	    r->id, r->ncores, r->nlsid, r->svn,
	    r->identity_valid ? "yes" : "no", r->released ? "yes" : "no");
	aprint_normal_dev(self,
	    "probe result %d, primary %d cleanup %d, ACPI %s cleanup ACPI %s\n",
	    error, r->error, r->cleanup_error,
	    AcpiFormatException(r->acpi_status),
	    AcpiFormatException(r->cleanup_acpi_status));
}

static const struct device_compatible_entry cixvpu_acpi_compat[] = {
	{ .compat = "CIXH3010" },
	DEVICE_COMPAT_EOL
};

static int
cixvpu_acpi_match(device_t parent, cfdata_t cf, void *aux)
{

	(void)parent;
	(void)cf;
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
	struct cixvpu_acpi_softc *sc = device_private(self);
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
	bool defer_probe = false;

	(void)parent;
	aprint_naive(": CIX P1 video codec (disabled)\n");
	aprint_normal(": CIX P1 video codec (diagnostic attachment)\n");
	sc->sc_dev = self;
	sc->sc_mode = device_cfdata(self)->cf_flags;
	if (sc->sc_mode > 4) {
		aprint_error_dev(self, "unsupported flags %#x; use 0, 1, 2, 3 or 4\n",
		    sc->sc_mode);
		return;
	}

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
	if (sc->sc_mode != 0) {
		/* The node is ACPI-owned. Keep no pointer to attach-stack arguments. */
		sc->sc_aa = *aa;
		sc->sc_resources = description;
		defer_probe = true;
	}
done:
	/* The parser initializes its lists before any failure; free them too. */
	acpi_resource_cleanup(&resources);
	if (defer_probe) {
		if (sc->sc_mode == 4) {
			config_mountroot(self, cixvpu_acpi_allocation);
			return;
		}
		/* Providers attach among the other ACPI children before this callback. */
		config_defer(self, cixvpu_acpi_deferred);
		return;
	}
disabled:
	aprint_error_dev(self,
	    "codec disabled: resource activation, DMA and firmware are not enabled\n");
}

CFATTACH_DECL_NEW(cixvpu_acpi, sizeof(struct cixvpu_acpi_softc),
    cixvpu_acpi_match, cixvpu_acpi_attach, NULL, NULL);
