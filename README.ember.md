# EmberBSD: hardware adaptations and build instructions

EmberBSD is an independent NetBSD 11 fork for single-board computers and
embedded systems. Its primary target is Raspberry Pi 5. The repository
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
The [CIX P1 persistent-root profile](ember/image/cix-p1.md) provides a reduced
GCC16 userland, checked FFS startup and background RTL8822CE WPA/DHCP startup.
Five unattended SD-root boots pass persistence checks on one board.
Both legacy bands pass post-reboot WPA2, DHCP and checksummed TCP transfers.

For target selection and installation, follow [Quick Start](ember/quick-start.md).
Use the [User Guide](ember/user-guide.md) for first login, network access, applications, updates and recovery.
The [amd64 VM walkthrough](ember/boot/amd64-vm-install.md) describes a full local-set development ISO.

## Hardware support and validation

The [board catalog](ember/boards/README.md) is the maintained hardware index.
Select a board there for its capability table, boot path, detailed notes and
validation limits. The [short board selector](README.md#supported-boards) stays
in the root README; detailed support is maintained on each board page.
See [adding a board](ember/boards/adding-a-board.md) for external contributions.
All shared changes follow [CONTRIBUTING](CONTRIBUTING.md).
The [kernel guide](ember/development/kernel.md) and
[board-change worksheet](ember/development/board-change.md) define the relevant evidence.

The reduced CIX profile also selects static native OpenSSH and local password
tools. SSH startup requires an administrator-provisioned configuration, account
and host keys. The image creates no credentials and leaves SSH disabled by
default. [SSH provisioning and source-contract limits](ember/image/cix-p1.md#optional-ssh-access)
remain separate from physical login acceptance.

The following accessory and protocol limits also apply:

| Platform or feature | Status |
| --- | --- |
| Raspberry Pi 5: built-in Bluetooth over UART | H4 receive fix, SSP/Secure Connections, authentication, encryption, and reconnection with an iPhone tested on hardware |
| WM8960 Audio HAT over RP1 | Microphones and speaker output tested; output loudness depends on the attached speakers |
| USB audio | One USB Audio Module A showed repeated device disconnections during playback; stable operation has not been confirmed |
| AirPods | Connection confirmed; reliable audio quality and headset microphone operation have not been confirmed |
| ELM327 | Testing with a physical adapter is still pending |
| BLE | Not implemented in the added Bluetooth management tools |
| CM5: WPA3-Personal | SAE group 19/H2E, required PMF, file transfer, reconnect, wrong-password rejection and WPA2 regression pass on CYW43455 7.45.265; [configuration and limits](ember/boot/bwfm-sae.md) |
| Zero 3W: WPA3-Personal | AIC8800D80 SAE group 19/H2E, required PMF, protected SA Query and automatic recovery after AP removal are physically checked; nonzero initial IGTK IPN remains unsupported ([configuration and limits](ember/boot/aicwf-sae.md)); VHT80 1SS has a physical WPA2 data-transfer check; SAE/PMF with nonzero IGTK IPN and complete k/v remain [driver work](ember/boot/aicwf-vht.md) |
| CAN FD | Raw sockets, virtual canlo interfaces and canconfig mode control pass native rump checks; physical drivers and data-phase timing are not implemented ([guide](ember/can/README.md)) |
| MOXA UPort 1150 | Experimental `umoxa` USB/ucom parent; 12 wire + 22 parent groups and strict-parameter regression pass on host. Automatic cold firmware upload/warm attachment and 4096-byte RS-232 loopback passed at 9600/115200 8N1 in an isolated macOS/QEMU trial requiring a process-local passthrough workaround. Bare-metal and broader serial acceptance remain pending ([guide](ember/boot/moxa-serial.md)) |
| MOXA UPort 1450-G2 | Experimental `umoxag` parent for `110a:1452`; 13 protocol + 31 actual-parent host groups and eight native actual-parent/workqueue/ucom/TTY groups pass with USB doubles. External firmware, real USB-controller and electrical acceptance pending ([guide](ember/boot/moxa-g2.md)) |

The `bwfm` driver leaves roaming and WPA authentication to the host.
It disables firmware WNM transitions as well as autonomous roaming:
CYW43455 firmware 7.45.265 can otherwise select SAE after a band-steering
request in a mixed WPA2/WPA3 network while the host still holds WPA2 keys.
This WNM mitigation remains in place. The separate
[external-SAE implementation](ember/boot/bwfm-sae.md) adds WPA3-Personal
with required PMF. The [host-managed station transport](ember/boot/bwfm-kv.md)
adds Neighbor Report exchange and BSS Transition Management. Physical Pi 5
checks passed acceptance, rejection and traffic under WPA2 and SAE/required
PMF on one Keenetic mesh. The guide records measured gaps and remaining limits.
802.11r fast transition remains unsupported.
The [scan metadata correction](ember/boot/bwfm-scan.md) preserves firmware
primary channels and unsigned signal-quality ranking. CM5 also provides an
optional [minimum active fan level](ember/boot/thermal-fan.md).
On one CM5 fan, automatic cooling started at 50.1 °C using 125/255 PWM.
All 272 active tachometer windows in a five-minute check showed rotation.
The operator reports quieter, steady rotation. The source regression passes
136 assertions on the host and CM5. Higher thermal transitions, measured
noise and long-duration operation have separate validation limits.
The [disconnect recovery](ember/boot/bwfm-link-events.md) handles firmware
events and reconciles a silently lost association with host state. On CM5,
a reproduced stuck connection recovered automatically, followed by an
8 MiB Wi-Fi round trip; this is a functional check, not long-run acceptance.
The on-demand `hw.bwfm0.report` diagnostic includes radio
authentication, PMF and station counters to distinguish this failure
from an SDIO transmit-window stall.
The separate [DHCP reconnect correction](ember/boot/dhcpcd-dad.md) prevents
address validation from accepting an unconfirmed lease. It keeps IPv4LL
fallback available and removes it after a usable DHCP bind.

The Zero 3W [PWM cooling path](ember/boot/a733-fan.md) uses the board's
fan connector and all five temperature sensors. Source contracts cover
stepped cooling, hysteresis, hardware errors and shared PWM clocks.
Physical speed and noise acceptance remains pending.

On A733 Zero 3W, GCC16.2 kernel `be8f96cace8f` passes all 21 GPU diagnostic
stages, three [Vulkan compute lifecycles](ember/boot/a733-pvr-execution.md#physical-compute-boundary),
and exact [offscreen graphics and KWin/Zink frames](ember/boot/a733-pvr-graphics.md#physical-acceptance-boundary).
KWin 6.8 and Qt Quick use the physical PowerVR GPU on a virtual output.
Native HDMI/KMS, a complete Plasma session, firmware GROW, endurance and
Vulkan conformance remain unaccepted. On the same kernel, NPU powered
identification, Engine1 and one [40-byte FE/MMU/IRQ stream](ember/boot/a733-npu-engine.md#physical-acceptance)
pass. NN instructions, model output, output coherency and guard-fault
behavior remain unaccepted. The [board guide](ember/boards/orange-pi-zero-3w.md#accelerator-acceptance-2026-10-20)
separates these results from display mode admission and older cohorts.

## Building

Cross-compilation on the development host is the preferred path. The wrapper
uses the fork's `build.sh` by default on macOS and NetBSD alike. Set
`EMBER_BUILD_MODE=native` explicitly for a required NetBSD/AArch64 fallback.
Use a full source export of a pinned commit and an existing host Python 3
for the legacy source contracts. External GCC16 reproducible builds use explicit
source/object roots and standard path maps; see the [cross-build guide](ember/boot/cross-build.md):

```sh
sh ember/build-kernel.sh /absolute/output EMBER64
```

The build produces an ELF kernel, a native kernel image, device trees for
Zero 2 W, Orange Pi Zero 4 and Zero 3W, and Ethernet, Bluetooth UART, and WM8960
modules, plus the optional Pi 5 power-button fallback module. Foreign-host
builds run portable contracts and report native contracts as pending; run
the full suite on EmberBSD before deployment. The
[headless base update](ember/boot/aarch64-base-update.md) builds the fork's
kernel, required board modules and base userland together. Its Pi 5 hardware
receipt is separate from validation of a complete general release image.

The [AArch64 development image](ember/image/README.md) combines verified
NetBSD 11 base sets, an accepted EmberBSD kernel, the corrected static
`fsck_ffs`, CTF/libdwarf 2.2, and the Ports GDB 18.1nb1/LLVM 23.1.2nb1
package closure. All eighteen packages install offline. Installed GDB, DWP
and CTF matrices pass on first boot and after a normal reboot in QEMU/HVF.
This is a bounded development image, not a complete OS rebuilt with GCC16
or a physical-board installation image.

For an AArch64 desktop VM, see the [UTM framebuffer configuration](ember/boot/utm-framebuffer.md).
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

The RV32 kernels are cross-built with the stock `build.sh`:

```sh
./build.sh -U -u -m riscv -a riscv32 -O /absolute/obj -T /absolute/tools tools kernel=ESP32S31
```

`ESP32S31` runs in QEMU `virt` with 16 MB and on the board with Ethernet.
`ESP32S31W` adds the radio and needs the vendor libraries prepared as one
object file; without it the configuration does not link. Board boot loaders
and flash layout are prepared outside this tree.

Orange Pi Zero 4 and Zero 3W start through the board vendor's boot0 and
U-Boot. They are closed binaries and are not included in the repository: an
image builder copies them from the vendor's own card image; the Zero 4
binaries start the Zero 3W as well. U-Boot then runs
`ember/boot/orangepi-zero4-boot.cmd`, which loads the native kernel and
the device tree from the first partition. The boot loader does not name
the board, so the script reads the pins of the Ethernet MAC: with no PHY
they float and follow the pull resistors both ways, and only then the
Zero 3W tree is chosen.

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

The [installer branding](ember/boot/installer-branding.md) keeps the x86
BIOS, PXE, UEFI and sysinst screens consistent with EmberBSD.
The colored ASCII mascot follows the project's flame, capsule eyes and smile.
BIOS and UEFI use red, amber, yellow and white text attributes.
Serial console banners use the same mascot in plain ASCII.
Kernel filenames and partition types
retain their NetBSD compatibility names. OS sets select the EmberBSD channel;
that service is not yet published, so use the sets on the installation medium.
Source, ASCII and ISO contracts are separate from boot and installation acceptance.
The product version defaults to `1.0`. The NetBSD base version remains separate.
The amd64 [text installer](ember/boot/text-installer.md) provides six guided steps,
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
kernels, modules, firmware, and NetBSD userland. A fully validated general
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
