# Bounded VHT wire values

`ieee80211_vht_ies.c` owns the shared VHT TLV decoder extracted from
`aicwf_assoc_ies_parse()`. The AICWF adapter maps the result to its existing
association metadata. Firmware envelopes, capability qualification and
`ME_CONFIG` construction remain in the AICWF driver.

The pure codec allocates no memory and retains no input pointers. It has no
station, driver, clock or radio dependency. It does not enable VHT,
establish a rate, or grant channel/DFS transmission permission. The one
interpretation it owns is the operating-channel derivation below.
The existing 73-byte raw PHY snapshot and its lifecycle are separate.

## Values and decoding

`struct ieee80211_vht_cap` retains the capability info word, RX MCS map,
RX highest field, TX MCS map and TX highest field. Highest fields preserve
all 16 wire bits, including reserved and extended-NSS encodings.
`struct ieee80211_vht_operation` retains width, both center segments and
the basic MCS map. `struct ieee80211_vht_ies` has separate presence flags.
Absent values are zero; a zero map alone does not establish presence.

`ieee80211_vht_ies_parse(out, data, len)` reads a complete bounded TLV window.
It returns zero on success and minus one on failure. VHT Capabilities
(element 191) must have 12 payload bytes. VHT Operation (element 192) must
have five. Duplicate VHT elements and malformed framing fail. Other complete
TLVs are skipped. Empty input succeeds with absent values. NULL data is
valid only with zero length. A NULL output fails. Failure clears the entire
output so a valid prefix or previous result cannot become fresh evidence.
Input and output for decoding must not overlap.

Decoding is structural. It preserves unsupported or reserved semantic
values for observation. Capability width-set zero excludes 160 MHz and
80+80 MHz; it still includes the base VHT widths, including 80 MHz.
Operation width zero refers to HT 20/40 MHz. Neither value means that this
codec selected a 20 MHz link. Apart from the channel derivation below,
interpreting these values belongs to callers.

The wire layouts were checked against FreeBSD `ieee80211.h` and
`ieee80211_vht.c` at
[`d2145e9b96448da34d3cfede24c509af6d830c92`](https://github.com/freebsd/freebsd-src/tree/d2145e9b96448da34d3cfede24c509af6d830c92/sys/net80211).
The bounded decoder originates in EmberBSD's BSD-licensed AICWF helper.
No FreeBSD station policy or capability template is imported.

## Explicit payload encoding

`ieee80211_vht_cap_encode(buf, len, cap)` writes exactly 12 payload bytes.
`ieee80211_vht_operation_encode(buf, len, operation)` writes exactly five.
They do not write TLV headers or synthesize capability defaults. Callers
supply every field. Both return zero on success and minus one for NULL
pointers or insufficient output space. Failure changes no output. Extra
space is untouched. Each encoder snapshots its input before writing, so
input and output may overlap. Wire loads and stores support unaligned bytes.

## MCS map mechanisms

Each map contains eight two-bit entries, starting with NSS 1 in bits 0..1.
Entries 0, 1 and 2 support MCS 0..7, 0..8 and 0..9, respectively.
Entry 3 means unsupported. `ieee80211_vht_mcs_intersect(a, b)` processes
all eight entries. Unsupported dominates; other pairs use minimum support.
Unsupported holes do not terminate the map. An all-unsupported result
(`0xffff`) is a valid empty candidate.

`struct ieee80211_vht_mcs_maps` contains `tx_map` and `rx_map` only.
`ieee80211_vht_mcs_candidates(local, peer)` returns this structure by value.
Its TX map intersects local TX with peer RX. Its RX map intersects local RX
with peer TX. Inputs and outputs are values, with no pointer lifetime or
overlap requirements. Highest-rate fields cannot influence this interface.

`ieee80211_vht_mcs_meets(supported, required)` checks every required entry.
Required entry 3 imposes no requirement. Other required entries need
supported MCS at least that large; unsupported entry 3 cannot satisfy them.
Callers select the direction and decide whether a BSS basic map applies.
These helpers establish no station validity, PHY rate, bandwidth, SGI,
MPDU policy or transmission permission. They do not qualify a radio.

The entry encoding and TX/RX pairing were checked against the pinned
FreeBSD `ieee80211_setup_vht_rates()` reference above. No station policy
or highest-rate calculation is imported.

## Operating channel and association candidate

`ieee80211_vht_channel_derive(ies, len, primary, out)` turns one bounded
HT/VHT Operation window into `struct ieee80211_vht_channel`: primary,
center and width (40 or 80), all IEEE channel indices. It follows IEEE Std
802.11ac-2013 Table 8-183x and §10.39.1. HT Operation supplies the primary,
which must equal the caller's current channel, the secondary offset (above
or below) and a set STA channel width bit. VHT width 0 is the HT 40 MHz
channel; its segments are reserved and ignored. Width 1 is 80 MHz at
segment 0. A nonzero segment 1 is accepted only as the 160 MHz center
eight channels away or as a secondary 80 MHz more than 16 channels away;
the result is the primary 80 MHz in both cases. The deprecated widths 2
(160 at segment 0) and 3 (80+80) also yield the primary 80 MHz. The center
must be an IEEE 802.11-2016 Annex E 5 GHz block (40: 38..159, 80: 42, 58,
106, 122, 138, 155), and for 80 MHz the secondary offset must name the
primary's own 40 MHz partner. VHT20, reserved widths, 2.4 GHz and every
inconsistent value fail and clear the output. A station with Supported
Channel Width Set 0 may operate on the primary 80 MHz of a 160 or 80+80
BSS: §10.39.1 only requires that its PPDUs stay inside the primary
segment, and the AP knows that set. Width 1 with a segment 1 that is
neither 0, eight channels away nor more than 16 away is rejected, as is
every primary from 169 to 177; Linux treats such odd values as plain
80 MHz, so this is a deliberate strictness trade-off.

`ieee80211_vht_assoc_candidate(local, ies, len, chan)` accepts only the
strict local NSS1 MCS0-7 profile, a 73-byte or smaller window, mutual MCS
maps, HT 40 MHz support with MCS0-7, and a window that derives exactly
`chan`.

### Deviation: HT protection mode 3

The HT Operation protection envelope shared by HT20 and VHT accepts
operation words 0, 6 and, deliberately and on 5 GHz only, non-HT mixed
mode 3 (`0x0003`, or `0x0007` with non-greenfield stations present). The
observed AP above advertises `0x0007`. No RTS/CTS or CTS-to-self exchange
is added. The upstream rtw88 driver requests RTS only through `use_rts` or
the length threshold and does not read the HT Protection field; this
deviation is not a claim of conformance with the base-standard HT
protection rules (IEEE Std 802.11-2016 §10.26.3), which 802.11ac §9.23.6
applies to VHT stations. It rests on four preconditions of this stack:
5 GHz, where every non-HT station is OFDM and defers on the L-SIG length of
mixed-format HT and VHT PPDUs; single unaggregated MPDUs; no TXOP bursts;
and no length-triggered RTS (the RTS threshold exceeds every MPDU).
Non-member protection (mode 1), mode 2 without non-greenfield, OBSS non-HT
presence and every other operation selector remain closed. The HT
Operation in beacons is compared byte for byte: an AP that changes its
mode-3 word (for example to `0x0004`, or adds OBSS `0x0010`) withdraws the
HT/VHT profile and the station reassociates.

### Deviation: Basic VHT-MCS set

IEEE Std 802.11ac-2013 §10.39.7 forbids association unless the station
supports every NSS/MCS tuple of the AP's Basic VHT-MCS and NSS set. The
candidate deliberately does not enforce that set. Real APs often list their
own full support there; an observed 160 MHz AP on primary 64 sets
`0xff9a` (NSS1, NSS2 and NSS4 MCS0-9 and NSS3 MCS0-8, i.e. a four-stream
requirement), which would keep every NSS1 MCS0-7 station
out of VHT. The association request offers VHT only when
`ieee80211_vht_basic_rates_supported()` finds a nonempty legacy Basic Rate
Set that the station supports, so group-addressed frames from such an AP
remain non-HT (§9.7.5.3). The local TX map against the peer RX map and the
local RX map against the peer TX map are still required. The committed
Basic VHT-MCS map stays in `ni_ht20.vht.operation` and is still compared in
every beacon. The station asks the driver through
`ic_vht_admit(ic, chan, cap)` for that exact channel before both the
association request offer and the response commit. The committed node keeps
the channel in `ni_ht20.vht.channel`. The offer records the channel derived
from the scan observation; a response commits VHT only when it derives the
same channel and the provider returns the same capability, otherwise the
association stays HT20. A later beacon whose VHT Operation differs byte for
byte from the committed one, is missing, repeats or has another length
withdraws the HT/VHT profile, as a changed HT Operation does. This is
strict: a 160 MHz AP that narrows segment 1 also resets the profile. A
node without VHT ignores VHT Operation framing in beacons. None of this
grants regulatory, DFS or radio permission; the driver owns admission.

## Software contracts

Run the real source and its existing firmware consumers with GCC 16:

```sh
CC=gcc-16 sh ember/tools/net80211-vht-ies-contract.sh
CC=gcc-16 sh ember/tests/aicwf-vht.sh
CC=gcc-16 sh ember/tests/aicwf-sta.sh
CC=gcc-16 sh ember/tools/net80211-vht-ies-analysis.sh
```

The contracts cover all payload length values, truncation boundaries,
duplicates, unrelated TLVs, NULL inputs, failure clearing, raw bit retention,
byte-exact roundtrips, overlapping encoder inputs and output guards.
The AICWF checks execute the production builder and decoder adapter.
The MCS contract exhausts all 16 entry pairs at each of eight NSS positions
for intersection and requirements. It checks asymmetric directions,
unsupported holes, empty candidates and the highest stream position.
The station contract links the shared source into the actual extracted
management path. `TEST_RUNNER` accepts an executable wrapper, including a
Valgrind Memcheck wrapper. `VHT_IES_TEST_CFLAGS`, `AICWF_VHT_CFLAGS` and
`AICWF_STA_CFLAGS` select compiler diagnostics or sanitizers.
These userland contracts do not qualify a kernel, firmware or physical radio.
