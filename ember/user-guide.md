# User Guide

Follow [Quick Start](quick-start.md) through target selection, image verification and installation.
This guide continues from the installed system's console.
Commands in this guide run **inside EmberBSD**, except where a host is named.
Replace uppercase placeholders with observed device values.
Use `man` for the installed command's version-specific options.

## First login and local configuration

Use the credentials configured during installation.
The AArch64 development image allows a passwordless root console and disables SSH.
Do not assume other images use those defaults.
Keep console access until network access survives a reboot.

As root, set a root password interactively and create a normal account:

```sh
passwd
useradd -m -G wheel -s /bin/sh emberuser
passwd emberuser
```

Choose your own unused account name if `emberuser` already exists.
Do not reuse the base system's `operator` service account.
The `wheel` group permits administration through `su`; it does not install `sudo`.
Use the normal account for daily work.
Keep passwords, keys and personalized images outside public repositories.

Edit `/etc/rc.conf` as root with `vi` or your installed editor.
Preserve the existing defaults include and any board-specific settings.
Set or update these assignments once; do not append conflicting duplicates:

```sh
rc_configured=YES
hostname="ember-device"
```

Use a distinct hostname for each device.
Override services in `/etc/rc.conf`; do not edit `/etc/defaults/rc.conf`.
If the device does not use a local mail service, set `postfix=NO` there.
The checked amd64 installation does not require Postfix.

Verify the running system and storage before changing more settings:

```sh
uname -srm
sysctl kern.version
sysctl kern.buildinfo
date
mount
df -h
```

Compare the source ID and artifacts with the build record.
Check the clock before TLS connections or package downloads.
If `kern.buildinfo` is unavailable, use the image manifest and its recorded kernel hash.
Keep the timezone selected by sysinst; use `man date` for a necessary local clock correction.

## Connect the network

Inspect the attached drivers and interface names:

```sh
dmesg
ifconfig -a
netstat -rn
```

For Ethernet DHCP, replace `IFACE` below with the observed name, such as `wm0`.
As root, test the connection:

```sh
dhcpcd IFACE
ifconfig IFACE
netstat -rn
cat /etc/resolv.conf
```

Check the address, default route and resolver.
Test a known host or service on your network; ICMP may be filtered.
Do not publish private network details from these outputs.
For persistence, set `dhcpcd=YES` and `dhcpcd_flags="-qM IFACE"` in `/etc/rc.conf`, using the actual name.
Keep existing interface configuration consistent; do not start two DHCP clients on one interface.
For static addressing, follow the installed `ifconfig.if(5)` and `rc.conf(5)` manuals.

Wi-Fi depends on the board and radio firmware.
Use the board's linked instructions: [Pi 5/CM5 WPA3](boot/bwfm-sae.md)
or [Zero 3W WPA3](boot/aicwf-sae.md), where applicable.
Read their security modes and unsupported cases before changing an existing network block.
Preserve the accepted firmware files and licenses recorded in [image-assets.tsv](boot/image-assets.tsv).
A network test on Ethernet does not validate Wi-Fi or Bluetooth.

## Enable SSH

Create a key on your **client host**, or use an existing key under your control.
Copy only its public key to the normal user's `~/.ssh/authorized_keys` on EmberBSD.
Use owner-only permissions: `chmod 700 ~/.ssh` and `chmod 600 ~/.ssh/authorized_keys` as that user.
Verify the file and directory belong to that user.
Keep the private key on the client.

As root, generate missing host keys, validate the server, and start it:

```sh
ssh-keygen -A
/usr/sbin/sshd -t
ssh-keygen -l -f /etc/ssh/ssh_host_ed25519_key.pub
```

Set `sshd=YES` in `/etc/rc.conf`, then run `/etc/rc.d/sshd start`.
On the client, connect with `ssh emberuser@DEVICE_ADDRESS` and confirm the host-key fingerprint through the console.
Check an administrative `su -` session while console access remains available.
For key-only access, set `PasswordAuthentication no` and `KbdInteractiveAuthentication no` in `/etc/ssh/sshd_config`.
Set `PermitRootLogin no` after the normal user's administration path works.
Run `sshd -t` again before `/etc/rc.d/sshd reload`.
Open a second client session before closing the first one.

## Install applications

[Ports](https://github.com/oxtech-ember/EmberBSD-Ports) owns packages, dependency versions and portability patches.
[Examples](https://github.com/oxtech-ember/EmberBSD-Examples) owns runnable application scenarios.
Select the application's documented profile and its tested architecture.
Runtime and SDK are at the design stage; no released EmberBSD application-management CLI is available.

Inspect the package tools and installed packages:

```sh
command -v pkg_info pkg_add pkg_delete pkgin
pkg_info
```

Not every image includes these tools or `pkgin`.
Do not interpret their absence as a request to use a different distribution's package service.
For a compatible local package bundle, verify the archive hashes and full dependency closure first.
Preview the operation, then install as root:

```sh
pkg_add -n /absolute/package.tgz
pkg_add /absolute/package.tgz
pkg_admin check
```

Follow the selected Ports profile for repository configuration, build instructions and acceptance checks.
Do not assume upstream binary packages contain this fork's adaptations or the common GCC16 profile.
Package installation is not an OS-base upgrade.
Before removal, inspect reverse dependencies with `pkg_info -R PACKAGE`; then use `pkg_delete PACKAGE`.
Retain package inventory and application data backups before updating dependencies.

## Update EmberBSD

There is no accepted general automatic OS-update or rollback service yet.
Choose the update recipe for the exact architecture, firmware and current installation.
The [headless AArch64 base update](boot/aarch64-base-update.md) has a physical Pi 5 receipt.
It records its in-tree GCC12 bootstrap boundary separately from the common GCC16 toolchain.
Do not treat that receipt as acceptance on another board or of a complete general release.

1. Read the selected recipe, release/build notes and [UPDATING](../UPDATING).
2. Record the installed source ID, modules, firmware, configuration and application inventory.
3. Prepare and verify a coherent candidate kernel, required modules and userland from recorded sources.
4. Back up the old kernel, complete matching module tree, boot files, base, firmware and configuration.
5. Test reading the backup and booting the recovery medium before replacing active files.
6. Follow the recipe's order: kernel and matching modules, reboot and check, then compatible userland.
7. Merge configuration with `etcupdate`; inspect `postinstall check` using the recipe's candidate source.
8. Restore and verify board firmware overlays that a base-set installation can replace.
9. Reboot normally; verify source identity, network, SSH and the same useful application operation.

Do not extract an `etc` archive over your live configuration.
The [published amd64 candidate](https://github.com/oxtech-ember/EmberBSD/releases/tag/build-a0674c0d2c1b067339b9436cf6148426546649ef-amd64-no-x11)
provides an exact HTTPS source for installation. Use its matching media and manifest.
This channel does not establish a general in-place OS upgrade or automatic rollback service.
Changing the source URL does not update the running installer kernel or its modules.
For a VM installation, preserve a powered-off disk and its UEFI variables together before an experimental upgrade.
A qcow2 copy is a backup, not a validated automatic rollback mechanism.
An update with no accepted device-specific recipe needs preparation through [CONTRIBUTING](../CONTRIBUTING.md).

Firmware updates are separate operations with their own board and recovery requirements.
On the checked Pi 5 UEFI path, the active kernel is the root partition's ELF `/netbsd`.
Replacing only FAT `netbsd.img` does not update it.
Application-package updates must preserve the selected Ports dependency profile and the application's data format.

## Recover or report a failure

Keep a console, the verified previous build and a readable backup available.
If a new kernel fails, use the recorded boot-loader path to load the old kernel with its matching modules.
If userland fails, use the recovery medium and restore the compatible base and configuration together.
Do not repeatedly write firmware or format a disk to diagnose an unknown failure.
Use the exact board page for loader and storage details.

Record the expected operation, actual result, source/image hashes and the last successful stage.
Collect relevant read-only output from `uname -srm`, `sysctl kern.version`, `dmesg`, `mount`,
`df -h`, `ifconfig -a`, `netstat -rn` and `ps -ax -o pid,comm`.
Include the command's exit status and a small reproducible case.
Remove credentials, host keys, personal addresses and unrelated logs before sharing evidence.
Report a platform defect or reusable fix to the [owning repository](../CONTRIBUTING.md#1-select-the-owner).

## Use an assistant, if desired

[EmberBSD User Skills](https://github.com/oxtech-ember/EmberBSD-User-Skills)
supports device operation and personal software changes.
Its current desktop-VM installation selector targets AArch64; it does not replace the amd64 walkthrough.
For shared kernel, board, Ports or documentation changes, use
[EmberBSD Development](../CONTRIBUTING.md#development-through-agents).
The assistant follows the same preparation, tests, independent review and hardware acceptance as a human author.
