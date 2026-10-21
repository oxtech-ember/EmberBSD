# Native RTL8822CE station driver proposal

Status: staged implementation. Legacy WPA2 station checks pass on both bands.

## Problem and target

An Orange Pi 6 Plus with CIX P1 CD8180, 32 GB RAM and board ID
A4 (PHECDA-Rev A) exposes a Realtek RTL8822CE PCI function, `10ec:c822`.
The initial diagnostic kernel reported that function as not configured.
The existing `rtwn` driver targets different Realtek generations.

The delivered firmware is `BiosV1.5-W20260706-e321056a`.
An experimental EmberBSD diagnostic build reached a PL011 console and a
RAM root. A short bidirectional ICMP test passed through one RTL8126.
Those observations do not establish Wi-Fi, internal storage or full board support.
The initial console needed a temporary correction of the firmware SPCR
from UART3 to the UART2 exposed by DEBUG and DBG2. The installed profile now
applies the correction on each boot.
The opt-in [loader correction](../boot/cix-spcr.md) is a separate boot
dependency. See the [current board evidence](../boards/orange-pi-6-plus.md).

## Scope and baseline

The accepting owner is the OS repository. Start from source
`93b4fe37fb25c8dbf571953722434695a923e86f`.
The physical diagnostic baseline was the experimental local revision
`7c29a5ba7172b5e43417919ec7bcb5b6769de5ee`.
That revision is not a published release or the implementation baseline.

Add a native PCI attachment, bounded DMA rings, RTL8822C hardware operations
and station integration with the existing `net80211` stack.
Use the existing firmware loader, `wpa_supplicant` and DHCP interfaces.
Do not introduce a parallel wireless stack or LinuxKPI WLAN dependency.
The primary code owners are `sys/dev/ic`, `sys/dev/pci`, the selected kernel
configuration and focused contracts under `ember/tools`.

Inspect the Realtek `rtw88` implementation at FreeBSD source revision
`a143197801c680b958ab662c88cf086668a1958b` as prior art.
Record each imported file's license and revision. GPL-only implementation
is protocol reference only. Verify the separate firmware asset's license,
origin, size and SHA256 before distributing it through an image profile.
No radio firmware binary is added to this proposal.

## Current source boundary

The native source includes firmware validation, PCI DMA rings, RTL8822C
register tables and bounded frame transfer. The core exposes copied-frame
callbacks for a native station wrapper. It rejects CRC/ICV errors and stale
DMA tags. The RX callback excludes the FCS and expires when it returns.
A DMA TX completion does not prove an acknowledgement from an access point.
Management input occupies one contiguous mbuf because native parsers use
the first segment as the frame end. Nonblocking allocation retains the
4096-byte input bound. Data input keeps the native chained-copy path.

The legacy port0 helper programs the local address, BSSID, AID, basic rates,
slot time and software-only crypto. It returns hardware and mailbox errors
before the caller can publish a successful station state. Frames use a
band-valid basic rate and retain software data sequence numbers.
Startup applies the Wi-Fi-only antenna grants and calibrated 20 MHz TX
power limits. It sends the general-info and PHYDM packet commands through
the H2C DMA ring before publishing started state. Runtime retuning pauses
TX and restores it only after both channel and power setup succeed.

Startup and the internal runtime packet-H2C API share one bounded producer.
It retains the ring position and 16-bit command sequence for each firmware
epoch. The shared 80-byte DMA buffer is reused only after hardware consumes
the preceding packet and POSTWRITE synchronization completes. Unexpected
hardware state, uncertain posts and completion failures fence the epoch.
Only a confirmed new firmware download clears that fence.
Callers must pin core lifetime and serialize command, channel and stop work
in process context without the PCI spin lock. This transport does not run
IQK or prove firmware semantic acceptance.

Normal table load keeps DPK mode tables out of the cold startup path.
Those tables require a separate calibration transaction and register restore.
The actual-source call graph and pre-ADCK AFE register checks cover this boundary.
Physical trials still report residual nonconvergence after this correction.

Cold startup runs the native ADCK/DACK helper before HCI starts. Its bounded
noise filter computes a trimmed mean after at most 101 replacement passes.
A spread above five only prevents early exit. ADC/DAC residual quality uses
a threshold below five and at most ten valid attempts per path. As in the
reference, residual nonconvergence is advisory. Startup emits an explicit
numeric degraded warning; it does not qualify RF quality. Acquisition skips out-of-range pairs,
including filter replacements, within 10202 requests per measurement.
Insufficient valid samples, readiness timeouts and bus failures stop startup
and require a power reset.
Pinned-source trace tests include persistent noise and bounded nonconvergence
followed by both paths and control restore. Completion, control restore,
measured paths and converged paths have separate result fields. Physical cold
startup completes the procedure and starts HCI.
ADC AB and DAC B residuals have not converged. Numeric diagnostics report the failure stage,
attempts, last measurement, ADC means and programmed compensation.
The failure output includes the already-read silicon identity and RFE fields.
The tenth ADC attempt also reads back five ordinary BB control registers
after measurement and before the convergence decision. These native diagnostics
add at most ten reads across both paths. A read failure stops all later I/O.
State failures now retain numeric call stages and station inputs before stop.
Stage 1 is channel setup, 2 is station setup, and 3 is an inactive core.
The output includes both channels, rate mask, slot, AID and rate count.
BSSID flags report presence, a group address and an all-zero address.
The output contains no station address or network name. Input checks remain strict.
The diagnostic kernel confirmed a zero BASIC rate mask during RUN setup.
Native STA rate negotiation now retains the AP's BASIC flags for supported rates.
Other modes retain the local BASIC flags. There is no default-mask fallback.
Actual-source negotiation and wrapper-consumer tests cover both legacy bands.
Physical kernel `71877588ca099bf69686a35b96a00771a26feebc` passes station RUN,
WPA2-PSK/CCMP and fresh DHCP on channels 8 and 149. Each band transfers
16777216 TCP bytes in each direction, with matching size and SHA256.
The simultaneous transfers take 251.13 s on 2.4 GHz and 53.26 s on 5 GHz.
During the 2.4 GHz transfer, three ping replies take 1.83-2.25 s.
After each transfer, router ping is about 3.4 ms with no loss in three probes.
Those RAM-root transfers do not qualify analog performance or higher rates.
The separate [persistent SD profile](../image/cix-p1.md) passes five unattended
boots, WPA2/DHCP recovery and 2 MiB TCP checks in each direction on both bands.
AP-loss recovery and long-term stability remain pending.

Multicast list changes keep the radio running. The MAC multicast hash is
already all ones. The ioctl consumer updates the native list and acknowledges
its `ENETRESET` result without repeating cold calibration or association.
Other configuration reset requests retain their existing restart behavior.
The actual-source stop contract covers both membership operations in each
UP/RUNNING combination and preserves configuration and error handling.
This contract does not establish reconnect latency or packet-loss recovery.
A GCC16 physical kernel at `6a1a34c93e3eb105bfdb035a9f30cbadfb678a9f`
passes three IPv6 address add/remove cycles. The cold calibration count stays
at four, the station stays in WPA2 COMPLETED, and DAD leaves both addresses ready.
A separate 60-probe link-local IPv6 run receives all replies.
The IPv4 run loses one of 60 replies. This is a short regression, not endurance acceptance.
The read-only `hw.rtwce_stats` output also reports the current `basic` rate
mask and an encoded unicast DATA sample. `txdata` counts successful encodings;
`txrate` is the last descriptor rate code. `txbasic` is the BASIC mask at
that encoding; an explicit fixed rate does not use this mask for selection.
`txr0` through `txr11` count encodings at 1, 2, 5.5, 11, 6, 9, 12, 18, 24,
36, 48 and 54 Mbps. `txother` counts any encoded rate outside that legacy set.
A zero `txdata` means no sample; rate code zero alone does not prove 1 Mbps.
Counters remain cumulative across radio restarts. Compare snapshots around one
transfer. Encodings count even if a later doorbell write fails. DMA completion
and these counters do not prove an air transmission, ACK or delivered payload.
The snapshot and updates use the existing bus interrupt lock. No addresses or
payload bytes are retained. An undersized stats buffer returns `ENOSPC`.
The native PCI contract checks maximum-value output and every capacity through
2048 bytes. This diagnostic does not change selected rates or enable WME/HT.
Target build and physical rate measurements remain separate acceptance steps.

Explicit fixed legacy media has a separate opt-in TX path. The wrapper reads
`ic_fixed_rate` as an index into `ic_sup_rates[ic_curmode]`. It passes a numeric
rate in 500 kbps units, the peer rate mask, mode, channel and association proof
to the core. The core checks the exact 12-rate legacy map before DMA. It does
not choose the highest peer rate or apply fallback. AUTO still uses the old
lowest-basic-rate descriptor policy. This is not adaptive rate control.

The fixed path requires the current associated STA BSS in RUN, an active policy,
no pending state work and the current core channel. Only ToDS unicast payload
DATA is eligible. Management, null DATA and group DATA keep their old descriptor
bytes. For ToDS the group destination is addr3, not the AP receiver in addr1.
Malformed explicit inputs return an error. OFDM with ERP protection requested
returns `EOPNOTSUPP` until a separate RTS/CTS policy is qualified. This API does
not enable WME or HT. The native fixed contract executes the complete wrapper
handoff and core, plus actual association rate negotiation with reordered peer
rates. Kernel object layout, channel lookup and bus services are modeled.
The fixed contract reuses only the core startup fixture and labels it as such.
The separate start runner retains all native state/default EDCA tests.
The fixed contract does not execute complete media-change and station-join functions.
The 2.4 GHz declarations include CCK, OFDM and DYN. Native `ifattach`
requires OFDM to derive 11g mode support. The media contract executes the
complete native attach, media registration, selection and mode consumers.
It checks explicit 6, 24 and 54 Mbps 11g entries, 11a and 11b rate tables,
band-limited active sets and AUTO restoration. It also executes the complete
native media callback, real ERP/slot helpers, `setupscan` and the SCAN_REQ arm.
An AUTO association can change runtime mode without narrowing active channels.
The native callback uses its previous configured media word to detect a newly
selected mode or the first fixed rate. If shared media handling keeps the same
runtime mode, the callback reapplies that mode before restart. New constraints
follow normal `setmode` channel-list replacement. Repeated requests and rate-only
changes within an existing constraint preserve a narrower configured list.
Init failure restores the wrapper cache; existing shared rate/mode mutations are
not a full configuration rollback. Kernel services, object layout and ifmedia
storage/lookup are models. Init and scan scheduling are modeled;
Association runtime mode is an explicit input, not execution of station join.
This is not an ioctl transport test.
Target build and fixed-rate physical acceptance remain pending.

Manual SSID updates now stage the desired network without requesting a cold
restart. Automatic and device-controlled roaming retain their reset result.
The BSD supplicant skips a media SET when the configured media word is unchanged.
Changed media requests and transport errors retain their existing behavior.
The legacy scan path still sends an explicit `IEEE80211_IOC_SCAN_REQ` after
SSID staging. The native stack retains the active channel set selected by
media mode or channel-list policy. It prepares INIT and requests SCAN.
That request does not widen a fixed 11b/11g mode to 5 GHz. The setup helper accepts
the existing active-set pointer without an overlapping copy. Distinct legacy
configuration inputs still replace the active set. Association remains an
explicit MLME operation after scan completion.

The [manual scan contract](../tools/net80211-manual-scan-contract.py) runs the
complete BSD legacy scan, media and SSID functions. It compiles the native
NWID, ROAMING and SCAN_REQ switch arms, the complete `setupscan` body, and the
complete RTL8822CE ioctl consumer. Restricted-set cases check AUTO, 11a,
11b/11g and a configured two-channel policy as explicit inputs. The separate
media contract checks mode-to-active-set derivation. Kernel layouts, copyin,
ioctl transport and state scheduling use explicit host models. Unrelated switch arms, WPA setup,
management capabilities and physical radio operations are outside this contract.

Run with a native GCC16 compiler:

```sh
CC=/absolute/gcc16/bin/gcc python3 ember/tools/net80211-manual-scan-contract.py
```

`MANUAL_SCAN_CFLAGS` accepts ASan/UBSan flags. `MANUAL_SCAN_ANALYZE=1` runs
Cppcheck, GCC `-fanalyzer -c -O0` and Clang Static Analyzer on the selected
source model. The GCC branch first requires a causal NULL dereference to fail
with `-Wanalyzer-null-dereference`; a syntax-only invocation is insufficient.
`MANUAL_SCAN_SOURCE` selects an explicit source tree for causal comparison.
These host checks do not establish a target build or a lower physical cold-init
count. Reconnect latency, DHCP DAD timing and radio acceptance remain separate.

The [bounded A-MSDU prerequisite](net80211-amsdu.md) remains disabled.
Its host parser and native-consumer models do not establish HT interoperability.

The kernel configuration selects the native station wrapper and ingress
generation policy. Use the standard interface tools when the PCI wrapper
attaches it. Legacy diagnostic writes return EBUSY while net80211 owns the
device. RSSI, thermal power tracking and higher-rate modes remain outside
this profile. Source contracts do not establish a linked wireless network.

The native `ifconfig` accepts `roaming auto|manual|device` for net80211.
It only selects the existing roaming owner. It does not raise the interface.
For application control, run `down`, `roaming manual` and `up` separately.
The new source contract checks parsing and the exact ioctl request.
The persistent profile executes this native command before WPA starts on the
physical board. The source contract and post-reboot network checks have separate
boundaries; a complete base release remains unqualified.

The read-only PCI statistics expose direct RF18 A/B windows and numeric RX
handoff counters. Native beacon, channel and element-drop counters are
snapshots from the most recent net80211 input. The diagnostic path reads
that copy without entering the network stack. It does not print SSIDs or
BSSIDs, retune the radio or change frame admission.

The core contains a dormant WME hardware prerequisite. Independent bounded
producers cover BE, BK, VI and VO, with 32 bounce buffers per queue.
QoS TID0/3 use BE, TID1/2 use BK, TID4/5 use VI and TID6/7 use VO.
Descriptors retain the actual TID as QSEL and preserve software sequence
numbers. A mismatched queue, TID8..15, HT Control or non-QoS on BK/VI/VO
is rejected before DMA. Legacy BE and management keep QSEL0 and QSEL18.
Each queue has its own completion index, capacity and startup reset.
Stop accounts for all outstanding frames. Failed DMA shutdown retains all
mappings for a safe retry. The additional producers require 384 KiB of
coherent bounce buffers.
`rtw88_edca_apply` validates all four ACs before I/O, uses the current band
and caller-supplied slot time, pauses TX, and restores the previous pause
only after all writes succeed. An I/O error quarantines TX and station setup
until a full successful start; stop and failed start retain the error.
A failed bus operation may have taken effect, so hardware pause is uncertain
and the caller must stop the core. QoS HT-control headers are unsupported.
Legacy state transitions apply contextual DCF EDCA before new producers.
They do not advertise WME, HT or VHT.
The registered strict STA callback remains dormant until capability enablement.
The shared [TXOP width prerequisite](net80211-txop.md) preserves wire values
and rejects unsupported consumer widths. The shared
[STA offer prerequisite](net80211-wme.md) validates emitted offers, peer
parameters and sequence domains. The optional strict STA callback applies
immutable parameters synchronously
and commits software state only after success. Its dormant native consumer
uses core quarantine on I/O errors. Contextual default EDCA is checked through
the actual native state worker and core with modeled scheduling. Native
four-AC wrapper routing uses actual classification before encap, target-queue
backpressure before sequence/crypto, and an all-AC capacity gate for ALTQ/TBR.
Capability advertisement remains a separate physical acceptance gate.

Run the actual-source contracts with GCC16:

```sh
CC=gcc-16 sh ember/tools/rtw88-contract.sh
CC=gcc-16 sh ember/tools/rtw88-pci-contract.sh
CC=gcc-16 sh ember/tools/rtw88-start-contract.sh
CC=gcc-16 sh ember/tools/rtw88-fixed-contract.sh
CC=gcc-16 RTW88_MEDIA_ANALYZE=1 sh ember/tools/rtw88-media-contract.sh
CC=gcc-16 RTW88_ROUTING_ANALYZE=1 sh ember/tools/rtw88-routing-contract.sh
