# Intel BE200 Wi-Fi: iwx(4) port proposal (work in progress)

Status (2026-11-08): the BZ/MLD BE200 adapter now opts in to the shared
native net80211 SAE/PMF implementation. See [the integration contract](#native-saepmf-integration-2026-11-08)
for firmware evidence, software verification and remaining bench work.
There is no new hardware acceptance result. The WPA2 measurements and
stage descriptions below describe the earlier port.

Status (2026-11-12): the mode report and the diagnostics below ran on
the MSI (`VHT mode 11ac`, 80 MHz, two streams, MCS 9). Also run there:
the driver follows the AP on an announced channel switch instead of
restarting the interface (one switch within a 160 MHz block, 1.0 s from
the announcement to the new channel, no ping lost). `iwx_csa_rx()` keeps the
announcement from the AP's beacons and action frames (CSA, ECSA, secondary
channel offset, wide bandwidth channel switch); on the firmware's
`CHANNEL_SWITCH_START_NOTIF` `iwx_csa_task()` moves the link when the band
and the channel width stay the same: link inactive, PHY context replaced,
link rates and rate scaling sent again, link active, the order of Linux
iwlmld. The station and its keys stay. It then expects a beacon on the
new channel within two seconds. Anything else (no announcement seen, the
width or band changes, a 6 GHz operating class, a firmware error, no
beacon afterwards) restarts the interface as before and logs the reason.
With the old behaviour a channel change of the AP cost 9 to 11 s of
connectivity on the MSI, about half of it the DHCP client re-validating
the address after the carrier loss. Contracts: `iwx_ht_csa_*()` in
`ember/tools/iwx-ht-contract.sh`, the command order in
`ember/tools/iwx-width-contract.sh`. `iwx_csa_task()` and `iwx_csa_rx()`
themselves have no fixture yet. Run `ifconfig iwx0 debug` to log the
announcement as it is received.

160 MHz VHT is off by default and ran on the MSI with `hw.iwx.ht=3`
(`802.11ac, 160 MHz, 2 spatial streams`, firmware rates VHT MCS 8-9, two
streams, 160 MHz; no throughput figure yet, the measuring peer shared the
same AP): with `ht=3` the driver offers 160 MHz and short GI at 160 MHz
in its VHT capabilities where the channel allows it, takes the 160 MHz
layout from the AP's VHT operation (CCFS1 next to CCFS0, CCFS2 of HT
operation for an AP with Extended NSS BW, or the deprecated width 2;
centres 50, 114 and 163 only), sets the PHY context to 160 MHz with the
eight control channel positions and gives the rate scaling the 160 MHz
rate set for as many streams as the AP receives at that width. With
`ht=2` nothing changes. The association line in dmesg now shows the AP's
three centre channels, its 160 MHz capability bits and whether 160 MHz
is allowed and enabled. Contract: `test_width160()` in
`ember/tools/iwx-ht-contract.sh`.

802.11ax (HE) is written but off by default and **not yet run on
hardware**: with `ht=4` (80 MHz) or `ht=5` (160 MHz), on the MLD firmware
API and the 5 GHz band, the association request carries an HE
capabilities element, unless the NVM lacks 11ax or the firmware forbids
it in the current country (bit 13 of the `MCC_UPDATE` capability word). Its values are those of Linux iwlwifi for a
two-antenna FM radio in station mode, less what the host side does not
do here: no +HTC-HE and OM control (received frames carry no HT control
field), no A-MSDU inside A-MPDU, A-MPDUs no longer than with VHT.
Triggered transmissions and sounding feedback are the firmware's. If the
AP answers with HE capabilities and HE operation and its basic HE-MCS
set fits, the driver sets `he_support` in the MAC context, the packet
extension thresholds and `ack_enabled` in the station, the BSS colour in
the link, and HE mode with MCS 0-11 in the rate scaling, following
Linux iwlmld. Not done: MU EDCA, TWT, spatial reuse, the 256-frame Block
Ack window (64 stays), HE on 2.4 GHz, 6 GHz. `ifconfig` keeps the 11ac
media word, dmesg says `802.11ax`. Contract: `test_he()` in
`ember/tools/iwx-ht-contract.sh`.

A lost AP is noticed (ran on the MSI: an AP that changed channel
without an announcement was found again by the restart, 12.5 s without
connectivity, about 5 s of it scanning and 5 s the DHCP client). The driver
used to answer the firmware's missed-beacons notification with a probe
request through the legacy `ieee80211_send_mgmt()`, which has none, so
nothing happened. `iwx_rx_bmiss()` now sends a directed probe request
after 8 beacons missed in a row (`ieee80211_beacon_miss()`) and restarts
the interface after 16 if the AP stayed silent, logging `lost the AP`.
Contract: `iwx_ht_bmiss()` in `ember/tools/iwx-ht-contract.sh`.

Joining another BSS while associated (roaming,
`IEEE80211_MLME_ASSOC` from `wpa_supplicant`) is done in place.
net80211 makes the node of the new BSS `ic_bss` before the
driver is asked for anything, while the firmware contexts and the state
needed to remove them (`in_phyctxt`, `in_macaddr`, the HT state) belong
to the node being left. OpenBSD's net80211 keeps one `ic_bss` and copies
the new BSS into it; here the driver remembers the node that owns the
contexts (`sc_fw_in`, with a reference) and `iwx_run_stop()` and
`iwx_deauth()` work through it. The usual teardown then removes the old
station, link, MAC and PHY contexts, `iwx_auth()` creates the new ones,
and net80211 is entered as from a scan so that it sends the first
authentication frame, which it otherwise sends only on the way up; the
same holds for a join of the BSS already in use. The node left behind
gets the net80211 cleanup of `ieee80211_sta_leave()` (association state,
pairwise key) without the disassociation event, so user space sees one
association follow another and the link state stays up. From the moment
another state is requested no data frames are sent: `ic_bss` has no key
yet, and the firmware still talks to the old BSS. Limits: the AP left
behind gets no deauthentication frame; with native SAE the join still
restarts the interface; a failed join leaves the station without a BSS
until `wpa_supplicant` picks one. Ran on the MSI between the 5 GHz and
2.4 GHz BSS of one AP: under a second each way, one ping of five per
second lost, no disassociation event, no DHCP exchange. One join
requested 0.9 s after the previous association ended in a firmware
error: the task that answers ADDBA requests was adding an RX Block Ack
session while the station was being removed. The teardown now cancels
what that task and the TX queue task have pending and waits for the one
at work, and neither starts anything once another state is requested;
that fix has not run on hardware. Contracts: the state dispatcher and
the owner of the contexts in `ember/tools/iwx-width-contract.sh`, the
receive channel rule in `ember/tests/iwx-sae.sh`.

New and **not yet run on hardware**: 802.11be (EHT) on 5 GHz, up to
160 MHz, without MLO, punctured channels or 6 GHz, with `hw.iwx.ht=6`
(80 MHz) or `7` (160 MHz). Following Linux iwlwifi/iwlmld: the
association request carries an EHT Capabilities element right after the
HE one (the bytes Linux sends for a two-antenna FM radio on 5 GHz, less
EHT OM control, since our HE element has no +HTC-HE) and no ESS bit; the
AP's EHT Capabilities and Operation are parsed, and EHT is used when
both are there, HE is, the channel is 80 MHz or wider, the AP's EHT
layout is not narrower or 320 MHz, no subchannel is punctured and its
basic EHT-MCS set is within reach. Then the MAC context gets
`eht_support` (in the version-3/4 byte layout of this firmware), the
station the EHT packet extension and, in EHT mode only, the HE and EHT
A-MPDU exponents,
and the rate scaling EHT mode with MCS 0-13, LDPC and the extra EHT-LTF.
The link gets no EHT parameters (none exist there in this firmware's
version); the PHY context keeps no puncturing. Only with MAC_CONFIG
version 2-4 and TLC_MNG_CONFIG version 4, and not where the firmware
forbids 11be in the current country (bit 8 of the `MCC_UPDATE`
capability word). dmesg says `802.11be`, or why EHT was offered but not
used. Contract: `test_eht()` in `ember/tools/iwx-ht-contract.c`.

New and **not yet run on hardware**, found on the MSI: net80211 can
leave a BSS without the driver. Before a scan it sets its own state back
by hand (`ieee80211_setupscan()`). After a deauthentication by the AP
the driver takes the association down and starts the authentication
again, which makes contexts for that BSS anew; these stayed in the
firmware across the scan, and the next authentication added them again: `fatal firmware error`,
`could not add phy context`, a restart, and with it most of a minute
without the network. `iwx_newstate_task()` now removes what is left,
through the node that owns it, before it scans or authenticates.
Also: the current channel follows scan results from the moment
`iwx_scan()` marks the scan, not only in SCAN state, because the scan
command sleeps while the firmware already reports; one association was
attempted on a channel the BSS was not on. And scan results carry the
signal level in dBm (`IEEE80211_SCAN_RSSI_DBM`), so that `wpa_supplicant`
applies its dBm rules when it compares BSSes; before, it took the
driver's units for an unknown scale and would roam on a difference of
two. Contracts: `test_stale()` in `ember/tools/iwx-width-contract.sh`,
`test_dbm()` in `ember/tools/iwx-ht-contract.sh`, the receive path in
`ember/tests/iwx-sae.sh`.

A scan while associated keeps the association (ran on the MSI: 30
networks on both bands in the results, no ping lost, nothing logged).
net80211 leaves the BSS before any scan it is asked for, so
`iwx_ioctl()` takes the scan request (`IEEE80211_IOC_SCAN_REQ`) of an
associated station itself: the firmware scans with the timing OpenBSD
uses for a background scan (at most 120 TU away from the channel of the
BSS at a time), beacons and probe responses of other BSSes go straight
to the net80211 scan table (`iwx_scan_assoc_rx()`), and the end is
reported as a finished scan (`RTM_IEEE80211_SCAN`), without
`ieee80211_end_scan()`. No net80211 state changes meanwhile. The other
requests `wpa_supplicant` makes before a scan (manual roaming, the WPA
setting, the SSID) restart nothing: net80211 asks for no reset while
roaming is manual. If the firmware reports no end within 30 s the driver
says so, aborts the scan and reports what was found. Leaving the BSS while such
a scan runs restarts the interface, with a log line. A scan is refused
while the AP announces a channel switch, and a switch that starts during
a scan is not followed. `wpa_supplicant` here is
built without its periodic background scan, so such a scan starts on
request only (`wpa_cli scan`). Contracts: `test_scan()` in
`ember/tools/iwx-ht-contract.sh`; the request, the end of the scan, the
overrun and the state dispatcher in `ember/tools/iwx-width-contract.sh`;
the receive path in `ember/tests/iwx-sae.sh`. `iwx_scan_assoc_rx()` has
no fixture.

A beacon or probe response is filed under the channel it names for its
BSS: the DS Parameter Set, or, as 5 GHz beacons usually carry no DS
element, the primary channel of HT Operation. net80211 checks only the DS
element, so the firmware's receive channel used to decide; on hardware
`wpa_supplicant` then tried one 5 GHz BSS at three frequencies and timed
out on the wrong ones. `iwx_rx_frame()` now drops such a frame when the
two disagree, counts it in `is_rx_chanmismatch` and, with `IFF_DEBUG`,
logs it like the other scan results it does not file. Contracts:
`test_scan_chan()` in `ember/tools/iwx-ht-contract.sh` and the `offchan`
case in `ember/tests/iwx-sae.sh`.

Status (2026-11-11): `ifconfig iwx0` reports the negotiated mode
(`VHT mode 11ac`, `HT mode 11na`/`11ng`) instead of the first legacy rate;
net80211 has no HT or VHT mode, so the driver supplies the media word
(`iwx_media_status()`, contract `iwx_ht_media()` in
`ember/tools/iwx-ht-contract.sh`). The MCS and width stay in dmesg
(`firmware TX rate ...`). At association the driver also logs why a link
stays on legacy rates, or what the AP offered and whether the channel
allows 40 and 80 MHz; each `MCC_UPDATE` answer logs the country, the
capability word and the number of valid 6 GHz channels. The amd64 module
builds with the SDK Clang without warnings; not yet run on hardware.

Status (2026-10-20): **stage 1 works on the MSI, HT/VHT negotiates**.
The module loads at boot, the BE200 runs firmware `100.3f3ef548.0` with
PNVM `1464be58`, scans 2.4/5 GHz, associates with WPA2-PSK (CCMP)
through `wpa_supplicant`, gets a DHCP lease and carries SSH and internet
traffic with Ethernet unplugged.

HT/VHT (`src/if_iwx_ht.h`, contract `ember/tools/iwx-ht-contract.sh`):
net80211 here has no 802.11n, so the driver adds HT/VHT capabilities to
the association request, reads the AP's answer and programs PHY (20/40/80
MHz), link, STA and TLC itself; WMM is switched on (net80211 no longer
does it, `#if 0`); ADDBA requests are declined (an AP dropped the station
every 15 s while they went unanswered); the country the firmware detects
(LAR) is applied, so 5 GHz channels become active. Checked on two APs:
`802.11ac, 80 MHz, 2 spatial streams` with WMM.

RX Block Ack (A-MPDU reception) works: ADDBA requests are accepted, the
firmware allocates a BAID (`RX_BAID_ALLOCATION_CONFIG_CMD` v2), frames go
through a reorder buffer (`iwx_ht_reorder_*`, host-tested), DELBA and the
firmware's FRAME_RELEASE / BAR_FRAME_RELEASE are handled; A-MSDU inside
A-MPDU is refused because net80211's software CCMP would drop subframes
sharing a PN. A bug that blocked it: management frames in RUN state went
out at CCK 1 Mbit/s on 5 GHz and failed (`iwx_min_basic_ridx()`), so the
ADDBA responses never reached the AP.

TX aggregation works the way Linux iwlmld does it (TX_AMPDU_SETUP_IN_HW):
unicast QoS data of each TID goes to its own firmware queue
(`SCD_QUEUE_CONFIG_CMD` v3, allocated by a task on the first frame), the
firmware negotiates the Block Ack session and aggregates, and BA_NOTIF
frees the acknowledged TFDs.

Measured (200 MB `nc` between the MSI and a Mac):

| Driver | Place | Download | Upload |
| --- | --- | --- | --- |
| HT/VHT, no Block Ack | office, OxTech ch 64, Mac wired | 17-20 Mbit/s | 14-17 Mbit/s |
| + RX Block Ack | office, same | 31 Mbit/s | 17 Mbit/s |
| + TX aggregation | home, Netcraze-1387 ch 149, Mac on the same Wi-Fi | 107 Mbit/s | 160 Mbit/s |

The last row is not the same setup (other AP, the Mac's traffic also
crosses the air), but the gain is far larger than that difference.
`ifconfig iwx0 debug` prints TX/RX/Block Ack counters every 5 s. The link
held 9+ minutes without a drop after these fixes; earlier it dropped while
moving between the 2.4 and 5 GHz BSS (likely the same 5 GHz management
rate bug; not re-verified). A fatal firmware error is still seen during
system shutdown.

Still missing from that earlier stage: detach/unload, hardware crypto,
MFP and RLC_CONFIG v3. Native SAE now has the RX-only crypto/PMF adapter
described below; detach/unload and RLC_CONFIG v3 remain separate port work.

## Native SAE prerequisite audit (2026-11-08)

This historical audit describes the prerequisites before the integration
below. At the audit point, BE200 was opted out of the common native SAE/PMF owner. The module
Makefile does not define `IWX_HT`, but the separate local HT/VHT and Block
Ack implementation is active. Software CCMP alone does not establish the
raw management receive contract needed to advertise WPA3.

The unresolved boundary is RX authentication-byte preservation.
`iwx_rx_mpdu_mq()` subtracts the combined `MIC_CRC_LEN` descriptor field
from every MPDU, and its arguments omit the packet's RADA status. That
does not establish which MIC/MMIE bytes remain available to the common
software verifier. In [Linux v6.18 receive code](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/intel/iwlwifi/mvm/rxmq.c),
`iwl_mvm_create_skb()` removes that combined tail while the CCMP receive
path marks the MIC stripped after hardware authentication. The equivalent
[MLD receive path](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/intel/iwlwifi/mld/rx.c)
also removes the descriptor tail. [OpenBSD iwx revision 1.230](https://github.com/openbsd/src/blob/3ccbab699b92dd6c8c084ef39ef892ce6e52c566/sys/dev/pci/if_iwx.c)
documents firmware-dependent MIC removal without a known capability
discriminator. Rejecting `DEC_DONE` frames or omitting hardware keys is
insufficient evidence that firmware retained all bytes for software PMF.
An adapter needs an explicit raw-frame metadata contract; guessing the
MIC/FCS split from packet length would not resolve this prerequisite.

Once that contract is established, the local integration must also:

- Run private association/action handling through `ic_recv_mgmt`, after
  common PMF verification; the current early action hook accepts plaintext
  ADDBA/DELBA and rejects protected action frames.
- Preserve kernel provenance tags when `iwx_ht_tx_assoc()` replaces an
  mbuf, and tag private action TX with sufficient software-crypto headroom.
- Retire native state before deferred state changes or stop, and check the
  current generation immediately before handing a frame to firmware.
- Disable firmware-originated TX Block Ack negotiation for native SAE
  until firmware can protect its action frames. Host-authenticated RX
  Block Ack and HT/VHT rates are separate capabilities.

The DMA doorbell is the TX ownership boundary: already submitted
ciphertext remains associated with its old key; it is not deferred host
plaintext. This audit does not require recalling those submitted frames.
No SAE capability, hardware deployment, or new hardware result is claimed.
The earlier WPA2 measurements above remain historical results. This source
audit was prepared with OpenAI Codex assistance.

## Native SAE/PMF integration (2026-11-08)

The driver is an RX hardware adapter to the common owner. SAE exchange,
association admission, controlled-port policy, software CCMP transmit,
CCMP replay, BIP/MMIE verification and SA Query remain in net80211 and its
existing supplicant interface. The adapter is enabled for BZ devices using
the MLD firmware API; it does not enable the dormant `IWX_HT` implementation.

### Firmware receive and key contract

The implementation uses the authenticated receive contract in pinned
[Linux iwlmld RX](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/intel/iwlwifi/mld/rx.c),
[key programming](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/intel/iwlwifi/mld/key.c),
[station setup](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/intel/iwlwifi/mld/sta.c),
[RX descriptors](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/intel/iwlwifi/fw/api/rx.h)
and [RX queue synchronization ABI](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/drivers/net/wireless/intel/iwlwifi/fw/api/datapath.h).

- `STA_CONFIG.mfp` and RX-only `SEC_KEY` snapshots install the PTK, GTKs
  and IGTKs. `NO_TX` preserves common software encryption. Firmware gets
  the correct unicast/multicast and MFP flags; common per-TID replay remains
  authoritative instead of copying one PN into every firmware TID.
- Only a protected CCMP MPDU with `SEC_CCM`, `MIC_OK`, a retained eight-byte
  IV, a known installed key and the published hardware epoch receives
  authenticated metadata. The common owner validates that metadata against
  its current key before accepting firmware plaintext. Tag allocation
  failure, missing authentication, stale epochs and bad bounds drop the frame.
- The descriptor's combined MIC/FCS tail is trimmed as specified by the
  authenticated firmware path; it is not guessed into separate components.
  Descriptor versions, actual notification length and optional header padding
  are checked before extraction. Monitor mode retains the descriptor FCS tail.
  Group robust management frames retain MMIE for common BIP verification.
- Key replacement first fences host RX/TX and queued reorder frames, removes
  the previous keys, waits for an opaque-cookie RX queue drain, then installs
  the complete new set. Exact command identity and response bounds are checked.
  Timeout, wrong acknowledgements and ownership changes fail closed. A stale
  notification cannot publish a new epoch or complete a newer drain.

`ieee80211_sae_attach_hw()` defines this shared snapshot/metadata interface.
Its callbacks specify non-sleeping retirement and serialized, potentially
sleeping replacement; failure never makes a partial set usable. The driver
stores no second SAE state machine and no persistent copy of key material.

### Management and aggregation

Association and action handling now runs in `ic_recv_mgmt`, after common
management admission and PMF verification. Association mbuf replacement
copies all provenance tags and handles allocation failure. Private Block Ack
responses use `ieee80211_send_action()` for common crypto headroom, node
ownership and PMF tagging. Deferred state work, TX submission, stop,
RFkill and fatal interrupts check or retire the common generation; combined
RX/error interrupts fence native admission before processing RX.

Native BE200 transmit uses the firmware non-QoS queue and the common
`IEEE80211_SAE_HW_NO_QOS_TX` producer policy. It therefore does not trigger
firmware-originated TX Block Ack actions whose PMF protection is unproven.
This limits native TX aggregation performance, not SAE or PMF security.
Host-authenticated RX Block Ack and HT/VHT rates remain available.
A-MSDU within A-MPDU is explicitly declined in ADDBA responses and rejected
if received with an active BAID. Standalone firmware-deaggregated A-MSDU is
accepted through common CCMP replay: first index, sequence/TID, increasing
subframe indices, PN, key epoch and last-subframe boundaries must agree.

### Software verification and bench remainder

The host fixtures execute production functions with bounded kernel/firmware
service models. They cover shared owner/key transactions, real CCMP/BIP,
management admission and mbuf ownership, descriptor extraction, command
acknowledgements, RX drain, A-MSDU replay and simultaneous fatal/RFkill RX.
Causal mutations test that broken fencing, acknowledgements, MIC admission,
payload bounds and interrupt ordering are rejected. These tests do not run
firmware or reproduce arbitrary kernel scheduling interleavings.

Run from the source root with an explicit compiler:

```sh
CC=clang sh ember/tests/net80211-sae.sh
CC=clang sh ember/tools/ccmp-contract.sh
CC=clang sh ember/tests/iwx-sae.sh
CC=clang sh ember/tests/net80211-bip.sh
CC=clang sh ember/tools/net80211-rxba-contract.sh
CC=clang sh ember/tools/iwx-width-contract.sh
CC=clang sh ember/tools/iwx-ht-contract.sh
```

`SAE_TEST_CFLAGS` selects instrumentation; `IWX_TEST_CFLAGS` can override it
for the adapter fixtures. `IWX_ANALYZE=1` runs Clang analysis and Cppcheck
on their exact generated translation units. The net80211 workflow runs the
adapter alongside existing consumers under Memcheck, source analysis and
ASan/UBSan. The amd64 module and changed kernel units also compile with the
EmberBSD LLVM SDK and target headers.

The broader target analysis also found and fixed unsupported WPA/RSN
pairwise-selector shifts, signed firmware capability masks, invalid monitor
allocation bounds and command-mbuf allocation failure. These paths have
actual-source sanitizer regressions. Whole-target analysis is not claimed
clean: Clang still reports four pre-existing final-shift dead stores in
`sys/sys/bitops.h`; Cppcheck 2.22.0 reports kernel poison/atomic/type-check
macros, correct `size_t` formats and a channel-profile NULL branch excluded
by the validated callers. Those diagnostics remain enabled and recorded.
Scoped production-function fixtures pass Clang analysis and Cppcheck.

Bench acceptance still needs the matching kernel and module, recorded
firmware/PNVM versions, WPA3-only and transition-mode association, SAE retries,
protected/unprotected SA Query/deauth/action injection, CCMP/BIP replay and
PTK/GTK/IGTK replacement, interface stop/restart, RFkill/reset and sustained
traffic including RX aggregation. Capture descriptor/notification traces and
over-the-air frames to verify retained IV/MMIE, firmware key acknowledgements,
drain ordering and protected management transmission on the deployed firmware.
No physical session, flash, deployment or radio test was performed for this change.

A raw software-only PMF receive path without hardware keys remains unproven:
the combined tail field does not identify the MIC/FCS split. Enabling that
alternative, or firmware-originated protected TX BA, requires an explicit
firmware ABI guarantee or correlated descriptor/air captures. This is a
firmware-contract uncertainty, not evidence of a BE200 hardware limitation;
the implemented authenticated RX/non-QoS TX path does not depend on it.
Implementation and regression work was assisted by OpenAI Codex.

## Channel width and station lifecycle

When an HT/VHT station leaves RUN, `iwx_run_stop()` flushes traffic and
marks the MAC disassociated. It does not change PHY width or rate scaling.
This follows the teardown in OpenBSD `if_iwx.c` revision 1.230.
`iwx_deauth()` removes the station before removing its PHY context.
For a new association, `iwx_auth()` creates a fresh 20 MHz PHY context.
An error removing the station stops teardown before PHY removal.

Width changes while associated remain in `iwx_phy_ctxt_task()`: rate
scaling changes before narrowing the PHY and after widening it.

Firmware assert `0x20101A28` can indicate that Tx rate selection permits
40 MHz while the PHY permits only 20 MHz. See the
[upstream developer explanation](https://mail-archive.com/bugs@openbsd.org/msg24406.html).
The teardown contract does not establish why an AP disconnects a station
or demonstrate that all firmware failures are resolved. Physical reconnect
and sustained-operation acceptance remain pending for this change.

Run the source contract from the repository root:

```sh
sh ember/tools/iwx-width-contract.sh
sh ember/tools/iwx-ht-contract.sh
```

The width contract extracts the actual state dispatcher, teardown, auth,
dynamic-width and TLC v3/v4 functions and wire structures. Its userland
transport records the command order and payload. It covers HT40/VHT80
teardown, 20 MHz, monitor mode, station-removal and PHY-add failures,
and separate dynamic narrowing and widening. `CC`, `CFLAGS`
and `TEST_RUNNER` select the compiler, options and execution wrapper.
An optional argument selects another `if_iwx.c` with its sibling headers.

These checks validate driver control flow and submitted command contents.
They do not execute kernel locking, firmware scheduling, or radio traffic.
Module compilation and physical reconnect tests remain separate acceptance
steps. The repair and regression were prepared with OpenAI Codex assistance.

Boot setup used on the MSI:

| Item | Value |
| --- | --- |
| Module | `/stand/amd64/11.0/modules/if_iwx/if_iwx.kmod`, listed in `/etc/modules.conf`, `modules=YES` |
| Firmware | `/libdata/firmware/if_iwx/iwx-gl-c0-fm-c0-100` and `iwx-gl-c0-fm-c0.pnvm` (linux-firmware `iwlwifi-gl-c0-fm-c0-100.ucode`, `.pnvm`) |
| WPA | `wpa_supplicant=YES`, `wpa_supplicant_flags="-B -s -i iwx0 -c /etc/wpa_supplicant.iwx0.conf"` |
| DHCP | `dhcpcd_flags="-qM rge0 iwx0"` |
| Country | `modload -s mcc=XX` (default `ZZ`); replaced by the country the firmware detects |
| HT/VHT/HE | `modload -i ht=N` or, at run time, `sysctl -w hw.iwx.ht=N`: 0 legacy, 1 HT, 2 HT+VHT up to 80 MHz (default), 3 HT+VHT up to 160 MHz, 4 with HE up to 80 MHz, 5 HE up to 160 MHz, 6 with EHT up to 80 MHz, 7 EHT up to 160 MHz (6 and 7 not yet run on hardware). A sysctl change applies from the next association: `ifconfig iwx0 down`, then `up`. For a module loaded by the boot loader, put `hw.iwx.ht=N` in `/etc/sysctl.conf`. A module loaded with `ht=0` attaches without QoS and accepts only 0; mode 0 chosen by sysctl keeps WMM and only stops offering HT |

Bring-up fixes on NetBSD (commits 579e0ec..bcc5a61): raw `CSR_HW_REV` as
prph scratch `mac_id` (the GL ROM ignored the converted value), 94
firmware sections, PCI bus mastering, softc via `device_private()`,
interface attach after NVM, `ifioctl_common()`, re-entrant ioctl lock,
MCC response v8, PHY context v5/v6, receive-channel tracking while
scanning, `IEEE80211_F_SCAN` for firmware scans, MIC/FCS tail stripped
from MPDUs, NULL-safe `free()`, 2 KB RX clusters.

Porting state relative to the original 782-error snapshot:

- All HT/VHT/BA/bgscan/MFP/UAPSD/hw-key paths are compiled out behind
  `#if defined(IWX_HT)` (never defined); station-mode legacy 11a/b/g only.
- NetBSD glue rewritten after if_iwm(4): attach/interrupt setup (MSI/INTx,
  no MSI-X in stage 1), ethercom + if_initialize/if_percpuq/if_register,
  ieee80211_media_init_with_lock, newstate task, init/start/stop/ioctl/
  watchdog signatures, RX via ieee80211_input (no mbuf_list), TX software
  crypto via ieee80211_crypto_encap, EDCA from ic_wme, match table with
  PCI_PRODUCT_INTEL_WIFI7 (BE200) and a GL/FM device-config case
  (fw `iwx-gl-c0-fm-c0-100`, PNVM `iwx-gl-c0-fm-c0.pnvm`, BZ family).
- `if_iwx_compat.h` grew OpenBSD kernel/net80211 shims (taskq/timeout/refcnt
  on NetBSD primitives, fls, flags, HT/VHT constant values, a minimal
  ieee80211_rxinfo, the systq workqueue allocated lazily in attach).
- `modules/if_iwx/ioconf.c` is hand-written (EmberBSD bsd.kmodule.mk has
  no IOCONF support; component arrays must be non-static for modcmd);
  the module carries its own `MODULE()` + `if_iwx_modcmd()`.
- Firmware installed from linux-firmware `intel/iwlwifi/` (main):
  `iwx-gl-c0-fm-c0-100` SHA256
  `f8cc54927ae551025a7550cf2117bdda30dc1f392ee925e5c60d80a9cdb0179b`,
  `iwx-gl-c0-fm-c0.pnvm` SHA256
  `f481e3b0403ca43a0a444b01c04fa4d1582868afcb026b1e697905f0039ccfa3`.
- The running kernel cannot `modunload` (no FINI): each hardware probe
  needs an MSI reboot.

## Target

| Item | Value |
| --- | --- |
| Adapter | Intel Wi-Fi 7 BE200, PCI `8086:272b`, revision `0x1a` |
| Host | MSI Vector 17 HX AI (MS-17S3), Intel Core Ultra 9 275HX |
| OS | EmberBSD 1.0 amd64 release `c5d396f5698b` (NetBSD 11.0 base) |
| Today | `Intel Wi-Fi 7 ... not configured`; only iwi/iwn/iwm exist (up to the 9000 series) |
| Goal, stage 1 | station mode, 11a/b/g legacy rates, WPA2-PSK with net80211 software crypto, DHCP |
| Out of scope for stage 1 | 11n/11ac/11ax/11be, A-MPDU/BA, hardware crypto, MFP/WPA3, power save, monitor mode |

## Chosen approach

Port OpenBSD iwx(4) rather than Linux iwlwifi:

- iwx is a native BSD net80211 driver of the same lineage as iwm(4),
  which NetBSD already ported from OpenBSD (`sys/dev/pci/if_iwm.c`).
  The NetBSD iwm port is the reference for every glue change.
- iwx already supports the BZ device family (`IWX_DEVICE_FAMILY_BZ`),
  the gen3 context info, PNVM loading, and has `IWX_CFG_MAC_TYPE_GL`
  and `IWX_CFG_RF_TYPE_FM` constants. OpenBSD ships BZ with
  `iwx-bz-b0-gf-a0-100` (firmware API 100).
- BE200 is MAC type GL with an FM radio, BZ family. Linux firmware
  `iwlwifi-gl-c0-fm-c0-100.ucode` and `iwlwifi-gl-c0-fm-c0.pnvm`
  exist in linux-firmware with the same API generation.
- EmberBSD net80211 is the NetBSD (pre-VAP) stack with no HT/VHT,
  so the FreeBSD iwx port does not apply.

Upstream sources (OpenBSD `src`, fetched 2026-10-19):

| File | OpenBSD commit | SHA256 of the fetched file |
| --- | --- | --- |
| `sys/dev/pci/if_iwx.c` | `65596250e064` (2026-09-19) | `ade02a314dd9aa63ccc7c87df76099c22f76ec172d42e05a7ad72e0d39450d57` |
| `sys/dev/pci/if_iwxreg.h` | `30e95afee06c` (2026-05-28) | `63c2418dd2cccedf8364375c9a5ca28f6555809065af5ca8f3555ec293091fe8` |
| `sys/dev/pci/if_iwxvar.h` | `e1a9c1b93889` (2026-03-20) | `0e6fd5014b45f0ef7ae2e5e486b9d8d4f8ddb1a5fc2f02865765b156724c7f04` |

Licence: ISC (OpenBSD iwx). The compat header is new and ISC as well.

## What is in `src/`

| File | State |
| --- | --- |
| `if_iwx_compat.h` | Shims for OpenBSD kernel APIs: task(9) on workqueue(9), timeout(9) on callout(9), rwlock names, refcnt, `tsleep_nsec`, `loadfirmware` on firmload(9) (`/libdata/firmware/if_iwx/`), `nitems`, `letoh*`, `mallocarray`, `iwx_free` |
| `if_iwx.c` | OpenBSD file plus mechanical edits: NetBSD includes, `IC2IFP`, `free()` to `iwx_free()`, `DEVNAME()` to `device_xname()`, `NBPFILTER 0` |
| `if_iwxvar.h` | softc: `device_t sc_dev`, `struct ethercom sc_ec`, `krwlock_t`, `pci_intr_handle_t *sc_pihp` |
| `if_iwxreg.h` | unchanged |
| `modules/if_iwx/` | kernel module Makefile and ioconf (`iwx* at pci?`) |
| `files.iwx` | `device iwx: ifnet, arp, wlan, firmload` entry to append to `files.pci` |

## Remaining work, in order

1. **Compat gaps found by the first build** (add to `if_iwx_compat.h`):
   `fls`/`flsl` (use `fls32`/`fls64` from `<sys/bitops.h>`), `M_CANFAIL` (0),
   `caddr_t`, `BUS_DMA_64BIT` (0), `BUS_DMA_ZERO` (absent on NetBSD:
   `memset` the mapping after `bus_dmamem_map`), minimal `struct mbuf_list`
   and `struct ieee80211_rxinfo`, `IEEE80211_BA_MAX_WINSZ` (64),
   `IEEE80211_NUM_TID` (16), a dummy `struct ieee80211_node_switch_bss_arg`.
2. **Compile out stage-2 features** with `#ifdef IWX_HT` (never defined):
   HT/VHT rate setup, RX reorder and BA sessions (`iwx_init_reorder_buffer`
   through `iwx_ampdu_tx_start`), `iwx_rx_reorder`, VHT channel width,
   `iwx_rs_ht_rates`/`iwx_rs_vht_rates`, bgscan, MFP. Keep firmware
   commands valid for 20 MHz legacy operation (PHY context with
   `IWX_PHY_VHT_CHANNEL_MODE20`, rate scaling with legacy rates only).
3. **Rewrite the glue after NetBSD iwm** (`sys/dev/pci/if_iwm.c`, line
   numbers for c5d396f):
   - attach: `iwm_attach` 7758, `iwm_config_complete` 8082 (ethercom,
     `if_initialize`/`ieee80211_ifattach`/`if_percpuq_create`/`if_register`,
     `ieee80211_media_init_with_lock`, `config_mountroot`);
     interrupts with `pci_intr_alloc` + `pci_intr_establish_xname` (start
     with MSI or INTx and `iwx_intr`; MSI-X later);
   - add `PCI_PRODUCT_INTEL_WIFI7` (`0x272b`, already in `pcidevs`) to the match table and a
     case: `sc_fwname = "iwx-gl-c0-fm-c0-100"`,
     `sc_pnvm_name = "iwx-gl-c0-fm-c0.pnvm"`,
     `sc_device_family = IWX_DEVICE_FAMILY_BZ`, `sc_integrated = 0`,
     `sc_uhb_supported = 1`, LTR/xtal values as in Linux `iwl_gl_trans_cfg`;
   - `iwx_newstate`/`iwx_newstate_task`: NetBSD `iwm_newstate` 6354 and
     `iwm_newstate_cb` 6325, `iwm_do_newstate` 6183;
   - `iwx_init`/`iwx_start`/`iwx_stop`/`iwx_watchdog`/`iwx_ioctl`: iwm
     6769-6990 (`ic_mgtq` with `M_GETCTX`, `ieee80211_classify`,
     `ieee80211_encap`, `ieee80211_find_txnode`);
   - RX: `iwx_rx_frame` to the shape of `iwm_rx_rx_mpdu` 3900
     (`ieee80211_find_rxnode`, `ieee80211_input(ic, m, ni, rssi, tstamp)`,
     `ieee80211_free_node`), no reorder;
   - TX: `iwx_tx` encryption via `ieee80211_crypto_encap` as iwm 4592;
     no hardware keys (`ic_set_key`/`ic_delete_key` not installed);
   - scan end: `ieee80211_end_scan(ic)`; begin: `ieee80211_begin_scan(ic, 0)`;
   - EDCA: `ic->ic_wme.wme_chanParams.cap_wmeParams[ac]` instead of
     `ic_edca_ac[]`;
   - node: `ieee80211_free_node` for `ieee80211_release_node`.
4. **Firmware**: install `iwlwifi-gl-c0-fm-c0-100.ucode` as
   `/libdata/firmware/if_iwx/iwx-gl-c0-fm-c0-100` and the PNVM as
   `/libdata/firmware/if_iwx/iwx-gl-c0-fm-c0.pnvm`; record linux-firmware
   commit and SHA256. If API 100 is rejected, try 98/96/92 and record it.
5. **Hardware stages** (each one stops on failure, saves `dmesg`):
   module builds; `modload` attaches and prints hw rev, firmware version,
   PNVM version and MAC address; `ifconfig iwx0 up` and
   `ifconfig iwx0 scan` list access points; `wpa_supplicant` reaches
   COMPLETED with WPA2-PSK; `dhcpcd` lease; ping the gateway;
   `modunload` without panic; reboot with the module and repeat.
6. **Tests**: a host-side contract for the compat shims and for the
   device-config selection of `0x272b` (pattern: `ember/tools/*-contract.c`).

## Building on the target

`build-native.sh` copies `src/` into `/usr/src` and builds the module with
pkgsrc clang. The script prints the error count; `/tmp/iwx-build.log`
holds the full log. The first full build of this snapshot:
`errors: 782` (`-ferror-limit=0`).

## Acceptance (ember-wiki debt N44)

BE200 attaches in GENERIC or as a module with firmware from the release
sets, WPA2 and DHCP to a customer network, survives reboot; physical
check on the MSI; no regressions in kernels without the adapter.
