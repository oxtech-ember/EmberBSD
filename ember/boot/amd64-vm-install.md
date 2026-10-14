# Install an amd64 UEFI VM

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

Download the full ISO, `SHA256` and `manifest.json` from the
[published candidate](https://github.com/oxtech-ember/EmberBSD/releases/tag/build-c5d396f5698b68e956b29ee5b256bc7877e6c517-amd64-no-x11).
Verify the ISO before attaching it. Its SHA256 is
`e9b397228a5c0fc5f95b8fdb241de82c0d726f57c73d3851b05040a272270e63`.
Read the release notes for the no-X11 profile and compiler/hardware limits.
Continue at [Create an isolated VM](#create-an-isolated-vm).

## Optional: build the image from source

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
    -drive file=/absolute/EmberBSD-1.0-amd64.iso,format=raw,media=cdrom,if=none,id=media \
    -device ide-cd,drive=media,bus=ide.2,bootindex=1 -nic none
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
4. Select **Custom installation** for the ISO and HTTPS profile. Set **Yes** for
   GENERIC, kernel modules, base, configuration files, compiler tools, games,
   graphics firmware, both manual-page sets, miscellaneous, recovery tools,
   tests and text tools. Leave GENERIC_KASLR, 32-bit compatibility, X11 and
   source/debug sets unselected. Select **Install selected sets**: 13 sets.
   The `comp` set supplies headers and tools; this profile omits native GCC commands.
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

## Install from GitHub HTTPS

Use the same fresh VM and candidate ISO. Replace `-nic none` with
`-nic user,model=e1000`; do not add a host port forward.
Use the same GPT layout and custom selection of 13 sets above.
Choose **EmberBSD CDN → HTTPS** instead of the local medium.
Choose `wm0`, keep the automatic media type and accept autoconfiguration.
Enter a hostname and review the DHCP address, default route and DNS resolver.

The release builder supplies the server and exact candidate path:

```text
Host: github.com
Base directory: oxtech-ember/EmberBSD/releases/download/build-c5d396f5698b68e956b29ee5b256bc7877e6c517-amd64-no-x11
Binary set directory: empty
```

Keep those values for this candidate and choose **Get Distribution**.
The client follows GitHub's HTTPS redirect and verifies its certificate.
All 13 selected sets must unpack without errors.
Complete configuration and boot without media as described below.
The fresh installed system creates its own trust store from the base-set roots.

For a certificate failure, check the clock, exact hostname and trust anchors.
Do not disable certificate verification or substitute sets from another build.
A missing optional set can be omitted only when the manifest records it as absent.
For a required set's 404 or failed extraction, stop and check the release.

## Boot the installed disk

At the installer's main menu, choose **Exit Install System** and return to its shell.
Run `halt -p` and wait for guest power-off before stopping QEMU.
Restart with the same `disk.qcow2` and `vars.fd`, removing the media drive
and its `ide-cd` device. Keep the target disk's explicit boot priority.
The installed system must start without installation media.
Log in on the guest console, then follow [first-login configuration](../user-guide.md#first-login-and-local-configuration).
Set `postfix=NO` in `/etc/rc.conf` if this VM does not use a mail service.

Run as **root inside the guest**:

```sh
uname -srm
sysctl kern.version
sysctl kern.buildinfo
sha256 /netbsd
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

The published candidate pins image, kernel, modules and base to
`c5d396f5698b68e956b29ee5b256bc7877e6c517`.
Its ISO SHA256 is `e9b397228a5c0fc5f95b8fdb241de82c0d726f57c73d3851b05040a272270e63`.
Its compressed UEFI USB SHA256 is `9e0f818b501b7919efa73de7d347b093dae785103c1ba025c43e275811f67d3d`.
Its installed GENERIC kernel SHA256 is `a64c1d2638469a394d54ef0f09ff0a5655a8c07fae93f39679fda63da6483c04`.

On 2026-10-14, three fresh 32 GiB GPT guests installed from USB, a controlled
HTTPS fixture and the published GitHub HTTPS defaults.
All booted without media and retained a test file after a normal reboot.
The installed kernel matched its set on both boots in each guest.
Both HTTPS routes installed the complete 13-set profile without errors or skips;
all downloaded set hashes matched the candidate manifest.
The public route retained the baked server and exact release directory, without entering a download URL.
Saved DHCP configuration and default-trust HTTPS worked after its reboot.
Both normal and minimal FTP clients rejected unknown CAs, wrong certificate
names, missing anchors and HTTP 404. GitHub redirects and resume passed.
The temporary fixture CA did not enter the installed trust store.

An external client downloaded the public ISO with its exact hash and checked
HTTP 206 resume and a missing release's 404.
The release is an immutable prerelease with 20 verified assets.
The public guest matched the kernel, four critical base files and all 339 module files on both boots.
Its configuration and test-data hashes were unchanged after reboot.
The earlier `a0674c0d2c1b067339b9436cf6148426546649ef` release has a separate three-VM receipt dated 2026-10-13.
The build uses the in-tree GCC12.5 host bootstrap and excludes native GCC commands.
It does not qualify an installed amd64 GCC16 compiler, X11, physical hardware,
BIOS installation, SSH deployment, automatic OS updates or long-duration operation.
The [earlier development image](installer-branding.md#earlier-development-image)
has its own historical component identities and acceptance.
