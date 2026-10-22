# Upgrade and recover an amd64 UEFI VM

This recipe covers two pinned EmberBSD 1.0 builds with the NetBSD 11.0 compatibility base.
It uses the amd64 UEFI no-X11 profile and QEMU 11.1.1.
It does not establish automatic rollback, an application-data migration, or physical-device acceptance.

## Select complete builds

Use the full ISO, manifest and checksums from each exact release.
Do not mix the old installer kernel with the new distribution to claim new-media acceptance.
Keep the previous complete release and its recovery medium available.
The [previous release](https://github.com/oxtech-ember/EmberBSD/releases/tag/build-a0674c0d2c1b067339b9436cf6148426546649ef-amd64-no-x11)
uses source `a0674c0d2c1b067339b9436cf6148426546649ef`.
The [next release](https://github.com/oxtech-ember/EmberBSD/releases/tag/build-c5d396f5698b68e956b29ee5b256bc7877e6c517-amd64-no-x11)
uses source `c5d396f5698b68e956b29ee5b256bc7877e6c517`.
Both are immutable prereleases. The measured upgrade and recovery are recorded below.

Record `sysctl kern.buildinfo`, the `/netbsd` SHA256, the complete module-tree hashes,
critical base-file hashes, configuration, and application/user-data inventory before an upgrade.
Keep a small user file and a configuration marker to test persistence.

## Preserve a powered-off VM

Shut down the guest normally and wait for QEMU to exit.
Do not copy a writable running qcow2 image.
Preserve the disk and its writable UEFI variables together:

```sh
set -e
mkdir before-upgrade
qemu-img check disk.qcow2
qemu-img convert -O qcow2 disk.qcow2 before-upgrade/disk.qcow2
cp vars.fd before-upgrade/vars.fd
cp /absolute/qemu/uefi-code.fd before-upgrade/firmware-code.fd
qemu-img check before-upgrade/disk.qcow2
qemu-img compare disk.qcow2 before-upgrade/disk.qcow2
shasum -a 256 before-upgrade/disk.qcow2 before-upgrade/vars.fd \
    before-upgrade/firmware-code.fd > before-upgrade/SHA256
```

Keep the matching firmware code, QEMU version and launch arguments with the backup.
Test reading the copy before replacing the active installation.
A standalone copy here is a local recovery point. Use independent backup storage for device loss.

## Run the installer upgrade

Start the same guest disk with the new full ISO and its recorded UEFI firmware.
Use the existing `disk.qcow2` and `vars.fd`. Do not create a new disk or replace
the installed guest's variables with a firmware template.
From the existing VM directory, start only QEMU:

```sh
qemu-system-x86_64 \
    -machine q35,accel=tcg -cpu max -m 2048 -smp 2 \
    -drive if=pflash,format=raw,readonly=on,file=/absolute/qemu/uefi-code.fd \
    -drive if=pflash,format=raw,file=vars.fd \
    -drive file=disk.qcow2,format=qcow2,if=none,id=target \
    -device ide-hd,drive=target,bus=ide.0,bootindex=2 \
    -drive file=/absolute/new-full.iso,format=raw,media=cdrom,if=none,id=media \
    -device ide-cd,drive=media,bus=ide.2,bootindex=1 \
    -nic user,model=e1000
```

Keep the existing network configuration. This connected profile needs the NIC
for the post-upgrade HTTPS check. No host port forward is required.

1. Select the language and keyboard layout.
2. Select **Upgrade EmberBSD on a hard disk** and confirm the upgrade warning.
3. Select the existing virtual disk and installed root filesystem. Do not format or create a new filesystem.
4. Select **Custom installation** with the same 13-set no-X11 profile.
5. Select the local installation image with its matching sets.
6. Require successful extraction of all selected sets. Resolve every error before proceeding.
7. Review the automatic `postinstall` result. Sysinst does not run `etcupdate`.
8. Complete the upgrade and shut down normally.
9. Remove the ISO from the next launch.

The upgrade stages configuration files separately; do not extract `etc.tar.xz` over the live `/etc` directory.
After the first boot, review and merge the staged defaults as root:

```sh
etcupdate -s /.sysinst
postinstall -s /.sysinst -d / check
```

Inspect each configuration difference. Preserve intended local settings and
credentials. Resolve reported checks before accepting the upgrade.
Verify the new source identity and exact kernel, module and base-file hashes after boot.
Verify the saved configuration marker, user file and default-trust HTTPS download.
Reboot normally and repeat the same checks.

## Recover the previous VM

Recovery replaces the complete guest state with the saved copy.
Changes made after that copy, including new user data, are lost.
Preserve any data you need separately before recovery.

Shut down the upgraded guest and wait for QEMU to exit.
Rename its disk and variables aside if they are needed for diagnosis.
Verify `before-upgrade/SHA256` with the host's checksum tool.
Restore both saved files to unused paths:

```sh
set -e
shasum -a 256 -c before-upgrade/SHA256
mkdir recovered
qemu-img convert -O qcow2 before-upgrade/disk.qcow2 recovered/disk.qcow2
cp before-upgrade/vars.fd recovered/vars.fd
qemu-img check recovered/disk.qcow2
qemu-img compare before-upgrade/disk.qcow2 recovered/disk.qcow2
```

Start QEMU with these recovered files, the matching firmware, and no installation medium.
Require the previous source ID, kernel, complete module hashes and critical base-file hashes.
Require the saved configuration and the user-data state at the backup point.
Require network access and default-trust HTTPS, then an ordinary reboot with the same results.
Do not call a kernel-only replacement a complete rollback of modules, userland and configuration.

## Acceptance record

On 2026-10-14, the QEMU profile above completed these operations:

1. Install the previous release's 13-set profile. Boot without media and reboot normally.
2. Verify the powered-off disk copy, UEFI variables and matching firmware code with SHA256.
   `qemu-img check` passed; `qemu-img compare` reported identical disk contents.
3. Upgrade that existing disk through the next release's full ISO, without formatting.
   All 13 sets extracted successfully. The installer's `postinstall fix` reported no failed checks.
4. Boot without media. Review `etcupdate` differences and retain the local account, service and console settings.
   A separate `postinstall check`, with the system command paths available, reported no failed checks.
   Reboot normally and repeat the file and network checks.
5. Add new test data, shut down, then restore the complete previous disk and variables with the saved firmware.
   Disk checks and comparison passed. Boot and reboot without media, then repeat the original checks.

Each installed-state check covered 346 files: `/netbsd`, four critical base files,
all 339 module files, `/etc/rc.conf`, and a test data file.
The upgraded boots matched the next release. Both recovered boots matched the previous release and backup data.
Data added after the backup disappeared on recovery, as expected.
The configuration marker, DHCP, SSH access and default-trust GitHub HTTPS worked after each reboot.
The three baseline QEMU device warnings were unchanged after the upgrade.

| Artifact | Previous release SHA256 | Next release SHA256 |
| --- | --- | --- |
| Full ISO | `7ca271e2be317e2204fb28eff1ecb80e8baab2ac23d0f909b24239225c28fdb7` | `e9b397228a5c0fc5f95b8fdb241de82c0d726f57c73d3851b05040a272270e63` |
| Installed GENERIC kernel | `3cf50410b2aa1c5fac4f4d0b822e1dcd2be5d457ceb362e7b4e8099a14ba2bbc` | `a64c1d2638469a394d54ef0f09ff0a5655a8c07fae93f39679fda63da6483c04` |

This acceptance covers two source builds of EmberBSD 1.0 with the same 11.0 compatibility base.
It does not qualify arbitrary major-version changes, other profiles, application-data migrations, or physical hardware.
Both builds use the recorded in-tree GCC12.5 bootstrap and omit native GCC commands.
They do not establish a complete native amd64 GCC16 environment.
Recovery here is a manual replacement from a powered-off backup, without an automatic update or rollback service.
The test starts recovery after a successful upgrade. It does not simulate failed boot or power loss.
