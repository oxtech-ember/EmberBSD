/* $NetBSD: efiacpi.c,v 1.13 2022/08/14 11:26:41 jmcneill Exp $ */

/*-
 * Copyright (c) 2018 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jared McNeill <jmcneill@invisible.ca>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* Origin: EmberBSD opt-in CIX SPCR command, 2026-10-15. */
#include "efiboot.h"
#include "efiacpi.h"
#include "smbios.h"
#include "cix_spcr.h"

struct acpi_rdsp {
	char signature[8];
	uint8_t checksum;
	char oemid[6];
	uint8_t revision;
	uint32_t rsdtphys;
	uint32_t length;
	uint64_t xsdtphys;
	uint8_t extcsum;
	uint8_t reserved[3];
};

static EFI_GUID Acpi20TableGuid = ACPI_20_TABLE_GUID;
static EFI_GUID Smbios3TableGuid = SMBIOS3_TABLE_GUID;
static EFI_GUID SmbiosTableGuid = SMBIOS_TABLE_GUID;

static void *acpi_root = NULL;
static void *smbios_table = NULL;

static int acpi_enabled = 1;

int
efi_acpi_probe(void)
{
	EFI_STATUS status;

	status = LibGetSystemConfigurationTable(&Acpi20TableGuid, &acpi_root);
	if (EFI_ERROR(status))
		return EIO;

	status = LibGetSystemConfigurationTable(&Smbios3TableGuid, &smbios_table);
	if (EFI_ERROR(status)) {
		status = LibGetSystemConfigurationTable(&SmbiosTableGuid, &smbios_table);
	}
	if (EFI_ERROR(status)) {
		smbios_table = NULL;
	}

	return 0;
}

int
efi_acpi_available(void)
{
	return acpi_root != NULL && acpi_enabled;
}

int
efi_acpi_enabled(void)
{
	return acpi_enabled;
}

void *
efi_acpi_root(void)
{
	return acpi_root;
}

void *
efi_acpi_smbios(void)
{
	return smbios_table;
}

static char model_buf[128];

void
efi_acpi_enable(int val)
{
	if (acpi_root == NULL) {
		printf("No ACPI node\n");
	} else
		acpi_enabled = val;
}

const char *
efi_acpi_get_model(void)
{
	struct smbtable smbios;
	struct smbios_sys *psys;
	const char *s;
	char *buf;

	memset(model_buf, 0, sizeof(model_buf));

	if (smbios_table != NULL) {
		smbios_init(smbios_table);

		buf = model_buf;
		smbios.cookie = 0;
		if (smbios_find_table(SMBIOS_TYPE_SYSTEM, &smbios)) {
			psys = smbios.tblhdr;
			if ((s = smbios_get_string(&smbios, psys->vendor, buf, 64)) != NULL) {
				buf += strlen(s);
				*buf++ = ' ';
			}
			smbios_get_string(&smbios, psys->product, buf, 64);
		}
	}

	if (model_buf[0] == '\0')
		strcpy(model_buf, "ACPI");

	return model_buf;
}

void
efi_acpi_show(void)
{
	struct acpi_rdsp *rsdp = acpi_root;

	if (!efi_acpi_available()) {
		return;
	}

	command_printtab("ACPI", "v%02d %c%c%c%c%c%c\n", rsdp->revision,
	    rsdp->oemid[0], rsdp->oemid[1], rsdp->oemid[2],
	    rsdp->oemid[3], rsdp->oemid[4], rsdp->oemid[5]);

	if (smbios_table) {
		command_printtab("SMBIOS", "%s\n", efi_acpi_get_model());
	}
}

void
efi_acpi_cix_spcr(void)
{
	struct cix_spcr_map map;
	struct cix_spcr_plan plan;
	EFI_MEMORY_DESCRIPTOR *buffer;
	EFI_PHYSICAL_ADDRESS shadow = 0;
	EFI_STATUS status;
	UINTN size, key, stride;
	UINT32 version;
	const char *result = NULL;
	unsigned pass;
	int allocated = 0;

	if (!efi_acpi_available()) {
		printf("CIX SPCR: ACPI is unavailable or disabled\n");
		return;
	}
	buffer = AllocatePool(CIX_SPCR_MAP_MAX);
	if (buffer == NULL) {
		printf("CIX SPCR: cannot allocate memory map\n");
		return;
	}
	/* Allocation changes the map. Refresh it and repeat all input checks. */
	for (pass = 0; pass < 2; pass++) {
		size = CIX_SPCR_MAP_MAX;
		stride = 0;
		version = 0;
		status = uefi_call_wrapper(BS->GetMemoryMap, 5, &size, buffer,
		    &key, &stride, &version);
		if (EFI_ERROR(status)) {
			result = "cannot read bounded EFI memory map";
			break;
		}
		map.data = buffer;
		map.size = size;
		map.stride = stride;
		map.version = version;
		if (pass == 0) {
			result = cix_spcr_prepare((uintptr_t)acpi_root, &map, &plan);
			if (result != NULL)
				break;
			shadow = plan.rsdt != NULL ? UINT32_MAX : 0;
			status = uefi_call_wrapper(BS->AllocatePages, 4,
			    plan.rsdt != NULL ? AllocateMaxAddress : AllocateAnyPages,
			    EfiACPIReclaimMemory, 1, &shadow);
			if (EFI_ERROR(status)) {
				result = "cannot allocate ACPI shadow page";
				break;
			}
			allocated = 1;
		} else {
			result = cix_spcr_build((uintptr_t)acpi_root, &map, shadow);
		}
		if (result != NULL)
			break;
	}
	if (result == NULL) {
		/* Owned ACPI reclaim memory survives ExitBootServices for handoff. */
		acpi_root = (void *)(uintptr_t)shadow;
		result = "shadow installed (MMIO 0x040d0000, GSI 330)";
	} else if (allocated) {
		status = uefi_call_wrapper(BS->FreePages, 2, shadow, 1);
		if (EFI_ERROR(status))
			printf("CIX SPCR: cannot free unused shadow page\n");
	}
	FreePool(buffer);
	printf("CIX SPCR: %s\n", result);
}
