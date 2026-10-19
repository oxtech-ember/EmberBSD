/* $NetBSD: efiboot.c,v 1.24 2024/08/15 05:59:49 skrll Exp $ */

/*-
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
#include "efifile.h"
#include "efiblock.h"
#include "efirng.h"

#ifdef EFIBOOT_FDT
#include "efifdt.h"
#endif

#ifdef EFIBOOT_ACPI
#include "efiacpi.h"
#endif

#include <sys/reboot.h>
#include "cm5trace.h"

EFI_HANDLE IH;
EFI_DEVICE_PATH *efi_bootdp;
EFI_LOADED_IMAGE *efi_li;

int howto = 0;

#ifdef _LP64
#define PRIxEFIPTR "lX"
#define PRIxEFISIZE "lX"
#else
#define PRIxEFIPTR "X"
#define PRIxEFISIZE "X"
#endif

#ifdef EFIBOOT_DIAGNOSTIC_UART
uintptr_t efi_cm5_uart_base;

/* Packed ACPI fields are copied to avoid unaligned table accesses. */
static void
efi_cm5_console_probe(void)
{
	const uint8_t *rsdp, *xsdt, *table;
	uint64_t address, uart;
	uint32_t length, table_length;
	unsigned offset;
	volatile uint32_t *regs;

	rsdp = efi_acpi_root();
	if (rsdp == NULL || rsdp[15] < 2)
		return;
	memcpy(&address, rsdp + 24, sizeof(address));
	if (address == 0)
		return;
	xsdt = (const uint8_t *)(uintptr_t)address;
	memcpy(&length, xsdt + 4, sizeof(length));
	if (memcmp(xsdt, "XSDT", 4) != 0 || length < 36 || length > 65536)
		return;
	for (offset = 36; offset + 8 <= length; offset += 8) {
		memcpy(&address, xsdt + offset, sizeof(address));
		if (address == 0)
			continue;
		table = (const uint8_t *)(uintptr_t)address;
		memcpy(&table_length, table + 4, sizeof(table_length));
		if (memcmp(table, "SPCR", 4) != 0 || table_length < 52 ||
		    table[40] != 0)
			continue;
		memcpy(&uart, table + 44, sizeof(uart));
		if (uart == 0 || uart > UINTPTR_MAX - 0x30 || (uart & 3) != 0)
			return;
		efi_cm5_uart_base = (uintptr_t)uart;
		regs = (volatile uint32_t *)efi_cm5_uart_base;
		printf("[CM5 EFI] SPCR UART=0x%lx FR=0x%x CR=0x%x\n",
		    (unsigned long)uart, regs[0x18 / 4], regs[0x30 / 4]);
		efi_cm5_trace("[CM5 EFI] direct UART probe\r\n");
		return;
	}
	printf("[CM5 EFI] No memory-mapped SPCR UART found\n");
}
#endif

static EFI_PHYSICAL_ADDRESS heap_start;
static UINTN heap_size = 8 * 1024 * 1024;
static EFI_EVENT delay_ev = 0;

EFI_STATUS EFIAPI efi_main(EFI_HANDLE, EFI_SYSTEM_TABLE *);

EFI_STATUS EFIAPI
efi_main(EFI_HANDLE imageHandle, EFI_SYSTEM_TABLE *systemTable)
{
	EFI_STATUS status;
	u_int sz = EFI_SIZE_TO_PAGES(heap_size);

	IH = imageHandle;

	InitializeLib(imageHandle, systemTable);
#ifdef EFIBOOT_DIAGNOSTIC_UART
	{
		uint64_t pending;
		__asm volatile("dsb sy; isb; mrs %0, isr_el1" : "=r"(pending)
		    :: "memory");
		printf("[CM5 EFI] initial ISR=0x%lx\n", (unsigned long)pending);
	}
#endif

	uefi_call_wrapper(ST->ConOut->Reset, 2, ST->ConOut, TRUE);
	uefi_call_wrapper(ST->ConOut->SetMode, 2, ST->ConOut, 0);
	uefi_call_wrapper(ST->ConOut->EnableCursor, 2, ST->ConOut, TRUE);

	status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, sz, &heap_start);
	if (EFI_ERROR(status))
		return status;
	setheap((void *)(uintptr_t)heap_start, (void *)(uintptr_t)(heap_start + heap_size));

	status = uefi_call_wrapper(BS->HandleProtocol, 3, imageHandle, &LoadedImageProtocol, (void **)&efi_li);
	if (EFI_ERROR(status))
		return status;
	status = uefi_call_wrapper(BS->HandleProtocol, 3, efi_li->DeviceHandle, &DevicePathProtocol, (void **)&efi_bootdp);
	if (EFI_ERROR(status))
		efi_bootdp = NULL;
	else
		efi_bootdp = DuplicateDevicePath(efi_bootdp);

#ifdef EFIBOOT_DEBUG
	Print(L"Loaded image      : 0x%" PRIxEFIPTR "\n", efi_li);
	Print(L"FilePath          : 0x%" PRIxEFIPTR "\n", efi_li->FilePath);
	Print(L"ImageBase         : 0x%" PRIxEFIPTR "\n", efi_li->ImageBase);
	Print(L"ImageSize         : 0x%" PRIxEFISIZE "\n", efi_li->ImageSize);
	Print(L"Image file        : %s\n", DevicePathToStr(efi_li->FilePath));
#endif

#ifdef EFIBOOT_ACPI
	efi_acpi_probe();
#endif
#ifdef EFIBOOT_FDT
	efi_fdt_probe();
#endif
	efi_pxe_probe();
	efi_net_probe();
	efi_file_system_probe();
	efi_block_probe();
	efi_rng_probe();
	efi_gop_probe();

#ifdef EFIBOOT_DIAGNOSTIC_UART
	efi_cm5_console_probe();
	efi_cm5_pending("before boot");
#endif

	boot();

	return EFI_SUCCESS;
}

void
efi_cleanup_checked(struct efi_cleanup_result *out)
{
	EFI_STATUS status;
	EFI_MEMORY_DESCRIPTOR *memmap;
	UINTN nentries = 0, mapkey = 0, descsize = 0;
	UINT32 descver = 0;
	struct efi_cleanup_result value;

	memset(&value, 0, sizeof(value));

	efi_cm5_pending("before memory map");
	memmap = LibMemoryMap(&nentries, &mapkey, &descsize, &descver);
	value.map_available = memmap != NULL;
	if (memmap == NULL) {
		printf("WARNING: memory map unavailable before ExitBootServices\n");
		goto done;
	}

	efi_cm5_trace("[CM5 EFI] calling ExitBootServices\r\n");
	efi_cm5_pending("before EBS");
	value.ebs_attempted = true;
	status = uefi_call_wrapper(BS->ExitBootServices, 2, IH, mapkey);
	value.ebs_status = status;
	efi_cm5_trace("[CM5 EFI] ExitBootServices call returned\r\n");
	efi_cm5_pending("after EBS");
	if (EFI_ERROR(status)) {
		printf("WARNING: ExitBootServices failed\n");
		goto done;
	}

#ifdef EFIBOOT_RUNTIME_ADDRESS
	efi_cm5_trace("[CM5 EFI] SetVirtualAddressMap enter\r\n");
	efi_fdt_set_virtual_address_map_checked(memmap, nentries, mapkey,
	    descsize, descver, &value);
	efi_cm5_trace("[CM5 EFI] SetVirtualAddressMap returned\r\n");
	efi_cm5_pending("after VA map");
#endif
done:
	if (out != NULL)
		*out = value;
}

void
efi_cleanup(void)
{

	efi_cleanup_checked(NULL);
}

void
efi_exit(void)
{
	EFI_STATUS status;

	status = uefi_call_wrapper(BS->Exit, 4, IH, EFI_ABORTED, 0, NULL);
	if (EFI_ERROR(status))
		printf("WARNING: Exit failed\n");
}

void
efi_reboot(void)
{
	uefi_call_wrapper(RT->ResetSystem, 4, EfiResetCold, EFI_SUCCESS, 0, NULL);

	printf("WARNING: Reset failed\n");
}

void
efi_delay(int us)
{
	EFI_STATUS status;
	UINTN val;

	if (delay_ev == 0) {
		status = uefi_call_wrapper(BS->CreateEvent, 5, EVT_TIMER, TPL_APPLICATION, 0, 0, &delay_ev);
		if (EFI_ERROR(status))
			return;
	}

	uefi_call_wrapper(BS->SetTimer, 3, delay_ev, TimerRelative, us * 10);
	uefi_call_wrapper(BS->WaitForEvent, 3, 1, &delay_ev, &val);
}

void
efi_progress(const char *fmt, ...)
{
	va_list ap;

	if ((howto & AB_SILENT) != 0)
		return;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}
