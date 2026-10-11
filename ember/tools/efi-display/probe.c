/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted). One-shot UEFI display diagnosis. */
#include <efi.h>
#include <efilib.h>
#include "rpi-firmware-protocol.h"

static EFI_BOOT_SERVICES *bs;
static EFI_FILE_HANDLE volume, journal;
static EFI_STATUS journal_status;
static EFI_GUID image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID path_guid = EFI_DEVICE_PATH_PROTOCOL_GUID;
static EFI_GUID info_guid = EFI_FILE_INFO_ID;
static EFI_GUID firmware_guid = RASPBERRY_PI_FIRMWARE_PROTOL_GUID;
static EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
/* DisplayDxe.inf FILE_GUID in the pinned Raspberry Pi firmware source. */
static EFI_GUID display_guid = { 0xc5deae31, 0xfad2, 0x4030,
    { 0x84, 0x1b, 0xcf, 0xc9, 0x64, 0x4d, 0x2c, 0x5b } };
static CHAR16 original[] = L"\\EFI\\BOOT\\ember-hdmi-original.efi";

EFI_STATUS EFIAPI efi_main(EFI_HANDLE, EFI_SYSTEM_TABLE *);

static void
record(const char *name, UINT64 value)
{
	char line[160];
	const char hex[] = "0123456789abcdef";
	UINTN len = 0, size;
	int shift;

	while (*name && len < sizeof(line) - 24)
		line[len++] = *name++;
	line[len++] = '=';
	line[len++] = '0';
	line[len++] = 'x';
	for (shift = 60; shift >= 0; shift -= 4)
		line[len++] = hex[(value >> shift) & 15];
	line[len++] = '\r';
	line[len++] = '\n';
	if (journal != NULL && !EFI_ERROR(journal_status)) {
		size = len;
		journal_status = journal->Write(journal, &size, line);
		if (!EFI_ERROR(journal_status) && size != len)
			journal_status = EFI_DEVICE_ERROR;
		if (!EFI_ERROR(journal_status))
			journal_status = journal->Flush(journal);
	}
}

static EFI_STATUS
file_info(EFI_FILE_HANDLE file, EFI_FILE_INFO **info, UINTN *size)
{
	EFI_STATUS status;

	*info = NULL;
	*size = 0;
	status = file->GetInfo(file, &info_guid, size, NULL);
	if (status != EFI_BUFFER_TOO_SMALL || *size > 65536 ||
	    *size < SIZE_OF_EFI_FILE_INFO)
		return EFI_DEVICE_ERROR;
	status = bs->AllocatePool(EfiLoaderData, *size, (void **)info);
	if (EFI_ERROR(status))
		return status;
	status = file->GetInfo(file, &info_guid, size, *info);
	if (EFI_ERROR(status)) {
		bs->FreePool(*info);
		*info = NULL;
	}
	return status;
}

static EFI_STATUS
open_output(CHAR16 *name, UINT64 length, EFI_FILE_HANDLE *file)
{
	EFI_FILE_INFO *info;
	UINTN size;
	EFI_STATUS status;

	*file = NULL;
	status = volume->Open(volume, file, name,
	    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
	if (EFI_ERROR(status))
		return status;
	status = file_info(*file, &info, &size);
	if (!EFI_ERROR(status)) {
		info->FileSize = length;
		status = (*file)->SetInfo(*file, &info_guid, size, info);
		bs->FreePool(info);
	}
	if (EFI_ERROR(status)) {
		(*file)->Close(*file);
		*file = NULL;
	}
	return status;
}

/* Restore the unchanged loader before probing, so the next boot is ordinary. */
static EFI_STATUS
restore_loader(void)
{
	EFI_FILE_HANDLE input = NULL, output = NULL;
	EFI_FILE_INFO *info;
	UINT8 *data = NULL, *check = NULL;
	UINTN size, length = 0, count, i;
	EFI_STATUS status;

	status = volume->Open(volume, &input, original, EFI_FILE_MODE_READ, 0);
	if (EFI_ERROR(status))
		return status;
	status = file_info(input, &info, &size);
	if (EFI_ERROR(status))
		goto out;
	length = info->FileSize;
	bs->FreePool(info);
	if (length < 512 || length > 2 * 1024 * 1024) {
		status = EFI_BAD_BUFFER_SIZE;
		goto out;
	}
	status = bs->AllocatePool(EfiLoaderData, length, (void **)&data);
	if (EFI_ERROR(status))
		goto out;
	count = length;
	status = input->Read(input, &count, data);
	if (EFI_ERROR(status) || count != length || data[0] != 'M' ||
	    data[1] != 'Z') {
		status = EFI_LOAD_ERROR;
		goto out;
	}
	status = bs->AllocatePool(EfiLoaderData, length, (void **)&check);
	if (EFI_ERROR(status))
		goto out;
	status = open_output(L"\\EFI\\BOOT\\BOOTAA64.EFI", length, &output);
	if (EFI_ERROR(status))
		goto out;
	count = length;
	status = output->Write(output, &count, data);
	if (EFI_ERROR(status) || count != length) {
		status = EFI_DEVICE_ERROR;
		goto out;
	}
	status = output->Flush(output);
	if (EFI_ERROR(status))
		goto out;
	status = output->SetPosition(output, 0);
	if (EFI_ERROR(status))
		goto out;
	count = length;
	status = output->Read(output, &count, check);
	if (EFI_ERROR(status) || count != length) {
		status = EFI_DEVICE_ERROR;
		goto out;
	}
	for (i = 0; i < length; i++) {
		if (data[i] != check[i]) {
			status = EFI_CRC_ERROR;
			break;
		}
	}
out:
	if (input != NULL)
		input->Close(input);
	if (output != NULL)
		output->Close(output);
	if (data != NULL)
		bs->FreePool(data);
	if (check != NULL)
		bs->FreePool(check);
	return status;
}

static void
report_firmware(void)
{
	RASPBERRY_PI_FIRMWARE_PROTOCOL *fw;
	EFI_STATUS status;
	UINT32 width = 0, height = 0, revision = 0;

	status = bs->LocateProtocol(&firmware_guid, NULL, (void **)&fw);
	record("firmware.locate", status);
	if (EFI_ERROR(status))
		return;
	status = fw->GetFirmwareRevision(&revision);
	record("firmware.revision.status", status);
	record("firmware.revision", revision);
	status = fw->GetFBSize(&width, &height);
	record("firmware.geometry.status", status);
	record("firmware.width", width);
	record("firmware.height", height);
}

static void
report_gop(const char *stage)
{
	EFI_HANDLE *handles;
	EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
	UINTN count, i;
	EFI_STATUS status;

	status = bs->LocateHandleBuffer(ByProtocol, &gop_guid, NULL,
	    &count, &handles);
	record(stage, status);
	if (EFI_ERROR(status))
		return;
	record("gop.count", count);
	for (i = 0; i < count; i++) {
		status = bs->HandleProtocol(handles[i], &gop_guid, (void **)&gop);
		record("gop.status", status);
		if (EFI_ERROR(status) || gop->Mode == NULL)
			continue;
		record("gop.maxmode", gop->Mode->MaxMode);
		record("gop.mode", gop->Mode->Mode);
		record("gop.base", gop->Mode->FrameBufferBase);
		record("gop.size", gop->Mode->FrameBufferSize);
		if (gop->Mode->Info == NULL)
			continue;
		record("gop.width", gop->Mode->Info->HorizontalResolution);
		record("gop.height", gop->Mode->Info->VerticalResolution);
		record("gop.format", gop->Mode->Info->PixelFormat);
		record("gop.pitch", gop->Mode->Info->PixelsPerScanLine);
	}
	bs->FreePool(handles);
}

static EFI_STATUS
chainload(EFI_HANDLE image, EFI_HANDLE device)
{
	EFI_DEVICE_PATH *base, *node, *path;
	EFI_HANDLE next;
	EFI_STATUS status;
	UINTN bytes = 0, size, i, file_bytes = sizeof(original) + 4;
	UINT8 *dest;

	status = bs->HandleProtocol(device, &path_guid, (void **)&base);
	if (EFI_ERROR(status))
		return status;
	node = base;
	while (!IsDevicePathEnd(node)) {
		size = DevicePathNodeLength(node);
		if (size < 4 || bytes + size > 65536)
			return EFI_BAD_BUFFER_SIZE;
		bytes += size;
		node = NextDevicePathNode(node);
	}
	status = bs->AllocatePool(EfiLoaderData, bytes + file_bytes + 4,
	    (void **)&path);
	if (EFI_ERROR(status))
		return status;
	dest = (UINT8 *)path;
	for (i = 0; i < bytes; i++)
		dest[i] = ((UINT8 *)base)[i];
	node = (EFI_DEVICE_PATH *)(dest + bytes);
	node->Type = MEDIA_DEVICE_PATH;
	node->SubType = MEDIA_FILEPATH_DP;
	SetDevicePathNodeLength(node, file_bytes);
	for (i = 0; i < sizeof(original); i++)
		dest[bytes + 4 + i] = ((UINT8 *)original)[i];
	node = (EFI_DEVICE_PATH *)(dest + bytes + file_bytes);
	SetDevicePathEndNode(node);
	status = bs->LoadImage(FALSE, image, path, NULL, 0, &next);
	record("chain.load", status);
	bs->FreePool(path);
	if (EFI_ERROR(status))
		return status;
	if (journal != NULL) {
		journal->Close(journal);
		journal = NULL;
	}
	volume->Close(volume);
	return bs->StartImage(next, NULL, NULL);
}

EFI_STATUS EFIAPI
efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system)
{
	EFI_LOADED_IMAGE *loaded;
	EFI_FILE_IO_INTERFACE *fs;
	EFI_HANDLE *handles;
	EFI_STATUS status;
	UINTN count, i;

	bs = system->BootServices;
	status = bs->HandleProtocol(image, &image_guid, (void **)&loaded);
	if (EFI_ERROR(status))
		return status;
	status = bs->HandleProtocol(loaded->DeviceHandle, &fs_guid, (void **)&fs);
	if (EFI_ERROR(status))
		return status;
	status = fs->OpenVolume(fs, &volume);
	if (EFI_ERROR(status))
		return status;
	journal_status = open_output(L"\\ember-hdmi-probe.log", 0, &journal);
	record("probe.version", 1);
	status = restore_loader();
	record("restore.status", status);
	if (!EFI_ERROR(status) && !EFI_ERROR(journal_status)) {
		report_firmware();
		report_gop("gop.before");
		status = bs->LocateHandleBuffer(ByProtocol, &display_guid, NULL,
		    &count, &handles);
		record("display.locate", status);
		if (!EFI_ERROR(status)) {
			record("display.count", count);
			for (i = 0; i < count; i++) {
				status = bs->ConnectController(handles[i], NULL, NULL, FALSE);
				record("display.connect", status);
			}
			bs->FreePool(handles);
		}
		report_gop("gop.after");
	}
	return chainload(image, loaded->DeviceHandle);
}
