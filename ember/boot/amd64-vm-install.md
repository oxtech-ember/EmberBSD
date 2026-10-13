# Install an amd64 development VM

This route installs a full EmberBSD development ISO onto a new virtual disk.
It uses UEFI and local sets, then boots the installed system without the ISO.
Use it to learn the installer before working with physical storage.
It does not establish support for a physical amd64 computer or an AArch64 board.

## Prepare the host and image

The host needs QEMU with `qemu-system-x86_64`, `qemu-img`, an x86_64 UEFI code image and matching variable template.
Locate the firmware in your QEMU installation; do not copy another running VM's variables.
Use TCG for this cross-architecture example.
The checked host used QEMU 11.1.1 on Apple Silicon macOS.
TCG can take several minutes to boot or extract sets.

There is no general published release image yet.
Use a complete local-set ISO from an identified build, or prepare one from this public source tree.
The following **host** recipe uses the source tree's own bootstrap compiler.
The [release builder](../release/README.md) supplies verified HTTPS and a pinned GitHub set path.
Read [BUILDING](../../BUILDING), [UPDATING](../../UPDATING) and `tools/compat/README` for host requirements.
Install host C/C++ compilers and development headers first.
The image validator also needs host Ruby and a libarchive `tar` that can read ISO files.
The checked macOS host supplies that `tar`; GNU tar cannot read this format.
On another host, make the ISO-capable libarchive tool available as `tar` in the validator's `PATH`.
Use a case-sensitive source/build volume when the host filesystem has colliding names.
Reserve space for the full source tree, tools, destination, object and release directories.
The 32 GiB guest disk below is separate from that build storage.

In a clean clone, substitute a recorded full commit and unused absolute output path without spaces:

```sh
set -e
revision=FULL_COMMIT_ID
out=/absolute/new-amd64-build
git fetch origin
git cat-file -e "$revision^{commit}"
sh ember/release/build-amd64.sh "$revision" "$out"
```

Stop on a failed archive export or build; never accept an earlier image from a reused output directory.
The complete CD is `$out/release/images/EmberBSD-1.0-amd64.iso`.
A boot-only CD is insufficient for this offline walkthrough.
Use the sets from this completed release, not sets from another source revision.
Check the resulting image with the OS-owned validator:

```sh
ruby ember/tests/installer-image.rb \
    "$out/release/images/EmberBSD-1.0-amd64.iso" "$revision" 1.0 github.com
shasum -a 256 "$out/release/images/EmberBSD-1.0-amd64.iso"
```

Run those commands from the source repository.
Keep the source ID, successful log, tool versions and output hashes together.
The checked development image below used the in-tree GCC 12.5 bootstrap.
It is not a complete GCC16-built release.
The build flags are checked against the source build interface; this documentation work does not repeat a clean full-source build.
Each newly built image needs its own installation acceptance.

## Create an isolated VM

Run these commands on the **host**, in a new empty VM directory.
Substitute the absolute firmware and full-ISO paths:

```sh
mkdir ember-vm
cd ember-vm
qemu-img create -f qcow2 disk.qcow2 32G
cp /absolute/qemu/uefi-variable-template.fd vars.fd
qemu-system-x86_64 \
    -machine q35,accel=tcg -cpu max -m 2048 -smp 2 \
    -drive if=pflash,format=raw,readonly=on,file=/absolute/qemu/uefi-code.fd \
    -drive if=pflash,format=raw,file=vars.fd \
    -drive file=disk.qcow2,format=qcow2,if=none,id=target \
    -device ide-hd,drive=target,bus=ide.0,bootindex=2 \
    -cdrom /absolute/amd64cd.iso -boot once=d -nic none
```

Do not attach host disks, shared directories or another VM's writable disk.
The guest has no network adapter; installation and first boot use the console.
QEMU's default display provides the keyboard and screen.
Firmware filenames depend on the host package.
For the checked Homebrew package, code was `edk2-x86_64-code.fd` and variables were `edk2-i386-vars.fd`.
Use the pair supplied by the same package.
The explicit disk boot priority prevents the firmware from preferring a stale
installation-medium entry after that medium is removed.

## Install from local sets

Choose **Install EmberBSD** in the media boot menu.
Wait for sysinst, then choose the language and keyboard layout.
Select **Install EmberBSD to hard disk**.
Read its warning, then select and activate **Yes**.
Letters select menu items; Enter activates them.

1. Select the new disk. This configuration shows `wd0`, `32G`, `QEMU HARDDISK`.
2. Use GPT and the full disposable disk. Keep the proposed EFI, root and swap partitions unless testing another layout.
3. Review the final disk and partition summary before allowing writes.
4. Select **Installation without X11**. This matches the image's `MKX11=no` profile.
5. Select the local CD-ROM/installation-medium sets. Keep the ISO attached until extraction completes.
6. Use the architecture and set directory already supplied for that medium.
7. If the manifest lists optional `base32` as absent, acknowledge its notice.
   For an HTTPS 404 on that set, select **Additional sources → Skip set**,
   then return to **EmberBSD CDN → HTTPS** for the remaining sets.
   Do not skip a required set or a certificate error.
8. Set the timezone. Configure console passwords for a connected deployment.
9. Leave network configuration and SSH disabled for this isolated exercise.
10. Set the hostname as described below, then complete installation and return to the main menu.

The no-network profile has no interface through which sysinst can configure a hostname.
In the final configuration menu, choose **Run /bin/sh**.
Run `mount` and confirm the installed FFS root is mounted at `/targetroot`.
Read `/targetroot/etc/rc.conf` and preserve its defaults include.
For this fresh installation, add the following only if neither assignment is already present:

```sh
echo 'hostname="ember-vm"' >> /targetroot/etc/rc.conf
echo 'postfix=NO' >> /targetroot/etc/rc.conf
exit
```

If either assignment exists, edit it instead of appending a conflicting value.
Then select **Finished configuring**.
This headless exercise does not configure a mail service.
Setting the hostname and disabling unused Postfix avoids its empty-hostname startup failure.

Source-menu labels can vary by image revision.
Choose the medium containing the matching sets, not the unavailable Internet channel.
The current product default and the release builder use `1.0`.
Both labels are separate from the NetBSD 11 compatibility version.
Current images offer `Bootable USB / image`; the older checked ISO uses the earlier CD-ROM source menu.
Use the image's recorded revision to determine which menu applies.

## Boot the installed disk

At the installer's main menu, choose **Exit Install System** and return to its shell.
Run `halt -p` and wait for guest power-off before stopping QEMU.
Restart the same command with the same `disk.qcow2` and `vars.fd`, removing `-cdrom` and `-boot once=d`.
The installed system must start without installation media.
Log in on the guest console, then follow [first-login configuration](../user-guide.md#first-login-and-local-configuration).
Set `postfix=NO` in `/etc/rc.conf` if this VM does not use a mail service.

Run as **root inside the guest**:

```sh
uname -srm
sysctl kern.version
sysctl kern.buildinfo
mount
df -h
printf 'EmberBSD first-boot check\n' > /root/first-boot-check
cat /root/first-boot-check
shutdown -r now
```

After the reboot, log in and read `/root/first-boot-check` again.
Verify the root filesystem comes from the installed disk and the file survived.
Keep the observed build information with the installation record.
Use `shutdown -p now` for the final shutdown.
Once QEMU stops, run `qemu-img check disk.qcow2` on the host.
Do not check or copy a writable disk while its VM is running.

Only after securing console accounts should you add a VM network and follow the [network and SSH guide](../user-guide.md#connect-the-network).
For an experimental update, preserve the powered-off disk and variables together, then follow the [update guide](../user-guide.md#update-emberbsd).

## Checked boundary

The development ISO's assembly/catalog source is `4a65cc07247c558e88665ca134d67543cac52e5d`; it includes local sets.
Its SHA256 is `6a9791d56e7dfa6c500cf6ba4471057ba73d088f352f6c8d3b7aef9163e7435c`.
The [installer identity guide](installer-branding.md#earlier-development-image) records its original UEFI/BIOS and disk-installation acceptance.
Its GENERIC kernel reports `kern.buildinfo=EmberBSD-95a2bc9bad05`.
The kernel extracted from its set has SHA256 `5cdd6eeb1a3ef07f9cd148bbc98bed608f8995d95c804e4bfc5ec3b1a2585432`.
That older identity differs from the assembly commit; these checks do not accept a uniform whole-OS rebuild of that commit.
Record the actual component identities when preparing a new image, not just the installation banner.
On 2026-10-13, this walkthrough installed that ISO onto a fresh 32 GiB GPT disk in the isolated QEMU profile above.
An independent check booted without the ISO, created a normal account, logged in and used `su` successfully.
A normal reboot retained both user and root test files; the installed kernel hash matched its set on both boots.
That artifact is a checked development input, not a publicly downloadable release promise.
Network, SSH, physical hardware and long-duration operation need their own acceptance.
