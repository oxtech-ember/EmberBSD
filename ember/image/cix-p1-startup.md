# Reduced-root startup contract

The kernel must mount the disk root read-only. The startup script checks FFS
before remounting it writable. A check failure enters single-user recovery.
A modified mounted root requires a reboot without sync.
`/var/run` and `/tmp` use bounded tmpfs instances. DHCP leases remain on disk.
The profile loads and refreshes the private entropy file with `rndctl`.

Wi-Fi startup takes the interface down, sets `roaming manual`, then raises it.
One background `wpa_supplicant` owns association and WPA authentication.
Dual-stack `dhcpcd -b -C wpa_supplicant` owns DHCPv4, IPv6 router solicitation,
SLAAC, DHCPv6, routes and DNS hooks. The reduced userland must be rebuilt with
`SMALLPROG=1 SMALLPROG_INET6=1` before deploying this startup profile.
An absent AP does not block the console. Both daemons can retry after boot.
Startup reports launch failures on the console. WPA logs use mode `0600`.
Shutdown lowers the interface, then stops the dual-stack daemon and any legacy
IPv4-only daemon before terminating WPA. The BSD WPA backend restores automatic
roaming on exit. The legacy `-4` cleanup supports an IPv4-only rollback daemon.
These profile changes have host orchestration coverage. The new dhcpcd target
build, IPv4 recovery, IPv6 RA/SLAAC and DHCPv6 require physical acceptance.
IPv6 link-local access alone does not establish SLAAC or DHCPv6 operation.
Use `shutdown -r now` for a reboot with shutdown hooks.
The separate `reboot` command does not execute `/etc/rc.shutdown`.

The profile starts native `powerd -d` in the background after root setup.
Native AArch64 MAKEDEV supplies `/dev/power` through its `sysmon` entry.
The profile also requires native `stty` and `usbdevs`, and requests USB control
and controller nodes zero through nineteen. Native MAKEDEV supplies PLcom1/3
and their dialout aliases. It also requires `envstat`, `nvmectl`, `wdogctl` and `acpidump`;
the `sysmon` entry supplies `/dev/sysmon` and `/dev/watchdog`, and `nvme0` the
NVMe controller node. The DEBUG UART nodes are not included or opened.
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

The current network stop policy takes the interface DOWN before DHCP address
cleanup, then terminates WPA. Multicast-list changes can restart an UP radio
through the native ioctl path. Quiescing first avoids that restart request.
The actual-consumer contract checks the old ordering as a failing control.
Local DHCP cleanup remains requested; delivery of DHCPRELEASE after DOWN is
not guaranteed. Source-contract success does not qualify a physical shutdown deadline.

This development profile exposes an unauthenticated root shell on its trusted
UART console. It starts no network login server unless optional SSH checks pass. Configure access separately
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
