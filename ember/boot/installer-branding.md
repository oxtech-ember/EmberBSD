# EmberBSD installer identity

The x86 BIOS, PXE and UEFI loaders identify the product as EmberBSD.
The installation menus and sysinst catalogs use the same identity.
The version number comes from this source tree's `sys/conf/osrelease.sh`.
`EMBER_BUILD_ID` identifies the source commit used for an installation build.
Set it to the full commit ID when building a release.
An unset ID is shown as `development`, not as a published release.

UEFI keeps the firmware text console and keyboard menu.
A small flame appears beside the heading when GOP and a suitable screen exist.
Serial consoles, small displays and missing GOP retain the text menu.
The checked-in raster needs no image decoder or external files at boot.
Its source is [the EmberBSD flame](../branding/emberbsd-logo.png).
The optional generator uses ImageMagick; normal OS builds do not need it.

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
- The explicit `based on NetBSD` attribution and the upstream base version.

The flame asset and its generated raster are supplied under BSD-2-Clause.
The original source licences remain in each adapted loader and sysinst file.

## Checks

```sh
ruby ember/tests/installer-branding.rb
cc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
    ember/tests/boot-logo.c -o /tmp/ember-boot-logo-test
/tmp/ember-boot-logo-test
ruby ember/tools/boot-logo.rb --check
```

The first check detects product-name regressions while allowing compatibility names.
The second executes the real logo placement logic and checks every palette index.
The optional third check reproduces the checked-in raster.
These checks do not prove that a release image boots or installs.
Build the loaders, language catalogs and complete image from a clean source commit.
Check real UEFI and BIOS screens and install onto a disposable virtual disk.
Record the source commit, tools, image hashes, screenshots and installation result.
VM acceptance does not establish physical-device acceptance.
