# Quick Start

Start here to choose a device, install EmberBSD and check its first boot.
Use the [User Guide](user-guide.md) for configuration, applications, updates and recovery.
No AI assistant or private project documentation is required.

## 1. Choose your target

Read the exact model's [board page](boards/README.md), including its boot path and limits.
A working Ethernet driver does not establish Wi-Fi, display or storage support.
Choose the functions you need before choosing an installation route.

| Target | Start with | Installation boundary |
| --- | --- | --- |
| amd64 installation VM | [UEFI VM installation](boot/amd64-vm-install.md) | Published no-X11 candidate; local and HTTPS installation on disposable QEMU disks |
| AArch64 desktop VM | [Development image](image/README.md), then [UTM display setup](boot/utm-framebuffer.md) | Requires the listed verified inputs; VM image, not a board image |
| Raspberry Pi 5 or CM5 | [Board catalog](boards/README.md) | Board firmware, boot medium and compatible base must be prepared; kernel support alone is not an installer |
| Zero 2 W, Orange Pi Zero 4 or Zero 3W | [Board catalog](boards/README.md) | Follow that board's device-tree and boot-loader path; images are not interchangeable |
| ESP32-S31 | [Board guide](boards/esp32-s31.md) | RV32 bring-up with its own loader and flash layout; no general sysinst route |
| A model with no accepted profile, including Orange Pi 6 Plus | [Board-change worksheet](development/board-change.md) | Identify the board and test the current boot path first; a port may be unnecessary |

## 2. Obtain a compatible image

Download the [amd64 UEFI installation candidate](https://github.com/oxtech-ember/EmberBSD/releases/tag/build-a0674c0d2c1b067339b9436cf6148426546649ef-amd64-no-x11).
For a first VM, choose `EmberBSD-1.0-amd64.iso` and download `SHA256` and `manifest.json` beside it.
The release also contains a compressed UEFI USB image and matching binary sets.
Read its release notes: this candidate has no X11 and does not qualify physical machines or boards.
Its source is `a0674c0d2c1b067339b9436cf6148426546649ef`; all OS components use that same pin.
The [VM guide](boot/amd64-vm-install.md) covers installation with local sets or GitHub HTTPS.
Other architectures still require their specific source-build or documented development-image route.

Keep the source commit, architecture, image type, compiler profile and checksum manifest with the image.
Kernel, modules and base sets must belong to a compatible, recorded build.
Do not substitute upstream NetBSD downloads for missing EmberBSD components.
The documented AArch64 development image explicitly records its upstream base-set boundary.

On a build host, inspect a clean source checkout before selecting a revision:

```sh
git clone https://github.com/oxtech-ember/EmberBSD.git
cd EmberBSD
git fetch origin
git status --short
git rev-parse origin/main
```

Record the full commit, then use it in the selected build recipe.
Adaptation patches in this tree are already applied.

## 3. Verify before installation

Compare the downloaded file with the publisher's manifest before opening or decompressing it.
For a locally built file, save its hash and the successful build log.
On macOS, the host command is:

```sh
shasum -a 256 /absolute/image-file
```

On EmberBSD, use `sha256 /absolute/image-file`.
Check the exact filename and digest, not just the manifest's presence.
A checksum confirms bytes; it does not establish board support or a trusted publisher.
After decompression, verify an uncompressed digest too when the manifest supplies one.

| File type | Correct use |
| --- | --- |
| Full installation ISO | Attach as virtual CD/DVD; keep its compatible sets attached through installation |
| qcow2 | Attach as a virtual hard disk; keep a pristine copy or a disposable overlay |
| Raw whole-disk image | Write only using the exact board's media procedure after identifying the destination |
| Compressed image | Verify, decompress, then use the resulting image type |
| Kernel, DTB, module or set archive | Input to a documented boot/update procedure; not a whole-disk image |

## 4. Install and start

For a first experiment, follow the [amd64 VM walkthrough](boot/amd64-vm-install.md#install-from-local-sets).
It uses a new virtual disk and the ISO's local sets.
For network installation of this candidate, follow [GitHub HTTPS installation](boot/amd64-vm-install.md#install-from-github-https).
Keep the exact release path and the custom selection of 13 included sets.

For a physical board, identify its model, revision, firmware and boot medium first.
Back up the medium and retain a working console and recovery path.
Follow the board's specific installation recipe; do not write an ISO or qcow2 as a raw board image.
If that recipe or its compatible artifacts are missing, stop before writing the medium.
Use the [shared change procedure](../CONTRIBUTING.md) to prepare the missing support.

After installation, boot from the installed medium without the installation ISO.
Log in on the console with the account configured by the installer or image recipe.
Some development images start with a passwordless root console; configure access before adding a network.
Record these guest results:

```sh
uname -srm
sysctl kern.version
sysctl kern.buildinfo
mount
df -h
```

`NetBSD` can appear in ABI names and diagnostic output.
Compare build information and installed artifacts with the image's record to establish EmberBSD provenance.
An older image may not provide every build-information field; retain its manifest and source ID.

## 5. Configure, test and update

Continue with [first-login configuration](user-guide.md#first-login-and-local-configuration).
Set a hostname, configure accounts, then configure the observed network interface.
Enable SSH only after checking account access and server configuration.
Run one useful operation, then perform a normal reboot and repeat it.
Record failures and missing functions before relying on the device.

Install optional applications through [Ports and Examples](user-guide.md#install-applications).
Use the [update procedure](user-guide.md#update-emberbsd) and keep a tested recovery medium.
Firmware, OS base and application packages have separate update procedures.
For agent-assisted shared development, use [EmberBSD Development](../CONTRIBUTING.md#development-through-agents)
and the same contribution checks as a human author.
