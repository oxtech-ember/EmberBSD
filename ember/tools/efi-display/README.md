# One-shot UEFI display diagnosis

This AArch64 application separates firmware display discovery from EmberBSD's
framebuffer attachment. It records Raspberry Pi firmware geometry and GOP
before the OS loader runs. It can connect the Raspberry Pi DisplayDxe handles;
it does not enumerate arbitrary controllers or access display MMIO.

EmberBSD owns the probe and host checks. They are AI-assisted BSD-2-Clause
code. The unmodified `rpi-firmware-protocol.h` preserves its upstream authors
and license. It is [RpiFirmware.h at c4b5d05](https://github.com/eotics-com/edk2-platforms/blob/c4b5d05de1f2ef633bdb4c175b5c118fcb2666ed/Platform/RaspberryPi/Include/Protocol/RpiFirmware.h),
SHA256 `576b90b60c3e9d1464d82530d037b14370fb58acacf1078ebfb268b200bd3ff6`.
The DisplayDxe GUID comes from that same revision. GNU-EFI startup code and
headers come from the selected EmberBSD source checkout.

## Build and isolated checks

Use the current GCC16 cross tools and a verified EmberBSD sysroot. No target
program runs on the build host. Output directories must not already exist.

```sh
sh ember/tools/efi-display/build.sh \
    "$EMBER_SOURCE" "$NEW_OUTPUT" "$CROSS_TOOLS" "$EMBER_SYSROOT"

QEMU=/absolute/qemu-system-aarch64 \
EFI_CODE=/absolute/raw/edk2-aarch64-code.fd \
EFI_VARS=/absolute/raw/edk2-arm-vars.fd \
sh ember/tools/efi-display/check-qemu.sh \
    "$EMBER_SOURCE" "$NEW_TEST_OUTPUT" "$CROSS_TOOLS" "$EMBER_SYSROOT"
```

The host needs mtools, QEMU and matching raw AArch64 UEFI code/blank variables
images. Do not supply a production firmware image containing device NVRAM.
Tests create disposable FAT and variables files with no physical disk access.
They cover GOP present, GOP absent and a journal creation failure. Each case
requires byte-identical restoration of the original loader and successful
chainloading of a child which writes a marker and shuts down QEMU.

The intermediate ELF uses explicit program headers; its single RWX load
segment is not the final PE section-permission map. Link errors remain fatal.
The builder rejects unresolved symbols and records source hashes, compiler,
source revision and the output PE hash. Build success alone is not a board pass.

## Controlled board use

First identify the board's mounted EFI partition. Save its existing
`EFI/BOOT/BOOTAA64.EFI` off that partition and verify its SHA256. Place a second,
byte-identical copy at `EFI/BOOT/ember-hdmi-original.efi`. Only then replace
`BOOTAA64.EFI` with the built probe, verify the deployed hash, sync and perform
the intended warm or cold boot. Keep firmware settings fixed between controls.

At entry, the probe restores `BOOTAA64.EFI` from the saved original, flushes
and compares it byte for byte. It then records the firmware/GOP state in
`ember-hdmi-probe.log` and chainloads the original loader. If restoration or
journal setup fails, display probing is skipped and chainloading is attempted.
Journal write/flush failures are retained internally; a partial log is not a
complete result. Restoration is not atomic across power loss. Keep the offline
backup and recovery access until the restored loader hash has been checked.

## Physical result: Raspberry Pi 5, 2026-10-11

One Raspberry Pi 5 Model B was checked with the monitor on and HDMI0 connected.
The exact board revision and installed UEFI source revision were not recorded.
UEFI identifies as `rpi5-20260730.1`; firmware revision is `0x690b8b4e`.
The same ordinary EmberBSD `EMBER64 #0` kernel was used throughout. Its V3D
driver remains a passive observer, so this is **HDMI console evidence only**.

| Boot condition | Firmware geometry | GOP before probe | EmberBSD result |
| --- | --- | --- | --- |
| Warm reboot, framebuffer_depth=32 | 0 x 0 | EFI_NOT_FOUND | No framebuffer or signal |
| Warm reboot, only depth changed to 24 | 0 x 0 | EFI_NOT_FOUND | No framebuffer or signal |
| Original depth=32 restored; shutdown and ten-second power removal | 1920 x 1080 | Already present | genfb/wsdisplay console; user confirmed visible console |

Cold-boot GOP reported base `0x3f400000`, size `0x7e9000`, BGR format and
1920 pixels per scanline. EmberBSD attached a 1920 x 1080, 32-bpp framebuffer
with stride 7680. DisplayDxe connection returned EFI_NOT_FOUND in every case;
on the cold boot GOP was already valid before that call. Thus forced driver
connection and depth=24 did not fix the failing warm boots.

| Checked file | SHA256 |
| --- | --- |
| Original/restored OS loader | `cb9a28ae753ee0374ad7cbdb3fddeb38be47c8df4252efe23be825b51e584c74` |
| Kernel | `05e4682968c341d85be5bf5727333b74d2b48b251d107bfc7c7e45bcc9788b7d` |
| Original/restored config.txt | `c49f77f6125b1c97b37aade9915b46e8c05a42ef51ae9de8f9432a3530279a30` |
| Installed RPI_EFI.fd, including local variables | `409767d4487c322a79b81d826b4976483a64cf68cb5dd77224ae4861413e0459` |
| Deployed diagnostic EFI | `8796359af9065ac395c6527b299b1dd488558cf9bfd51fa302aa47b1df19f1da` |
| Diagnostic probe.c used on hardware | `7ecdb5640bef04582152cef61f220488c500536a77ffe66dac6f793be95e93b5` |

The subsequently published journal-error handling is covered by isolated
checks; that revised binary has not been rerun on this RPi5. The firmware image
is not distributed because it contains local variables. Warm reboot after the
successful cold boot, hotplug, CM5 HDMI, accelerated scanout and Plasma remain
unverified. The observation narrows the failure to pre-OS display initialization;
it does not identify its firmware, HPD or retained-state cause.

## CM5 comparison after cold boot, 2026-10-11

The CM5 Rev 1.0 / BCM2712 D0 (4 GiB, Waveshare CM5-NANO-B carrier) was shut
down normally and disconnected from power for ten seconds with the monitor
on. The user reported no signal. The same monitor and cable had just shown
the RPi5 console; the CM5 used a different adapter. Its earlier operation
with this exact board/monitor is unknown. Carrier PCB and UEFI source revisions
remain unknown.

The revised probe (`64cc0863c54885399f7a35f1f2f2cd812ec55e624e0f39f94bfaa1d4053d840f`)
again found firmware geometry 0 x 0 and no GOP. It restored and chainloaded
the original loader successfully. The installed EMBERV3DRENDER #11 kernel,
matching modules and depth=32 configuration were unchanged; no framebuffer
attached. This does not invalidate the separate offscreen V3D acceptance.

Read-only captures of the original firmware DTB in its configured RAM region
located `/chosen/log` and `/chosen/bootloader`. The message ring format is
documented by Raspberry Pi's [vclog source](https://github.com/raspberrypi/utils/blob/master/vclog/vclog.c).
Decode framed messages rather than treating binary record headers as text.
The retained logs distinguish the two cold boots:

| Board | Firmware bootloader revision | HDMI0 discovery |
| --- | --- | --- |
| CM5 | `5855b10b2bd22b2a116dc91f6b5b1bc4b2ea849f` (2025-06-13) | hotplug 0; no EDID read or chosen mode |
| RPi5 | `57db150d63864d47e6c9071f6b086a5401eb4e92` (2025-11-05) | hotplug 1; EDID blocks 0/1; 1920x1080 at 60 Hz |

Both report HDMI1 hotplug 0. This localizes the missing CM5 display before
UEFI/EmberBSD; it does not identify an adapter, carrier or firmware defect.
The firmware versions also differ, so this is not a controlled adapter-only
comparison. The cached CM5 bootloader settings contain BOOT_UART and BOOT_ORDER,
with no HDMI-disable override. No EEPROM update was attempted.

The full DTB/log captures remain private because they can contain hardware
identifiers and seeds. Capture SHA256: CM5 EFI log
`f2f8888df3034882f0d2773032d11051debaa30dfefe8386ddfa37e4c317aec8`;
CM5 firmware log `d7b35dae27269814ecd6bdf5fb8abb1cd1dcd38d55751994be9d6cb9d8f6569f`;
RPi5 firmware log `fa6449b7784da7f83648ad59403fd082ab8ed8e2289319bdc40c17a7935010fe`.

## Analysis scope

GCC 16.2 `-fanalyzer` and Cppcheck 2.22 warning/performance/portability checks
cover both C files with target GNU-EFI headers. Cppcheck reads GCC-preprocessed
C with `--platform=unix64`; parsing raw compiler builtins was unsupported.
The checks are supplemental, not proof of the UEFI ABI or all error paths.
There is no Valgrind execution path for this freestanding EFI program.
