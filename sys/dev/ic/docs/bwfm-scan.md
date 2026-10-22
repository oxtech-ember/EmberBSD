# Wi-Fi scan channels and signal quality

A FullMAC radio scans channels independently of net80211's current channel.
`bwfm` must retain the primary channel supplied by each firmware BSS record.
Previously it started from the host's current channel and relied on a DS
parameter IE to correct it. A 5 GHz beacon may omit that IE, leaving the BSS
on an unrelated channel and affecting access-point selection and joining.

The driver now uses the firmware control channel, or decodes its versioned
20/40/80/160 MHz chanspec when that field is absent. Unsupported encodings and
channels absent from the driver's channel table are not inserted as valid
scan results. The firmware supplies regulatory channel availability; this
change does not select a country or enable otherwise unavailable channels.

Firmware RSSI is signed little-endian 16-bit dBm. The legacy NetBSD scan ABI
uses unsigned signal quality. `bwfm` now exports `dBm + 100`, clamped to
1–100, consistent with the AIC8800 driver. For example, -62 dBm becomes 38
instead of wrapping to 194. This field is quality, not a dBm reading.

Run the metadata regression on the build host or on EmberBSD:

```sh
sh ember/tests/bwfm-scan.sh
```

Its 166 assertions cover cross-band records, primary subchannels in wide
channels, legacy and current firmware encodings, malformed metadata, and
monotonic RSSI ranking. Passing this check is separate from hardware scan,
association and traffic acceptance.

This correction does not implement 802.11r Fast Transition or 802.11v BSS
Transition Management. The published `bwfm` path still disables autonomous
firmware roaming and WNM transitions. See [SAE and PMF](bwfm-sae.md) for the
separate WPA3-Personal implementation and its firmware limits.

## Hardware check


Current physical scope and source pins are in [DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/bcm2712-cm5/interfaces.md).

This is a primary-channel and unsigned-quality metadata correction. It does not
prove that metadata caused every earlier band selection; AP steering and radio
placement remain separate. The BSD client's status may report `freq=0`; verify
the matching BSS and firmware chanspec rather than treating zero as absence of
5 GHz support.
