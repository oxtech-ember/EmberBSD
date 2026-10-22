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
Use the common external GCC 16.2.0 for all target compilation, including
the kernel and modules. Do not select the in-tree compiler or set `HAVE_GCC=16`.

Prepare the [Ports cross compiler](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/development-toolchain/cross/README.md)
and [runtime bootstrap](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/development-toolchain/bootstrap/README.md)
from recorded full Ports commits and the pinned GCC archive/checksums.
The base needs the application compiler with a matching AArch64 seed,
CRT, C++ headers and runtime; the kernel-only compiler cannot replace it.
Select the [external runtime adapter](../../../tools/docs/external-gcc-runtime.md) below.
It supplies build-time paths, not an installed target runtime.

Keep the OS and Ports commits, seed provenance, compiler inputs and hashes,
runtime receipts, build flags and artifact hashes in one recorded build cohort.
Do not combine a newer kernel with older modules or unrecorded base libraries.
First build the host tools and kernel with the fork's `build.sh`:

```sh
export PATH=/sbin:/usr/sbin:/bin:/usr/bin
export MAKECONF=/dev/null
src=/absolute/source
out=/absolute/output
revision=FULL_COMMIT_ID
compiler=/absolute/application-toolchain
runtime=/absolute/gcc-runtime-bootstrap/stage/usr/pkg/gcc16
test "$("$compiler/bin/aarch64--netbsd-gcc" -dumpfullversion)" = 16.2.0
test "$("$compiler/bin/aarch64--netbsd-gcc" -dumpmachine)" = aarch64--netbsd
mkdir "$out"
cd "$src"
sh ./build.sh -U -u -j4 -m evbarm -a aarch64 \
    -O "$out/obj" -T "$out/tools" -D "$out/dest" -R "$out/release" \
    -V "EXTERNAL_TOOLCHAIN=$compiler" -V TOOLCHAIN_MISSING=yes \
    -V MKGCCCMDS=no -V MKCOMPAT=no -V MKCXX=yes \
    -V "EMBER_EXTERNAL_GCC_RUNTIME=$runtime" \
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

The [power-button fallback](../../../sys/modules/rpi5button/README.md) must not
run alongside a working ACPI button event source. On the validated device,
these four were the only modules loaded from disk; their dependencies and
the root filesystem driver were built into `EMBER64`. This profile does
not provide other optional loadable drivers. Check the actual device's
module requirements before using that reduced module tree.

Use a separate destination tree for the reduced base. The following flags
keep C/C++ support and omit new compiler commands, debuggers, tests,
manuals, X11 and additional modules. The explicit build targets reuse the
already completed tools and object directories; use them only after the
preceding toolchain/kernel build from this same revision succeeds.
Create the complete object-directory layout once if it does not already
exist; this does not compile the omitted components.

```sh
"$out/tools/bin/nbmake-evbarm" -C "$src" obj
sh ./build.sh -U -u -j4 -m evbarm -a aarch64 \
    -O "$out/obj" -T "$out/tools" -D "$out/minimal-dest" -R "$out/minimal-release" \
    -V "EXTERNAL_TOOLCHAIN=$compiler" -V TOOLCHAIN_MISSING=yes \
    -V MKCOMPAT=no -V MKCXX=yes -V "EMBER_EXTERNAL_GCC_RUNTIME=$runtime" \
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
```

Pass `BUILDTARGETS` directly to make. Passing it through `build.sh -V`
exports an environment variable, allowing the root Makefile to append
its default targets and repeat work.

Before creating sets, audit the target runtime closure and its installation
ownership. The [Ports runtime package](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/development-toolchain/runtime/README.md)
supplies target DSOs separately from build-only headers, CRT and static archives.
Every target ELF dependency must resolve to a declared payload from this cohort.
Keep one GCC runtime provider and record its package hash and SONAMEs.
The build-only adapter does not perform this installation or prove closure.

`-U` records ownership and permissions in `DESTDIR/METALOG`. Keep it when installing.
Reconcile the selected set lists, destination inventory and runtime provider first.
The [AArch64 set list](../../../distrib/sets/lists/base/ad.aarch64) does not own
external GCC headers, CRT, archives or package-prefix files. Use a new destination;
a previously seeded SDK or runtime copy must not enter the OS sets.
The [set ownership regression](../../tests/external-gcc-setlist.md) checks this
boundary and preserves strict missing/extra-file rejection. It does not qualify
a complete distribution or its ELF dependency closure.
Audit `METALOG` ownership separately. The inherited `checkflist` implementation
compares the destination and selected lists; its metadata comparison is disabled.
Stop if the selected profile has no matching payload and ownership recipe.
Do not copy a compiler SDK to silence missing-file errors or relax `checkflist`.

Only after that audit succeeds, run the unchanged strict set checks:

```sh
"$out/tools/bin/nbmake-evbarm" -C "$src" -j4 sets
```

A partial destination tree or a successful kernel build is not a completed base.
Record hashes of the kernel, native image, modules, runtime package and both sets.

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

Follow the native order in [BUILDING](../../../BUILDING): install the kernel
and matching modules, reboot and verify the loaded build information,
then install the completed userland. A temporary build filesystem must be
unmounted before reboot. Reattach its recorded backing file afterwards;
do not recreate or format the image.

Before replacing userland, verify the candidate's loader, shell, OpenSSL
and SSH in a chroot. An unprivileged build does not create device nodes:
provide a temporary `/dev/null` using the target architecture's `MAKEDEV`
definition, then remove it after the check. Check the complete candidate
libc with the
[CAS contract](../../../common/lib/libc/arch/aarch64/atomic/docs/aarch64-outlined-cas.md) and
[binary128 tests](../aarch64-binary128.md). Keep process-specific
library selection out of system and service configuration.

To install, repeat the reduced `build.sh` command with the same directories
and options, replacing `makewrapper` with `install=/`. `INSTALLSETS=base`
installs only the base set. Merge configuration changes from the new `etc`
set separately, using `etcupdate` and reviewing `postinstall check`.
Preserve network settings, SSH access and keys, enabled board modules,
Bluetooth bonds and existing application startup.

The ARM64 embedded and ARM/AMD64 live-image `rc.conf` templates select
cloud DHCP flags without `rc.subr`. This permits maintenance tools such as
`postinstall` to source the configuration directly. Run
`sh ember/tests/rc-conf-cloud.sh` to check both cloud detector results and
preserve existing DHCP flags. This host test does not validate cloud boot.

Restore the accepted Pi 5 radio firmware before rebooting, including after
an interrupted installation. The upstream base set contains Broadcom
43455 firmware 7.45.18.0; the tested Pi 5 bundle uses Cypress 7.45.265 and
its matching CLM/NVRAM files. The binary, CLM data and Bluetooth firmware
are pinned in [image-assets.tsv](../../boot/image-assets.tsv). Preserve the licences
and verify the hashes. Record this board firmware overlay separately
from the unmodified base set; a UEFI hash check does not protect `/libdata`.

Reboot after installing the base so services load the new libraries.
Compare the installed base files and modules against their accepted
outputs. Verify the loaded kernel revision, SSH, networking and existing
services. Keep the update receipt and recovery files after removing
temporary build storage.

## Validation boundary

These GCC16 instructions describe source configuration and prerequisites.
They do not establish a completed reduced base, full image or physical update.
The runtime/set composition gate above remains separate from compiler and kernel acceptance.

The [historical Pi 5 receipt](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/bcm2712-rpi5/README.md)
records older kernel, module, libc and installed-file checks with the in-tree GCC 12.5 bootstrap.
Those results do not qualify this external GCC16 cohort or recommend the old compiler.
Complete post-reboot network and general release acceptance were not established.
