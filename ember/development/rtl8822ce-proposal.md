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
`txpost0` through `txpost4` count successful producer-index writes for MGMT,
BE, BK, VI and VO. A failed write does not increment them, even after an
uncertain hardware effect. Ring ownership and the existing `tx_queued` counter
keep their previous conservative semantics on that failure. Queue counters
remain cumulative across stop/start and do not count DMA completion or air ACKs.
The actual core contract checks each queue, full rings, reclaim and restart.
It injects producer-write failures before and after the modeled bus effect.
The snapshot and updates use the existing bus interrupt lock. No addresses or
payload bytes are retained. An undersized stats buffer returns `ENOSPC`.
The native PCI contract checks maximum-value output and every capacity through
4096 bytes. The stats sysctl allocates its per-call snapshot before driver locks
and frees it after lookup or any error. The wrapper contract applies the
3584-byte limit to its host layout and checks nested calls and buffer lifetime.
A target kernel build must validate the native stack frame separately.
This diagnostic does not change selected rates or capability advertisement.
Target build and physical rate measurements remain separate acceptance steps.

Explicit fixed legacy media has a separate opt-in TX path. The wrapper reads
`ic_fixed_rate` as an index into `ic_sup_rates[ic_curmode]`. It passes a numeric
rate in 500 kbps units, the peer rate mask, mode, channel and association proof
to the core. The core checks the exact 12-rate legacy map before DMA. It does
not choose the highest peer rate or apply fallback. AUTO payloads use the
separate bounded firmware rate-adaptation path described below.

The fixed path requires the current associated STA BSS in RUN, a committed
active policy and the current core channel. Only ToDS unicast payload
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
The GCC16 cross-built CIX kernel at `287792a96` passed physical fixed-rate checks.
Explicit 6, 24 and 54 Mbps 11g selections each completed WPA2-PSK/CCMP association.
Each selection passed a 1 MiB file transfer in each direction with matching SHA256.
Encoded DATA counters matched the requested rate. No new TX DMA errors, drops or
RX ring-full events appeared during these bounded checks. AUTO was restored.
These are short tests on one 2.4 GHz AP, not adaptive-rate or HT/VHT acceptance.
Descriptor encodings and DMA completions do not establish over-the-air ACKs.
PHY rate limits do not specify application throughput.

## Legacy AUTO firmware rate adaptation

The native RUN worker intersects numeric local and peer legacy rates under the
stack lock. It copies only mode, channel, common mask, protection flag and host
policy epoch into the core. Existing media join/leave and default-port commands
still own MACID0. After channel, station and EDCA preparation, the worker posts
RA_INFO H2C `0x40` before committing acceptance and calling parent RUN. Posted
mailbox writes are not firmware acknowledgements. Command failure quarantines
TX and station/EDCA/RA updates until a complete successful fresh start; the
worker stops the core. It does not silently switch to a basic-rate fallback.

The seven-byte command uses MACID0, BW20, SGI0, LDPC0, VHT0, initial RA level1
and DIS_PT. Rate IDs are B8, G7 and mixed BG6; the mixed legacy mask uses the
pinned `0xff5` filter. Missing RSSI is handled with the neutral all-rate host
mask corresponding to reference RSSI level0. No fabricated RSSI report is sent.
Only current RUN STA ToDS unicast payload DATA/QoS with a matching epoch,
channel and firmware generation clears descriptor USE_RATE/DISDATAFB. Fixed,
management, null and group frames keep their explicit existing descriptor path.
A missing capsule rejects AUTO payloads before DMA.

The parent RUN callback may invoke the first producer before the state worker
releases ownership. Payload acceptance follows the committed policy and current
numeric capsule, not the worker's scheduling flag. A new request withdraws
acceptance and changes the epoch before any later producer. The combined
attach/association test checks both the first posted DATA and a real SCAN
request issued inside parent RUN. The latter retains the queued DATA without
posting it. Worker scheduling and strict WME apply gates remain unchanged.

Wide RA is eligible only without ERP protection. Initial protection or a later
USEPROT event selects a visible legacy CCK policy only if the actual lowest
BASIC descriptor rate is common CCK. Otherwise transmission returns
`EOPNOTSUPP`, counted by the native interface error path. Clearing USEPROT does
not reopen wide RA until another successful RUN configuration. No RTS24 policy
or per-packet mailbox mutation is introduced.

C2H `0x0c` accepts at least seven payload bytes, counts trailing bytes and parses
the known prefix. MACID must be zero, rate must be legacy0..11, BW20 and SGI0.
Malformed, foreign and unsupported reports have separate counters. Every valid
report is explicitly association-unattributed telemetry: the wire has no BSSID
or association token, and MACID0 is reused. `ra_observed_gen` is the host firmware
observation generation only. A report never selects host policy, establishes
current peer acceptance, changes keys, or proves per-frame ACK. `outside_mask`
is a diagnostic comparison, not evidence that a report belongs to this peer.
`txr*`/`txrate` remain descriptor-encoding seeds; `fwra_tx` and the report fields
are separate. DMA `txdone` remains independent of both.

The pinned BSD-3-Clause protocol reference is FreeBSD revision
`d2145e9b96448da34d3cfede24c509af6d830c92`, files
[`fw.c`](https://github.com/freebsd/freebsd-src/blob/d2145e9b96448da34d3cfede24c509af6d830c92/sys/contrib/dev/rtw88/fw.c),
[`fw.h`](https://github.com/freebsd/freebsd-src/blob/d2145e9b96448da34d3cfede24c509af6d830c92/sys/contrib/dev/rtw88/fw.h),
[`main.c`](https://github.com/freebsd/freebsd-src/blob/d2145e9b96448da34d3cfede24c509af6d830c92/sys/contrib/dev/rtw88/main.c),
[`main.h`](https://github.com/freebsd/freebsd-src/blob/d2145e9b96448da34d3cfede24c509af6d830c92/sys/contrib/dev/rtw88/main.h)
and [`tx.c`](https://github.com/freebsd/freebsd-src/blob/d2145e9b96448da34d3cfede24c509af6d830c92/sys/contrib/dev/rtw88/tx.c).
No new radio firmware binary or HT/VHT advertisement is added.

The RA contract executes actual RUN/state/caller/core and real C2H RX dispatch
with modeled kernel services, accepted peer tables and bus/firmware behavior.
It does not execute a complete association exchange or firmware algorithm.
Cases cover B/G/BG/A serializer bytes, reordered numeric peer intersections,
first DATA ordering, stale epoch/generation, ERP closure, mailbox writes before
and after side effect, timeout/cancellation, quarantine, restart and unattributed
reports after MACID reuse. Core and consumer are checked separately with GCC
analysis, Cppcheck and Clang Static Analyzer; GCC16 performs all compilation.
ASan/UBSan executes actual core functions. Source tests do not qualify radio
rate selection or performance. Initial physical qualification requires fresh
firmware and one association, matching transfer SHA256 plus changing valid
firmware telemetry and throughput. Reconnection data is a separate result:
old queued DMA is not proven drained by this atom, and reports cannot be
attributed across reassociation. Legacy recovery must remain available.

## Shared HT20 association staging (driver admission remains off)

The shared stack has a bounded HT20 offer/response capsule and the native
request callback consumes its generation/peer/channel fence. Protocol attach
initializes the optional provider to NULL. RTL8822CE does not register a provider
in this step, so ordinary real-board association still emits legacy rates/WME
and performs no HT/VHT radio, power, rate or aggregation configuration.

A future qualified provider supplies a current generation, equal local RX/TX
MCS masks (mandatory MCS0..7, at most0..15), an A-MPDU parameter with exponent0
and qualified spacing, and all radio/power/protection/RXBA/scheduler/A-MSDU
readiness bits. Missing/unknown readiness never creates capabilities. The
initial wire profile is HT20, mixed format, long GI and SMPS disabled; no
40MHz/SGI/LDPC/STBC/beamforming/VHT bits are emitted. TX MCS parameters explicitly
say defined/equal to RX; unequal local masks are rejected for this profile.

Actual queued association/reassociation bytes must contain a unique matching
HT Cap IE and valid unique WME offer before publishing an offer capsule. A full
HT queue uses the configured capacity or IFQ_MAXLEN and returns ENOBUFS through
the existing caller/node-reference cleanup. The legacy queue policy is unchanged.
Manual HT/VHT Cap/Operation optional IEs and known vendor HT IEs are rejected by
association and probe producers even without an admission provider. Existing
opaque RSN/WME optional-byte behavior is preserved.

The actual association response consumer validates the current offer identity,
full bounded TLVs, HT Cap26/Operation22 lengths, selected primary channel and
supported basic MCS before committing. Local TX intersects peer RX MCS; this
is not evidence of the peer's TX mask or the on-air NSS/rate. Static/dynamic
peer SMPS restricts the accepted TX mask to one stream until a later protection
policy exists. A malformed matching offered HT response follows the existing
association failure cleanup; unsolicited HT cannot activate a legacy station.
A valid legacy response can complete without HT.

Fresh matching ASSOC-to-RUN state retains the capsule. The provider must request
invalidation before replacing active configuration/profile; node cleanup also
discards the capsule. Each native RUN request revalidates its current profile.
The worker repeats validation before hardware setup and policy commit. Shared
profile mutation requires Giant, held through validation and parent publication.
The native pre-PCI consumer rejects stale active HT and withdraws existing TX acceptance/RA through the normal coalesced
INIT request, including direct RUN callers. Failed worker acceptance cancels the
capsule before existing parent INIT cleanup. A rejected parent state withdraws
the current request through normal INIT ownership without waiting on its worker.
Newer requests survive. Rejected INIT stops once and clears running state.
No new node/key freeing path or
media/AID ABI is introduced.

The host contract executes actual shared management producers, WME/HT response
consumers, native newstate callback, complete state worker, RA invalidation and
request/commit epoch consumers. Allocation/queueing, locks, clocks, scheduling,
core operations, restart and non-HT parent effects are explicit services. The
worker fixture copies the accepted interface snapshot into its native container.
Earlier accepted RUN is a modeled input to the direct-RUN regression. Separate
RA/reconnect contracts execute core and restart I/O. No RF setup is claimed. It
checks actual allocation extent, golden request bytes, all AMPDU parameter
encodings, unaligned HT bytes, malformed/duplicate/missing IEs, basic MCS limits,
SMPS, security/profile changes, allocation/prepend/queue failures, coalesced
requests, delayed profile changes, parent rejection, INIT failure and unchanged
legacy/WME paths. Disabling the actual writer, parser or
native consumer must make the same contract fail.

This staging does not establish RX BlockAck, encrypted reorder or protected HT
operation. Those require the next separate coherent data/control/lifecycle atom,
including narrow native forwarding of20-byte compressed BAR, CCMP replay ordering,
A-MSDU3839, hardware BlockAck and radio/power/protection acceptance before enabling
any real HT advertisement. Wire semantics are adapted from FreeBSD net80211 at
`d2145e9b96448da34d3cfede24c509af6d830c92`, using its BSD license option and
preserved notices; no LinuxKPI/vap ABI is imported.

## Firmware hardware-feature snapshot

Fresh RTL8822C startup writes request `0xfd` to the dedicated MMIO event slot
`0x01a0` immediately before firmware download. After confirmed firmware-ready,
it accepts ID `0x19` and snapshots thirteen bytes at `0x01a2..0x01ae`, checks
that the ID remains stable, releases the owned slot, then publishes a capsule
fenced by the current firmware generation. This is neither an H2C command nor
a DMA RX C2H event handler. Offset +1 is not interpreted as a length.

The wire layout follows FreeBSD rtw88 at
[`d2145e9b96448da34d3cfede24c509af6d830c92`, main.c](https://github.com/freebsd/freebsd-src/blob/d2145e9b96448da34d3cfede24c509af6d830c92/sys/contrib/dev/rtw88/main.c)
and its `efuse.h`, under the BSD-3-Clause option. Payload bytes4..7 form an
explicitly byte-assembled little-endian word: HCI bits3..0, bandwidth18..16,
NSS20..19, antennas23..21 and protocol27..26. The numeric diagnostic reports
these raw fields, independently validated known-field bits and request/observation
generations; it does not expose the entire raw payload.

Conservative validation recognizes bandwidth6 as20/40MHz and7 as20/40/80MHz,
NSS and antenna counts1/2, and protocol3 as the reference VHT token. Zero and
unsupported encodings remain unknown; there is no default NSS2/BW80 and no
antenna reconfiguration. HCI is opaque telemetry. These are firmware claims,
not measured RF capabilities, peer negotiation or admission to HT/VHT modes.
Current channel setup, TX power limits and advertised legacy operation are
unchanged by the report.

Missing or foreign event IDs and a replaced slot leave capabilities unknown,
with separate counters, while legacy startup continues. A foreign/replaced
slot is not cleared. Actual request/read/release I/O failures use existing
startup cleanup; no partially read or unreleased report is published. Stop
and fresh-start generation creation invalidate the capsule. There is one
bounded read attempt per fresh firmware startup and no late activation.
The wire format has no proven length metadata; a stable ID and thirteen MMIO
reads cannot independently detect a firmware short write or same-ID replacement.

The synthetic HW-feature contract executes the complete actual core startup,
request/read/release implementation and decoder. Firmware-ready/report publication,
MMIO and other device services are modeled. It checks every mailbox operation
with before/after I/O errors and stop/generation changes, unknown/foreign/unstable
responses, restart invalidation, unaligned exact-length decoding and all numeric
field encodings. The separate actual PCI diagnostic contract checks the stored
capsule and every output buffer capacity, including maximum numeric values.
No physical report bytes are fixtures. Board qualification must record private
raw report bytes, firmware SHA/generation and continuing legacy WPA2/data operation;
HT/VHT throughput, NSS and width still require separate physical acceptance.

## Linked-peer reconnect boundary

A new native state request latches hardware-peer invalidation before changing
its host epoch. The latch survives coalesced RUN/INIT/SCAN requests. The worker
reuses full core stop/start before a linked peer is replaced. It does not infer
firmware FIFO emptiness from DMA completion or a zero host queue count. The
existing core stop masks IRQs, drains its interrupt owner, powers down and
verifies bus-master disable. A failed shutdown or fresh start keeps acceptance
closed and reports stage6. No new peer table or payload is published on failure.

The worker runs lifecycle operations under Giant without the PCI lock. It does
not call net_init/net_stop, which wait for the same workqueue. After successful
quiescence it drains the actual native management queue, including node-context
references. Coalescing can bypass the shared INIT drain. Ethernet if_snd remains
owned by the stack and receives fresh encapsulation/crypto later. Pending DMA
completion accounting retains its existing global softintr semantics. Old queued RX
keeps its epoch and is rejected by the native softintr after a new acceptance.

Administrative UP retains a freshly started READY core when the interface was
running, even when the latest state is INIT. A synchronous stop owner that has
already withdrawn RUNNING keeps the core quiescent. IFF_RUNNING is set only after successful core start; parent INIT
does not invent a scan. This permits the next WPA SCAN_REQ, whose shared setup
requires both UP and RUNNING. Administrative DOWN stays stopped. A newer request
wins after stop/start; dying during start stops the fresh core and publishes no
obsolete parent or RX/TX acceptance. Normal subsequent scan channels do not
repeat the cold start. Direct WME EDCA updates do not use this state barrier.

The reconnect contract executes actual newstate/worker/core and native
management-queue drain. It executes the unchanged SCAN_REQ arm and complete
setupscan body. Kernel services, peer tables, parent state outcomes and documented
hardware-reset effects are modeled. It checks both bands, all DATA queues,
management references, INIT-only scan/rejoin, admin DOWN, timeout/readiness and
allocation/shutdown failures, stale requests and dying. The separate routing
consumer executes real newstate and softintr to check a queued old RX epoch;
latest worker acceptance is an explicit model input in that fixture. The host
contracts do not execute a complete WPA exchange or prove physical quiescence,
reconnect latency, RF throughput or loss recovery. Full firmware reload and
calibration add latency; measure it on the board before qualification.

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
device. The bounded source-only signal-quality path below does not enable
firmware RSSI feedback, thermal power tracking or higher-rate modes.
Source contracts do not establish a linked wireless network.

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

The core provides the native WME hardware consumer. Independent bounded
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
Native attach now explicitly enables legacy WME after strict consumer
registration. Software default initialization waits for the late BSS node.
HT/VHT advertisement remains disabled.

A GCC16 CIX kernel at `ee13cd4c` passed bounded physical queue/data checks on
one 2.4 GHz AP. A complete repeat delivered twelve ICMP replies per AC, with
matching producer counters and no new interface, DMA, drop or ring-full errors.
The first BK probe lost one reply. Normal IPv4 SSH service recovered with its
existing IPQoS policy. Bidirectional 1 MiB transfers over normal SSH, raw IPv4
and raw IPv6 retained matching SHA256. WPA2-PSK/CCMP, DHCP and SLAAC remained
available. That kernel used legacy basic-rate AUTO; it did not include the new
RA, reconnect or hardware-report atoms above. These trials do not establish
RF scheduling, fairness, adaptive rates, throughput limits or endurance.
Cold-calibration quality remains unqualified.

A GCC16 CIX kernel at `e25f25939` also passes the adaptive legacy RA,
linked-peer restart and hardware-report path on one 2.4 GHz channel 8 AP.
Each AC delivers twelve ICMP replies without new interface or DMA errors.
Bidirectional 1 MiB SHA256 checks pass over normal SSH, raw IPv4 and raw IPv6
TCP. Raw downloads take 0.337 and 0.333 seconds, respectively, about 25 Mbps.
Descriptor seed rate 0 does not establish the over-air rate. Firmware RA
reports rate code 11 within the current firmware generation; they remain
unattributed to an association token or air ACK.

Three reassociations and three administrative down/up cycles restore
WPA2-PSK/CCMP, IPv4 data and SLAAC. External IPv6 SSH passes after each cycle.
IPv4 data readiness takes 28, 12, 15, 15, 15 and 18 seconds. Conflict detection
remains enabled. Firmware generations advance from 2 to 14, with a fresh
hardware report after each restart and no interface, DMA, drop or full errors.
The report claims BW 20/40/80, NSS 2, two antennas and VHT. These firmware
claims do not enable a PHY mode or qualify the analog path. Full AP-loss
recovery, upper 5 GHz on this AP, HT/VHT and endurance remain unqualified.

The shared [TXOP width prerequisite](net80211-txop.md) preserves wire values
and rejects unsupported consumer widths. The shared
[STA offer prerequisite](net80211-wme.md) validates emitted offers, peer
parameters and sequence domains. The optional strict STA callback applies
immutable parameters synchronously
and commits software state only after success. Its native consumer uses core
quarantine on I/O errors. Contextual default EDCA is checked through
the actual native state worker and core with modeled scheduling. Native
four-AC wrapper routing uses actual classification before encap, target-queue
backpressure before sequence/crypto, and an all-AC capacity gate for ALTQ/TBR.
The complete attach/offer/response/RUN path has host source contracts.
Physical radio QoS acceptance remains separate.

Run the actual-source contracts with GCC16:

```sh
CC=gcc-16 sh ember/tools/rtw88-contract.sh
CC=gcc-16 sh ember/tools/rtw88-pci-contract.sh
CC=gcc-16 sh ember/tools/rtw88-start-contract.sh
CC=gcc-16 sh ember/tools/rtw88-fixed-contract.sh
CC=gcc-16 RTW88_MEDIA_ANALYZE=1 sh ember/tools/rtw88-media-contract.sh
CC=gcc-16 RTW88_ROUTING_ANALYZE=1 sh ember/tools/rtw88-routing-contract.sh
CC=gcc-16 RTW88_ENABLE_ANALYZE=1 sh ember/tools/rtw88-enable-contract.sh
CC=gcc-16 RTW88_RA_ANALYZE=1 sh ember/tools/rtw88-ra-contract.sh
CC=gcc-16 RTW88_RECONNECT_ANALYZE=1 sh ember/tools/rtw88-reconnect-contract.sh
CC=gcc-16 RTW88_RECONNECT_ANALYZE=1 sh ember/tools/rtw88-aploss-contract.sh
CC=gcc-16 RTW88_HWFEATURE_ANALYZE=1 sh ember/tools/rtw88-hwfeature-contract.sh
CC=gcc-16 HT20_ANALYZE=1 sh ember/tools/net80211-ht20-contract.sh
CC=gcc-16 sh ember/tools/rtw88-wme-contract.sh
CC=gcc-16 sh ember/tools/rtw88-fw-init-contract.sh
CC=gcc-16 sh ember/tools/rtw88-net-contract.sh
CC=gcc-16 sh ember/tools/rtw88-ifconfig-roaming-contract.sh
CC=gcc-16 sh ember/tools/rtw88-mbuf-contract.sh
CC=gcc-16 sh ember/tools/rtw8822c-txpwr-contract.sh
CC=gcc-16 sh ember/tools/rtw8822c-coex-contract.sh
CC=gcc-16 sh ember/tools/rtw8822c-dack-contract.sh
CC=gcc-16 sh ember/tools/rtw8822c-init-contract.sh
```

Earlier station checks cover 2.4 GHz and 5 GHz. The fixed-rate tests above
cover one 2.4 GHz AP. RF quality, adaptive rates, HT/VHT, AP-loss recovery and
long-term operation remain unqualified.

## First acceptance scenario

Use GCC16 cross builds on the development host and the physical board for
radio execution. Keep the kernel, modules and firmware manifest coherent.

1. Identify the actual silicon revision and calibration data.
2. Validate firmware sections and DMA addresses before enabling transfers.
3. Scan permitted channels and associate with a controlled WPA2-Personal AP.
4. Obtain a DHCP address and transfer 16 MiB in each direction with matching SHA256.
5. Complete three reconnect cycles and a ten-minute continuous workload.
6. Detect AP loss, report bounded failures and recover without reboot.
7. Repeat a cold boot using the same verified artifacts.

The transfer size and duration are proposed acceptance criteria, not results.
WPA3, HT/VHT aggregation, AP mode, monitor mode and power management are
outside the first station scenario. Higher rates and additional bands need
separate evidence. Do not infer them from the chip data sheet.

## Validation and recovery

Test firmware and efuse bounds, descriptor codecs, address overflow,
resource failure cleanup and stale completion handling before integration.
Add executable causal regressions, cross-build checks and applicable static
analysis. Contracts and VM execution do not establish physical DMA or radio timing.
The board's 32 GB memory configuration requires an explicit DMA address
contract. Never silently truncate an address or disable the IOMMU.

Retain the accepted diagnostic SD image and coherent source manifests.
Use only the selected disposable external SD. Do not write internal storage
or replace the stock board firmware as part of this driver proposal.
Publish the tested capability boundaries after independent review and the
physical station scenario. Existing DDR training and ACPI observations
remain separate from Wi-Fi diagnosis.

Follow [CONTRIBUTING](../../CONTRIBUTING.md), the
[kernel guide](kernel.md) and the [board worksheet](board-change.md).

## Idle AP-loss source contract

`rtw88-aploss-contract.sh` executes the actual native watchdog, state request,
RA invalidation, policy epoch, shared beacon-miss and node-leave notification.
It also executes the owning RUN-supervision and RUN-to-SCAN source branches.
The interface countdown is actual source; timer dispatch and hardware worker
completion are explicit host services. This fixture does not repeat RF restart.
Functional mutants remove idle supervision, policy withdrawal, RA withdrawal
and the disconnect event. Each mutant must compile and then fail assertions.

The first timeout sends a directed probe; the next miss requests SCAN.
WPA uses MANUAL roaming. The actual shared leave path emits DISASSOC and
link-down, while the BSD userland route case delivers EVENT_DISASSOC.
The event consumer also aborts an outstanding connected scan. The supplicant's
subsequent network selection, scan, handshake and address recovery are outside
this host fixture. No wpa_cli operation is used by the tested source path.
The fixture retains the current shared beacon-miss-count lifecycle across RUN.
It also executes the actual HT20 state-request and reset helpers with the
real capsule layout. The AP-loss suite has no active HT provider. An unexpected
active RUN profile fails the host boundary check; the separate HT20 suite owns
its validity semantics. The native helper calls must occur before the PCI lock.
A pending offer is reset by the actual SCAN request without weakening its fence.

The source path is present; this change adds regression coverage only.
It adds no second timer, AUTO-roaming override or radio capability.
Actual AP disappearance and return remain a separate physical acceptance gate.
Controlled reassociation and interface down/up do not establish that gate.

## Bounded relative signal quality

`rtw88_rx_signal_decode` consumes only the descriptor-bounded driver-info bytes.
It accepts current-generation RTL page0 CCK and page1 legacy OFDM observations
for the actual two-path BW20 profile. CCK correction uses three real gain-bound
readbacks after PHY initialization. Missing, malformed, stale or unsupported
metadata has an explicit numeric reason and quality0; valid DATA is still delivered.
Readback errors invalidate CCK context without reusing previous boot values.

The core decodes before borrowed DMA bytes expire. The native callback copies
quality and firmware generation into its bounded epoch-tagged queue. Softintr
rejects stale firmware owners and supplies the frame's unsigned0..100 index to
the existing stack entry point. It does not pass negative power through that ABI.
The index follows `rtw_phy_power_2_db(signal_power)` from the pinned BSD source.
It is relative stack quality, not the two-path firmware RSSI average or SNR.
The optional absolute dBm scan flag remains clear. No H2C0x42 is added.

`hw.rtwce_stats` exposes the last `signal valid/reason/generation`, page, length,
rate, channel, paths, descriptor BW, RXSC, raw PWDB/gain, vendor power estimates
and current gain-cache generation/validity/bounds/error. `sig0` counts valid
metadata; `sig1..10` count missing/short/page/rate/channel/width/gain/generation/
numeric/path reasons. `fw_stale` counts native queued frames from an old firmware.
No payload, address or network name is included. These are vendor observations;
RF calibration quality and physical signal acceptance remain separate.

```sh
CC=gcc-16 RTW88_START_ANALYZE=1 sh ember/tools/rtw88-signal-contract.sh
```

The contract executes the actual parser, core RX and native callback/softintr.
The DMA device, mbuf allocation and stack dispatcher are explicit host services.
A separate actual `ieee80211_add_scan`, node comparison and scan-copyout consumer
checks weak/strong selection and unsigned ABI output. It does not replace that
result with a parameter logger. This is not the full `ieee80211_input` path.
Causal baselines retain the old core0 and queue0 results before the respective
wire changes; both fail actual scan ranking. No physical RSSI or speed claim
follows from these synthetic tests. Primary packet samples and a GCC16 target
build remain required before hardware acceptance.

### Physical relative-signal boundary

GCC16 kernel `76ccc895977720ba2745cefcf0f6a31fcec7c1b3` passes an
unattended warm boot, WPA2/CCMP, DHCP/SLAAC and normal SSH on one 2.4 GHz AP.
Actual CCK page0 and legacy OFDM page1 reports have32-byte PHY records and
current-channel metadata. Eight finite page1 snapshots yield relative quality
62..65. After one reassociation and one administrative down/up, valid PHY
observations and gain-cache generations advance2,4,6 together. IPv4 data returns
in27/18 seconds; LAN link-local IPv6 SSH succeeds after each cycle.

All four ACs return12/12 ICMP replies. Bidirectional1MiB transfers over SSH and
raw IPv6 preserve SHA256; interface, DMA, crypto and queue-full error counters
remain zero. These checks do not establish calibrated dBm, maximum speed,
AP-loss recovery or endurance. Invalid metadata counters remain visible.
ADCK/DACK residual warnings persist. HT admission remains disabled.

### Disruptive manual-scan ownership

A legacy `SCAN_REQ` restarts scanning through a forced INIT state. For an
associated STA, setup now calls the existing `ieee80211_sta_leave` before
that bypass. This retains node/key cleanup and the DISASSOC notification.
The supplicant can then select the same BSSID and request a fresh association.
It cannot retain a false COMPLETED state after hardware and RA were reset.
Initial, repeated and non-STA scan requests retain their prior behavior.
The active channel list and configured PHY/media constraint are preserved.

The source contract runs actual setupscan/SCAN_REQ, the complete native worker
and core, the owning shared SCAN branch and leave notification, BSD routing
consumers, supplicant identity reset and its same-BSSID connection decision.
It checks both legacy bands, first AUTH posting and a fresh RUN/RA commit.
AUTH frame construction, node crypto storage, OS event transport and successful
on-air AUTH/ASSOC/WPA responses are explicit services. The full supplicant event
handler, RF scan scheduling and physical interoperability are outside this host
qualification.
The separate crypto and HT contracts retain their own key/node lifecycle gates.

```sh
CC=gcc-16 RTW88_START_ANALYZE=1 sh ember/tools/rtw88-manualscan-contract.sh
```

GCC16 kernel `d175df270` passes two manual scans from a linked STA on one
2.4 GHz AP. WPA2 and IPv4 data recover without an explicit disconnect or
reassociate command, in 26 and 34 seconds. Firmware generations advance
from 2 to 4 to 6; each new RUN sends a fresh RA configuration. Link-local
IPv6 SSH works after each cycle. Subsequent four-AC probes and 1 MiB
SSH/raw-IPv6 transfers pass with matching hashes in both directions.
These bounded trials do not qualify AP loss, reconnect latency or endurance.
This change does not implement an association-preserving background scan.

## Managed register-mailbox checkpoint

The existing native state worker plans zero to three register-mailbox commands.
A non-started INIT has no mailbox access or wait. Started unlinked setup sends
media0x01; linked setup also sends port0x2c and, when needed, legacy RA0x40.
Busy required boxes cause a bounded process sleep without holding PCI. Each
wake returns to actual HT/BSS/security/rate/EDCA/RA preparation under Giant.
An unchanged request epoch does not make an old profile snapshot current.

Once all required boxes are free, the existing locked setup uses a scoped
single-read command reservation. It cannot fall back to a one-second locked
poll if a box unexpectedly becomes busy. Original bytes, order and box rotation
remain unchanged. Request, firmware generation and basebox mismatches cancel
old work. Every outcome releases its software reservation. The actual first
AUTH/RUN producer remains after the complete current commit.

The existing native capacity check gates management and all four DATA queues
before dequeue while admission is closed. It retains queued mbufs and node
owners. Source contracts execute this gate, the actual management producer,
core descriptor post and scheduled softintr continuation. Their kernel sleep,
clock and interrupt dispatch are explicit host services. They do not qualify
real scheduling latency or a full concurrent stop/detach execution.

The same-generation HT-spacing case executes actual provider/capsule checks.
That test profile does not enable HT on hardware. Packet-H2C transport, RFK
commands/completion, RX BA and R2 firmware RSSI feedback remain separate.
Run the existing enable/routing contracts with GCC16. GCC16 kernel `d5b249d0b`
passes a clean warm boot and two disruptive manual scans on one 2.4 GHz AP.
Automatic WPA2/IPv4 recovery takes 27 and 26 seconds; link-local IPv6 SSH
works after each cycle. Four-AC probes return 12/12 replies each. Subsequent
1 MiB SSH/raw-IPv6 transfers preserve hashes in both directions.
These trials qualify legacy regression. They do not force occupied mailboxes
or measure scheduler latency. HT admission remains disabled.

### Bounded shared RX BA foundation

The [RX BA contract](net80211-rxba.md) connects actual Action/output producers,
ciphertext reorder, key lifecycle, ordered CCMP/AES input and native timer
ownership. The per-node limit includes retained mbuf allocations and metadata.
A stale standalone A-MSDU grant cannot bypass current HT/security checks before
CCMP processing. Narrow compressed BAR ingress retains existing receive gates.
These source contracts do not enable HT or configure hardware BlockAck.

The GCC16 AArch64 kernel `6decb0f51` passes a legacy regression on the physical
CIX board. Two disruptive scans recover WPA2/IPv4 in 27 and 12 seconds.
LAN link-local IPv6 SSH works after each. A bounded four-AC recheck receives
12/12 replies per queue. Subsequent 1 MiB SSH/raw-IPv6 transfers preserve hashes
in both directions. The first four-AC attempt fails while reading diagnostic
stats with `ENOMEM`; it does not qualify as a passing run. The capacity-hint fix below addresses that read race. HT admission stays off.

## Posted native TX drain before managed setup

The actual state worker now waits for already-posted DATA/MGMT descriptors
before channel/station writes and its existing mailbox reservation. It shares
the IRQ's device read-index completion decoder, including wrap validation and
exactly-once callbacks. It never fabricates completion by clearing queue counts.
The existing all-five admission fence retains queued mbuf/node/crypto owners.

A finite per-request drain deadline survives every wake. PCI is released before
sleep; the outer worker revalidates the full current peer/profile afterward.
Read/index/timeout failures follow the existing INIT/stop owner. A non-started
INIT has no drain IO or sleep. Linked-peer full-reset ownership is unchanged.

Host contracts execute real posted MGMT, completion IRQ and first AUTH/RUN,
plus an explicitly broader five-queue core matrix. Bus RP progress and scheduler
execution are modeled. The HT exchange model rejects posted-DMA inputs outside
its declared domain; it does not fabricate successful hardware draining.
GCC16 AArch64 kernel `4f152fc21` passes clean warm boot and legacy regression.
Two disruptive scans recover in 26/33 seconds, with link-local IPv6 after each.
Four-AC probes return 12/12 replies. SSH/raw-IPv6 1 MiB hashes match both ways.
Descriptor consumption does not prove radio idle, an air ACK or RFK readiness.
Packet-H2C, calibration and hardware RX BlockAck remain separate prerequisites;
the real HT provider remains NULL.


### Bounded diagnostic string retries

The stats handler keeps its 4096-byte heap allocation outside driver locks.
A short read or size query now returns that capacity as a retry hint.
A sufficient read still returns the actual string length. Generic validation,
copy errors, write protection and formatter errors keep their existing behavior.
This prevents counter growth between the CLI's first read and its single retry
from causing a second `ENOMEM`. No zero padding or global sysctl change is used.

The [actual-source contract](../tools/rtw88-sysctl-contract.c) executes the real
CLI string branch, generic lookup and wrapper. It passes 1000 checks with GCC16
O0/O2 and ASan/UBSan. GCC path analysis and Cppcheck pass. CSA retains one pinned
inherited dead store in generic lookup and finds no new issue; it is not clean.
On physical kernel `4f152fc21`, 100 CLI reads interleaved with 20 peer pings pass.
Strings span 1373 through 1380 bytes, exceeding the CLI's initial 1024 bytes.
These bounded tests do not qualify arbitrary concurrency or long-term stability.


### Pinned default-ACK register parity

The final native RCR mask now retains `BIT_VHT_DACK` (bit 26).
It changes from `0xf010400e` to `0xf410400e`; every other bit stays unchanged.
Earlier native MAC initialization already set this bit. The final mask had
cleared it. FreeBSD source `d2145e9b96448da34d3cfede24c509af6d830c92`
sets the bit unconditionally in its default-ACK setup. Its RTL RX aggregation
start/stop callbacks perform no per-TID device operation.

The actual-start contract detects the old mask with five failures.
The corrected source passes 338601 checks, including station unlink/link,
stop/start and final-write failure recovery. Appended BA SSN, hardware crypto,
BA parser state and BAR ACK type remain unchanged. GCC path analysis,
Cppcheck and CSA pass for the start and core/PCI consumers.

GCC16 AArch64 kernel `9c1de345a` passes a physical legacy regression:
clean warm boot, WPA2/CCMP, DHCP, IPv6 and two disruptive scan recoveries
in 26/12 seconds. Each recovery restores LAN link-local IPv6 SSH.
Four queues return 12/12 replies. Bidirectional 1 MiB SSH/raw-IPv6 hashes match.
A separate 11a transition fails before scanning: initial setup retains an
old 2.4 GHz channel. After the trial restores AUTO, data returns in 26 seconds.
That failed trial leaves seven output errors; it does not qualify 5 GHz
on the current AP. Earlier data/queue probes have zero error increments.

This result qualifies register-source parity and bounded legacy regression.
It includes no direct physical RCR readback or transmitted ACK/BlockAck proof.
Hardware BA with software CCMP needs independent radio evidence, including
sequence/bitmap behavior and reconnect teardown. The HT provider remains NULL.
Calibration, HT/VHT and maximum throughput remain unqualified.


### Initial scan after a band change

Native initialization now selects a declared active initial channel if the
previous channel is NULL, ANY or outside the current active set.
It runs after the existing stop owner and before core start.
An already valid current channel stays unchanged. An active set without
any declared channel fails before firmware start. Shared scanning still owns channel
hopping and BSS rates. Mode, EDCA and slot guards stay strict.

The existing actual-source contract now connects shared media selection,
native initialization, the state worker and shared INIT/SCAN arms.
It executes the real scan start/reset/next-channel functions. Kernel services,
node reset and other states remain explicit model boundaries.
The old source fails 57 assertions. The corrected source passes 379384 checks,
including reverse band changes, narrow lists, coalescing and startup faults.
ASan/UBSan and three static analyzers pass. The exact CI sparse input set passes.

GCC16 AArch64 kernel `3d9255b15` passes clean warm boot and legacy recovery.
Two disruptive scans restore WPA2/IPv4 in 26/26 seconds, with IPv6 SSH after each.
A separate 11a trial reaches channel 149 and receives PHY reports and beacons.
The old initial-setup EINVAL does not recur. Authentication still times out;
no 5 GHz data acceptance is claimed. Explicit AUTO restoration returns data
in 27 seconds. HT/VHT admission remains disabled.

The diagnostic sysctl host contract now uses visible byte transfers for its
copyout service. It retains exact-length, zero-length and copy-fault checks.
A true uninitialized source byte must fail its O2 analyzer control.
Production sysctl, generic lookup and CLI bodies are unchanged.
The historical Linux bulk-copy analyzer failure is retained as evidence.
Local runtime and analysis pass; exact Linux CI acceptance is still pending.
