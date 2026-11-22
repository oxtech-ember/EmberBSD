# Headless Wi-Fi boot diagnostics

`ember_wifi_diag` is an optional, bounded rc.d collector. It does not configure
the network, initiate scans, or read configuration files and keys for diagnostics.
Its separate power-off option is disabled by default.

Install `ember/etc/rc.d/ember_wifi_diag` as `/etc/rc.d/ember_wifi_diag`, owned by
root:wheel with mode `0555`, through a private image overlay. No kernel rebuild
is required. In that overlay's `rc.conf`, enable:

```sh
critical_filesystems_local="OPTIONAL:/var /boot"
ember_wifi_diag=YES
ember_wifi_diag_interface="aicwf0"
ember_wifi_diag_poweroff=YES
```

Preserve any other critical local mount entries. `/usr` must already be available
if it is separate. The service requires `CRITLOCALMOUNTED`, after `/var/run`
cleanup, and precedes `network`, `wpa_supplicant`, and `dhcpcd`. It refuses to
write unless the actual mount list identifies `/boot` as `msdos`.

The service creates `/boot/ember-diag` before networking. Each start allocates
`run-1`, `run-2`, etc. with exclusive `mkdir`; existing runs are retained.
A synchronous `snapshot-0` precedes network startup. Later snapshots run in the
background at ten-second intervals, subject to a 180-second total collection
budget. The initial snapshot consumes part of that budget.

Each snapshot contains `dmesg`, `ifconfig -a`, existing `wpa_cli status` and
`scan_results`, routes, process names without arguments, and the last 200 lines
of `/var/run/rc.log` and `/var/log/messages`. Missing services or commands leave
their error and exit status in the corresponding file. No active scan or `-K`
option is used. Logs can still contain SSIDs, addresses, and other private data.

Base `/usr/bin/timeout -f -k 1 3` limits each command. The outer timeout controls
the worker's process group and allows five seconds after TERM before KILL.
There are also at most 18 background snapshots. Every snapshot calls `sync`;
completion records the worker status and attempts two final syncs. Exit 124 is
normal when the collection window expires. Kernel or device I/O that cannot be
interrupted is outside a userspace timeout's guarantee.

Only `ember_wifi_diag_poweroff=YES` runs `/sbin/shutdown -p now` after collection
and final sync attempts, including a worker failure. This is orderly shutdown,
not an instruction to unplug immediately. Verify power-off on the selected board
before removing power or the card. Default `NO` leaves the system running.

## Private daemon logging

For a freshly prepared diagnostic card, retain existing security and interface
settings and add local daemon logs. With the native debug-file support enabled:

```sh
wpa_supplicant_flags="-d -t -f /boot/ember-diag/wpa.log -i aicwf0 -D bsd -c /etc/wpa_supplicant.conf"
dhcpcd_flags="-d -j /boot/ember-diag/dhcpcd.log"
```

Keep the existing `ctrl_interface` setting for `wpa_cli`. Do not add `-K` or
weaken WPA/SAE or management-frame protection for this collection. These daemon
logs are outside the collector's numbered runs; archive them before reusing a
diagnostic card. Their paths and interface choice belong in private configuration.

## Verification limits

Run `sh ember/tools/wifi-diag-test.sh`. It uses the actual service in an isolated
copy with rc and command mocks. It checks the initial snapshot, command deadlines,
bounded loop, command failure, unique runs, mount refusal, and opt-in shutdown.
It does not prove target timeout signals, physical FAT durability, network
startup, or board power-off. Verify those with the diagnostic image and retain
the resulting FAT files. No device writes occur in the test.
