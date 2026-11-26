# U-Boot and FFS boot slots

This adapter implements the [OS service interface](automatic-system-update.md#loader-and-installer-interface).
It uses an EFI FAT partition, two GPT FFS roots named by the hardware profile, an ARM64 raw kernel image and an FDT.
The firmware must support a one-shot selector, its FAT recovery entry and a hardware reset before and during kernel startup.
The board boot script handles device detection, register access, the root argument and the watchdog handoff.

The image description supplies the hardware model, kernel tag, module and DTB lists, target ABI, health program and board script.
It also supplies two free RAM addresses for the selector's marker and script buffers.
The selector consumes and reads back the one-shot marker before the board script starts a trial.
On a failed confirmation write the backend uses the watchdog period reported by the running system.

The image producer installs the profile, two roots and selector before first boot.
Use the [common image procedure](automatic-system-update.md#build-an-image-with-automatic-updates).
The common FFS installer prepares the candidate. The boot adapter reads and confirms the selected slot.
The [Zero 3W description](../../image/os-profiles/orangepi-zero3w.json) supplies one supported U-Boot layout.

Run the [selector regression](../../boot/test-uboot-ffs-slots.py) with host Python.
It checks FAT marker consumption, verified readback, the trial boot and the recovery entry.

## Repair an earlier installation

For a card running the G4 updater, use its matching [enrollment and selector recovery procedure](https://github.com/oxtech-ember/EmberBSD/blob/4786829fa46/ember/tools/docs/uboot-ffs-slots.md).
Get the [G4 tools](https://github.com/oxtech-ember/EmberBSD/tree/4786829fa46/ember/image) from the same revision.
Identify the active release and exact hardware profile before replacing its loader.
