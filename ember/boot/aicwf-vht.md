# AIC8800D80: VHT80 and roaming status

The Zero 3W target is `802.11ac/k/v, 1x1, 80 MHz`.
The current EmberBSD driver does not provide that complete combination.
Hardware capability, firmware capability and accepted driver behavior are separate results.

| Component | Current evidence | Remaining work |
|---|---|---|
| Radio | Zero 3W has a physical WPA2 VHT80 check with one spatial stream | Other boards, antennas and regulatory channels need separate acceptance |
| Firmware | Connection metadata and the AP independently confirm 80 MHz with the checked revision | Accept other firmware revisions separately |
| `aicwf` PHY setup | `ME_CONFIG` enables VHT MCS 0-9, one stream and at most 80 MHz for the checked firmware ABI | WPA2 VHT80 unicast is accepted; SAE/PMF and longer loads remain separate |
| 802.11k | Real request ACK and an empty AP response are physically checked | A nonempty neighbor list and coherent RRM advertisement |
| 802.11v | Real query ACK and connected-scan continuity are physically checked | Physical BTM acceptance/rejection and directed-transition checks |
| WPA3/PMF | SAE/H2E, required PMF and protected SA Query have separate physical checks | Support nonzero initial IGTK IPNs, then accept VHT80 with PMF and the completed k/v path |

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

## VHT implementation

The driver gates VHT80 on firmware `0x06090101`, VHT feature bits, PHY bandwidth
and spatial-stream limits, and the available 5 GHz band. Other combinations
retain the existing HT40 request. Successful connection indications report
bounded VHT IEs and firmware channel metadata. Capability rates are not
presented as negotiated link rates. The net80211 channel ABI is unchanged.

The production builder and decoder pass 1703 ASan/UBSan checks.
The management-frame suite still passes 79 checks. A separate source review
checked the request and indication layouts against the pinned vendor ABI.
The physical WPA2 result below provides a separate data-transfer check.

## Software validation

The management-frame fix in `8a3e2338d` passes 79 source-derived checks with
ASan/UBSan. The tests exercise the production RX dispatcher and BSD client
initialization. The previous source fails the corresponding ABI, action-code
and bounds regressions. Existing SAE, PMF and link-recovery contracts also pass.

GCC16 built the complete `EMBER64` kernel, four matched modules and board DTBs
from `df0fdc105b9336eda2eeb3c7dcf20be3aaab9196`. The portable kernel suite passes.
Target static analysis adds no new functional findings; one helper has a
new variable-scope style warning. Inherited
findings and GCC path-complexity limits remain. This is a host build and software
validation. It does not establish completed k/v or replace the hardware checks.

## Physical VHT80 check

An Orange Pi Zero 3W with 4 GiB RAM ran the `df0fdc105` kernel and its four
matched modules on 2026-10-12. Its existing DTB and NPU configuration were
preserved. The radio used the firmware revision above.

Both firmware metadata and the AP confirm 802.11ac, one stream and 80 MHz
under WPA2-CCMP. The primary frequency was 5260 MHz; the center was 5290 MHz.
The station advertised RX/TX MCS maps `0xfffe`. The AP used a 160 MHz radio
channel, but this station correctly negotiated 80 MHz.
An 8 MiB file transfer passed SHA256 verification in both directions.
The observed data path was not limited to broadcast frames.

An initial 20-second `iperf3` run measured 95.5 Mbit/s from the board and
70.2 Mbit/s to it. Both the board and the Mac peer used Wi-Fi.
A separate 30-second run on `6b9ebde32c47` measured 97.6 Mbit/s from the board
and 65.8 Mbit/s to it, using one TCP stream in each direction.
Both peers again used Wi-Fi. This is application throughput through two
wireless links, not a PHY rate or a measurement against a wired peer. The earlier 2.4 GHz HT40 baseline
measured 61.1/60.1 Mbit/s under different radio conditions.

Two further 60-second TCP checks passed after authentication recovery.
An 8 MiB file check overlapped part of the reverse load. Input errors stayed
at zero; the output-error counter stayed at the value of 18 recorded before
the load. Neither test added interface errors. These short checks do not
establish long-duration reliability.

SAE/H2E authentication succeeds on the same AP, but the connection then
fails when its IGTK has a nonzero initial IPN. The kernel reports
`nonzero initial IGTK IPN is unsupported`; the supplicant reports
`Failed to configure IGTK`. This existing key-installation limit remains.
The board returned to its saved WPA2 policy automatically. No AP settings
or credentials were changed. VHT80 traffic with SAE/PMF is not accepted.

The supplicant and CLI in that VHT80 trial were version 2.11. A Neighbor Report request
returns `FAIL`; this CLI does not include `wnm_bss_query`. The kernel also
returns no k/v capabilities and rejects incomplete station operations.
An AP's `k` label therefore does not establish a working host exchange.
The new [station transport](aicwf-kv.md) addresses that software boundary.
It requires a matching client build and separate physical acceptance.

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
