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

- Rates: HT20 MCS0-7 and VHT one spatial stream MCS0-7 at 20, 40 or 80 MHz,
  BCC, no STBC or beamforming. Firmware rate adaptation may transmit with
  the short guard interval when the AP receives it at the committed width
  (HT SGI20 for HT20, HT SGI40 at 40 MHz, VHT SGI80 at 80 MHz); the station
  does not advertise short-GI reception. The width follows the
  tuple the AP's HT and VHT Operation derive, if the core admits it and can
  build its TX power program. Firmware rate adaptation uses rate ID 5 for
  HT20 and 10 for VHT, with the committed width in RA_INFO.
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
- Protection: HT protection modes 0 and 6, and on 5 GHz 3 and 7, without
  RTS or CTS-to-self. Beacons may change the mode within that set.

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

Not established: association on 52-60 or 100-144, other APs, two spatial
streams, MCS8-9, LDPC, long-duration stability and the default boot
selection.
