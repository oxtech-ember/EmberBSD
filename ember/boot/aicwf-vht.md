# AIC8800D80: VHT80 and roaming status

The Zero 3W target is `802.11ac/k/v, 1x1, 80 MHz`.
The current EmberBSD driver does not provide that complete combination.
Hardware capability, firmware capability and accepted driver behavior are separate results.

| Component | Current evidence | Remaining work |
|---|---|---|
| Radio | AIC8800D80 supports one spatial stream and channels up to 80 MHz | Verify the selected board, antenna and regulatory channels |
| Firmware | A saved Zero 3W report has VHT support, one stream and an 80 MHz PHY limit | This does not establish the negotiated association width |
| `aicwf` PHY setup | `ME_CONFIG` enables HT MCS 0-7 and limits width to 40 MHz; VHT is disabled | Implement and accept complete VHT capabilities and data transfer |
| 802.11k | Full host Neighbor Report exchange is not available | Association capabilities, protected actions and complete station transport |
| 802.11v | Reconnection after loss is not BSS Transition Management | Candidate handling, connected scans, real TX completion and directed transitions |
| WPA3/PMF | SAE/H2E, required PMF and protected SA Query have separate physical checks | Repeat those checks with VHT and the completed k/v path |

See the [SAE guide](aicwf-sae.md) and [board results](../boards/orange-pi-zero-3w.md).
The `bwfm` results on Pi 5 and CM5 do not validate the AIC firmware protocol.

## Firmware facts

Protocol facts come from the pinned
[Radxa SDIO driver](https://github.com/radxa-pkg/aic8800/tree/d13d07963cd15d731e2895e8288a04cca6152ac9/src/SDIO/driver_fw/driver/aic8800/aic8800_fdrv).
Relevant files are `hal_desc.h`, `lmac_msg.h`, `lmac_mac.h`, `rwnx_msg_tx.c`
and `rwnx_mod_params.c`.
The native BSD driver uses protocol facts; it does not import Linux implementation code.

Firmware `0x06090101` reported PHY `0x5ee24111` and features `0x01e877d7`
on a Zero 3W.
The vendor definitions decode PHY bits 11:8 as one stream.
Bits 25:24 are bandwidth value 2, which means 80 MHz.
Feature bit 9 reports VHT; bit 13 reports management-frame protection.
These are firmware-reported limits, not measured traffic or negotiated rates.

The 112-byte `ME_CONFIG_REQ` carries these fields:

| Offset | Field |
|---|---|
| 32 | VHT capability word |
| 36 | RX VHT MCS map |
| 40 | TX VHT MCS map |
| 102 | Maximum PHY bandwidth |
| 103 | HT enable |
| 104 | VHT enable |

A one-stream MCS 0-9 map is `0xfffe` in each direction.
Each remaining two-bit entry must mark the corresponding stream unsupported.
A zero map announces MCS 0-7 on all eight streams.
Setting only the VHT-enable byte would therefore advertise incorrect capabilities.

The vendor also leaves the initial 64-byte PHY block in `MM_START_REQ` zeroed.
That zero block alone does not explain the previous VHT data failure.
The existing comment about association with broadcast-only traffic has no
preserved request or packet capture proving its cause.

`SM_CONNECT_IND` contains actual request/response IEs and channel width.
The pinned ABI places IE lengths at 14 and 16, IE bytes at 20,
width at 826 and the first center frequency at 828.
Validate indication lengths before using any fixed offset or IE length.
Expose actual association information before treating an AP status label as proof.

## Acceptance after implementation

Use a recoverable board with exclusive access to its network interface.
Record the source revision, kernel, DTB, firmware hash and AP configuration.
Check one stream, the negotiated VHT IEs, width and center frequency.
An 80 MHz channel is subject to the AP and regulatory configuration.

Check IPv4 unicast in both directions, ARP, DHCP and file hashes.
Include sustained traffic so aggregation and receive reordering are exercised.
Run `iperf3` against a wired peer in both directions.
Record duration, retries and drops alongside throughput.
Application throughput is not the advertised PHY link rate.
Repeat WPA2-CCMP and SAE with required PMF on the same radio channel.

For k, require a real Neighbor Report response associated with the current peer.
For v, test accepted and rejected BTM requests, candidate selection and both transition directions.
Check traffic continuity and measure the interruption instead of assuming seamless roaming.
Test stale generations, wrong peers, oversized frames and protected-frame replay in software.
Over-the-air PMF injection and long-duration tests remain separate acceptance steps.
