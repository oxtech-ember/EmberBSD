# CIX P1 persistent development root

Status: source contracts. Physical reboot acceptance is pending.
This profile targets the observed CIX P1 CD8180 board and RTL8822CE adapter.
It builds a reduced static C userland from EmberBSD sources with GCC16.
It does not produce a complete base release or an unattended disk installer.

## Inputs and build

Use a clean source commit, complete EmberBSD host tools, and the common
AArch64 GCC16 toolchain. The build uses separate object and destination trees.
It rebuilds the selected libraries before linking the selected commands.
It fences target headers against old compiler `sys-include` symlinks.
The receipt records source, compiler and runtime hashes.
Every selected target executable must be little-endian static AArch64 ELF.

```sh
sh ember/image/cix-p1-build-userland.sh \
    /absolute/source /absolute/new-userland /absolute/tools /absolute/gcc16
sh ember/image/cix-p1-root.sh \
    /absolute/new-userland /absolute/source /absolute/tools \
    /absolute/rtw8822c_fw.bin /absolute/new-root
```

The root assembler requires the exact source commit in the userland receipt.
Run it with the startup scripts from the reviewed profile revision.
The external Realtek firmware must have SHA256
`36b483e37640359a6c686be4661b5f2d94928f93e126a2de09e67c5e04c01b26`.
The image retains the Realtek firmware license. The
[firmware provenance](../../sys/dev/ic/rtw88-provenance.md) records its source.
The output is a new 2 GiB UFS2 image and its staging tree, metadata and hash.
The assembler never opens a block device. It does not include credentials.

## Target provisioning

Keep a working UART console and a known working kernel/initrd pair.
Identify the exact SD device, sector size, GPT disk GUID and partition extents.
Save and verify both GPT copies and every existing boot partition.
Add a new, non-overlapping FFS partition named `ember-cix-root` in unused space.
Do not replace the whole disk with this filesystem image.
Write it only to the verified new partition. Read back and compare its hash.
Before its first boot, check the unmounted new partition with the newly built
native `fsck_ffs`. Do not repair the mounted recovery filesystem.

Mount the installed root from the recovery system. Install the private WPA
configuration as `/etc/wpa_supplicant.conf`, owned by root with mode `0600`.
Use a WPA2-PSK network block with CCMP and the intended SSID.
Keep `update_config=0`. Do not bind the normal profile to one BSSID or frequency
if both bands use the same SSID. Never store the profile in Git or a shared image.

Install the [opt-in boot menu](../boot/cix-p1-persistent.cfg) at `boot.cfg` in
the loader's EFI filesystem root. Verify its kernel and recovery aliases first.
The normal entry clears initrd and supplies `root=NAME=ember-cix-root`.
The menu repeats the narrowly gated SPCR shadow correction and disables
`acpibat` through loader userconf. This does not repair firmware or battery ACPI.
Keep the original boot configuration as a separate rollback file.

## Startup and shutdown

The kernel must mount the disk root read-only. The startup script checks FFS
before remounting it writable. A check failure enters single-user recovery.
A modified mounted root requires a reboot without sync.
`/var/run` and `/tmp` use bounded tmpfs instances. DHCP leases remain on disk.
The profile loads and refreshes the private entropy file with `rndctl`.

Wi-Fi startup takes the interface down, sets `roaming manual`, then raises it.
One background `wpa_supplicant` owns association and WPA authentication.
IPv4-only `dhcpcd -b -4 -C wpa_supplicant` owns DHCP, routes and DNS hooks.
An absent AP does not block the console. Both daemons can retry after boot.
Startup reports launch failures on the console. WPA logs use mode `0600`.
Shutdown releases the IPv4 daemon and lowers the interface before terminating
WPA, because the BSD WPA backend restores automatic roaming on exit.
Use `shutdown -r now` for a reboot with shutdown hooks.
The separate `reboot` command does not execute `/etc/rc.shutdown`.

This development profile exposes an unauthenticated root shell on its trusted
UART console. It starts no network login server. Configure access separately
before deployment outside a controlled development bench.
Native `getty` establishes the console session before starting the shell.
The `ember-cix` entry selects the local wrapper explicitly; no login or PAM
service participates in this development-console path.

## Checks

```sh
ruby ember/image/test-cix-p1-userland.rb
ruby ember/image/test-cix-p1-startup.rb
ruby ember/image/test-cix-p1-root.rb
CC=gcc-16 TEST_CFLAGS=-O2 \
    sh ember/tests/cix-p1-boot.sh /absolute/new-boot-check
```

The startup contracts execute the actual shell functions with command-boundary
doubles. They check ordering, profile permissions, failed launches and FFS
failure handling. They do not qualify target startup or a complete base system.
Physical acceptance must show the disk-root device, a retained marker,
automatic WPA completion, DHCP and checksummed data after an unattended reboot.
Test each required band separately. Preserve source and artifact hashes.
