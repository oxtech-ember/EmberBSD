/* SPDX-License-Identifier: BSD-2-Clause */
/* One-shot boot selection on the EFI System Partition. */

#include <efi.h>
#include <efilib.h>

#include <lib/libsa/stand.h>
#include <lib/libkern/libkern.h>

#include "efislot.h"

#define SLOT_CFG_MAX	32768

int efi_slot_disk_unit = -1;
static bool trial_selected;

void
efi_slot_boot_failed(void)
{
	if (trial_selected) {
		printf("EFI trial kernel did not start; restarting\n");
		uefi_call_wrapper(RT->ResetSystem, 4, EfiResetCold,
		    EFI_LOAD_ERROR, 0, NULL);
		for (;;)
			;
	}
}


bool
efi_slot_on_disk(EFI_DEVICE_PATH *esp, EFI_DEVICE_PATH *disk)
{
	if (esp == NULL || disk == NULL)
		return false;
	while (!IsDevicePathEnd(disk)) {
		if (IsDevicePathEnd(esp) ||
		    DevicePathNodeLength(esp) != DevicePathNodeLength(disk) ||
		    memcmp(esp, disk, DevicePathNodeLength(disk)) != 0)
			return false;
		esp = NextDevicePathNode(esp);
		disk = NextDevicePathNode(disk);
	}
	return DevicePathType(esp) == MEDIA_DEVICE_PATH &&
	    DevicePathSubType(esp) == MEDIA_HARDDRIVE_DP &&
	    IsDevicePathEnd(NextDevicePathNode(esp));
}

static EFI_STATUS
read_file(EFI_FILE_HANDLE root, CHAR16 *name, void *data, UINTN *size)
{
	EFI_FILE_HANDLE file;
	EFI_STATUS rv, close_rv;

	rv = uefi_call_wrapper(root->Open, 5, root, &file, name,
	    EFI_FILE_MODE_READ, 0);
	if (EFI_ERROR(rv))
		return rv;
	rv = uefi_call_wrapper(file->Read, 3, file, size, data);
	close_rv = uefi_call_wrapper(file->Close, 1, file);
	return EFI_ERROR(rv) ? rv : close_rv;
}

static EFI_STATUS
read_marker(EFI_FILE_HANDLE root, CHAR16 *name, char *value)
{
	char data[2];
	UINTN size = sizeof(data);
	EFI_STATUS rv;

	rv = read_file(root, name, data, &size);
	if (EFI_ERROR(rv))
		return rv;
	if (size != 1 || (data[0] != 'A' && data[0] != 'B' && data[0] != '-'))
		return EFI_VOLUME_CORRUPTED;
	*value = data[0];
	return EFI_SUCCESS;
}

static bool
consume(EFI_FILE_HANDLE root)
{
	EFI_FILE_HANDLE file;
	char clear = '-', value;
	UINTN size = 1;
	EFI_STATUS rv, close_rv;

	rv = uefi_call_wrapper(root->Open, 5, root, &file, L"\\EFI\\EmberBSD\\osnext.dat",
	    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
	if (EFI_ERROR(rv))
		return false;
	rv = uefi_call_wrapper(file->Write, 3, file, &size, &clear);
	if (!EFI_ERROR(rv) && size == 1)
		rv = uefi_call_wrapper(file->Flush, 1, file);
	close_rv = uefi_call_wrapper(file->Close, 1, file);
	return !EFI_ERROR(rv) && size == 1 && !EFI_ERROR(close_rv) &&
	    !EFI_ERROR(read_marker(root, L"\\EFI\\EmberBSD\\osnext.dat", &value)) && value == '-';
}

static EFI_STATUS
read_config(EFI_FILE_HANDLE root, char slot, char *buf)
{
	CHAR16 *name = slot == 'A' ? L"\\EFI\\EmberBSD\\boot-a.cfg" :
	    L"\\EFI\\EmberBSD\\boot-b.cfg";
	UINTN size = SLOT_CFG_MAX + 1;
	EFI_STATUS rv;

	rv = read_file(root, name, buf, &size);
	if (EFI_ERROR(rv))
		return rv;
	if (size == 0 || size > SLOT_CFG_MAX || memchr(buf, '\0', size) != NULL)
		return EFI_VOLUME_CORRUPTED;
	buf[size] = '\0';
	return EFI_SUCCESS;
}

/*
 * The boot manager resets volatile variables on reboot. Set and verify this
 * record for the current loader execution before handing a trial to its OS.
 */
static bool
record_variable(CHAR16 *name, const char *bytes, UINTN len)
{
	static EFI_GUID guid = { 0x404b8673, 0x6bc3, 0x4602,
	    { 0xaa, 0x7d, 0x4d, 0xbb, 0x89, 0x75, 0xe2, 0xb5 } };
	UINT32 attrs = EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS;
	UINT32 actual = 0;
	char readback[32];
	UINTN size = sizeof(readback);
	EFI_STATUS rv;

	rv = uefi_call_wrapper(RT->SetVariable, 5, name, &guid, attrs,
	    len, __UNCONST(bytes));
	if (EFI_ERROR(rv))
		return false;
	rv = uefi_call_wrapper(RT->GetVariable, 5, name, &guid, &actual,
	    &size, readback);
	return !EFI_ERROR(rv) && actual == attrs && size == len &&
	    memcmp(readback, bytes, size) == 0;
}

static bool
record_boot(char slot, bool trial)
{
	char bytes[2] = { slot, trial ? 'T' : 'C' };

	return record_variable(L"EmberBootSlot", bytes, sizeof(bytes));
}

#ifdef EFIBOOT_TRIAL_RESET
static bool trial_watchdog;

bool
efi_slot_watchdog_trial(void)
{
	return trial_watchdog;
}

static bool
select_watchdog(EFI_FILE_HANDLE root)
{
	const char *provider;
	char requested[32];
	UINTN size = sizeof(requested);

	if (EFI_ERROR(read_file(root, L"\\EFI\\EmberBSD\\reset-provider",
	    requested, &size)) || size == sizeof(requested))
		return false;
	provider = efi_md_trial_reset_provider(requested, size);
	return provider != NULL &&
	    record_variable(L"EmberBootReset", provider, strlen(provider));
}
#endif

EFI_STATUS
efi_slot_config(EFI_HANDLE device, char **config)
{
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
	EFI_FILE_HANDLE root;
	EFI_STATUS rv, good_rv, back_rv;
	char good = '-', back = '-', next = '-';
	char *buf;
	bool watchdog = false, manual = false;
#ifdef EFIBOOT_SLOT_TEST
	char diagnostic[2], plan;
	UINTN diagnostic_size = sizeof(diagnostic), plan_size = 1;
#endif

	*config = NULL;
	rv = uefi_call_wrapper(BS->HandleProtocol, 3, device,
	    &FileSystemProtocol, (void **)&fs);
	if (EFI_ERROR(rv))
		return EFI_NOT_FOUND;	/* Network and other non-ESP loaders. */
	rv = uefi_call_wrapper(fs->OpenVolume, 2, fs, &root);
	if (EFI_ERROR(rv))
		return rv;
	good_rv = read_marker(root, L"\\EFI\\EmberBSD\\osgood.dat", &good);
	back_rv = read_marker(root, L"\\EFI\\EmberBSD\\osback.dat", &back);
	if (good_rv == EFI_NOT_FOUND && back_rv == EFI_NOT_FOUND) {
		rv = EFI_NOT_FOUND;	/* Ordinary installation. */
		goto out;
	}
	if (EFI_ERROR(good_rv) || good == '-')
		good = back;
	if (good != 'A' && good != 'B') {
		rv = EFI_VOLUME_CORRUPTED;
		goto out;
	}
#ifdef EFIBOOT_TRIAL_RESET
	watchdog = select_watchdog(root);
#endif
#ifdef EFIBOOT_SLOT_TEST
	printf("EFIBOOT_SLOT_TEST_ONLY\n");
	/* Qualification image only. Production binaries have no test marker. */
	manual = read_file(root, L"\\EFI\\EmberBSD\\osplan.dat", &plan, &plan_size)
	    == EFI_NOT_FOUND && !EFI_ERROR(read_file(root,
	    L"\\EFI\\EmberBSD\\manual-test", diagnostic, &diagnostic_size)) &&
	    diagnostic_size == 1 && diagnostic[0] == '1';
#endif
	buf = alloc(SLOT_CFG_MAX + 1);
	if (buf == NULL) {
		rv = EFI_OUT_OF_RESOURCES;
		goto out;
	}
	rv = read_marker(root, L"\\EFI\\EmberBSD\\osnext.dat", &next);
	if (!EFI_ERROR(rv) && next != '-' && next != good && consume(root) &&
	    (watchdog || manual) &&
	    !EFI_ERROR(read_config(root, next, buf)) && record_boot(next, true)) {
#ifdef EFIBOOT_TRIAL_RESET
		trial_watchdog = watchdog;
#endif
		trial_selected = true;
		printf("EFI trial slot %c\n", next);
		*config = buf;
		rv = EFI_SUCCESS;
		goto out;
	}
	rv = read_config(root, good, buf);
	if (EFI_ERROR(rv)) {
		/* Missing configuration on an enrolled volume is an error. */
		rv = EFI_VOLUME_CORRUPTED;
		dealloc(buf, SLOT_CFG_MAX + 1);
		goto out;
	}
	(void)record_boot(good, false);
	printf("EFI confirmed slot %c\n", good);
	*config = buf;
out:
	(void)uefi_call_wrapper(root->Close, 1, root);
	return rv;
}
