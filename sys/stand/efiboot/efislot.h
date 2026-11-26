/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef _EFISLOT_H_
#define _EFISLOT_H_

/* The disk immediately above the loaded image's EFI partition. */
extern int efi_slot_disk_unit;
bool efi_slot_on_disk(EFI_DEVICE_PATH *, EFI_DEVICE_PATH *);

/* Read the selected boot.cfg from the volume that loaded this EFI image. */
EFI_STATUS efi_slot_config(EFI_HANDLE, char **);
void efi_slot_boot_failed(void);

#ifdef EFIBOOT_TRIAL_RESET
const char *efi_md_trial_reset_provider(const char *, size_t);
bool efi_md_trial_reset_arm(void);
bool efi_slot_watchdog_trial(void);
#endif

#endif
