# Raspberry Pi 5 (C1)

[Board catalog](README.md) · [Build instructions](../../README.ember.md#building)

The capability table preserves earlier published hardware results. The
headless-base update below records a separate physical run.
Board revision: C1 in the earlier support record.

## Capabilities

| Function | Result and limits |
| --- | --- |
| SoC | BCM2712 |
| Boot path | UEFI and ACPI, firmware built from this tree |
| Boot storage | microSD: Tested |
| Serial console | Tested |
| All CPU cores | Tested |
| Ethernet | Tested |
| Wi-Fi | Tested |
| Bluetooth | Classic: Tested; BLE: No |
| Temperature sensor | Tested |
| Fan control | Tested |
| Watchdog | Tested |
| Power button | Tested: a press powers the board off, the next one powers it on |
| I2C | Tested (WM8960 codec) |
| Audio | WM8960 HAT: Tested |
| Pin multiplexing and GPIO | Not validated |
| Real-time clock | Not validated |
| Processor frequency control | Tested |
| Voltage regulators | Not validated |
| USB | Limited USB 2.0 control reads and STALL recovery validated; other transfer types and power management not validated |
| Hardware random numbers | Not validated |

Long-run stability has not been established. Application results from an
AArch64 VM do not validate this board's camera, audio or GPU/NPU paths.

## Evidence

### USB control recovery, 2026-10-13

A Pi 5 Model B with 8 GiB RAM and DMI version `D04170` used UEFI/ACPI.
The test preserved its existing firmware; DMI alone does not establish a silicon revision.
Kernel `EMBER64 #5` was cross-built with GCC 16.2.0 from `f9e7019a729`.
Its SHA256 was `16f469cc64c7d9abde44f170413f7b69dd3875f21c94af0cf112b5826b0edf42`.
The matching board modules were installed and their hashes checked after reboot.

An Apple USB DFU device, VID `05ac`, PID `1227`, connected at high speed to a root port.
The [control probe](../boot/usb-control-recovery.md#physical-control-probe) passed
1024 descriptor reads in about 512 ms and 1024 STALL requests in about 256 ms.
Both series ended with a valid standard device descriptor.
The previous kernel had the same approximate latency; the synchronization fix did not improve throughput.
These short tests do not validate sustained load, bulk, isochronous transfers, power management or phone boot.

### Headless base update, 2026-10-07

The lab Pi 5 reported DMI product `Raspberry Pi 5 Model B`, version
`D04170`, four Cortex-A76 cores and 8 GiB RAM. The update used microSD,
UEFI/ACPI and the root-partition ELF `/netbsd`. The DMI value alone does
not establish a silicon revision. Existing UEFI firmware was preserved;
its SHA256 is recorded below, but its exact source revision was not
re-established during this update.

Native GCC 12.5 built `EMBER64 #0`, four required modules and a reduced
base from `0108f0692febed373069b5b14bb5912015b2fd88`. The
[headless update procedure](../boot/aarch64-base-update.md) retains the
C/C++ runtime and installs no desktop, camera stack or OpenCV. Existing
applications and developer tools were preserved, not removed by this
in-place update. A general installation image was not produced.

The first reboot at 12:47:28 UTC ran the new kernel with its matching
`if_cemac_acpi`, `bcm2712btcom`, `rp1wmcodec` and `rpi5button` modules.
Wi-Fi, DHCP, SSH and existing startup services returned at this stage.
All 16 memfd cases passed; the previous kernel failed the two partial-page
cases with the same test executable. All three FP-state modes passed.
The previous kernel also passed those FP modes on this CPU.

The complete candidate libc passed a bounded smoke check, 850 CAS checks,
two binary128 matrix/mode cases and 27 existing libc/pthread cases.
The binary128 trap case skipped because hardware IOE was not writable.
These checks selected and verified the candidate DSO explicitly; they
were performed before system-wide installation.

The reduced base passed strict release file-list checks and a chroot
shell/loader/OpenSSL/SSH check. Installation completed at 13:53:14 UTC.
Seven core files, including libc, init, the loader and sshd, matched their
accepted build outputs. Local configuration hashes were unchanged and
`sshd -t` passed. Required radio firmware was restored from the verified
pre-update copy. `postinstall` updated MAKEDEV and mtree; its obsolete
warning for the required `bcm43xx` firmware directory was not applied.

The final reboot started at 13:54:00 UTC. The old Wi-Fi address did not
respond afterwards or after a power cycle. The operator subsequently
reported finding the board over Ethernet and handed further work to
another developer. That report is not a recorded post-reboot SSH or libc
test. Wi-Fi recovery, installed-library acceptance after boot and sustained
operation remain unverified. Camera, OpenCV, audio playback and graphics
were outside this update's acceptance scope.

| Output | SHA256 |
| --- | --- |
| ELF kernel | `15551ed96138b07aa8db706e7d0b68434c591e4a1dea80f9e835b1b625e32f84` |
| Native kernel image | `939cb36f0a00388a6f58a2751daabac71a5d414969e2fe126deb9909d35103a7` |
| bcm2712btcom.kmod | `34e36e8ed6be79967eba5866e32b33e7dfad693d2c87cad33027ba1910993415` |
| if_cemac_acpi.kmod | `bad577df5ee64f2adf7f9da39c31fec4f62f836de4676c07ff4155b76ac16b7c` |
| rp1wmcodec.kmod | `3e3131ac25bd9b923f926f45a6690d6701980fea323408dbaf3f33ca5c7c325a` |
| rpi5button.kmod | `d008fc7bcfd162a4c4b2117b29b4a8d4ce9c52012755e3e17b4f40e489c89194` |
| base.tar.xz | `f0beccf515b1109be2ed7c455ca57f1624e6248db8b0995988b3c4400833ea65` |
| etc.tar.xz (merge input, not installed wholesale) | `1944c07e6a206d46451753b4b9b6d8f3b1ff1541ec6c180a39e3698a3cc8bf08` |
| Installed libc.so.12.224, before final reboot | `6e918818bb54d4c7f256254a5ec858ddb3144efb8019df0476b86fb921b0c912` |
| Preserved RPI_EFI.fd | `296a0913ca5acb9cb825380f0d14dce0f2fe2c30861fde6ace32390fdffbbc6c` |

### Earlier support matrix

Results were migrated from the [published support matrix](https://github.com/oxtech-ember/EmberBSD/blob/fe2727f6ef675868eba642efa73c5a0e33f92191/README.md#supported-boards)
and [hardware notes](https://github.com/oxtech-ember/EmberBSD/blob/fe2727f6ef675868eba642efa73c5a0e33f92191/README.ember.md#hardware-support-and-validation)
on 2026-10-07. These sources do not supply a complete per-check receipt
with tested OS/firmware revisions and dates. The source commit identifies
the documentation snapshot, not the kernel used in every original test.
Future validation should use the [evidence format](adding-a-board.md#record-the-result).
