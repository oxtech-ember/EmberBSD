# CIX P1 root from release sets

This profile assembles a CIX P1 root filesystem from the `base` and `etc`
release sets and a package bundle. The root has the shared libraries, the
full `rc.d` system and `pkg_add`, so packages from the common AArch64
catalog run on the board. It replaces the
[reduced static root](cix-p1.md) on boards that need such packages.
The board layer is small: the CIX kernel stays on the boot filesystem, and
the root adds the filesystem table, startup defaults and the Wi-Fi roaming
setting. Hardware support and physical evidence belong to
[DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/cix-p1-orangepi6plus/boot.md).

## Inputs

- A release set directory from the SDK OS release builder:
  `base.tar.xz`, `etc.tar.xz`, `SHA256` and `build-profile.json`.
  The profile must say `built` and `aarch64`. Both checksum sources must
  agree with the archives. The root records the profile source commit.
- A package bundle directory with `.tgz` archives for NetBSD/aarch64 with
  the prefix `/usr/pkg`. It must hold the complete dependency closure of
  the selected packages: the target installs from this bundle only.
  Build the packages on the sysroot of the same release.
- The full Ports commit ID of the package recipes.
- The OS host tools directory with `nbmakefs`.
- `EMBER_HOST_PREFIX`: the native SDK prefix with `python3.15`.

```sh
export EMBER_HOST_PREFIX=/absolute/native-prefix
sh ember/image/cix-p1-sets-root.sh /absolute/sets /absolute/packages \
    PORTS_COMMIT /absolute/tools /absolute/new-output
```

`CIX_ROOT_LABEL` sets the GPT partition name (default `ember-cix-sets`).
`CIX_ROOT_SIZE` sets the filesystem size (default `4g`). The output has
`root.fs`, `root.spec`, `ROOT-INPUTS.txt` and `ROOT-SHA256.txt`.
The assembler never opens a block device.

## Root contents

- All files of `base` and `etc`, with the distribution ownership and modes.
  Paths that differ only in case stop the build: a case-insensitive build
  volume would merge them. A file without target metadata stops the build.
- `/etc/fstab`: the root by its wedge name with WAPBL, `ptyfs`, and a
  `tmpfs` `/tmp` of a quarter of RAM.
- `/etc/rc.conf` defaults: `sshd`, `powerd`, `ember_packages`, no swap,
  no crash dumps, no `postfix`. Wi-Fi (`wpa_supplicant` and `dhcpcd` on
  `rtwce0`) is off until `/etc/rc.conf.d` enables it.
- `/etc/ifconfig.rtwce0` sets manual roaming before `wpa_supplicant`.
- No kernel modules: the release sets carry none, and the board kernel
  comes from another build.
- No credentials, host keys, Wi-Fi profiles or firmware.
- The bundle in `/var/ember/packages` and the `rc.d` scripts that its
  packages declare. `rcorder(8)` reads `/etc/rc.d` once per boot, so the
  scripts must be present before the first boot installs the packages.
  Each service is `NO` in `/etc/rc.conf` until `/etc/rc.conf.d` enables it.

## First boot

The `ember_packages` script runs after `mountall` and before `DAEMON`.
It verifies the checksums and the complete inventory of the bundle. Then
it runs `pkg_add` with the bundle as the only repository and checks each
package with `pkg_admin check`. It compares each placed `rc.d` script with
the installed package example. It records the installed manifest; later
boots skip the installation. Package services that start after `DAEMON`
start in the same boot. A failure stops only this script and writes an
error to the console and syslog. The boot continues. `rc.subr` skips a
service whose program is missing without a message, so the bundle
services stay down. The assembler therefore checks on the build host that
each package dependency matches a bundle package (`pkg_admin pmatch` of
the native prefix).

## Install next to the reduced root

The reduced root and its boot entries stay unchanged. The new root goes to
a new GPT partition on the same card, so the old entry is the rollback.
Commands run as root on the board unless a step names the build host.

1. Record `gpt show -l sd0`, `dkctl sd0 listwedges` and the default entry
   of `boot.cfg` on the EFI system partition. Find free space for the new
   partition after the last one. Stop if there is not enough space.
2. Back up the old root partition and `boot.cfg` to the build host.
   For example: `ssh board 'dd if=/dev/rdk5 bs=1m' > old-root.fs`.
3. Add the partition with the image size in sectors:
   `gpt add -a 1m -s SECTORS -t ffs -l ember-cix-sets sd0`.
   `dkctl sd0 listwedges` must show the new wedge `dkN`.
4. Write the image from the build host. SSH compression shrinks the empty
   blocks: `ssh -C board 'dd of=/dev/rdkN obs=1m' < root.fs`.
   Compare `dd if=/dev/rdkN bs=1m count=MIB | cksum -a sha256` with
   `ROOT-SHA256.txt`. Then run `fsck_ffs -n /dev/rdkN`.
5. Mount the new root on `/mnt` and copy the board's own data from the old
   root: SSH host keys, `/root/.ssh/authorized_keys`, the Wi-Fi profile
   with mode `0600`, the verified `rtw88` firmware and its license in
   `/libdata/firmware/rtwce`, the `dhcpcd` DUID and the entropy file.
   Enable services with files in `/etc/rc.conf.d`. Unmount it.
6. Add a boot entry. Copy the current default entry and change only the
   root: `root=NAME=ember-cix-sets`. The kernel stays on the old root.
   Keep the old entries. Keep the menu within the loader limit of 20 entries.
7. Select the new entry once from the boot menu on the console. After the
   first boot, check `pkg_info`, the services and the network. Reboot and
   check again. Then make the entry the default.

## Rollback

Select the old default entry in the boot menu, or restore the saved
`boot.cfg`. The old root and its partition are not changed by the install.
Remove the new partition only after the rollback works:
`gpt remove -i INDEX sd0`.

## Checks

```sh
export EMBER_TEST_NATIVE_PREFIX="$EMBER_HOST_PREFIX"
"$EMBER_HOST_PREFIX/bin/python3.15" -B ember/image/test-cix-p1-sets-root.py
```

The contract test runs the actual assembler with synthetic sets, packages
and a `makefs` double. It checks the rejections before an image exists,
the target metadata of added files, the placed `rc.d` scripts, the
startup defaults and the bundle verification.

On 4 November 2026 the root of OS `77cc52ebd1c5` with 24 packages
(`chrony`, `ptpd` nb2, `aravis` and their dependencies) was installed by
steps 3 to 7 in AArch64 QEMU guests with HVF and the release `EMBER64`
kernel. A guest with the full base played the old root. It added the
partition next to its mounted root and received the 4 GiB image over
compressed SSH in 29 to 71 seconds; the readback checksum matched. The new
root booted by wedge name. The first boot installed the bundle; `pkg_admin
check` found no errors. `chronyd` synchronised to a local NTP server
(stratum 2) and kept its drift file across reboots. `rcorder` placed
`ember_packages` before `DAEMON` and `chronyd` and `ptpd2` after it. Two
more boots did not reinstall the packages and started the same services.
The restored `boot.cfg` booted the old root again. The root used 11,646 of
541,694 inodes. Next to `chronyd`, the `ptpd2` service ran as a master-only
port with `clock:no_adjust`, and the Ports `probes/ptpd/check-ntp-owner.sh`
found no clock or kernel NTP state write in `PTP_MASTER` and in
`PTP_PASSIVE` behind a second guest with priority1 1 and clock class 6.
The guests have no entropy device: `entropy=wait` held each boot for about
66 seconds. This is a VM check. The CIX kernel, the SD card behind USB,
Wi-Fi and the board's own entropy source need the board.
