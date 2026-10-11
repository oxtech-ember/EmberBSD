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

The userland components compile with GCC 16. Production-function regressions
pass with ASan/UBSan. A clean EMBER64 kernel and its four board modules built
from revision `6700432be`. The final build includes the scan-padding fix and
action-TX diagnostics; that rebuild and physical k/v acceptance are pending.
The earlier WPA2-only prototype does not establish SAE/PMF interoperability.

The common net80211 ABI and BSD client changes also build for other drivers.
The RRM/BTM firmware transport in this change belongs to bwfm. It does not
establish k/v support on the Zero 3W AIC8800D80 driver.
