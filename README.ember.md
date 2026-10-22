# EmberBSD: source and build entry points

EmberBSD is an independent NetBSD 11 fork for single-board computers and
embedded systems. The repository
preserves the NetBSD source tree and history, adding device drivers,
kernel fixes, and build tools for specific boards. Changes in this fork
should not be treated as changes accepted by the NetBSD developers.

The kernel and common sources are based on the verified NetBSD 11.0
`syssrc.tgz` release archive. The rest of the tree retains the upstream
`netbsd-11` snapshot recorded in `ember/source.json`. The adaptation patches
are already applied to the source tree. Copies in `ember/patches` document
their provenance and support future porting; do not apply them again.

Build and deploy from pinned EmberBSD sources. The retained NetBSD ABI names,
triplets and upstream identifiers do not select an upstream NetBSD build.
Record the origin of base libraries and headers in cross sysroots; see the
[Ports provenance requirements](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-build-tools/cross/sysroot.md).
Component acceptance does not establish a complete current base release.
The [CIX P1 root builder](ember/image/cix-p1.md) describes the reduced userland
and startup contract. Physical applicability belongs to
[DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/cix-p1-orangepi6plus/README.md).

For target selection and installation, follow [Quick Start](doc/guide/quick-start.md).
Use the [User Guide](doc/guide/user-guide.md) for first login, network access, applications, updates and recovery.
The [amd64 VM walkthrough](doc/guide/amd64-vm-install.md) describes a full local-set development ISO.

## Hardware support and validation

[DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards) owns the hardware catalog,
loader requirements, media layout and installation applicability.
Use [adding a board](ember/development/adding-a-board.md) for OS integration and executable checks.
Document software mechanisms beside their subsystem. See [ownership](doc/emberbsd-ownership.md).

## Building

Cross-build on the development host with the common GCC16 toolchain.
Use a full source export of a pinned commit and an existing host Python 3.
Select the external compiler explicitly; omitting it selects the older in-tree compiler.
See the [cross-build contract](tools/docs/cross-build.md) and
[SDK release workflow](https://github.com/oxtech-ember/EmberBSD-SDK/blob/main/docs/os-release.md).

```sh
EMBER_BUILD_MODE=cross EMBER_EXTERNAL_TOOLCHAIN=/absolute/gcc16 \
    sh ember/build-kernel.sh /absolute/output EMBER64
```

The build produces an ELF kernel, a native kernel image, device trees for
Zero 2 W, Orange Pi Zero 4 and Zero 3W, and Ethernet, Bluetooth UART, and WM8960
modules, plus the optional Pi 5 power-button fallback module. Foreign-host
builds run portable contracts and report native contracts as pending; run
the full suite on EmberBSD before deployment. The
[headless base update](ember/tools/docs/aarch64-base-update.md) builds the fork's
kernel, required board modules and base userland together. Its Pi 5 hardware
receipt is separate from validation of a complete general release image.

The [legacy AArch64 image assembler](ember/image/README.md) documents its
bounded development profile and upstream base-set dependency. It does not
produce a complete current EmberBSD release. Use SDK for release orchestration.

For an AArch64 desktop VM, see the [UTM framebuffer configuration](sys/dev/fdt/docs/utm-framebuffer.md).
It documents the PCI ownership fix, the `viogpu` boot override and the
visible-display regression check.

Cross-build Raspberry Pi 5 UEFI with native host tools on macOS or EmberBSD.
The target compiler must be AArch64 GCC major 16. The host also needs GNU Make,
iASL, DTC, Python 3 and pyelftools. Use `CROSS_COMPILE` to select an explicit
compiler prefix. `EMBER_AARCH64_ELF_TOOLCHAIN` can add a local toolchain on macOS.
The [firmware profile](ember/firmware/README.md) isolates compiler headers and
library searches from the application sysroot. It preserves upstream module
flags, platform defines, and LTO. Source, cache, and toolchain paths must not
contain whitespace.

```sh
bash ember/build-firmware.sh /absolute/cache --rp1-console
```

The eotics firmware source and submodules are pinned. The adaptation patch
order is defined in `ember/firmware/series`. Standard, console, and diagnostic
variants use separate output directories. Radio firmware assets are fetched
according to `ember/boot/image-assets.tsv`, with hash verification and
license preservation; their binaries are not included in the repository.
The firmware receipt records the compiler path, version, machine, identity and
SHA256. The builder checks the compiler hash again before publication.
Real GCC16 smoke controls check the bounded freestanding header, ELF, LTO,
and search contract. Neither these controls nor an accepted NetBSD target
identity prove a complete TF-A/EDK2 build or physical firmware acceptance. `--prepare` needs only Git and preserves accepted outputs.

The `ESP32S31` and `ESP32S31W` configurations belong to `sys/arch/riscv/conf`.
The radio configuration requires the matching vendor object.
Use the [DevBoards profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/esp32-s31/README.md)
for bootloader, flash layout and current hardware limits.

A733 firmware and DT selection requirements belong to the
[DevBoards boot guide](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/a733-zero4/boot.md).
The OS retains the consumed `ember/boot/orangepi-zero4-boot.cmd` script.

Starting a UEFI rebuild removes the previous output for the selected
variant and its receipt before checking tools or sources. An early prerequisite
failure cannot leave the old image available. A failed build, copy, rename or
receipt write removes the selected image and receipt. The build log remains.
The final image appears only after the receipt is ready.
Invalid arguments do not select or invalidate a variant.
The host-side controls run the actual wrapper with
fake Git, compiler and build services: `ruby ember/tools/firmware-contract.rb`.
They do not contact the network, build firmware or access a device.

## Installation identity

The [installer branding](usr.sbin/sysinst/docs/installer-branding.md) keeps the x86
BIOS, PXE, UEFI and sysinst screens consistent with EmberBSD.
The colored ASCII mascot follows the project's flame, capsule eyes and smile.
BIOS and UEFI use red, amber, yellow and white text attributes.
Serial console banners use the same mascot in plain ASCII.
Kernel filenames and partition types
retain their NetBSD compatibility names. OS sets select the EmberBSD channel;
that service is not yet published, so use the sets on the installation medium.
Source, ASCII and ISO contracts are separate from boot and installation acceptance.
The product version comes from `EMBER_VERSION` in `share/mk/bsd.own.mk`.
The NetBSD base version remains separate.
The amd64 [text installer](usr.sbin/sysinst/docs/text-installer.md) provides six guided steps,
automatic GPT, binary-set profiles, source checks and a final write confirmation.
Its controller, disk, source and renderer contracts pass with ASan/UBSan.
Booted-image acceptance for this wizard is separate from the earlier installer below.
An amd64 CD image passed UEFI and BIOS boot and a UEFI installation to a disposable VM disk.
The installed system then booted without the CD image. Physical installation is a separate check.

## Deploying to a device

This repository provides OS sources, hardware adaptations, and build tools.
It can serve as a base for devices running different applications.
Applications are installed separately and are not required to build the
kernel or UEFI firmware.

For reproducible builds, pin the fork commit and verify the hashes of the
resulting components. An installation image must contain compatible
kernels, modules, firmware, and EmberBSD userland. A fully validated general
installation image of this fork has not yet been published.

Wi-Fi configuration, SSH keys, tokens, and other credentials are added by
the operator when preparing each device and remain outside Git. Bluetooth
bonds are tied to the controller: when moving to another board, use its
address and its own bonds. Personalized images containing these settings
are not intended for public distribution.

## Upstream and licensing

Upstream is [NetBSD/src](https://github.com/NetBSD/src), the official CVS
mirror. This fork's `main` branch maintains its own history; automatic
mirror updates do not alter already published fork revisions. Upstream
updates are incorporated through separate commits followed by rebuilds
and hardware validation.

NetBSD and third-party licenses remain in the source files. Additional
utilities under `ember/` use the BSD-2-Clause license, except where a file
carries its own license.
