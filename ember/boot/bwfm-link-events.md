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
disconnect's type, reason and flags. It records no SSID, address or keys.

Run `sh ember/tests/bwfm-link-events.sh` from the checkout. It compiles the
production station dispatch and event subscription mask, replacing only
the net80211 boundary. The kernel wrapper runs it on the build host.
Hardware acceptance of the corrected kernel is pending; the reproduction
above does not itself validate the fix or explain the AP's removal policy.

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
