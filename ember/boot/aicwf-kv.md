# AIC8800D80 station transport

The native `aicwf` driver connects Neighbor Reports and host-managed BSS
transitions to the common BSD wpa_supplicant backend.
The transport uses `IEEE80211_IOC_STA_MGMT` version 2.
It requires D80 firmware `0x06090101` and a matching kernel and WPA client.
This document describes the implementation and software checks.
Physical k/v acceptance on Zero 3W is still pending.

## Security and ownership

The transport supports WPA2-PSK with CCMP and PMF disabled, or external SAE
with CCMP and required PMF. Other security profiles retain ordinary scanning.
The existing [SAE limits](aicwf-sae.md#current-boundary) remain in force.
In particular, the firmware key command cannot set a nonzero initial IGTK IPN.
Do not disable PMF or omit its replay boundary to bypass that limitation.

The host selects the candidate through upstream RRM and WNM policy.
The driver submits a directed association and validates the reported peer.
Unexpected successful connection indications do not authorize a host transition.
The pinned firmware interface has no documented WNM-disable readback.
Its autonomous behavior therefore requires a separate physical check.

Management reception checks the current peer, VIF, frame bounds and protection.
Protected actions require the current PTK, successful CCMP status and a fresh PN.
Unprotected reasons 6 and 7 still enter the existing protected SA Query path.
Receiving a BTM request does not itself disconnect the station.
These checks do not replace over-the-air forgery and replay tests.

## Association and transmit completion

GET returns the station address, current peer, frequency and association generation.
It also returns bounded request and response IEs from `SM_CONNECT_IND`.
The client uses the actual IEs to determine negotiated RRM support.
Capabilities are cleared for an associated security profile outside the supported set.

Each management operation has a user cookie and an association generation.
Each firmware transmission also has a monotonically increasing host token.
Tokens are not reused during the firmware lifetime.
A write to the SDIO FIFO is not a transmit acknowledgement.
The real TX confirmation must match the pending token and current association.
Only its ACK status bit produces a positive transmit result.
Cancellation, a profile change or a disconnect retires the pending result.

## Connected scans

A connected scan preserves the current BSS, keys and RUN state.
The driver validates each frequency against the active channel set.
The scan cache admits only bounded frames for the current VIF, channel and scan lifetime.
Scan results retain RSSI and capability information for candidate selection.

The firmware START acknowledgement only confirms command receipt.
The separate terminal scan indication completes the scan.
Cancellation retires the user cookie immediately and requests firmware cancellation.
The CANCEL acknowledgement does not prove that old results have drained.
A replacement scan waits for the terminal indication before reusing the radio.
An error before any FIFO write releases scan ownership.
An attempted write with an ambiguous result retains ownership until completion.
If the terminal indication never arrives, further scans return `EBUSY`.
The current implementation needs a firmware restart to recover that last case.

## Reproducible software checks

Run from the source root on a supported development host:

```sh
sh ember/tests/aicwf-sta.sh
sh ember/tests/aicwf-command.sh
sh ember/tests/aicwf-scan.sh
sh ember/tests/aicwf-scan-channel.sh
sh ember/tests/aicwf-sae-lifetime.sh
sh ember/tests/aicwf-alive.sh
sh ember/tests/bwfm-sta.sh
sh ember/tests/bsd-wpa-cipher.sh
```

The fixtures execute production functions with controlled firmware, queue and ioctl edges.
They cover real TX status, stale tokens, scan cancellation and late completion.
They also cover failures before FIFO publication and scan-cache admission.
The shared client tests exercise actual association, scan and route-event callbacks.
Recovery tests replace the association while teardown or a recovery scan is queued.
The interface lock, association generation and AUTH serial protect both operations.
A stopped interface or a newer join cancels the old recovery task.
ASan and UBSan check the host executions. They are not Valgrind Memcheck results.
Kernel translation-unit analysis uses the actual target compilation configuration.
Inherited findings and analyzer path-complexity limits must remain visible in its receipt.

## Physical acceptance

Build the kernel, matching modules and WPA clients from a recorded clean revision.
Preserve the board's working DTB, boot image, module tree and configuration.
Arrange timed recovery before testing the only network interface.
Keep credentials and complete diagnostic logs outside source control.

Use the common [k/v client commands](bwfm-kv.md#reproducible-checks) with `aicwf0`.
The `bwfm-sta-ioctl.c` tool tests the shared ABI on either driver.
Build it with the same EmberBSD net80211 headers as the installed client.

Require a real Neighbor Report response from the current peer.
For BTM, require the request, host response, firmware ACK and target association.
Test both acceptance and rejection, then confirm bidirectional data transfer.
An AP capability label or a successful command submission is insufficient.
Measure the reply gap during each transition and report the security profile.
Repeat with SAE and required PMF when the AP supplies a supported initial IGTK IPN.
Record nonzero-IPN failure separately until the complete PMF path is implemented.

See [radio results and acceptance](aicwf-vht.md#acceptance-after-implementation)
for the VHT80 and traffic checks. Pi 5 and CM5 results do not validate this radio.

## Protocol source

The independent BSD implementation uses wire-interface facts from the
[pinned Radxa SDIO source](https://github.com/radxa-pkg/aic8800/tree/d13d07963cd15d731e2895e8288a04cca6152ac9/src/SDIO/driver_fw/driver/aic8800/aic8800_fdrv).
Relevant structures and handlers are in `lmac_msg.h`, `rwnx_tx.c`,
`rwnx_msg_rx.c`, `rwnx_rx.h` and `aicwf_sdio.c`.
No Linux driver implementation is imported. Firmware licensing remains separate.
