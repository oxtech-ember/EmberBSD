/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted cached diagnostic. No hardware observation or access grant. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <dev/acpi/acpivar.h>
#include <arm/broadcom/bcm2712_iommuc.h>
#include <arm/broadcom/bcm2712_iommuc_native.h>

MODULE(MODULE_CLASS_MISC, bcm2712_iommuc_status, NULL);

static int
bcm2712_iommuc_status_modcmd(modcmd_t command, void *arg)
{
	struct bcm2712_iommuc_native_status status;
	struct bcm2712_iommuc_provider *provider = NULL;
	ACPI_HANDLE handle = NULL;
	int error;

	(void)arg;
	if (command == MODULE_CMD_FINI)
		return 0;
	if (command != MODULE_CMD_INIT)
		return ENOTTY;
	memset(&status, 0, sizeof(status));
	if (ACPI_FAILURE(AcpiGetHandle(NULL, BCM2712_IOMMUC_PATH, &handle)))
		return ENXIO;
	error = bcm2712_iommuc_lookup(handle, &provider);
	if (error != 0)
		return error;
	error = bcm2712_iommuc_cached_status(provider, &status);
	if (error != 0)
		return error;
	aprint_normal("bcm2712_iommuc_status: cached only; attached %u, "
	    "core initialized %u; observation stage %u, started %u, mapped %u, "
	    "raw valid %u, CONTROL %08x, first error %d, reads %u/%u; "
	    "MMU4 attempted %u, first error %d, fault %d; no new MMIO\n",
	    status.software_attached, status.core_initialized,
	    status.observation.stage, status.observation.started,
	    status.observation.mapped, status.observation.raw_valid,
	    status.observation.raw_control, status.observation.first_error,
	    status.observation.reads_attempted, status.observation.reads_completed,
	    status.mmu4.attempted, status.mmu4.first_error, status.mmu4.fault);
	return 0;
}
