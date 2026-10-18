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
#include <dev/acpi/cixvpu_acpi_boot.h>
#include <dev/firmload.h>
#include <dev/ic/cixvpu_dma.h>
#include <dev/ic/cixvpu_prepare.h>
#include <dev/ic/cixvpu_resources.h>

__CTASSERT(CIXVPU_ACPI_FIXED_MEMORY32 == ACPI_RESOURCE_TYPE_FIXED_MEMORY32);
__CTASSERT(CIXVPU_ACPI_EXTENDED_IRQ == ACPI_RESOURCE_TYPE_EXTENDED_IRQ);
__CTASSERT(CIXVPU_ACPI_END_TAG == ACPI_RESOURCE_TYPE_END_TAG);

struct cixvpu_acpi_softc {
	device_t sc_dev;
	struct acpi_attach_args sc_aa;
	struct cixvpu_resources sc_resources;
	struct cixvpu_acpi_control sc_control;
	/* Retain CPU owners on failed cleanup or after a boot attempt. */
	struct cixvpu_dma *sc_dma;
	struct cixvpu_prepared *sc_prepared;
	struct cixvpu_acpi_boot *sc_boot;
	struct cixvpu_boot_report sc_boot_report;
	bool sc_boot_once_invoked;
	unsigned int sc_mode;
	bool sc_probe_ran;
};

static void
cixvpu_acpi_boot_print(device_t self, const struct cixvpu_acpi_softc *sc,
    bool valid)
{
	const struct cixvpu_boot_report *r = &sc->sc_boot_report;
	unsigned int i;

	aprint_normal_dev(self,
	    "native boot: once invoked %s, caller prepared %s, DMA %s\n",
	    sc->sc_boot_once_invoked ? "yes" : "no",
	    sc->sc_prepared != NULL ? "yes" : "no",
	    sc->sc_dma != NULL ? "yes" : "no");
	aprint_normal_dev(self, "boot report valid %s\n", valid ? "yes" : "no");
	if (!valid)
		return;
	aprint_normal_dev(self, "boot stage %u last stage %u, primary %d cleanup %d\n",
	    (unsigned int)r->stage, (unsigned int)r->last_stage,
	    r->error, r->cleanup_error);
	aprint_normal_dev(self,
	    "boot attempted %s moved %s exposed %s held %s, "
	    "prepared retained %s IRQ registered %s masked %s\n",
	    r->started ? "yes" : "no", r->moved ? "yes" : "no",
	    r->exposed ? "yes" : "no", r->held ? "yes" : "no",
	    r->prepared_retained ? "yes" : "no",
	    r->irq_registered ? "yes" : "no", r->irq_masked ? "yes" : "no");
	aprint_normal_dev(self,
	    "boot ID %#x cores %u slots %u SVN %#x, identity valid %s\n",
	    r->control.id, r->control.ncores, r->control.nlsid, r->control.svn,
	    r->control.identity_valid ? "yes" : "no");
	aprint_normal_dev(self, "SWITCHED_IN %s PONG %s, IRQ seen %#x unexpected %#x\n",
	    r->switched_in ? "yes" : "no", r->pong ? "yes" : "no",
	    r->irq_seen, r->irq_unexpected);
	aprint_normal_dev(self, "boot terminate reads %u visits %u messages %u\n",
	    r->terminate_reads, r->visits, r->messages);
	aprint_normal_dev(self,
	    "boot RPC header valid %s state %#x callid %#x size %#x, "
	    "header error %d stage %u primary %d\n",
	    r->rpc_header_valid ? "yes" : "no", r->rpc_state,
	    r->rpc_callid, r->rpc_size, r->rpc_header_error,
	    (unsigned int)r->last_stage, r->error);
	aprint_normal_dev(self,
	    "boot RPC alloc valid %s size %u max_size %u region %u "
	    "log2_alignment %u\n",
	    r->rpc_alloc_valid ? "yes" : "no", r->rpc_alloc_size,
	    r->rpc_alloc_max_size, (unsigned int)r->rpc_alloc_region,
	    (unsigned int)r->rpc_alloc_log2_alignment);
	for (i = 0; i < __arraycount(r->core_status); i++) {
		aprint_normal_dev(self,
		    "boot core %u attempted %s released %s status %s cleanup %s\n",
		    i, (r->core_attempted & (1U << i)) != 0 ? "yes" : "no",
		    (r->core_released & (1U << i)) != 0 ? "yes" : "no",
		    AcpiFormatException(r->core_status[i]),
		    AcpiFormatException(r->core_cleanup[i]));
	}
}

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
	int prepare_error = 0, prepared_cleanup_error = 0;
	int boot_report_error = 0;
	bool boot_report_valid = false;
	bool prepare_attempted = false;

	if (sc->sc_probe_ran)
		return;
	sc->sc_probe_ran = true;
	/* Load and audit one fixed image before the selected owner transition. */
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
	if (sc->sc_mode == 4) {
		firmware_free(image, size);
		image = NULL;
	}
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
	if ((sc->sc_mode == 5 || sc->sc_mode == 7) && error == 0) {
		/* No page view survives this CPU ownership MOVE. */
		prepare_attempted = true;
		if (sc->sc_mode == 7)
			prepare_error = cixvpu_prepare_boot_cpu(&sc->sc_dma,
			    &plan, image, size, digest_expected, &sc->sc_prepared);
		else
			prepare_error = cixvpu_prepare_cpu(&sc->sc_dma, &plan,
			    image, size, digest_expected, &sc->sc_prepared);
		error = prepare_error;
	}
	if (sc->sc_mode == 7 && error == 0) {
		firmware_free(image, size);
		image = NULL;
		error = cixvpu_acpi_boot_create(self, &sc->sc_aa,
		    &sc->sc_resources, &sc->sc_boot);
		if (error != 0)
			goto out;
		/* Boot owns sync. Preserve every ownership state it returns. */
		sc->sc_boot_once_invoked = true;
		error = cixvpu_acpi_boot_once(sc->sc_boot, &sc->sc_prepared);
		boot_report_error = cixvpu_acpi_boot_report(sc->sc_boot,
		    &sc->sc_boot_report);
		boot_report_valid = boot_report_error == 0;
	}
out:
	if (image != NULL) {
		firmware_free(image, size);
		image = NULL;
	}
	if (!sc->sc_boot_once_invoked) {
		if (sc->sc_prepared != NULL)
			prepared_cleanup_error = cixvpu_prepared_free(&sc->sc_prepared);
		if (sc->sc_dma != NULL)
			cleanup_error = cixvpu_dma_free(&sc->sc_dma);
	}
	if (sc->sc_mode == 7) {
		cixvpu_acpi_boot_print(self, sc, boot_report_valid);
	} else if (sc->sc_mode == 5) {
		aprint_normal_dev(self,
		    "one-core CPU prepare fixture: pages %u checked %u, "
		    "retained DMA %s prepared %s\n",
		    pages, checked, sc->sc_dma != NULL ? "yes" : "no",
		    sc->sc_prepared != NULL ? "yes" : "no");
		aprint_normal_dev(self, "prepare attempted %s, error %d\n",
		    prepare_attempted ? "yes" : "no", prepare_error);
	} else {
		aprint_normal_dev(self,
		    "one-core allocation fixture: pages %u checked %u, owner retained %s\n",
		    pages, checked, sc->sc_dma != NULL ? "yes" : "no");
	}
	for (i = 0; i < __arraycount(roles); i++) {
		if (roles[i] != 0)
			aprint_normal_dev(self, "allocation role %u pages %u\n",
			    i, roles[i]);
	}
	if (sc->sc_mode == 7) {
		aprint_normal_dev(self,
		    "native boot result %d, primary %d close %d report %d "
		    "prepared cleanup %d DMA cleanup %d\n",
		    error != 0 ? error : close_error != 0 ? close_error :
		    boot_report_error != 0 ? boot_report_error :
		    prepared_cleanup_error != 0 ? prepared_cleanup_error : cleanup_error,
		    error, close_error, boot_report_error,
		    prepared_cleanup_error, cleanup_error);
	} else if (sc->sc_mode == 5) {
		aprint_normal_dev(self,
		    "prepare result %d, primary %d close %d "
		    "prepared cleanup %d DMA cleanup %d\n",
		    error != 0 ? error : close_error != 0 ? close_error :
		    prepared_cleanup_error != 0 ? prepared_cleanup_error : cleanup_error,
		    error, close_error, prepared_cleanup_error, cleanup_error);
	} else {
		aprint_normal_dev(self,
		    "allocation result %d, primary %d close %d cleanup %d\n",
		    error != 0 ? error : close_error != 0 ? close_error : cleanup_error,
		    error, close_error, cleanup_error);
	}
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
	if (sc->sc_mode == 6)
		error = cixvpu_acpi_control_probe_native_profile(&sc->sc_control,
		    self, &sc->sc_aa, &sc->sc_resources);
	else if (sc->sc_mode == 3)
		error = cixvpu_acpi_control_probe_native(&sc->sc_control, self,
		    &sc->sc_aa, &sc->sc_resources);
	else
		error = cixvpu_acpi_control_probe(&sc->sc_control, self, &sc->sc_aa,
		    &sc->sc_resources, sc->sc_mode == 2);
	aprint_normal_dev(self,
	    "%s: rate query attempted %s, rate valid %s\n",
	    sc->sc_mode == 1 ? "resource query" :
	    sc->sc_mode == 3 ? "native identity probe" :
	    sc->sc_mode == 6 ? "native hardware profile" : "identity probe",
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
	if (sc->sc_mode == 6) {
		const struct cixvpu_hw_profile *p = &sc->sc_control.profile;

		aprint_normal_dev(self, "profile valid %s\n", p->valid ? "yes" : "no");
		if (p->valid) {
			aprint_normal_dev(self, "STRAP_PIN2 %#x STRAP_PIN0 %#x PGCTRL %#x\n",
			    p->strap_pin2, p->strap_pin0, p->pgctrl);
			aprint_normal_dev(self, "repair raw PASS %#x ENBL %#x BUSY %#x\n",
			    p->repair_pass, p->repair_enable, p->repair_busy);
			aprint_normal_dev(self, "CLKFORCE %#x harvesting %#x usable core mask %#x\n",
			    p->clkforce, p->harvesting_mask, p->usable_core_mask);
		}
	}
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
	if (sc->sc_mode > 7) {
		aprint_error_dev(self, "unsupported flags %#x; use 0, 1, 2, 3, 4, 5, 6 or 7\n",
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
		if (sc->sc_mode == 4 || sc->sc_mode == 5 || sc->sc_mode == 7) {
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
