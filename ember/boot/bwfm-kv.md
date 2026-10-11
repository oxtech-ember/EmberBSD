# Neighbor Reports and host-managed BSS transitions

EmberBSD connects the upstream wpa_supplicant RRM and WNM implementations
to CYW43455 firmware through the BSD driver backend. The host selects a
candidate, sends the BTM response, and performs a new association. Firmware
provides radio operation, CCMP and PMF. Autonomous firmware roaming remains
disabled.

This interface supports 802.11k Neighbor Report exchange and 802.11v BSS
Transition Management. It does not implement every RRM or WNM operation.
It does not add 802.11r, 802.11ax or another spatial stream. See the
[radio and SAE limits](bwfm-sae.md).

## Required components

Use a matching EmberBSD kernel, board modules, wpa_supplicant and wpa_cli.
Build from one clean recorded revision. The normal base build enables WNM.
Install the current net80211 headers into the EmberBSD application sysroot
before building the client. Record the sysroot revision and library hashes.
See [cross compilation](cross-build.md).

The station transport accepts these security profiles:

- WPA2-PSK with CCMP and PMF disabled.
- WPA3-SAE with CCMP and required PMF, using the existing external-SAE path.

The access point must offer the corresponding security and k/v capabilities.
Keep the existing [SAE network profile](bwfm-sae.md#configuration-and-verification).
In this implementation, the default `disable_btm=0` permits host BTM.
Setting it to `1` prevents host BTM advertisement.
A capability shown by the access point is not an exchange test.

## Host and firmware responsibilities

The driver briefly enables WNM advertisement for a join when the client
requests BTM. It sets and reads back WNM disabled before entering RUN.
A failed or short reply does not establish host control. An uncertain
firmware state prevents normal association.

The versioned station ioctl carries a connection generation, peer,
frequency and operation cookie. Receive events must match the current
connection generation, peer and frequency. TX and scan completion also
require the pending operation cookie. Cancellation invalidates queued work.

The driver obtains association IEs from firmware. The client
uses the actual request and response to determine whether RRM was negotiated.

Connected scans retain the current association and refresh scan age.
Scan records preserve the historical quality field. Separate validity bits
carry absolute RSSI and the high capability byte. The client does not treat
an unknown noise level as a measurement.

Firmware processes radio measurement requests. The host transport receives
Neighbor Report responses and BTM requests. Firmware Action RX events carry
the action body and radio metadata, without the original Protected bit or
MIC/replay verdict. Host bounds, peer and epoch checks do not replace
firmware PMF authentication. Negative over-the-air PMF and replay tests
require an access point or injector that can send those frames.

## Reproducible checks

Run the source regressions on the build host:

```sh
sh ember/tests/bwfm-sta.sh
sh ember/tests/bwfm-scan-padding.sh
sh ember/tests/bwfm-key-epoch.sh
sh ember/tests/bwfm-link-check.sh
```

The tests execute extracted production functions with controlled firmware
and queue fixtures. They cover malformed data, cancellation, stale events,
short replies, zeroed scan-record padding and connection-scoped key operations.
They do not exercise the radio.

Use ASan/UBSan on a supported host. Record Valgrind availability separately
from sanitizer results.

Preserve Ethernet access and the previous kernel, module tree and client
before device testing. On an associated station, inspect:

```sh
wpa_cli -i bwfm0 status
wpa_cli -i bwfm0 signal_poll
wpa_cli -i bwfm0 neighbor_rep_request
wpa_cli -i bwfm0 wnm_bss_query 0
sysctl -w hw.bwfm0.report=1
```

Attach a wpa_cli event monitor before requesting a Neighbor Report. Require
a matching response with candidate information. A successful command send
alone is insufficient. Some access points do not answer a BTM Query; use
their transition command or controlled band-steering condition.

For BTM acceptance, require the received request, host response, firmware
TX completion, target association and data transfer. For rejection, use a
temporary BSSID constraint and verify the response and retained connection.
Remove the constraint after the test. Repeat with WPA2 and SAE/PMF.

Verify that data uses bwfm0 in both directions. An SSH connection to the
Wi-Fi address does not prove its return route when Ethernet shares the
subnet. Record the largest interval between actual replies during the
transition. Confirm SAE, PMF, firmware WNM disabled and host roaming after
the transition, and verify normal service startup after reboot.

Keep credentials and complete debug logs private. Do not enable key dumps.

## Validation boundary
The source checks execute 463 station assertions, 100 BSD callback assertions,
8 scan-metadata assertions, 28 padding assertions, 204 key-epoch assertions,
and 286 association-check assertions. ASan/UBSan passed on macOS AArch64.
Valgrind is unavailable on that host; sanitizer results are not Memcheck results.
Full translation-unit analysis used GCC 16.2, Cppcheck 2.22 and Clang 21.
Baseline findings and analyzer complexity limits were retained separately.

### Physical Raspberry Pi 5 check

The 2026-10-11 UTC check used a Pi 5 Model B, DMI `D04170`, UEFI
`rpi5-20260730.1`, CYW43455 firmware 7.45.265 (FWID `01-b677b91b`), and a
KeeneticOS 5.1.6 mesh. Kernel, four board modules and WPA clients came from
`8518dbd9e4f832e1c3d5dab6b587d638c3b99f4d`, cross-built with GCC 16.2.
The live ioctl tool from `67585a1ca` passed 54 checks under both security profiles.

Both WPA2-PSK and SAE/H2E group 19 with required PMF passed these checks:

- A Neighbor Report request received a matching response with five neighbors.
- A temporary BSSID constraint rejected two AP transition requests with status 7.
  Each response had firmware TX acknowledgement. The association remained active.
- After removing the constraint, a fresh scan selected the 5 GHz candidate.
  The host sent status 0, received TX acknowledgement, and associated with it.
- Firmware diagnostics retained `roam_off=1` and `wnm=0` after the transition.
  SAE retained required PMF and firmware `mfp=2`.

| Bounded transition run | WPA2-PSK | SAE/required PMF |
| --- | --- | --- |
| Request to target connection | 4.293 s | 4.559 s |
| Response TX ACK to target connection | 0.205 s | 0.438 s |
| Largest ICMP reply gap during the transition window | 1.265 s | 1.155 s |
| Largest ICMP reply gap in the complete run | 1.331 s | 1.155 s |
| ICMP replies/sent | 300/303 | 293/301 |
| TCP received over 60 seconds | 13,107,200 bytes | 136,183,808 bytes |
| Average TCP receive rate during that run | 1.747 Mbit/s | 18.155 Mbit/s |

An iperf3 TCP connection survived each transition. ICMP probes ran every
200 ms; the capture reported zero kernel drops. Captures stopped before the
final queued replies, so capture counts differ from the ping summaries.
An 8 MiB file passed a post-transition iperf3 `-F` round trip with identical
SHA256 in both directions. These rates are not a WPA2/WPA3 benchmark:
clients shared a busy wireless network and the runs occurred at different times.
The measured gaps do not establish seamless roaming.

Use iperf3 between the station and a known peer to repeat the data-plane check.
Bind the path to Wi-Fi, for example with IPv6 link-local addresses and interface
scopes. Keep management access separate where available. Save JSON summaries.
Use an ICMP capture to measure reply gaps; TCP throughput alone cannot show them.
With a tcpdump package that drops privileges and enters a chroot, write pcap to
stdout and let the shell open the private output file before the chroot.

BTM Query transmission received an ACK, but this AP did not answer it.
Its actual band-steering requests supplied the BTM acceptance test instead.
Negative PMF/replay injection, other AP families, reverse-direction transitions,
and long-duration roaming remain unvalidated. RRM coverage is Neighbor Reports,
not every measurement operation. No 802.11r or radio-generation claim follows.

The common ABI is also used by the separate
[AIC8800D80 station transport](aicwf-sae.md). Its hardware acceptance belongs
to the Zero 3W driver; this bwfm result does not validate that radio.
