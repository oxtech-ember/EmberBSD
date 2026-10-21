# AIC8800D80 external SAE

The AIC SDIO path uses the existing EmberBSD external-SAE interface and
upstream wpa_supplicant 2.11. Authentication and the four-way handshake stay
in userspace. Firmware handles association, CCMP and protected management
frames. This implementation targets firmware 0x06090101 with its MFP feature
bit; other firmware revisions do not advertise SAE until separately checked.

## Current boundary

The station path accepts SAE, CCMP and required PMF with BIP-CMAC-128.
The firmware key command has no initial IGTK packet-number field. The
legacy client therefore rejects a nonzero initial IPN instead of silently
losing the replay boundary. A separate [fixed-image extension](aicwf-firmware-bip.md)
has a hash-bound WLAN loader and a private initial-IPN wire contract. Its
instruction-model and physical acceptance remain pending. Legacy images cannot
complete networks or rekeys that supply such an IPN. On a physical Zero 3W,
required PMF negotiation, SAE/H2E,
IGTK installation and bidirectional traffic pass with an accepted initial IPN.
A separate [VHT80 trial](aicwf-vht.md#physical-vht80-check) reproduces the
nonzero-IPN failure after successful H2E authentication. Management-frame forgery
and replay injection remain untested; this is not a WPA3 certification claim.

SAE configuration is privileged. A generation and peer bind commands to one
association. Authentication frames must have bounded length, the selected
AP's addresses and SAE algorithm; fragments, foreign peers and other frame
types are rejected. An internal lifetime counter also retires queued commands and late replies
after a disconnect or profile change. Firmware response status must be present. External-auth requests accept
the two SAE selector byte orders supported by upstream wpa_supplicant. IGTK hardware
slots are separate from the pairwise-data-key slot.

Use the [SAE profile and client build guide](bwfm-sae.md#configuration-and-verification),
substituting `aicwf0` for the interface. Install the matching kernel/modules
and current base wpa_supplicant. Preserve a bootable kernel and a recovery
path before replacing the only network connection. Keep credentials and
unredacted logs outside source control. `vmstat -e` shows the driver's SAE
start, received, sent and rejected counters without displaying credentials.

This SAE path does not add 802.11r fast transition, 802.11ax or host AP mode.
Host-managed Neighbor Reports and BSS transitions use the separate
[station transport](aicwf-kv.md). VHT80 has a separate WPA2 check. The [Zero 3W board page](../boards/orange-pi-zero-3w.md)
records actual physical results separately.
See the [VHT80 and roaming status](aicwf-vht.md) for the `11ac/k/v 1x1 80 MHz` target.

## Band steering and acceptance

A successful SAE handshake does not prove sustained data traffic. A mesh AP
can remove a 2.4 GHz station under its band-steering policy while the client
still reports `COMPLETED`. Compare the AP log with host-side SSH and packet
checks before attributing that failure to SAE or the kernel. In the physical
Zero 3W trial, the AP explicitly prohibited 2.4 GHz about a minute after joining;
the same policy removed WPA2 and WPA3 associations.

Before the SA Query fix, the driver received unprotected deauthentication
frames without notifying the supplicant, and firmware sent no disconnect
indication. The 2026-10-09 physical acceptance now confirms a protected
query/response and automatic recovery from reason-6 state mismatch.
A separate host verified an 8 MiB transfer each way after recovery, before
any fallback service restart. Recovery interrupted traffic for about
40 seconds in that trial; it was not a seamless handover. The
[board evidence](../boards/orange-pi-zero-3w.md#protected-association-recovery-2026-10-09)
records the exact bundle and test boundary.

The host path now passes peer-validated unprotected deauthentication and
disassociation reasons 6/7 to upstream wpa_supplicant's SA Query state
machine. Such a frame starts a protected liveness check; it does not directly
terminate the association. Only the current peer and association can send or
receive SA Query frames. Firmware encrypts requests with the installed PTK.
Received responses require successful firmware CCMP/key status, matching
addresses and a fresh management packet number. Reinstalling an unchanged
PTK preserves the firmware transmit counter and the host replay boundary.
The supplicant verifies the transaction ID and reconnects on query timeout.

Firmware remains responsible for CCMP integrity verification. The host's
status/replay contracts do not substitute for forged-frame injection over
the air. The current interface supports the minimal SA Query exchange;
operating-channel validation (OCV) is not provided.

Diagnostic builds expose management-frame type, flags, length and hardware
status through `sysctl -w net.link.ieee80211.vap0.debug=524288` (verify that
`vap0.parent` is the intended interface). No frame body or key is logged.
Restore the previous debug value after the trial.

Do not pin the client to a BSSID that the AP's policy forbids. For a network
that requires 5 GHz, set the network profile's `freq_list` to its allowed
5 GHz frequencies, without a BSSID lock. Other network profiles can retain
2.4 GHz support. `freq_list` restricts candidate APs; it is not a preference
with a 2.4 GHz fallback. Preserve another profile if that fallback is needed.

When testing the only network interface, keep the recovery timer armed until
a separate host confirms SSH and bidirectional traffic after the operation.
A single successful gateway ping immediately after association is insufficient.

## Checks

```sh
sh ember/tests/aicwf-sae.sh
sh ember/tests/aicwf-scan.sh
sh ember/tests/aicwf-scan-channel.sh
sh ember/tests/aicwf-flow-control.sh
sh ember/tests/aicwf-command.sh
sh ember/tests/aicwf-sae-lifetime.sh
sh ember/tests/aicwf-sae-event.sh
sh ember/tests/aicwf-pmf.sh
sh ember/tests/aicwf-pmf-key.sh
sh ember/tests/aicwf-pmf-transport.sh
```

The first runs bounded AIC request/frame/IGTK checks and the shared RSN
security checks also used by bwfm. The extracted-function regressions cover
connected scans, adjacent-channel beacons, the D80 free-buffer register,
short command replies, stale commands and confirmations,
CONFIGURE during requested leave, and allocation failure in SAE events.
The PMF contracts cover peer/length/protection checks, packet-number replay,
key confirmation and retirement, duplicate PTK installation, allocation
failure and actual RX/TX forwarding. The key-task regression also checks
that callbacks under a net80211 spin lock never acquire an adaptive mutex.
For a live ABI check, build
`ember/tools/bwfm-sae-ioctl.c` with the current net80211 headers and run it as
root with `aicwf0`, with the supplicant stopped and recovery arranged.
It checks lengths, version/reserved fields, stale authorization and denial
of unprivileged configuration. Its historical filename does not restrict
the driver being tested.

For a positive on-air SA Query probe, build against current EmberBSD headers:

```sh
cc -O2 -Wall -Wextra -Werror ember/tools/aicwf-sa-query.c -o aicwf-sa-query
# First terminal, on the device, as root:
./aicwf-sa-query aicwf0
# Second terminal, after the probe reports that it is listening:
wpa_cli -i aicwf0 reassociate
```

The probe observes the new SAE generation, sends a protected query and
requires the matching response within 60 seconds. It changes no profile.
Reassociation interrupts traffic; arrange recovery first on Wi-Fi-only
boards. This positive probe does not exercise timeout recovery or RF forgery.

## Link-liveness watchdog

A mesh AP steers bands and may drop a silently idle station without any
deauthentication the firmware would report as SM_DISCONNECT_IND. The driver
now stamps the tick counter on every received frame, beacons included, and a
2-second callout checks the stamp while the driver believes it is associated.
After two consecutive silent checks (10 seconds without any frame from the
air - about a hundred lost beacons) the watchdog tears the firmware
association down through the ordinary SM_DISCONNECT_REQ path and returns
net80211 to scan, which re-associates through the same SAE/PMF supplicant
path as a fresh join. The firmware's own MM_CONNECTION_LOSS_IND (0x0043)
routes to the same teardown, and the AP-mode UAPSD housekeeping request
ME_TRAFFIC_IND_REQ (0x140b) is now recognized and ignored quietly instead of
logging as an unexpected message.

Recovery interrupts traffic for the rescan duration (observed around ten
seconds); it does not require userland. The watchdog never fires while the
driver is not associated, and a single in-flight work item bounds the
teardown. Queued recovery records the association lifetime. A stale work item
cannot tear down a replacement association.
Run `sh ember/tests/aicwf-alive.sh` for the decision math,
miss accounting and single-flight checks.

## 802.11r groundwork

The mesh advertises fast transitions, so the FT groundwork landed in two
pieces. The scan parser now records each BSSID's Mobility Domain element
(element 54): MDID and R0KH-ID land in a small per-softc cache, and the
driver prints the mobility domain when it associates with an 802.11r-capable
BSS - the live marker for whether a network actually offers FT. The FT key
hierarchy (KDF-Hash-256 with labels FT-R0/FT-R1, PMK-R0/R1 and their names
per 802.11r clause 13) is implemented and verified in
`ember/tools/aicwf-ft.c` against FIPS 180-2, RFC 4231 and independently
generated cross-vectors; run `sh ember/tests/aicwf-ft.sh`.

Not implemented yet: SAE-FT authentication on roaming, PMK-R0 derivation
wired to the live SAE PMK, and the reassociation exchange with the FTIE.
The vendor firmware advertises SM_FT_AUTH_IND/RSP in its message enum but
this vendor drop carries no handler or struct for them, so host-side
control of the exchange is the planned route.

## Host-managed 802.11k/v boundary

The [AIC station transport](aicwf-kv.md) connects actual association IEs,
firmware TX confirmations and connected scans to the common BSD client.
Its software checks do not establish on-air acceptance.
The initial IGTK IPN restriction above also applies to protected k/v.
The independent [BIP verifier](aicwf-bip.md) is not integrated into the driver.

## Protocol provenance

The wire layout was inspected in Radxa's AIC8800 SDIO driver at revision
[`d13d07963cd15d731e2895e8288a04cca6152ac9`](https://github.com/radxa-pkg/aic8800/tree/d13d07963cd15d731e2895e8288a04cca6152ac9/src/SDIO/driver_fw/driver/aic8800/aic8800_fdrv):
`lmac_msg.h`, `lmac_mac.h`, `rwnx_msg_tx.c`, `rwnx_msg_rx.c`, `rwnx_tx.c`,
`rwnx_rx.c`, `rwnx_rx.h` and `aicwf_sdio.c`. The BSD implementation uses those protocol facts; it does
not import the GPL Linux driver. Keep the vendor firmware's redistribution
terms separate from the host driver's licence.
