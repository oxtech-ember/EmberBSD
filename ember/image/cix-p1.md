# CIX P1 persistent development root

Status: source contracts and five unattended physical SD-root boots on 2026-10-17.
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

The profile starts native `powerd -d` in the background after root setup.
Native AArch64 MAKEDEV supplies `/dev/power` through its `sysmon` entry.
The profile also requires native `stty` and `usbdevs`, and requests USB control
and controller nodes zero through nineteen. Native MAKEDEV supplies PLcom1/3
and their dialout aliases. The DEBUG UART nodes are not included or opened.
Daemon diagnostics go to `/var/log/powerd.log` with mode `0600`.
A failed early launch reports a console warning and leaves the console available.
The one-second live-process check does not prove delivery of button events.
The profile's `power_button` policy accepts `pressed` and calls `shutdown -D -i -p now`.
An atomic guard prevents duplicate requests until reboot. Foreground `-D` keeps
early errors visible to the policy. A failed synchronous request releases the
guard so a later press can retry. Release events do nothing.
This policy retains shutdown hooks, sync, process termination and filesystem shutdown.
The explicit `-i` mode removes administrative pauses and waits adaptively for
process exit. Live services retain the normal TERM grace and KILL retry limit.
Three to five seconds is a healthy-system objective, not a safety deadline.
Network shutdown, filesystem operations and firmware can take longer.
The new timing mode requires rebuilt native shutdown/halt binaries and a separate
target trial. A late shutdown failure can retain the request guard; the policy
does not guarantee retry after process termination has begun.

One physical trial on 2026-10-18 uses the accepted `522474e4d` kernel, static
GCC16 utilities from `2231dbb6c`, and the `3794f23aa` console wrapper.
UART records sync and completed FFS unmount 4.05 s after the shutdown reason.
The operator measures about 5 s from the press to fan stop, instead of 15–20 s.
This is one healthy-system result. After DC restoration, the next unattended
boot reports clean FFS, retains the marker and new file hashes, and restores
WPA2/DHCP. Electrical power-off remains unmeasured. Power-on by a later short
press still fails; the operator must remove and restore input power.
The same failure reproduces after the built-in UEFI shutdown command in a
control boot stopped before OS execution. See the [GPIO evidence](../boot/cix-gpio.md).

The current network stop policy takes the interface DOWN before DHCP address
cleanup, then terminates WPA. Multicast-list changes can restart an UP radio
through the native ioctl path. Quiescing first avoids that restart request.
The actual-consumer contract checks the old ordering as a failing control.
Local DHCP cleanup remains requested; delivery of DHCPRELEASE after DOWN is
not guaranteed. After installing `fc9ab38b2`, one `shutdown -i -r now` trial
reaches completed FFS unmount 0.31 s after the reason log. No radio calibration
appears within that interval. The next boot reports clean FFS and restores
WPA2/DHCP; three gateway probes pass. The five-second button trial predates this
ordering change. New press-to-fan-stop and electrical timing remain unmeasured.

This development profile exposes an unauthenticated root shell on its trusted
UART console. It starts no network login server. Configure access separately
before deployment outside a controlled development bench.
Native `getty` establishes the console session before starting the shell.
The wrapper retains getty's noninteractive session leader and waits for its
interactive child. A normal child exit keeps its status. TERM ends the leader;
native terminal teardown sends HUP to the foreground process group and revokes
the controlling terminal. This avoids a TERM trap deferred at an idle prompt.
Normal interactive job control and Ctrl-C remain available. This requires a
controlling terminal; a raw pipe is not an equivalent console session.
Blocked terminal output, stopped jobs or custom signal policies can still
require the full native grace period. No service or filesystem deadline changes.
The `ember-cix` entry selects the local wrapper explicitly; no login or PAM
service participates in this development-console path.

## Checks

```sh
ruby ember/image/test-cix-p1-userland.rb
ruby ember/image/test-cix-p1-startup.rb
ruby ember/image/test-cix-p1-power.rb
ruby ember/image/test-cix-p1-root.rb
ruby ember/image/test-cix-p1-root-image.rb
CC=gcc-16 TEST_CFLAGS=-O2 \
    sh ember/tests/cix-p1-boot.sh /absolute/new-boot-check
```

The startup contracts execute the actual shell functions with command-boundary
doubles. They check ordering, profile permissions, failed launches and FFS
failure handling. They do not qualify target startup or a complete base system.
The power contract executes the actual policy and startup function with explicit
device, daemon and shutdown boundary doubles. It never executes host shutdown.
The root-image contract runs the actual assembler against synthetic Git sources,
runtime receipts, firmware data and host-tool doubles. It checks rejection before
image creation, final source pins, native ownership metadata and the getty recipe.
It neither reads a private Wi-Fi profile nor formats a real filesystem or device.
Physical acceptance must show the disk-root device, a retained marker,
automatic WPA completion, DHCP and checksummed data after an unattended reboot.
Test each required band separately. Preserve source and artifact hashes.

## Physical reboot check

One CIX P1 CD8180 board, 32 GB, A4 PHECDA-Rev A, passed five unattended boots
with stock `BiosV1.5-W20260706-e321056a`. No menu or userconf input was sent.
The first boot moved from the recovery RAM root to SD. Four further boots used
`shutdown -r now`; all cleanly unmounted FFS and the two tmpfs instances.
Each boot selected `dk2`, retained the same marker and started one WPA daemon
and one IPv4 DHCP daemon. The private profile retained root ownership and `0600`.
These five boots predate the power-button policy described above.

| Check | Result |
| --- | --- |
| Kernel | `71877588ca099bf69686a35b96a00771a26feebc`, GCC16 |
| Selected userland | `dafccbdc09153ffc85be31fadfd3f7a6cdf536fe`, including a recorded same-source getty extension; 77 static programs |
| Startup and assembler | `2079c3c6574885304ad1c8c71be02aa79854e22e` |
| Owning build recipe with getty | `a60132e4b683ce2bfb39c39b7ac54ece60545229` |
| Initial 2 GiB FFS image | SHA256 `6d9d309ae91d14d10cf664d00410721dad36c7c5f323837f7c55cf87896a6c9d`; full SD readback matches; forced native read-only fsck exits zero |
| Boot 1, unrestricted profile | Channel 149; WPA2-PSK, CCMP pairwise/group, DHCP and default route; 2 MiB TCP each way, SHA256 matches |
| Boot 2, selected 2.4 GHz AP | Channel 8; WPA2 and local-subnet TCP pass; the early snapshot does not capture a DHCP lease or default route |
| Boot 3, restored unrestricted profile | Channel 149; all checks pass again |
| Boot 4, selected 2.4 GHz AP | Channel 8; WPA2, DHCP lease, default route and TCP integrity pass |
| Boot 5, restored unrestricted profile | Channel 8; WPA2, DHCP lease, default route and TCP integrity pass |

The simultaneous TCP checks take 9.38 s, 35.53 s, 10.92 s, 33.27 s and 33.73 s.
Each subsequent router check receives three replies. This is a short integrity
check, not a throughput qualification. Association retries occur before connection.
Boot 4 reaches the console in 53 s; DHCP readiness is observed at 150 s.
The console appears before background WPA and DHCP complete. No connection-time
bound is qualified. The early boot 2 snapshot is not DHCP evidence; boot 4
separately confirms that check on 2.4 GHz.
The final profile has no BSSID or frequency restriction. Credentials stay off Git.
The original EFI configuration and the known working RAM recovery remain available.

The [startup CI](https://github.com/oxtech-ember/EmberBSD/actions/runs/39041299600)
passes all 148 host checks on the exact owning-recipe commit above.
These checks do not execute target AArch64 programs. The boot-menu contract has
separate Linux Memcheck and sanitizer acceptance.
Cold power removal, an absent AP, repeated AP loss and long-term operation remain
unqualified. The battery ACPI workaround, RTC warning and ADC/DAC quality warnings
remain. This reduced root does not qualify the complete board or a full base set.
