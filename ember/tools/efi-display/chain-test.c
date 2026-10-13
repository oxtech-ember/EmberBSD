/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted). QEMU-only chainload acceptance target. */
#include <efi.h>
#include <efilib.h>

EFI_STATUS EFIAPI efi_main(EFI_HANDLE, EFI_SYSTEM_TABLE *);

EFI_STATUS EFIAPI
efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system)
{
	EFI_GUID image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
	EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
	EFI_LOADED_IMAGE *loaded;
	EFI_FILE_IO_INTERFACE *fs;
	EFI_FILE_HANDLE root, file;
	EFI_STATUS status;
	char text[] = "PASS: chain loaded\n";
	UINTN size = sizeof(text) - 1;

	status = system->BootServices->HandleProtocol(image, &image_guid,
	    (void **)&loaded);
	if (EFI_ERROR(status))
		return status;
	status = system->BootServices->HandleProtocol(loaded->DeviceHandle,
	    &fs_guid, (void **)&fs);
	if (EFI_ERROR(status))
		return status;
	status = fs->OpenVolume(fs, &root);
	if (EFI_ERROR(status))
		return status;
	status = root->Open(root, &file, L"\\chain-pass.txt",
	    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
	if (!EFI_ERROR(status)) {
		status = file->Write(file, &size, text);
		if (!EFI_ERROR(status) && size != sizeof(text) - 1)
			status = EFI_DEVICE_ERROR;
		if (!EFI_ERROR(status))
			status = file->Flush(file);
		file->Close(file);
	}
	root->Close(root);
	system->RuntimeServices->ResetSystem(EfiResetShutdown, status, 0, NULL);
	return status;
}
