# EmberBSD installer identity

The x86 BIOS, PXE and UEFI loaders identify the product as EmberBSD.
The installation menus and sysinst catalogs use the same identity.
The version number comes from this source tree's `sys/conf/osrelease.sh`.
`EMBER_BUILD_ID` identifies the source commit used for an installation build.
Set it to the full commit ID when building a release.
An unset ID is shown as `development`, not as a published release.

BIOS, PXE and UEFI keep a text console and a keyboard menu.
A large ASCII flame and EMBERBSD wordmark use the shared libsa banner renderer.
The mark uses eight art rows and one blank row, with a maximum width of 72 columns.
The stock installation menus fit an 80-column, 25-line screen.
BIOS uses VGA cell attributes. UEFI uses firmware text attributes.
The flame is bright yellow; the wordmark is bright white.
The renderer restores the previous console attribute before the menu.
Serial consoles and other platforms retain the same printable ASCII without color.
UEFI color does not require GOP.
It needs no ANSI escape support, image decoder, graphical mode or external asset.
Custom boot.cfg banners appear below the mark. Keep their total menu height in mind.

## Distribution sets

The default HTTPS host is `cdn.emberbsd.oxteam.me`.
Its intended layout is:

```text
https://cdn.emberbsd.oxteam.me/releases/<EMBER_BUILD_ID>/<architecture>/binary/sets/
```

This is the EmberBSD channel contract. The service is not yet available.
Use an image containing the sets until the channel is published and checked.
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
```

The first check detects product-name regressions while allowing compatibility names.
The second executes the actual ASCII renderer under ASan/UBSan.
It checks printable bytes, line width and the stock installation menu height.\nIt also checks color-hook order, attribute restoration and unchanged fallback text.
These checks do not prove that a release image boots or installs.
Build the loaders, language catalogs and complete image from a clean source commit.
Check real UEFI and BIOS screens and install onto a disposable virtual disk.
Record the source commit, tools, image hashes, screenshots and installation result.
VM acceptance does not establish physical-device acceptance.
