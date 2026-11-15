# amd64 UEFI VM: installation, upgrade and recovery

This is the sysinst procedure for a disposable amd64 VM. For the conceptual
model, read the [configuration guide](https://github.com/oxtech-ember/EmberBSD-Docs/blob/main/configuration/README.md).
Hardware installation applicability belongs to DevBoards. A VM result does not
qualify a physical machine, another architecture or a different release profile.

## Select the image and supported operation

Use a complete EmberBSD ISO with its manifest, checksums and matching sets.
Verify the downloaded bytes before attaching it. Record the OS source, target,
profile and compiler/runtime selection; do not mix upstream NetBSD components
into an EmberBSD release. Build new images through the
[SDK release workflow](https://github.com/oxtech-ember/EmberBSD-SDK/blob/main/docs/os-release.md).
Component builds do not establish complete release acceptance.

The [text-installer contract](text-installer.md) owns the current guided UI,
set selection, desktop and runtime-bundle requirements. In particular, images
built with `EMBER_EXTERNAL_GCC_RUNTIME` require guided fresh installation and
refuse expert fresh installation, upgrade and reinstall before disk changes.
Do not use the upgrade procedure below for those images.

The [recorded no-X11 candidate](https://github.com/oxtech-ember/EmberBSD/releases/tag/build-c5d396f5698b68e956b29ee5b256bc7877e6c517-amd64-no-x11)
is an older, bounded installer reference, not a current-toolchain qualification.
For that image, Custom selects 13 sets: GENERIC, modules, base, configuration,
compiler tools, games, graphics firmware, both manual-page sets, miscellaneous,
recovery tools, tests and text tools. It omits GENERIC_KASLR, compatibility,
X11, source and debug sets. The compiler-tools set does not supply native GCC.
Do not impose this fixed selection on a different manifest.

## Prepare an isolated guest

Use QEMU host tools from the selected native SDK/Ports prefix. Select an x86_64
UEFI code image and matching variable template supplied with that QEMU setup.
The following host commands assume a new empty working directory and an
explicit native tool prefix. Substitute the absolute firmware and ISO paths:

```sh
NATIVE_PREFIX=/absolute/native-prefix
mkdir ember-vm
cd ember-vm
"$NATIVE_PREFIX/bin/qemu-img" create -f qcow2 disk.qcow2 32G
cp /absolute/uefi-variable-template.fd vars.fd
"$NATIVE_PREFIX/bin/qemu-system-x86_64" \
    -machine q35,accel=tcg -cpu max -m 2048 -smp 2 \
    -drive if=pflash,format=raw,readonly=on,file=/absolute/uefi-code.fd \
    -drive if=pflash,format=raw,file=vars.fd \
    -drive file=disk.qcow2,format=qcow2,if=none,id=target \
    -device ide-hd,drive=target,bus=ide.0,bootindex=2 \
    -drive file=/absolute/full.iso,format=raw,media=cdrom,if=none,id=media \
    -device ide-cd,drive=media,bus=ide.2,bootindex=1 -nic none
```

TCG permits a host with a different CPU architecture; boot and extraction can
take several minutes. Use QEMU's display for console input. Attach only the new
virtual disk and installation medium, with no host storage or shared directory.
Keep each VM's writable variables separate. For an HTTPS installation, replace
`-nic none` with `-nic user,model=e1000`; no host port forwarding is needed.

## Install and check the first boot

Follow the selected image's UI. The current guided path is documented in
[Install from a complete image](text-installer.md#install-from-a-complete-image).
For the older expert candidate, choose installation to the new disk, GPT and
its full disposable capacity, then Custom with the candidate's set selection.
In both cases, inspect the destination before confirming writes.

Use local sets for an offline run. For HTTPS, use the exact host and release
directory supplied by the release; the recorded candidate uses `github.com`
and `oxtech-ember/EmberBSD/releases/download/build-c5d396f5698b68e956b29ee5b256bc7877e6c517-amd64-no-x11`,
with an empty binary-set subdirectory. Check address, route, DNS and clock.
Keep TLS certificate verification enabled. A missing required set or failed
extraction is a failure; omit an optional set only when the manifest and selected
profile permit it. Current Custom selection fails when an explicitly selected
set is missing. Desktop bundles have separate checks in the installer contract.

Configure hostname, timezone, account and required services. The isolated old
expert profile with no NIC needs its hostname set through the configuration
shell: inspect the mount and installed `rc.conf` first, then update the hostname
once without removing the defaults include. Disable Postfix if unused.
Current first-login and SSH requirements are in
[First login](text-installer.md#first-login); an isolated old receipt is not
evidence of current remote-access acceptance.

Shut down normally and wait for QEMU to exit. Restart the same disk and variables
with the ISO drive and `ide-cd` device removed, retaining the disk boot priority.
Inside the guest, check `uname -srm`, `sysctl kern.version`, `sysctl kern.buildinfo`,
the `/netbsd` hash, mounted root and free space against the release record.
Preserve a test user file and configuration marker; reboot and verify both.
Check network, default-trust HTTPS and actual SSH login when the profile enables
them. Older images without build information need their recorded artifact hashes.

## Upgrade and configuration merge

First check that the image permits upgrade. Preserve the previous complete
release and a recovery medium. Record kernel, module-tree and critical base-file
hashes, configuration, package inventory and user/application data.
Shut down the guest; preserve its disk and variables as described below.

Boot the existing disk with the new full ISO and the same UEFI variables.
Start only QEMU from the earlier command: do not create a new disk or replace
the variables with a template. In the expert interface select upgrade of the
existing disk/root, without formatting. Select compatible sets from the new
image and require successful extraction. Review sysinst's `postinstall` result.
Sysinst stages configuration; it does not run `etcupdate` for the user.

After booting the upgraded disk without media, run as root:

```sh
etcupdate -s /.sysinst
postinstall -s /.sysinst -d / check
```

Review differences and preserve intended settings and credentials. Never unpack
an `etc` set over live `/etc`. Resolve failed checks before accepting the update.
Verify source identity, expected installed hashes, saved data, configuration and
network services, then reboot and repeat. See [etcupdate](../../etcupdate/etcupdate.8)
and [postinstall](../../postinstall/postinstall.8) for their exact options.

## Preserve and recover the VM

With QEMU stopped, check the source disk and create a standalone copy:

```sh
set -e
mkdir before-upgrade
"$NATIVE_PREFIX/bin/qemu-img" check disk.qcow2
"$NATIVE_PREFIX/bin/qemu-img" convert -O qcow2 disk.qcow2 before-upgrade/disk.qcow2
cp vars.fd before-upgrade/vars.fd
cp /absolute/uefi-code.fd before-upgrade/firmware-code.fd
"$NATIVE_PREFIX/bin/qemu-img" check before-upgrade/disk.qcow2
"$NATIVE_PREFIX/bin/qemu-img" compare disk.qcow2 before-upgrade/disk.qcow2
```

Record hashes for all three saved files, along with the QEMU version and launch
arguments. Keep an independent backup for loss of the host storage.
Recovery requires a stopped guest and verified saved bytes. Restore the saved
disk and variables together to unused paths and launch with the saved firmware.
Do not attach two writable copies of the same guest state concurrently.
Changes after the backup are lost unless separately preserved. Recheck the
original build, configuration and data through a normal reboot.

## Historical evidence

The original immutable records retain hashes, source pins, host inputs and exact
checks; they are evidence for those inputs, not instructions for all current images:

- [Installation receipt](https://github.com/oxtech-ember/EmberBSD/blob/343fff8fac548e30560a55a28bd32c68bcfef2c4/doc/guide/amd64-vm-install.md#checked-boundary): local and GitHub HTTPS installation of the pinned no-X11 candidate, then boot and reboot without media.
- [Upgrade and recovery receipt](https://github.com/oxtech-ember/EmberBSD/blob/343fff8fac548e30560a55a28bd32c68bcfef2c4/doc/guide/amd64-vm-upgrade.md#acceptance-record): two recorded EmberBSD 1.0 builds with the 11.0 compatibility base, full-file checks and powered-off backup restoration.
- [Earlier branding receipts](installer-branding.md#checked-10-image): older assembly/kernel identities with narrower scope.

Those trials used the recorded GCC 12.5 bootstrap and QEMU setup. They do not
qualify a current compiler/runtime, desktop bundle, physical target, arbitrary
version migration, failed-boot recovery or automatic rollback service.
This document reorganizes the procedure; it reports no new VM trial.
