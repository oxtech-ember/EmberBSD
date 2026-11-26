# U-Boot and FFS boot slots

This adapter implements the [OS service interface](automatic-system-update.md#loader-and-installer-interface).
It uses an EFI FAT partition, two GPT FFS roots named by the hardware profile, an ARM64 raw kernel image and an FDT.
The firmware must support a one-shot selector, its FAT recovery entry and a hardware reset before and during kernel startup.
The board boot script handles device detection, register access, the root argument and the watchdog handoff.

The image description supplies the hardware model, kernel tag, module and DTB lists, target ABI, health program and board script.
It also supplies two free RAM addresses for the selector's marker and script buffers.
The selector consumes and reads back the one-shot marker before the board script starts a trial.
On a failed confirmation write the backend uses the watchdog period reported by the running system.

Set `UPDATE_PROFILE` to the same image description used at installation. Enrollment and repair validate it against the running device.
The service and per-slot receipts continue to use the profile ID from the signed channel.
The published [Zero 3W description](../../image/os-profiles/orangepi-zero3w.json) is one such profile.

## Enable an already provisioned card

Use this operation only on an installed slot A with the same dedicated A, B and EFI wedges.
Keep its normal `boot.scr`. The live operation checks the active model, layout, kernel and SDK.
It checks the baseline archive against the digest from the verified release before writing new files.
It preserves existing site files outside the listed profile configuration.
Supply the OS source tree containing the two-slot support on the device, then run as root:

```sh
/usr/pkg/bin/python3.15 -I "$OS_SOURCE/ember/image/provision-uboot-ffs.py" \
    --profile "$UPDATE_PROFILE" --live --os-commit "$OS_COMMIT" --etc-archive "$SIGNED_ETC_ARCHIVE" \
    --etc-sha256 "$SIGNED_ETC_SHA256" --mkubootimage /usr/bin/mkubootimage
```

The bootstrap writes the current generation as zero, installs the one-shot loader, then enables `ember_os_update` for subsequent boots.
Use `--health /path/to/check` to install a site-specific probe. Run `/etc/rc.d/ember_os_update start` to start the first pass now.
An in-progress installation or another boot entry must be resolved before provisioning this profile.

## Repair the selector on an installed card

Use this operation to repair the stock slot backend and `boot.scr.uimg` on an enrolled system.
Use the confirmed slot A, including after a rejected trial. Obtain both original files from the trusted installation image or its saved build output.
Obtain a separate copy of the normal slot A `boot.scr` from the original installation image or its authenticated boot set.
Supply its original `slotboot.scr` with `--previous-board` when upgrading the board script or recovering an interrupted FAT rename.

On confirmed slot A, stop OS updates. Run the repair as root:

```sh
/etc/rc.d/ember_os_update stop
/usr/pkg/bin/python3.15 -I "$OS_SOURCE/ember/image/provision-uboot-ffs.py" \
    --profile "$UPDATE_PROFILE" --repair-loader --previous-loader "$ORIGINAL_SELECTOR" --previous-backend "$ORIGINAL_BACKEND" \
    --recovery-loader "$ORIGINAL_BOOT_SCR" --mkubootimage /usr/bin/mkubootimage
/etc/rc.d/ember_os_update start
```

The repair takes the service lock and checks the running slot, the recovery entry, and the empty next-boot marker.
It verifies both installed files before writing. It installs the compatible backend on the running root first, then the selector on FAT.
It flushes each file and its directory, then reads back the installed bytes.
During a FAT rename, a power loss can leave `boot.scr.uimg` absent. U-Boot then loads the verified `boot.scr` and boots slot A.
Run the same repair again with the same inputs to restore the preferred entry.
The installed generation and its recorded attempt survive the repair.
If a site diagnostic occupies the selector path, the check stops before writing it.

The selector uses bare filenames for FAT markers in the partition root.
The vendor FAT command can report success for a prefixed write while the file still contains the old slot.
The loader starts a trial only after one write and a separate one-byte readback of `-`.
