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

See the [SAE guide](aicwf-sae.md) and [board results](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/a733-zero3w/README.md).
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

## Receive aggregation

`aicwf` uses the shared [net80211 A-MSDU parser](../../../net80211/docs/net80211-amsdu.md).
The parser checks the complete aggregate before the driver allocates Ethernet packets.
A malformed final subframe rejects the entire aggregate.
An allocation failure frees the complete unpublished packet list.
The Ethernet writer preserves the receive interface and alignment.
It accepts RFC1042 and bridge-tunnel SNAP for both individual and aggregate packets.
Individual EAPOL retains its original path; aggregate EAPOL follows the common rejection policy.

The declared HT capability `AICWF_HT_INFO` is `0x016f`.
Its large-A-MSDU bit is clear, so the aggregate limit is 3839 bytes.
The VHT request selects the 3895-byte MPDU profile, which fits this bound.
Larger aggregates and subframes outside the common STA address/MTU policy are rejected.
Firmware descriptor decoding, decryption offsets, TID, sequence and reorder dispatch stay in the chip driver.

The existing native RX BA consumer cannot directly replace the AIC reorder path.
It owns raw 802.11 mbufs, native HT20 admission, ADDBA state, and node/key epochs.
The AIC firmware negotiates BA and flags packets for host ordering after Ethernet conversion.
A shared BA migration must preserve these different provider contracts.

Run the actual-source RX contract with GCC 16:

```sh
CC=gcc-16 python3 ember/tools/aicwf-amsdu-contract.py --work /path/to/build/obj
```

Set `AMSDU_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'` for memory checks.
Set `AMSDU_CFLAGS=-fanalyzer` for GCC path analysis.
Use `--keep` to retain the extracted source and `compile_commands.json` for Cppcheck or Clang analysis.
Use `TEST_RUNNER='valgrind --leak-check=full --error-exitcode=1'` on a supported host.
Use `--revision PARENT` to reproduce delivery of the valid prefix before this change.
The baseline must compile and then fail the malformed-tail delivery assertion.

The check compiles the real RX dispatcher, Ethernet writer, list ownership functions and common parser.
Only mbuf allocation, input queues, management sinks and reorder services are modeled.
The extraction suppresses one Cppcheck const-context suggestion because the common reader callback requires `void *`.
It checks limits of 3839 and 3840 bytes, malformed tails, allocation rollback and descriptor dispatch.
These host checks do not qualify target kernel integration, firmware cryptography, BA timing or a physical radio.
Valgrind does not support the macOS arm64 host; use GCC16 ASan/UBSan there.
Implementation and contract changes are AI-assisted.

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


Current physical scope and source pins are in [DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/a733-zero3w/interfaces.md).

Negotiated primary/center frequency, stream count and MCS maps must agree with
firmware and peer observations. Application throughput through two wireless
links is neither a PHY rate nor a wired-peer benchmark. Finite transfers do not
qualify endurance.

Nonzero initial IGTK IPN remains rejected with `nonzero initial IGTK IPN is
unsupported`; the supplicant can report `Failed to configure IGTK`. SAE
 authentication alone does not establish usable VHT80 with required PMF.
An AP's k/v capability label does not establish a host exchange. The
[station transport](aicwf-kv.md) requires matching client headers and its own
Neighbor Report/BTM acceptance.

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
