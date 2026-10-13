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

A CM5 with CYW43455 7.45.265 on `72c00a3bbc19` returned distinct 2.4 GHz
channels 3/8/11 and 5 GHz channel 60 in a mesh network. Signal quality was
32/36/39 and 46/26 respectively, without signed-value wraparound. WPA2 and
SAE/H2E with required PMF both connected on 5 GHz. An 8 MiB transfer in each
direction passed byte/hash comparison with the peer route pinned to bwfm0.
Three SAE reconnects are recorded in the [board receipt](../boards/compute-module-5.md).

The board had also joined 5 GHz before this fix. The test establishes a
metadata correction and working 5 GHz, not that this defect alone caused
every earlier 2.4 GHz selection. Radio placement and access-point steering
remain separate factors. The BSD client's `status` may report `freq=0`;
verify the matching BSS and firmware chanspec instead of treating zero as
evidence that 5 GHz is unavailable.
