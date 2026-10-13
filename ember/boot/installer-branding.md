# EmberBSD installer identity

The x86 BIOS, PXE and UEFI loaders identify the product as EmberBSD.
The installation menus and sysinst catalogs use the same identity.
`EMBER_VERSION` is the product version. It defaults to `1.0`.
The project owner assigned this version to the current kernel state.
The number does not extend the hardware or toolchain validation described below.
The NetBSD compatibility version still comes from `sys/conf/osrelease.sh`.
It controls ABI and package paths, not the product version shown on installation screens.
`EMBER_BUILD_ID` identifies the source commit used for an installation build.
Set it to the full commit ID when building a release.
An unset ID is shown as `development`, not as a published release.

BIOS, PXE and UEFI keep a text console and a keyboard menu.
The shared libsa renderer shows the EmberBSD mascot and a large EMBERBSD wordmark.
The character grid follows [the project's flame logo](../branding/emberbsd-logo.png).
The capsule eyes and smile remain readable at console resolution.
The mark uses thirteen art rows and one blank row, with a maximum width of 76 columns.
The stock installation menus fit an 80-column, 25-line screen, including the countdown.
BIOS uses VGA cell attributes. UEFI uses firmware text attributes.
The flame uses red, amber and yellow; the eyes, smile and wordmark use white.
The renderer restores the previous console attribute before the menu.
Serial consoles and other platforms retain the same printable ASCII without color.
UEFI color does not require GOP.
Boot needs no ANSI escape support, image decoder, graphical mode or external asset.
The optional source generator uses ImageMagick; normal OS builds do not need it.
Custom boot.cfg banners appear below the mark. Keep their total menu height in mind.

## Distribution sets

The default HTTPS host is `cdn.emberbsd.oxteam.me`.
Its intended layout is:

```text
https://cdn.emberbsd.oxteam.me/releases/<EMBER_BUILD_ID>/<architecture>/binary/sets/
```

This is the EmberBSD channel contract. The service is not yet available.
Use an image containing the sets until the channel is published and checked.
The main source menu offers `Bootable USB / image` and `EmberBSD CDN`.
Use a complete USB image for installation without a network connection.
ISO files remain available for virtual machines. Screens use the term installation image.
The CDN submenu offers HTTPS where the installation environment supports it.
HTTP remains an explicit transport choice for environments without TLS support.
It provides no transport authentication. Use only a trusted installation network.
`Additional sources` contains manual filesystem paths, other transports and set skips.
Leaving either submenu returns to source selection without unpacking a set.

For a patched build, publish a new immutable directory and select its build path.
Update the boot image too when a patch changes the kernel or modules.
Changing a download path alone does not update the running installer kernel.
The installer does not fall back to NetBSD servers for OS sets.
Release builders must publish the exact compatible kernel, modules and base sets.
Use a new immutable build directory for each source revision.
Do not mix sets from another revision with the kernel on the image.
`sysinst.conf` can select a separately verified set server.
The existing pkgsrc package defaults remain separate from the OS sets.

## Compatibility and attribution

These NetBSD names remain intentionally:

- Copyright notices, licences, upstream RCS identifiers and source references.
- The kernel filename `netbsd` and the commands that load it.
- ABI names, `uname`, compiler triplets, boot information and Multiboot tags.
- MBR/GPT partition types, disklabel names and diagnostic type descriptions.
- The compatibility path `EFI/NetBSD/boot.cfg` and existing installation paths.
- URLs for NetBSD documentation and the upstream pkgsrc package service.
- The documented NetBSD origin and the base version in the loader version command.

The ASCII mark is supplied under BSD-2-Clause.
The original source licences remain in each adapted loader and sysinst file.

## Checks

```sh
ruby ember/tests/installer-branding.rb
sh ember/tests/boot-logo.sh
ruby ember/tools/boot-logo.rb --check
ruby ember/tests/installer-image.rb full-amd64.iso source-commit-id 1.0
```

The first check detects product-name regressions while allowing compatibility names.
The second executes the actual ASCII renderer under ASan/UBSan.
It checks the eyes, smile, printable bytes, line width and full stock-menu height.
It also checks palette hooks, attribute restoration and unchanged fallback text.
The optional third check reproduces the grid from the source logo.
The fourth reads the actual complete amd64 ISO.
It checks the media menu, set archives, compiled server, product version and catalog build identity.
ISO builders apply media boot.cfg after copying base sets.
The base set's installed-system boot.cfg must not replace the installation menu.
These checks do not prove that a release image boots or installs.
Build the loaders, language catalogs and complete image from a clean source commit.
Check real UEFI and BIOS screens and install onto a disposable virtual disk.
Record the source commit, tools, image hashes, screenshots and installation result.
VM acceptance does not establish physical-device acceptance.

## Checked development image

The amd64 image from source `4a65cc07247c558e88665ca134d67543cac52e5d`
passed UEFI and BIOS boot in QEMU 11.1.1 with amd64 TCG on an arm64 Mac.
Both paths reached sysinst with the product version `Development`.
UEFI installation used the image's sets on a fresh 32 GiB GPT disk without X11.
The optional `base32` set was absent and skipped with the installer's notice.
The completion screen and a boot from the installed disk without an attached ISO were checked.
The root filesystem was mounted from the installed FFS partition.
The isolated guest had no network adapter; passwords and network configuration were skipped.
An empty hostname caused the existing Postfix startup check to fail; this service was not accepted.
Generic getty and motd remain outside the loader/sysinst branding scope.
Their upstream greeting and diagnostic `uname` still identify the NetBSD base.
No physical device or general release is accepted by these VM checks.
