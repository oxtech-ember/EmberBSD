# EFI system update images

An EFI update image has an EFI System Partition (ESP) and two FFS root
partitions on the same GPT disk. Each FFS contains its own ELF `/netbsd` and
matching kernel modules. The EFI loader stays on the ESP when the installer
updates an inactive root. It runs on AArch64 and x86-64.

The common image producer installs the fallback loader from the selected
boot set. AArch64 uses `EFI/BOOT/bootaa64.efi`. X86-64 uses
`EFI/BOOT/bootx64.efi`. The loader opens `EFI/EmberBSD` on the exact
volume that loaded it. An ordinary installation without either confirmed
marker keeps its normal boot configuration.

## One trial boot

The boot adapter and efiboot share the confirmed markers `osgood.dat` and
`osback.dat`. The next slot occupies one byte in `osnext.dat`. The loader
must replace that byte with `-`, flush it and read it back before a trial.
It reads `boot-a.cfg` or `boot-b.cfg` from the same ESP. In these menus,
`bootpartb:netbsd` and `bootpartc:netbsd` select partitions 2 and 3 on that
disk. Disk enumeration can change when a user connects another drive.

The volatile EFI variable `EmberBootSlot` contains the slot letter followed
by `T` for a trial or `C` for a confirmed boot. The adapter checks this
receipt against the mounted root. The installed service confirms the OS
after its health check. EFI `BootNext` selects a firmware boot entry for
one boot on implementations with persistent variable storage. This can
start an EFI loader. Slot selection and OS confirmation still use the ESP
and the installed service.

UEFI disables its boot services watchdog at `ExitBootServices`. Autonomous
trials need an independent reset provider with a kernel handoff. The CM5
profile selects `bcm2712-pm`. The loader checks its DMI binding and reports
the provider through the volatile `EmberBootReset` variable. After successful
exit from boot services it starts the BCM2712 PM timer for 15 seconds. The
kernel checks the running timer and takes responsibility for a bounded
startup interval. A missing capability prevents the installed client from
preparing and arming a new root. A VM can use the isolated firmware test
below to exercise one-shot selection.

Only a qualification loader built with `EFIBOOT_SLOT_TEST=yes` recognizes
test markers. Build it in a separate object directory. A test ESP can contain `EFI/EmberBSD/manual-test` with exactly
one byte, `1`. The test must have no `osplan.dat`. This permits explicit
VM trials without a reset provider. The factory image and the installed
updater do not create this file.

## Raspberry Pi Compute Module 5

Use the [CM5 update profile](../../../ember/image/os-profiles/bcm2712-cm5.json)
with `mkimage -h bcm2712-cm5 -U profile.json`. Supply the release root,
signed boot contents, initial etc archive, hash and OS commit as required
by the [image producer](mkimage). The kernel directory needs the ELF
`netbsd`, its `modules/` directory and the slot-aware `bootaa64.efi`.
Set `CM5_UEFI` to a verified CM5 `RPI_EFI.fd` file. Set
`CM5_FIRMWARE_DTB` to its matching `bcm2712-rpi-5-b.dtb`. The producer
installs the Raspberry Pi boot files and their license from the OS source.
See the [firmware build guide](../../../ember/firmware/README.md) for the
firmware input. Supply the site's network configuration through its image
overlay.

Run the [firmware selection test](../../../ember/tests/efiboot-slots-vm.py)
with `--arch`, `--loader`, `--test-loader`, `--code` and `--vars`. It uses QEMU, EDK2 and
mtools. Each run creates a private FAT image and a fresh copy of the variable
store. AArch64 and x86-64 firmware selection are separate from hardware reset
qualification for each board.
