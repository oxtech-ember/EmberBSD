# RTL8822CE Wi-Fi 5 experiment

This manual experiment profile runs the native RTL8822CE station with HT and
VHT on 5 GHz. It is not part of the ordinary kernel profiles. It complements
the staged [driver proposal](rtl8822ce-proposal.md), whose earlier sections
record how the HT path was introduced.

## Controlled boot boundary

Build the separate `EMBER64_CIX_WIFI5_DFS64` kernel profile. It sets
`RTW88_VHT_EXPERIMENT` and `RTW88_DFS64_EXPERIMENT`; both are compiled out
when `RTW88_RFK_EXPERIMENT` is set. Keep the working kernel and the default
and recovery menus. Stage the experimental kernel without changing their
selection and boot it manually. A failed trial returns to the working kernel
by an ordinary reboot.

## Profile

- Rates: HT20 MCS0-7, and VHT at 40 or 80 MHz with MCS0-9 on one spatial
  stream, or on two when this boot's firmware hardware-feature report is
  2T2R (two streams, two antennas). The negotiated maps are the
  intersection with the AP's, so an AP with fewer streams or MCS0-7 still
  negotiates VHT. No STBC or beamforming. Firmware rate adaptation uses
  rate ID 5 for HT20, 10 for one VHT stream and 9 for two, with the
  committed width in RA_INFO. It may transmit with the short guard interval
  when the AP receives it at the committed width (HT SGI20 for HT20, HT
  SGI40 at 40 MHz, VHT SGI80 at 80 MHz) and with LDPC when the AP receives
  LDPC (VHT RX LDPC, or HT LDPC for HT20). The VHT Capabilities advertise
  RX LDPC and short-GI reception at 80 MHz; the HT Capabilities stay
  long-GI BCC. The width follows the tuple the AP's HT and VHT Operation
  derive, if the core admits it and can build its TX power program.
- Channels: 36-48 and 149-165. With `RTW88_DFS64_EXPERIMENT`, 52-64 and
  100-144 are listed as passive DFS slave channels. Host TX on them needs a
  current beacon of the selected AP, and CSA, Quiet, beacon loss, a retune
  or a restart withdraw it. No active probe is sent on 52-144.
- Receive: block-ack reordering with windows up to 64 and A-MSDU up to
  3839 bytes ([net80211 BA](../../../net80211/docs/net80211-rxba.md)).
- Transmit aggregation: an originator block-ack session per TID with traffic,
  and A-MPDU for unicast QoS data of an established session on the active
  HT20 or VHT entry. MAX_AGG_NUM is bounded by half the recipient window and
  the AP's maximum A-MPDU length; the start spacing is the AP's. Raw, fixed,
  legacy and protection-limited frames are never aggregated.
- Protection: HT operation words 0, 4 and 6, and on 5 GHz 3 and 7,
  without RTS or CTS-to-self. Beacons may change the word within that set;
  `htbcn` showed the AP moving between `0x0007` and `0x0004`.

The deliberate deviations (Basic VHT-MCS set, protection mode 3, one PPDU
per channel access) are described in
[net80211 VHT](../../../net80211/docs/net80211-vht-ies.md).

## Diagnostics

`hw.rtwce_stats` reports the negotiation and data path as numbers:
`htprep`, `htoffer`, `vhtadmit`, `htresp`, `htreset`, `assocfail`,
`htstate`, `runreq`, `htrun` and `runpass` for preparation, offer, admission,
response, beacon resets and the RUN worker; `htbcn` for the last beacon
reset: its clause (`IEEE80211_HT20_BEACON_*` in `ieee80211_ht20.h`), HT
Capabilities octets 0-3 and HT Operation octets 0-5 of the beacon and of
the committed contract, and the first differing Basic HT-MCS octet; `raphy` and `tuple` for the
committed RA entry and channel tuple, with `sgi` for its SGI_EN bit;
`txrate` for the encoded descriptor seed; and `txba` (requests, accepts, refusals, timeouts, DELBAs, invalid
responses, last status, active TID mask) with `ampdu` (descriptors with
AGG_EN). These are encoding and protocol observations, not air-rate or ACK
reports.

`nformat` counts MPDUs refused by the ingress format policy. It is not a
counter of corrupt data packets. `nfmt` splits it, in order, into nonzero
802.11 version, generic control frame, reserved frame type, four-address
frame and BAR authorization refusal. These five counts sum to `nformat`.
`nctl0` through `nctl15` split generic control refusals by FC0 subtype.
`nctl9` names Block Ack; `nctl2` names Trigger; `nctl5` names VHT/HE NDP
Announcement. These names identify the subtype, not a validated control body
or sender. See the
[802.11 control subtype definitions](https://github.com/freebsd/freebsd-src/blob/main/sys/net80211/ieee80211.h).

Only frames that reach admission enter these counts. The core forwards
MPDUs of at least 24 bytes after FCS removal. Its short-frame exception
forwards exactly shaped 20-byte compressed BARs for TIDs 0-7. Normal short
ACK/CTS frames and other unsupported short control shapes stop before admission.
`nctl8` counts a BAR of at least 24 bytes, outside the supported short shape.
A forwarded short BAR requires an active RX BA session and matching receiver
and transmitter addresses. A session or address refusal increments the fifth
`nfmt` count. A nonzero security descriptor has the same result.
A valid authorized BAR still enters the shared sequence validator. Core integrity failures stop before the MPDU callback. At policy
entry, size and integrity checks take priority over the format classifier.

During TX A-MPDU, Block Ack responses can raise `nctl9` without a failed
association or payload loss. The driver does not consume these frames as
per-MPDU TX status. A total `nformat` from an older kernel cannot establish
which subtype or peer caused it. The counters contain no addresses, keys
or frame payload. They do not change the admission policy. The stats sysctl
uses an 8 KiB process-context heap buffer, including full-width RFK counts;
short reads retain the existing required-size and retry contract.

## Source checks

The RTL8822CE contracts run the default build, the VHT build and the VHT
build with DFS (`ember/tools/rtw88-ra-contract.sh`), and the net80211
contracts execute the shared producers and consumers
(`ember/tools/net80211-rxba-contract.sh`, `net80211-ht20-contract.sh`,
`net80211-vht-ies-contract.sh`). The AArch64 GCC16 `-fanalyzer` target gate
compiles the driver objects in four RFK/VHT combinations. CI also runs the
contracts with AddressSanitizer, UBSan and Valgrind, and with the Clang and
Cppcheck analyzers.

## Hardware results

Orange Pi 6 Plus, one AP (BSS on primary 64, 160 MHz, HT protection mode
`0x0007`), WPA2-PSK CCMP, recorded 2026-10-25 and 2026-10-26:

| Kernel | Result |
|---|---|
| 918e351cf | RUN at 64/58/80 MHz, VHT MCS7 seed, WPA COMPLETED, DHCP, ping |
| ec6609372 | Passive scan visits 52-64 and 100-144; no other DFS AP present |
| e89e8b965 | ADDBA accepted for TID 0; about 370 000 AGG_EN descriptors without errors, drops or resets |
| 702a3f9c5 | SGI_EN committed (`sgi 1`); about 295 000 AGG_EN descriptors without errors or drops; one beacon HT reset (reason 3), recovered without a cold restart |

TCP, 128 MiB per run, timed on the peer from first to last byte. The peer
is itself a Wi-Fi client of the same AP, so the figures are the limit of
that pair, not of the board alone. SGI changes only the board's transmit
direction; the peer-to-board figures are already at the pair's limit:

| Kernel | Board to peer | Peer to board |
|---|---|---|
| ec6609372, no A-MPDU | 55.1 and 54.4 Mbit/s | 64.4 and 65.8 Mbit/s |
| e89e8b965, A-MPDU | 89.2 and 84.4 Mbit/s | 93.1 and 95.5 Mbit/s |
| 702a3f9c5, A-MPDU and SGI | 100.9 and 93.9 Mbit/s | 95.0 and 96.1 Mbit/s |

A 2026-10-28 check with kernel `598860848` used primary 60, center 58 and
80 MHz. All 16 snapshots retained that tuple. During 440 seconds,
`nformat` rose by 44,190: 44,177 Block Ack, 9 Trigger and 4 NDP Announcement
subtypes. All other format reasons stayed zero. The counters establish
subtypes, not control bodies or senders. Six 128 MiB TCP transfers completed,
and ping received 20 of 20 replies. Interface errors and HT reset counts
stayed zero. The existing default kernel was restored after the manual trial.
The cold ADCK/DACK warning remained; RF quality is still unqualified.

Not established: association on 52, 56 or 100-144, other APs, two spatial
streams on the air, general long-duration stability, and automatic default
selection of this opt-in profile.

Cold ADCK/DACK nonconvergence remains advisory and does not qualify RF
quality. Both advisory masks and hard errors print the captured measurement
attempts, pre-compensation values, programmed compensation and last ADC
snapshot with its path and validity flag. This uses existing results and
adds no register reads or calibration attempts. A clean calibration keeps
the short report. Inspect these boot-time records before changing the
calibration sequence; do not rerun calibration on an occupied station.
