# Update a headless AArch64 base system

This profile updates an existing EmberBSD/NetBSD 11 AArch64 device with
the fork's kernel, required board modules, base libraries, commands and
services. It does not install a desktop, camera software or OpenCV.
Previously installed applications and local configuration are preserved.
This is an update procedure, not a general installation image.

## Build a pinned source revision

Use a clean export of a recorded EmberBSD commit. Keep build files outside
the installed system directories and record the source archive's SHA256.
The kernel, modules and base must come from the same source revision.
The native build uses the GCC toolchain included in that source tree.

First prepare the toolchain and kernel with the standard `build.sh`:

```sh
export PATH=/sbin:/usr/sbin:/bin:/usr/bin
export MAKECONF=/dev/null
src=/absolute/source
out=/absolute/output
revision=FULL_COMMIT_ID
mkdir "$out"
cd "$src"
sh ./build.sh -U -u -j4 -m evbarm -a aarch64 \
    -O "$out/obj" -T "$out/tools" -D "$out/dest" -R "$out/release" \
    -V MKDEBUG=no -V MKDEBUGLIB=no -V MKX11=no \
    -V "BUILDINFO=EmberBSD:$revision" \
    -V "KERNOBJDIR=$src/sys/arch/evbarm/compile" \
    tools kernel=EMBER64
```

Build the Pi 5 modules with the same generated make wrapper and kernel
headers, after the kernel finishes:

```sh
mkdir "$out/board-modules"
for module in if_cemac_acpi bcm2712btcom rp1wmcodec rpi5button; do
    "$out/tools/bin/nbmake-evbarm" -C "$src/sys/modules/$module" obj
    "$out/tools/bin/nbmake-evbarm" -C "$src/sys/modules/$module" -j4 all
    module_obj=$("$out/tools/bin/nbmake-evbarm" \
        -C "$src/sys/modules/$module" -V .OBJDIR)
    cp "$module_obj/$module.kmod" "$out/board-modules/"
done
```

The [power-button fallback](../../sys/modules/rpi5button/README.md) must not
run alongside a working ACPI button event source. On the validated device,
these four were the only modules loaded from disk; their dependencies and
the root filesystem driver were built into `EMBER64`. This profile does
not provide other optional loadable drivers. Check the actual device's
module requirements before using that reduced module tree.

Use a separate destination tree for the reduced base. The following flags
keep the C/C++ runtime and omit new compiler commands, debuggers, tests,
manuals, X11 and additional modules. The explicit build targets reuse the
already completed tools and object directories; use them only after the
preceding toolchain/kernel build from this same revision succeeds.
Create the complete object-directory layout once if it does not already
exist; this does not compile the omitted components.

```sh
"$out/tools/bin/nbmake-evbarm" -C "$src" obj
sh ./build.sh -U -u -j4 -m evbarm -a aarch64 \
    -O "$out/obj" -T "$out/tools" -D "$out/minimal-dest" -R "$out/minimal-release" \
    -V MKDEBUG=no -V MKDEBUGLIB=no -V MKX11=no \
    -V MKKMOD=no -V MKGCCCMDS=no -V MKGDB=no -V MKBINUTILS=no \
    -V MKATF=no -V MKKYUA=no -V MKMAN=no -V MKDOC=no -V MKINFO=no \
    -V MKHTML=no -V MKLINT=no -V MKPROFILE=no -V MKCVS=no \
    -V MKGROFF=no -V MKRUMP=no -V MKOBJDIRS=no \
    -V INSTALLSETS=base -V 'MAKETARSETS=base etc' \
    -V "BUILDINFO=EmberBSD:$revision" \
    -V "KERNOBJDIR=$src/sys/arch/evbarm/compile" \
    makewrapper
"$out/tools/bin/nbmake-evbarm" -C "$src" -j4 \
    'BUILDTARGETS=check-tools params clean_METALOG do-distrib-dirs includes do-lib do-compat-lib do-build do-obsolete' \
    distribution
"$out/tools/bin/nbmake-evbarm" -C "$src" -j4 sets
```

Pass `BUILDTARGETS` directly to make. Passing it through `build.sh -V`
exports an environment variable, allowing the root Makefile to append
its default targets and repeat work.

`-U` records ownership and permissions in `DESTDIR/METALOG`. Keep it when
installing. The normal release file-list checks remain enabled: a partial
destination tree or a successful kernel build is not a completed base.
Record hashes of the kernel, native image, modules and both sets.

## Recovery and installation

Identify the board, storage device, mounted partitions and actual boot
path. Preserve the old kernel, complete module tree, boot files, base
directories, `/libdata` firmware and local configuration. Keep backups
with host keys or network credentials private. Verify their hashes and
test reading them with retained static recovery tools. NetBSD rescue
`tar` needs a retained external `gzip` to read a compressed archive.

The tested Pi 5 UEFI configuration boots the root-partition ELF `/netbsd`.
Replacing only the FAT partition's `netbsd.img` does not update that path.
Stage the new kernel and required modules before changing active paths.
Retain the old kernel and its matching module tree for recovery.

Follow the native order in [BUILDING](../../BUILDING): install the kernel
and matching modules, reboot and verify the loaded build information,
then install the completed userland. A temporary build filesystem must be
unmounted before reboot. Reattach its recorded backing file afterwards;
do not recreate or format the image.

Before replacing userland, verify the candidate's loader, shell, OpenSSL
and SSH in a chroot. An unprivileged build does not create device nodes:
provide a temporary `/dev/null` using the target architecture's `MAKEDEV`
definition, then remove it after the check. Check the complete candidate
libc with the
[CAS contract](aarch64-outlined-cas.md) and
[binary128 tests](../tools/aarch64-binary128.md). Keep process-specific
library selection out of system and service configuration.

To install, repeat the reduced `build.sh` command with the same directories
and options, replacing `makewrapper` with `install=/`. `INSTALLSETS=base`
installs only the base set. Merge configuration changes from the new `etc`
set separately, using `etcupdate` and reviewing `postinstall check`.
Preserve network settings, SSH access and keys, enabled board modules,
Bluetooth bonds and existing application startup.

Restore the accepted Pi 5 radio firmware before rebooting, including after
an interrupted installation. The upstream base set contains Broadcom
43455 firmware 7.45.18.0; the tested Pi 5 bundle uses Cypress 7.45.265 and
its matching CLM/NVRAM files. The binary, CLM data and Bluetooth firmware
are pinned in [image-assets.tsv](image-assets.tsv). Preserve the licences
and verify the hashes. Record this board firmware overlay separately
from the unmodified base set; a UEFI hash check does not protect `/libdata`.

Reboot after installing the base so services load the new libraries.
Compare the installed base files and modules against their accepted
outputs. Verify the loaded kernel revision, SSH, networking and existing
services. Keep the update receipt and recovery files after removing
temporary build storage.

## Validation

On 2026-10-10, the full update profile was cross-built on Apple Silicon macOS
with the in-tree GCC 12.5 bootstrap: `EMBER64` with recorded `BUILDINFO`, the
four board modules and the reduced base set. The strict `checkflist` caught a
stale `libdwarf` entry in the set lists, fixed before building the sets. The
kernel, modules and base were installed on a physical Pi 5 in the documented
order (kernel and modules, reboot, base, reboot). Post-install acceptance on
the board passed: kernel `kern.buildinfo` matched the pinned revision, all
four modules loaded, 16 memfd and three FP-state checks passed, and the
installed libc passed the smoke, 850-case CAS, binary128 (one `invalid_traps`
skip remains a Cortex-A76 IOE limitation) and 27 libc/pthread checks. The
accepted radio firmware overlay kept its hashes, `etcupdate -a -l` merged the
etc set, and the SAE network block parsed again with the updated
wpa_supplicant. Wi-Fi association was not verifiable at that bench: no
configured network was in range. A single transient host `cc1` SIGTRAP under
`-j8` did not reproduce; the remainder of the build ran at `-j6`.

On 2026-10-07, the matching kernel booted on physical Pi 5 hardware and
passed 16 memfd and three FP-state checks. The reduced base passed strict
file-list validation, candidate libc contracts and a chroot command check.
Installation completed and seven core files matched the accepted outputs.
Local configuration and the radio firmware overlay retained their hashes.

After the userland reboot and a subsequent power cycle, the previous Wi-Fi
address was unreachable. The operator later reported finding the board
over Ethernet. No post-reboot shell, library or network acceptance was
recorded by the installer; investigation continues with another developer.
This is an installed base with incomplete final acceptance, not a validated
general release image. The [board receipt](../boards/raspberry-pi-5.md#headless-base-update-2026-10-07)
records the revision, hashes and boundaries of the completed checks.
