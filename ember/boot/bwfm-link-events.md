# Recovering from a firmware Wi-Fi disconnect

The OS owns `bwfm` station event handling. A firmware disconnect must leave
net80211 RUN and notify the host supplicant, even when firmware commands
still work and the old BSSID remains readable. An interface reset script
does not establish driver recovery.

## Reproduced failure

On 2026-10-11 a CM5 with CYW43455 firmware 7.45.265 and the kernel from
`4eb249554d9b41220b0f91bb894d7bd32dc37f14` joined a WPA3/PMF mesh AP on
2.4 GHz. The AP removed the station after 58 seconds. The firmware station
flags lost `BWFM_STA_ASSOC`, but the host remained in RUN and the supplicant
reported COMPLETED. Forced Wi-Fi ICMP probes stopped receiving replies.
The same AP's 5 GHz radio passed a five-minute control run.

The driver subscribed to DEAUTH and DISASSOC but ignored them in its event
dispatch. It did not subscribe to the corresponding peer indications in
station mode. LINK incorrectly used status/reason rather than the LINK
flag to distinguish an up event from a down event. These are independent
ways to leave the host holding a connection the firmware has already lost.

## Change and validation

Station mode subscribes to disconnect requests and peer indications.
LINK direction comes from the firmware flags. Only events for the selected
peer can restart a station in AUTH, ASSOC or RUN; stale previous-peer events,
an already stopped/scanning station and AP mode do not enter this path.
The existing on-demand radio report includes the event count and the last
disconnect's type, reason and flags. It also reads the effective firmware
subscription. The extended GET must request a nonzero mask length; CYW43455
returns length, maximum and mask without overwriting version/command.
It records no SSID, address or keys.

On physical CM5, event dispatch alone did not recover the reproduced loss.
Both extended and legacy readback retained the requested disconnect bits,
but no disconnect reached the event handler. Firmware commands still worked.
The driver therefore also checks the selected peer's firmware association
every five seconds while a station is in RUN. Two consecutive valid replies
without STA_ASSOC trigger the ordinary host scan/rejoin path. Command errors,
short/unknown responses and a changed peer or connection generation reset
the sequence. They do not establish a lost association.

The watchdog only queues work. The existing non-MPSAFE work queue performs
the sleeping query, with at most one poll pending. State-change requests
invalidate an outstanding query before the transition executes. The radio
report distinguishes firmware events, association queries and recoveries.
Detach stops new work, discards queued operations and drains a concrete
barrier before freeing net80211 state or destroying the queue. This also
prevents an outstanding poll from using a detached interface.

Run `sh ember/tests/bwfm-link-events.sh` from the checkout. It compiles the
production station dispatch and event subscription mask, replacing only
the net80211 boundary. `sh ember/tests/bwfm-link-check.sh` exercises the
production watchdog, worker and state transitions with a controlled firmware
boundary and the production station-info layout. It also executes the driver
task dispatcher, detach and producers to check the queue barrier and late
work rejection. Both run through the kernel wrapper on the build host. The original
driver fails the disconnect/recovery checks; corrected code passes, including
ASan/UBSan. Neither the reproduction nor these tests explains the AP's
removal policy.

## Physical CM5 check

On 2026-10-11, an EMBER64 kernel cross-built on macOS with GCC 16.2.0 from
`1123c71918530a86b21122ea58cb3ba0e288f766` ran on Compute Module 5 Rev 1.0,
BCM2712 D0, 4 GiB, with a Waveshare CM5-NANO-B carrier. The existing UEFI
was retained; its source revision is unknown. CYW43455 firmware was
7.45.265, FWID 01-b677b91b. Kernel SHA256:
`42444c607589a48fb6f078877258119a172e7fd5f84676a2e8c89760decacf43`.
The four matching board modules were byte-identical to the installed files.

With the external interface-reset watcher disabled, the AP removed the
2.4 GHz association at 08:10:24 UTC. The driver detected missing STA_ASSOC,
started scanning, and rejoined on 5 GHz with SAE/required PMF. The AP recorded
association at 08:10:47; the first successful Wi-Fi probe followed at
08:11:03. All subsequent probes passed through 08:15:28. The 150-sample
run had 139 replies, nine losses and two samples without a Wi-Fi address.
The association recovery counter increased once, with no query errors.

A separate temporary 2.4 GHz selection reconnected and passed 63 consecutive
probes through 08:19:54; it did not reproduce another unsolicited loss.
After clearing the selection, an 8 MiB upload and download over a verified
Wi-Fi host route matched byte for byte and by SHA256. Temporary routes were
removed, Ethernet management restored, and the interface-reset watcher
remained disabled. There were no SDIO control timeouts. This accepts recovery
of the reproduced stuck connection, not uninterrupted roaming or a long soak.

Evidence hashes: recovery log
`c1dfdfed52925fd2787187903368ed47376402fa2f12434f722da0f85f0d7d1b`,
repeat log `00b1da785e69310ddc7670fbb50f41cb02d04f05e8a24571d1102cb856d2ba46`,
transfer route/counters `fbe99f593283e014551d24b1056b7a2301db54a4af56992830ea17699d73530d`.
Raw logs contain private network configuration and are not published.

## Repeat hardware acceptance

Keep Ethernet management active during acceptance. Use a separate test peer
with a verified host route through Wi-Fi, and the Wi-Fi source address.
Temporarily stop any interface-reset watcher so it cannot hide a failure.
Observe the supplicant, firmware station flags, packets and AP events on a
common timeline. Restore temporary routes and BSSID selection afterward.
Do not publish private network configuration or raw root `ifconfig` output.

## Reference implementations

The event semantics were checked against
[Linux brcmfmac](https://github.com/torvalds/linux/blob/a5ebb76233b79db01e82e061173cb73c6d2b5c6b/drivers/net/wireless/broadcom/brcm80211/brcmfmac/cfg80211.c)
(`brcmf_is_linkdown`, `brcmf_notify_connect_status`) and
[OpenBSD bwfm](https://github.com/openbsd/src/blob/c27e9422e69bb315a6b6c6fbc4913e2a96c50796/sys/dev/ic/bwfm.c)
(`bwfm_rx_event_cb`). Existing Broadcom/OpenBSD authorship and the inherited
licence remain in the source. No firmware implementation is inferred from
the host-side references.
