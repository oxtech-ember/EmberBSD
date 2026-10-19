/* $NetBSD: efifdt.c,v 1.38 2024/12/07 19:29:04 jmcneill Exp $ */

/*-
 * Copyright (c) 2019 Jason R. Thorpe
 * Copyright (c) 2018 Jared McNeill <jmcneill@invisible.ca>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "efiboot.h"
#include "efifdt.h"
#include "efiblock.h"
#include "overlay.h"
#include "module.h"

#ifdef EFIBOOT_ACPI
#include "efiacpi.h"
#endif

#include <libfdt.h>
#include "cm5trace.h"

#define FDT_TABLE_GUID	\
	{ 0xb1b621d5, 0xf19c, 0x41a5, { 0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0 } }
static EFI_GUID FdtTableGuid = FDT_TABLE_GUID;

#define	FDT_MEMORY_NODE_PATH	"/memory"
#define	FDT_MEMORY_NODE_NAME	"memory"
#define	FDT_CHOSEN_NODE_PATH	"/chosen"
#define	FDT_CHOSEN_NODE_NAME	"chosen"

#define	FDT_MEMORY_USABLE(_md)	\
	((_md)->Type == EfiLoaderCode || (_md)->Type == EfiLoaderData || \
	 (_md)->Type == EfiBootServicesCode || (_md)->Type == EfiBootServicesData || \
	 (_md)->Type == EfiConventionalMemory)

#define	FDT_SPACE	(4 * 1024 * 1024)
#define	FDT_ALIGN	(2 * 1024 * 1024)

#ifdef _LP64
#define PRIdUINTN "ld"
#define PRIxUINTN "lx"
#else
#define PRIdUINTN "d"
#define PRIxUINTN "x"
#endif
static void *fdt_data = NULL;
static size_t fdt_data_size = 512*1024;

#ifdef WSDISPLAY_BOOTFB_HANDOFF
#include <dev/fdt/bootfb_source.h>
/* Actual loader history and outcomes, not caller-supplied permission. */
static struct {
	void *blob;
	size_t capacity;
	EFI_STATUS status;
	UINTN count;
	uint32_t disposition;
	int fdt_error;
	int final_error;
	bool acpi_created;
	bool overlay_attempted;
	bool external_dtb;
	bool gop_attempted;
	bool protocol_failed;
	bool packed_valid;
} efi_bootfb_source;

static int
efi_fdt_source_note(int result)
{

	if (result < 0 && efi_bootfb_source.fdt_error == 0)
		efi_bootfb_source.fdt_error = result;
	return result;
}
#define EFI_BOOTFB_FDT(expr) efi_fdt_source_note((expr))

static void
efi_fdt_source_finish(const struct efi_cleanup_result *cleanup)
{
	struct bootfb_source_record record;
	struct bootfb_source_tree tree;
	uint8_t bytes[BOOTFB_SOURCE_SIZE];
	void *old;
	int chosen, length, error;
	bool known;

	efi_bootfb_source.packed_valid = false;
	if (efi_bootfb_source.blob != fdt_data ||
	    efi_bootfb_source.capacity < sizeof(struct fdt_header) ||
	    fdt_check_full(fdt_data, efi_bootfb_source.capacity) != 0) {
		efi_bootfb_source.final_error = EINVAL;
		return;
	}
	/*
	 * A copied external record is not evidence from this loader attempt.
	 * Neutralize a fixed-size payload in our validated writable copy first.
	 * Malformed sizes/duplicates remain a strict-query refusal.
	 */
	chosen = fdt_path_offset(fdt_data, "/chosen");
	old = chosen < 0 ? NULL : fdt_getprop_w(fdt_data, chosen,
	    BOOTFB_SOURCE_PROPERTY, &length);
	record = (struct bootfb_source_record){0};
	if (old != NULL && length == BOOTFB_SOURCE_SIZE) {
		error = bootfb_source_encode(&record, old, (size_t)length);
		if (error != 0) {
			efi_bootfb_source.final_error = error;
			return;
		}
	}
	error = bootfb_source_scan(fdt_data, efi_bootfb_source.capacity, &tree);
	if (error != 0) {
		efi_bootfb_source.final_error = error;
		return;
	}
	known = efi_bootfb_source.acpi_created &&
	    !efi_bootfb_source.overlay_attempted && !efi_bootfb_source.external_dtb &&
	    sizeof(EFI_STATUS) == 8 && sizeof(UINTN) == 8;
	record.producer = known ? BOOTFB_SOURCE_EFI_ACPI64 : BOOTFB_SOURCE_UNKNOWN;
	record.status = (uint64_t)efi_bootfb_source.status;
	record.count = (uint64_t)efi_bootfb_source.count;
	record.disposition = efi_bootfb_source.disposition;
	if (!known || !efi_bootfb_source.gop_attempted ||
	    efi_bootfb_source.protocol_failed || efi_bootfb_source.fdt_error != 0 ||
	    !cleanup->map_available || !cleanup->ebs_attempted ||
	    EFI_ERROR(cleanup->ebs_status) || !cleanup->runtime_attempted ||
	    EFI_ERROR(cleanup->runtime_status) || cleanup->runtime_fdt_error != 0)
		record.disposition = BOOTFB_SOURCE_UNCLASSIFIED;
	if (record.disposition == BOOTFB_SOURCE_ABSENT && tree.framebuffer)
		record.disposition = BOOTFB_SOURCE_UNCLASSIFIED;
	error = bootfb_source_encode(&record, bytes, sizeof(bytes));
	if (error == 0)
		error = fdt_setprop(fdt_data, tree.chosen, BOOTFB_SOURCE_PROPERTY,
		    bytes, sizeof(bytes));
	efi_bootfb_source.final_error = error;
}

static void
efi_fdt_source_packed(void)
{
	struct bootfb_source_record record;
	int error;

	error = bootfb_source_query(fdt_data, efi_bootfb_source.capacity, &record);
	if (efi_bootfb_source.final_error == 0)
		efi_bootfb_source.final_error = error;
	efi_bootfb_source.packed_valid = efi_bootfb_source.final_error == 0;
}
#else
#define EFI_BOOTFB_FDT(expr) (expr)
#endif

static EFI_PHYSICAL_ADDRESS initrd_addr, dtb_addr, rndseed_addr;
static u_long initrd_size = 0, dtb_size = 0, rndseed_size = 0;

/* exec.c */
extern EFI_PHYSICAL_ADDRESS efirng_addr;
extern u_long efirng_size;

#ifdef EFIBOOT_ACPI
#define ACPI_FDT_SIZE	(128 * 1024)
static int efi_fdt_create_acpifdt(void);
#endif

int
efi_fdt_probe(void)
{
	EFI_STATUS status;

	status = LibGetSystemConfigurationTable(&FdtTableGuid, &fdt_data);
	if (EFI_ERROR(status))
		return EIO;

	if (fdt_check_header(fdt_data) != 0) {
		fdt_data = NULL;
		return EINVAL;
	}

	return 0;
}

int
efi_fdt_set_data(void *data)
{
	int err;

#ifdef WSDISPLAY_BOOTFB_HANDOFF
	efi_bootfb_source.acpi_created = false;
	efi_bootfb_source.blob = NULL;
	efi_bootfb_source.capacity = 0;
#endif
	if (fdt_check_header(data) != 0)
		return EINVAL;

	fdt_data = alloc(fdt_data_size);
	if (fdt_data == NULL)
		return ENOMEM;
	memset(fdt_data, 0, fdt_data_size);

	err = fdt_open_into(data, fdt_data, fdt_data_size);
	if (err != 0) {
		dealloc(fdt_data, fdt_data_size);
		fdt_data = NULL;
		return ENXIO;
	}

	return 0;
}

void *
efi_fdt_data(void)
{
	return fdt_data;
}

int
efi_fdt_size(void)
{
	return fdt_data == NULL ? 0 : fdt_totalsize(fdt_data);
}

bool
efi_fdt_overlay_is_compatible(void *dtbo)
{
	const int system_root = fdt_path_offset(fdt_data, "/");
	const int overlay_root = fdt_path_offset(dtbo, "/");

	if (system_root < 0 || overlay_root < 0)
		return false;

	const int system_ncompat = fdt_stringlist_count(fdt_data, system_root,
	    "compatible");
	const int overlay_ncompat = fdt_stringlist_count(dtbo, overlay_root,
	    "compatible");

	if (system_ncompat <= 0 || overlay_ncompat <= 0)
		return false;

	const char *system_compatible, *overlay_compatible;
	int si, oi;

	for (si = 0; si < system_ncompat; si++) {
		system_compatible = fdt_stringlist_get(fdt_data,
		    system_root, "compatible", si, NULL);
		if (system_compatible == NULL)
			continue;
		for (oi = 0; oi < overlay_ncompat; oi++) {
			overlay_compatible = fdt_stringlist_get(dtbo,
			    overlay_root, "compatible", oi, NULL);
			if (overlay_compatible == NULL)
				continue;
			if (strcmp(system_compatible, overlay_compatible) == 0)
				return true;
		}
	}

	return false;
}

int
efi_fdt_overlay_apply(void *dtbo, int *fdterr)
{
#ifdef WSDISPLAY_BOOTFB_HANDOFF
	efi_bootfb_source.overlay_attempted = true;
#endif
	int err = fdt_overlay_apply(fdt_data, dtbo);
	if (fdterr)
		*fdterr = err;
	return err == 0 ? 0 : EIO;
}

void
efi_fdt_init(u_long addr, u_long len)
{
	int error;

	error = fdt_open_into(fdt_data, (void *)addr, len);
	if (error < 0)
		panic("fdt_open_into failed: %d", error);

	fdt_data = (void *)addr;
#ifdef WSDISPLAY_BOOTFB_HANDOFF
	efi_bootfb_source.blob = fdt_data;
	efi_bootfb_source.capacity = (size_t)len;
#endif
}

void
efi_fdt_fini(void)
{
	int error;

	error = fdt_pack(fdt_data);
	if (error < 0)
		panic("fdt_pack failed: %d", error);
}

void
efi_fdt_show(void)
{
	const char *model, *compat;
	int n, ncompat;

	if (fdt_data == NULL) {
		return;
	}

	model = fdt_getprop(fdt_data, fdt_path_offset(fdt_data, "/"), "model", NULL);
	if (model) {
		command_printtab("FDT", "%s [", model);
	}
	ncompat = fdt_stringlist_count(fdt_data, fdt_path_offset(fdt_data, "/"), "compatible");
	for (n = 0; n < ncompat; n++) {
		compat = fdt_stringlist_get(fdt_data, fdt_path_offset(fdt_data, "/"),
		    "compatible", n, NULL);
		printf("%s%s", n == 0 ? "" : ", ", compat);
	}
	printf("]\n");
}

static int
efi_fdt_chosen(void)
{
	int chosen;

	chosen = fdt_path_offset(fdt_data, FDT_CHOSEN_NODE_PATH);
	if (chosen < 0)
		chosen = fdt_add_subnode(fdt_data,
		    fdt_path_offset(fdt_data, "/"),
		    FDT_CHOSEN_NODE_NAME);
	if (chosen < 0)
		panic("FDT: Failed to create " FDT_CHOSEN_NODE_PATH " node");

	return chosen;
}

void
efi_fdt_system_table(void)
{
#ifdef EFIBOOT_RUNTIME_ADDRESS
	int chosen;

	chosen = efi_fdt_chosen();

	fdt_setprop_u64(fdt_data, chosen, "netbsd,uefi-system-table", (uint64_t)(uintptr_t)ST);
#endif
}

void
efi_fdt_memory_map(void)
{
	UINTN nentries = 0, mapkey, descsize;
	EFI_MEMORY_DESCRIPTOR *md, *memmap;
	UINT32 descver;
	UINT64 phys_start, phys_size;
	int n, memory;

	memory = fdt_path_offset(fdt_data, FDT_MEMORY_NODE_PATH);
	if (memory < 0)
		memory = fdt_add_subnode(fdt_data, fdt_path_offset(fdt_data, "/"), FDT_MEMORY_NODE_NAME);
	if (memory < 0)
		panic("FDT: Failed to create " FDT_MEMORY_NODE_PATH " node");

	fdt_delprop(fdt_data, memory, "reg");

	const int address_cells = fdt_address_cells(fdt_data, fdt_path_offset(fdt_data, "/"));
	const int size_cells = fdt_size_cells(fdt_data, fdt_path_offset(fdt_data, "/"));

	memmap = LibMemoryMap(&nentries, &mapkey, &descsize, &descver);
	for (n = 0, md = memmap; n < nentries; n++, md = NextMemoryDescriptor(md, descsize)) {
		/*
		 * create / find the chosen node for each iteration as it might have changed
		 * when adding to the memory node
		 */
		int chosen = efi_fdt_chosen();
		fdt_appendprop_u32(fdt_data, chosen, "netbsd,uefi-memmap", md->Type);
		fdt_appendprop_u64(fdt_data, chosen, "netbsd,uefi-memmap", md->PhysicalStart);
		fdt_appendprop_u64(fdt_data, chosen, "netbsd,uefi-memmap", md->NumberOfPages);
		fdt_appendprop_u64(fdt_data, chosen, "netbsd,uefi-memmap", md->Attribute);

		if ((md->Attribute & EFI_MEMORY_RUNTIME) != 0)
			continue;

		if ((md->Attribute & EFI_MEMORY_WB) == 0)
			continue;
		if (!FDT_MEMORY_USABLE(md))
			continue;
		if ((address_cells == 1 || size_cells == 1) && md->PhysicalStart + (md->NumberOfPages * EFI_PAGE_SIZE) > 0xffffffff)
			continue;
		if (md->NumberOfPages <= 1)
			continue;

		phys_start = md->PhysicalStart;
		phys_size = md->NumberOfPages * EFI_PAGE_SIZE;

		if (phys_start & EFI_PAGE_MASK) {
			/*
			 * UEFI spec says these should be 4KB aligned, but
			 * U-Boot doesn't always, so round up to the next
			 * page.
			 */
			phys_start = (phys_start + EFI_PAGE_SIZE) & ~EFI_PAGE_MASK;
			phys_size -= (EFI_PAGE_SIZE * 2);
			if (phys_size == 0)
				continue;
		}

		memory = fdt_path_offset(fdt_data, FDT_MEMORY_NODE_PATH);
		if (address_cells == 1)
			fdt_appendprop_u32(fdt_data, memory, "reg",
			    (uint32_t)phys_start);
		else
			fdt_appendprop_u64(fdt_data, memory, "reg",
			    phys_start);

		if (size_cells == 1)
			fdt_appendprop_u32(fdt_data, memory, "reg",
			    (uint32_t)phys_size);
		else
			fdt_appendprop_u64(fdt_data, memory, "reg",
			    phys_size);
	}
}

void
efi_fdt_gop(void)
{
	EFI_STATUS status;
	EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
	EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode;
	EFI_HANDLE *gop_handle;
	UINTN ngop_handle = 0, n;
	char buf[48];
	int fb, chosen;

	status = LibLocateHandle(ByProtocol, &GraphicsOutputProtocol, NULL, &ngop_handle, &gop_handle);
#ifdef WSDISPLAY_BOOTFB_HANDOFF
	efi_bootfb_source.gop_attempted = true;
	efi_bootfb_source.status = status;
	efi_bootfb_source.count = ngop_handle;
	efi_bootfb_source.disposition = BOOTFB_SOURCE_UNCLASSIFIED;
	if (status == EFI_NOT_FOUND && ngop_handle == 0 && sizeof(EFI_STATUS) == 8)
		efi_bootfb_source.disposition = BOOTFB_SOURCE_ABSENT;
	else if (status == EFI_SUCCESS && ngop_handle != 0)
		efi_bootfb_source.disposition = BOOTFB_SOURCE_UNSUPPORTED;
#endif

	if (EFI_ERROR(status) || ngop_handle == 0)
		return;

	for (n = 0; n < ngop_handle; n++) {
		status = uefi_call_wrapper(BS->HandleProtocol, 3, gop_handle[n], &GraphicsOutputProtocol, (void **)&gop);
		if (EFI_ERROR(status)) {
#ifdef WSDISPLAY_BOOTFB_HANDOFF
			efi_bootfb_source.protocol_failed = true;
#endif
			continue;
		}
#ifdef WSDISPLAY_BOOTFB_HANDOFF
		if (gop == NULL || gop->Mode == NULL || gop->Mode->Info == NULL) {
			efi_bootfb_source.protocol_failed = true;
			continue;
		}
#endif

		mode = gop->Mode;
		if (mode == NULL)
			continue;

#ifdef EFIBOOT_DEBUG
		printf("GOP: FB @ 0x%" PRIx64 " size 0x%" PRIxUINTN "\n", mode->FrameBufferBase, mode->FrameBufferSize);
		printf("GOP: Version %d\n", mode->Info->Version);
		printf("GOP: HRes %d VRes %d\n", mode->Info->HorizontalResolution, mode->Info->VerticalResolution);
		printf("GOP: PixelFormat %d\n", mode->Info->PixelFormat);
		printf("GOP: PixelBitmask R 0x%x G 0x%x B 0x%x Res 0x%x\n",
		    mode->Info->PixelInformation.RedMask,
		    mode->Info->PixelInformation.GreenMask,
		    mode->Info->PixelInformation.BlueMask,
		    mode->Info->PixelInformation.ReservedMask);
		printf("GOP: Pixels per scanline %d\n", mode->Info->PixelsPerScanLine);
#endif

		if (mode->Info->PixelFormat == PixelBltOnly) {
			printf("GOP: PixelBltOnly pixel format not supported\n");
			continue;
		}

		chosen = efi_fdt_chosen();
		EFI_BOOTFB_FDT(fdt_setprop_u32(fdt_data, chosen, "#address-cells", 2));
		EFI_BOOTFB_FDT(fdt_setprop_u32(fdt_data, chosen, "#size-cells", 2));
		EFI_BOOTFB_FDT(fdt_setprop_empty(fdt_data, chosen, "ranges"));

		snprintf(buf, sizeof(buf), "framebuffer@%" PRIx64, mode->FrameBufferBase);
		fb = EFI_BOOTFB_FDT(fdt_add_subnode(fdt_data, chosen, buf));
		if (fb < 0) {
			/* Framebuffer node already exists. No need to create a new one! */
			return;
		}

		EFI_BOOTFB_FDT(fdt_appendprop_string(fdt_data, fb, "compatible", "simple-framebuffer"));
		EFI_BOOTFB_FDT(fdt_appendprop_string(fdt_data, fb, "status", "okay"));
		EFI_BOOTFB_FDT(fdt_appendprop_u64(fdt_data, fb, "reg", mode->FrameBufferBase));
		EFI_BOOTFB_FDT(fdt_appendprop_u64(fdt_data, fb, "reg", mode->FrameBufferSize));
		EFI_BOOTFB_FDT(fdt_appendprop_u32(fdt_data, fb, "width", mode->Info->HorizontalResolution));
		EFI_BOOTFB_FDT(fdt_appendprop_u32(fdt_data, fb, "height", mode->Info->VerticalResolution));
		EFI_BOOTFB_FDT(fdt_appendprop_u32(fdt_data, fb, "stride", mode->Info->PixelsPerScanLine * 4));	/* XXX */
		EFI_BOOTFB_FDT(fdt_appendprop_string(fdt_data, fb, "format", "a8b8g8r8"));

#ifdef EFIBOOT_ACPI
		/*
		 * In ACPI mode, use GOP as console.
		 */
		if (efi_acpi_available()) {
			snprintf(buf, sizeof(buf), "/chosen/framebuffer@%" PRIx64, mode->FrameBufferBase);
			EFI_BOOTFB_FDT(fdt_setprop_string(fdt_data, chosen, "stdout-path", buf));
		}
#endif

#ifdef WSDISPLAY_BOOTFB_HANDOFF
		if (efi_bootfb_source.status == EFI_SUCCESS &&
		    (mode->Info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor ||
		    mode->Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor))
			efi_bootfb_source.disposition = BOOTFB_SOURCE_PRESENT;
#endif

		return;
	}
}

void
efi_fdt_bootargs(const char *bootargs)
{
	struct efi_block_part *bpart = efi_block_boot_part();
	uint8_t macaddr[6];
	int chosen;

	chosen = efi_fdt_chosen();

	if (*bootargs)
		fdt_setprop_string(fdt_data, chosen, "bootargs", bootargs);

	if (bpart) {
		switch (bpart->type) {
		case EFI_BLOCK_PART_DISKLABEL:
			fdt_setprop(fdt_data, chosen, "netbsd,mbr",
			    bpart->hash, sizeof(bpart->hash));
			fdt_setprop_u32(fdt_data, chosen, "netbsd,partition",
			    bpart->index);
			break;
		case EFI_BLOCK_PART_GPT:
			if (bpart->gpt.ent.ent_name[0] == 0x0000) {
				fdt_setprop(fdt_data, chosen, "netbsd,gpt-guid",
				    bpart->hash, sizeof(bpart->hash));
			} else {
				char *label = NULL;
				int rv = ucs2_to_utf8(bpart->gpt.ent.ent_name, &label);
				if (rv == 0) {
					fdt_setprop_string(fdt_data, chosen, "netbsd,gpt-label", label);
					FreePool(label);
				}
			}
			break;
		default:
			break;
		}
	} else if (efi_net_get_booted_macaddr(macaddr) == 0) {
		fdt_setprop(fdt_data, chosen, "netbsd,booted-mac-address", macaddr, sizeof(macaddr));
	}
}

static void
efi_fdt_userconf_addprop(const char *cmd)
{
	const int chosen = efi_fdt_chosen();

	fdt_appendprop_string(fdt_data, chosen, "netbsd,userconf", cmd);
}

void
efi_fdt_userconf(void)
{
	userconf_foreach(efi_fdt_userconf_addprop);
}

void
efi_fdt_initrd(u_long initrd_addr, u_long initrd_size)
{
	int chosen;

	if (initrd_size == 0)
		return;

	chosen = efi_fdt_chosen();
	fdt_setprop_u64(fdt_data, chosen, "linux,initrd-start", initrd_addr);
	fdt_setprop_u64(fdt_data, chosen, "linux,initrd-end", initrd_addr + initrd_size);
}

/* pass in the NetBSD on-disk random seed */
void
efi_fdt_rndseed(u_long addr, u_long size)
{
	int chosen;

	if (size == 0)
		return;

	chosen = efi_fdt_chosen();
	fdt_setprop_u64(fdt_data, chosen, "netbsd,rndseed-start", addr);
	fdt_setprop_u64(fdt_data, chosen, "netbsd,rndseed-end", addr + size);
}

/* pass in output from the EFI firmware's RNG from some unknown source */
void
efi_fdt_efirng(u_long efirng_addr, u_long efirng_size)
{
	int chosen;

	if (efirng_size == 0)
		return;

	chosen = efi_fdt_chosen();
	fdt_setprop_u64(fdt_data, chosen, "netbsd,efirng-start",
	    efirng_addr);
	fdt_setprop_u64(fdt_data, chosen, "netbsd,efirng-end",
	    efirng_addr + efirng_size);
}

/* pass in module information */
void
efi_fdt_module(const char *module_name, u_long module_addr, u_long module_size)
{
	int chosen;

	if (module_size == 0)
		return;

	chosen = efi_fdt_chosen();
	fdt_appendprop_string(fdt_data, chosen, "netbsd,module-names", module_name);
	fdt_appendprop_u64(fdt_data, chosen, "netbsd,modules", module_addr);
	fdt_appendprop_u64(fdt_data, chosen, "netbsd,modules", module_size);
}

static void
apply_overlay(const char *path, void *dtbo)
{

	if (!efi_fdt_overlay_is_compatible(dtbo)) {
		printf("boot: %s: incompatible overlay\n", path);
		return;
	}

	int fdterr;

	if (efi_fdt_overlay_apply(dtbo, &fdterr) != 0) {
		printf("boot: %s: error %d applying overlay\n", path, fdterr);
	}
}

static void
apply_overlay_file(const char *path)
{
	EFI_PHYSICAL_ADDRESS dtbo_addr;
	u_long dtbo_size;

	if (strlen(path) == 0)
		return;
#ifdef WSDISPLAY_BOOTFB_HANDOFF
	efi_bootfb_source.overlay_attempted = true;
#endif

	if (load_file(path, 0, false, &dtbo_addr, &dtbo_size) != 0 ||
	    dtbo_addr == 0) {
		/* Error messages have already been displayed. */
		goto out;
	}

	apply_overlay(path, (void *)(uintptr_t)dtbo_addr);

out:
	if (dtbo_addr) {
		uefi_call_wrapper(BS->FreePages, 2, dtbo_addr,
		    EFI_SIZE_TO_PAGES(dtbo_size));
	}
}

static void
load_fdt_overlays(void)
{
	if (!dtoverlay_enabled)
		return;

	dtoverlay_foreach(apply_overlay_file);
}

static void
load_module(const char *module_name)
{
	EFI_PHYSICAL_ADDRESS addr;
	u_long size;
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s/%s.kmod", module_prefix,
	    module_name, module_name);

	if (load_file(path, 0, false, &addr, &size) != 0 || addr == 0 || size == 0)
	    return;

	efi_fdt_module(module_name, (u_long)addr, size);
}

static void
load_modules(const char *kernel_name)
{
	if (!module_enabled)
		return;

	module_init(kernel_name);
	module_foreach(load_module);
}


/*
 * Prepare kernel arguments and shutdown boot services.
 */
int
efi_fdt_prepare_boot(const char *fname, const char *args, u_long *marks)
{
	int error;

#ifdef WSDISPLAY_BOOTFB_HANDOFF
	struct efi_cleanup_result cleanup;
	bool overlay_attempted = efi_bootfb_source.overlay_attempted;

	memset(&efi_bootfb_source, 0, sizeof(efi_bootfb_source));
	efi_bootfb_source.overlay_attempted = overlay_attempted;
	efi_bootfb_source.external_dtb = get_dtb_path()[0] != 0;
#endif
	load_file(get_initrd_path(), 0, false, &initrd_addr, &initrd_size);
	load_file(get_dtb_path(), 0, false, &dtb_addr, &dtb_size);

	error = efi_md_prepare_boot(fname, args, marks);
	if (error) {
		return error;
	}
#ifdef EFIBOOT_ACPI
	/* ACPI support only works for little endian kernels */
	if (efi_acpi_available() && netbsd_elf_data == ELFDATA2LSB) {
		error = efi_fdt_create_acpifdt();
		if (error != 0) {
			return error;
		}
	} else
#endif
	if (dtb_addr && efi_fdt_set_data((void *)(uintptr_t)dtb_addr) != 0) {
		return EINVAL;
	}

	if (efi_fdt_size() > 0) {
		/*
		 * Load the rndseed as late as possible -- after we
		 * have committed to using fdt and executing this
		 * kernel -- so that it doesn't hang around in memory
		 * if we have to bail or the kernel won't use it.
		 */
		load_file(get_rndseed_path(), 0, false,
		    &rndseed_addr, &rndseed_size);

		efi_fdt_init((marks[MARK_END] + FDT_ALIGN - 1) & -FDT_ALIGN, FDT_ALIGN);
		load_modules(fname);
		load_fdt_overlays();
		efi_fdt_initrd(initrd_addr, initrd_size);
		efi_fdt_rndseed(rndseed_addr, rndseed_size);
		efi_fdt_efirng(efirng_addr, efirng_size);
		efi_fdt_bootargs(args);
		efi_fdt_userconf();
		efi_fdt_system_table();
		efi_fdt_gop();
		efi_fdt_memory_map();
	}

#ifdef WSDISPLAY_BOOTFB_HANDOFF
	efi_cleanup_checked(&cleanup);
#else
	efi_cleanup();
#endif
	efi_cm5_trace("[CM5 EFI] cleanup done\r\n");

	if (efi_fdt_size() > 0) {
#ifdef WSDISPLAY_BOOTFB_HANDOFF
		efi_fdt_source_finish(&cleanup);
#endif
		efi_fdt_fini();
#ifdef WSDISPLAY_BOOTFB_HANDOFF
		efi_fdt_source_packed();
#endif
		efi_cm5_trace("[CM5 EFI] FDT ready\r\n");
	}

	return 0;
}

/*
 * Free memory after a failed boot.
 */
void
efi_fdt_cleanup_boot(void)
{
	if (rndseed_addr) {
		uefi_call_wrapper(BS->FreePages, 2, rndseed_addr, EFI_SIZE_TO_PAGES(rndseed_size));
		rndseed_addr = 0;
		rndseed_size = 0;
	}
	if (initrd_addr) {
		uefi_call_wrapper(BS->FreePages, 2, initrd_addr, EFI_SIZE_TO_PAGES(initrd_size));
		initrd_addr = 0;
		initrd_size = 0;
	}
	if (dtb_addr) {
		uefi_call_wrapper(BS->FreePages, 2, dtb_addr, EFI_SIZE_TO_PAGES(dtb_size));
		dtb_addr = 0;
		dtb_size = 0;
	}
}

size_t
efi_fdt_alloc_size(void)
{
	return FDT_SPACE;
}

#ifdef EFIBOOT_ACPI
int
efi_fdt_create_acpifdt(void)
{
	void *acpi_root = efi_acpi_root();
	void *smbios_table = efi_acpi_smbios();
	void *fdt;
	int error;

	if (acpi_root == NULL)
		return EINVAL;

	fdt = AllocatePool(ACPI_FDT_SIZE);
	if (fdt == NULL)
		return ENOMEM;

	error = fdt_create_empty_tree(fdt, ACPI_FDT_SIZE);
	if (error)
		return EIO;

	const char *model = efi_acpi_get_model();

	EFI_BOOTFB_FDT(fdt_setprop_string(fdt, fdt_path_offset(fdt, "/"), "compatible",
	    "netbsd,generic-acpi"));
	EFI_BOOTFB_FDT(fdt_setprop_string(fdt, fdt_path_offset(fdt, "/"), "model", model));
	EFI_BOOTFB_FDT(fdt_setprop_cell(fdt, fdt_path_offset(fdt, "/"), "#address-cells", 2));
	EFI_BOOTFB_FDT(fdt_setprop_cell(fdt, fdt_path_offset(fdt, "/"), "#size-cells", 2));

	EFI_BOOTFB_FDT(fdt_add_subnode(fdt, fdt_path_offset(fdt, "/"), "chosen"));
	EFI_BOOTFB_FDT(fdt_setprop_u64(fdt, fdt_path_offset(fdt, "/chosen"), "netbsd,acpi-root-table",
	    (uint64_t)(uintptr_t)acpi_root));
	if (smbios_table)
		EFI_BOOTFB_FDT(fdt_setprop_u64(fdt, fdt_path_offset(fdt, "/chosen"), "netbsd,smbios-table",
		    (uint64_t)(uintptr_t)smbios_table));

	EFI_BOOTFB_FDT(fdt_add_subnode(fdt, fdt_path_offset(fdt, "/"), "acpi"));
	EFI_BOOTFB_FDT(fdt_setprop_string(fdt, fdt_path_offset(fdt, "/acpi"), "compatible",
	    "netbsd,acpi"));

	error = efi_fdt_set_data(fdt);
#ifdef WSDISPLAY_BOOTFB_HANDOFF
	if (error == 0 && efi_bootfb_source.fdt_error == 0) {
		efi_bootfb_source.acpi_created = true;
		efi_bootfb_source.blob = fdt_data;
		efi_bootfb_source.capacity = fdt_data_size;
	}
#endif
	return error;
}
#endif

#ifdef EFIBOOT_RUNTIME_ADDRESS
static uint64_t
efi_fdt_runtime_alloc_va(uint64_t pa, uint64_t npages)
{
#if EFIBOOT_RUNTIME_ADDRESS != 0
	static uint64_t va = EFIBOOT_RUNTIME_ADDRESS;
	static uint64_t sz = EFIBOOT_RUNTIME_SIZE;
	uint64_t nva;

	if (sz < (npages * EFI_PAGE_SIZE)) {
		panic("efi_acpi_alloc_va: couldn't allocate %" PRIu64 " pages",
		    npages);
	}

	nva = va;
	va += (npages * EFI_PAGE_SIZE);
	sz -= (npages * EFI_PAGE_SIZE);

	return nva;
#else
	return pa;
#endif
}

static void
efi_fdt_runtime_note(int error, struct efi_cleanup_result *result)
{

	if (result != NULL && result->runtime_fdt_error == 0 && error != 0)
		result->runtime_fdt_error = error;
}

void
efi_fdt_set_virtual_address_map_checked(EFI_MEMORY_DESCRIPTOR *memmap,
    UINTN nentries, UINTN mapkey, UINTN descsize, UINT32 descver,
    struct efi_cleanup_result *result)
{
	EFI_MEMORY_DESCRIPTOR *md, *vmd, *vmemmap;
	EFI_STATUS status;
	int n, nrt;
	void *fdt;

	fdt = efi_fdt_data();

	vmemmap = alloc(nentries * descsize);
	if (vmemmap == NULL)
		panic("FATAL: couldn't allocate virtual memory map");

	for (n = 0, nrt = 0, vmd = vmemmap, md = memmap;
	     n < nentries;
	     n++, md = NextMemoryDescriptor(md, descsize)) {

		if ((md->Attribute & EFI_MEMORY_RUNTIME) == 0) {
			continue;
		}

		md->VirtualStart =
		    efi_fdt_runtime_alloc_va(md->PhysicalStart, md->NumberOfPages);

		switch (md->Type) {
		case EfiRuntimeServicesCode:
			efi_fdt_runtime_note(fdt_appendprop_u64(fdt,
			    fdt_path_offset(fdt, "/chosen"),
			    "netbsd,uefi-runtime-code", md->PhysicalStart), result);
			efi_fdt_runtime_note(fdt_appendprop_u64(fdt,
			    fdt_path_offset(fdt, "/chosen"),
			    "netbsd,uefi-runtime-code", md->VirtualStart), result);
			efi_fdt_runtime_note(fdt_appendprop_u64(fdt,
			    fdt_path_offset(fdt, "/chosen"),
			    "netbsd,uefi-runtime-code",
			    md->NumberOfPages * EFI_PAGE_SIZE), result);
			break;
		case EfiRuntimeServicesData:
			efi_fdt_runtime_note(fdt_appendprop_u64(fdt,
			    fdt_path_offset(fdt, "/chosen"),
			    "netbsd,uefi-runtime-data", md->PhysicalStart), result);
			efi_fdt_runtime_note(fdt_appendprop_u64(fdt,
			    fdt_path_offset(fdt, "/chosen"),
			    "netbsd,uefi-runtime-data", md->VirtualStart), result);
			efi_fdt_runtime_note(fdt_appendprop_u64(fdt,
			    fdt_path_offset(fdt, "/chosen"),
			    "netbsd,uefi-runtime-data",
			    md->NumberOfPages * EFI_PAGE_SIZE), result);
			break;
		case EfiMemoryMappedIO:
			efi_fdt_runtime_note(fdt_appendprop_u64(fdt,
			    fdt_path_offset(fdt, "/chosen"),
			    "netbsd,uefi-runtime-mmio", md->PhysicalStart), result);
			efi_fdt_runtime_note(fdt_appendprop_u64(fdt,
			    fdt_path_offset(fdt, "/chosen"),
			    "netbsd,uefi-runtime-mmio", md->VirtualStart), result);
			efi_fdt_runtime_note(fdt_appendprop_u64(fdt,
			    fdt_path_offset(fdt, "/chosen"),
			    "netbsd,uefi-runtime-mmio",
			    md->NumberOfPages * EFI_PAGE_SIZE), result);
			break;
		default:
			break;
		}

		*vmd = *md;
		vmd = NextMemoryDescriptor(vmd, descsize);
		++nrt;
	}

	if (result != NULL)
		result->runtime_attempted = true;
	status = uefi_call_wrapper(RT->SetVirtualAddressMap, 4, nrt * descsize,
	    descsize, descver, vmemmap);
	if (result != NULL)
		result->runtime_status = status;
	if (EFI_ERROR(status)) {
		printf("WARNING: SetVirtualAddressMap failed\n");
		return;
	}
}

void
efi_fdt_set_virtual_address_map(EFI_MEMORY_DESCRIPTOR *memmap, UINTN nentries,
    UINTN mapkey, UINTN descsize, UINT32 descver)
{

	efi_fdt_set_virtual_address_map_checked(memmap, nentries, mapkey,
	    descsize, descver, NULL);
}
#endif
