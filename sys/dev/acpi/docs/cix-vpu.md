# CIX P1 H.265 codec foundations

Status: diagnostic ACPI attachment and checked source foundations.
The hardware driver is incomplete.
Origin: EmberBSD contributors, AI-assisted.

EmberBSD identifies the CIX P1 codec and validates its ACPI descriptions.
The default resource query does not activate the codec or establish its power state.
There is no video device or M2M interface.
The manual allocation and preparation profiles read an image into CPU memory.
An explicit boot trial publishes the prepared root and starts the engine.
Firmware readiness and frame encoding remain unqualified.
The target is Orange Pi 6 Plus with CIX CD8180 and stock ACPI firmware.
The OS owns firmware, DMA, interrupts, power/reset and the application API.
Ports owns FFmpeg and GStreamer consumers.

## Implemented scope

The independent BSD source in `sys/dev/ic` implements bounded foundations:

| Source | Contract | Remaining caller responsibility |
|---|---|---|
| `cixvpu_fw.[ch]` | Validate the protocol 3 firmware header, text extent, BSS bitmap and shared-region layout. Reject invalid input without publishing partial output. | Select an authorized blob, allocate pages and load/boot firmware. Structural validity is not authentication. |
| `cixvpu_ring.[ch]` | Pack and unpack bounded message rings, including the selected `-sum` firmware's asymmetric checksum framing. | Own coherent snapshots, locking, DMA synchronization, barriers and index publication. |
| `cixvpu_command.[ch]` | Serialize JOB and selected option payloads. Decode bounded control replies without losing option failures or missing indices. | Own session policy, ring framing, command submission, response matching and DMA synchronization. |
| `cixvpu_buffer.[ch]` | Serialize closed NV12 and empty BITSTREAM descriptors. Check returned identity, geometry and byte bounds against known slots. | Prove page ownership, perform DMA synchronization, submit queues and interpret completion separately. |
| `cixvpu_mmu.[ch]` | Encode 40-bit bus addresses and checked two-level page entries. Reject invalid permissions, occupied entries and stale removals. | Allocate/map DMA memory, stop the device and manage caches/TLBs. Removal is not safe during active DMA. |
| `cixvpu_fw_plan.[ch]` | Plan protocol-3 backing owners and firmware VA mappings without allocation. Check the text offset, BSS bounds, aliases, table coverage and page quota. | Allocate validated DMA backing, copy data, synchronize and publish only after stopped-device checks. |
| `cixvpu_dma.[ch]` | Allocate one native `bus_dma` owner per backing page. Seal moved ownership, synchronize unique maps and reject release after recorded exposure. | Keep addresses hidden. Qualify the hardware handoff, runtime synchronization and eventual teardown separately. |
| `cixvpu_prepare.[ch]` | Recheck the image pin and plan, verify CPU tables and move ownership. Offer initial synchronization, fixed boot messages and a guarded root capability. | End all mutable borrows before preparation. Serialize access and retain every exposed owner until a qualified teardown exists. |

The attachment selects the header parser, layout planner and CPU-only allocator.
The kernel also links CPU preparation, MMU and ring helpers. The explicit manual
preparation profile calls ordinary CPU preparation. A separate manual boot
profile calls the typed constructor, root publication and receive helpers.
`sys/videoio.h` adds the standard `V4L2_PIX_FMT_HEVC` fourcc without changing
structure layout or ioctl numbers. The existing `video(4)` driver remains
capture-only; the presence of M2M declarations does not implement two queues.

## Diagnostic ACPI attachment

`cixvpu_acpi` matches `CIXH3010`. `EMBER64` selects this diagnostic attachment.
It parses `_CRS` through the standard ACPI resource parser and evaluates `_CCA`.
Before that parse, an ACPICA resource walk rejects unsupported descriptor types.
Only `Memory32Fixed`, `ExtendedInterrupt` and `EndTag` are accepted.
This prevents the common parser from silently dropping extra unsupported resources.
The independent BSD validator in `sys/dev/ic/cixvpu_resources.[ch]` requires:

- Exactly two fixed memory resources and one interrupt, without I/O, variable
  memory ranges or DMA-channel resources.
- Each memory span has at least 64 KiB and four-byte aligned addresses and length.
- Each inclusive end fits in 64 bits. Bus spans and translated CPU spans do not overlap.
- CPU addresses do not wrap below their bus addresses during unsigned translation.
- The interrupt is a standard GIC SPI, GSI 32 through 1019, with level triggering.
- `_CCA` exists and is an integer with value zero or one. No coherence default is assumed.

The validator does not use a fixed physical-address allowlist.
It accepts relocated and adjacent disjoint spans, including a span ending at `UINT64_MAX`.
The attachment prints validated addresses, interrupt and coherence information.
Malformed or missing descriptions produce an error. Parsed lists are always released.
The standard parser supplies triggering but does not preserve interrupt polarity
or sharing in `struct acpi_irq`; this stage does not claim to validate those fields.
It does not accept extended GIC SPIs or variable placement requirements.

Firmware tables from one Orange Pi 6 Plus, CIX CD8180, A4 PHECDA-Rev A,
`BiosV1.5-W20260706-e321056a`, describe two 64 KiB `Memory32Fixed` windows,
GSI 358 and `_CCA = 0`. This is firmware evidence, not codec execution.
The driver reads the running firmware's descriptions rather than copying those addresses.

The default EMBER64 attachment defers a checked resource query until the ACPI
providers have attached. It validates the CLKT/RSTL/_PR0/_PR3 references,
claims the providers, reads GCLK and releases untouched claims. It does not
operate reset, activate VPU power, map VPU MMIO, allocate DMA, load firmware,
establish an interrupt handler or register a video device.
`flags 0x0` selects descriptions only. An explicit `flags 0x2` enables the
bounded [identity probe](cix-platform.md); it still does not start DMA or firmware.
The separate native profile uses SCMI128 and VPU3. One physical diagnostic boot
accepts clock67 enable/disable and reads ID `0x56648002`, four cores and four slots.
Checked reset/clock/power cleanup releases the controls. This does not qualify
per-core memory repair, device-visible DMA, firmware execution or video queues.
A separate [manual native hardware profile](cix-platform.md#manual-native-hardware-profile)
adds seven fixed raw state reads under the same controls. Its physical trial
reports a valid snapshot and checked cleanup on the observed board.
Parsing descriptions alone does not establish power, clock or reset ownership.
The manual allocation profile below uses the parser, planner and allocator.
The preparation profile also uses the page-entry encoders. Neither uses the ring.

The selected `-sum` protocol adds a cumulative checksum word between the TX
header and payload. That word is outside the header's payload size.
RX messages have no inline checksum. Their validation uses the entire committed
snapshot, prior consumed total and three published cumulative totals.
The helpers preserve payload bytes and checksum state on malformed input or
insufficient space. A short receive buffer can report the required payload length.
They do not emulate firmware or publish device-visible indices.

## Typed control command codec

The pure `cixvpu_command` codec writes protocol payloads, without native submission.
JOB accepts representable core and frame counts, including zero frames for unlimited
work, and the defined boundary-manager flag. The one-frame fixture uses cores 1,
frames 1 and flags 0. Hardware core limits remain caller policy.
The scalar option allowlist contains NALU format index 1, P-frame count index 16,
B-frame count index 17 and fixed-QP index 56. Counts use the full uint32 range;
fixed QP accepts the closed HEVC range 0 through 51. Wire validity does not
establish that the selected firmware accepts an option.

A typed helper emits HEVC Main profile 1 and one of the 13 explicit main-tier
level tokens. These are protocol values, not HEVC level_idc. No runtime level,
frame geometry, frame rate, QP or output capacity is selected by the codec.
Separate INPUT helpers emit FRAME_RATE type 9 with nonzero raw Q16 data, or
RATE_CONTROL type 10 with mode, target and maximum bitrate all zero. These
8-byte and 16-byte payloads use INPUT queue code 3003. They are not MESSAGE
SET_OPTION requests and do not create an option-confirmation transaction.
Global fixed QP remains an 8-byte MESSAGE option 56, separate from RC_OFF.

The response decoder distinguishes confirmation, option failure, fatal error,
state change and job dequeue. The reference describes both empty and indexed
confirmations. The decoder accepts exactly zero or four bytes and reports
`index_valid`; an absent index does not imply a matching pending option.
State replies accept STOPPED 0 or RUNNING 2. Failure text remains 128 bounded raw
bytes, without an assumed NUL terminator. Malformed recognized replies and
unknown codes fail without changing output.

The actual-source contract uses independent literal bytes and the existing ring
codec. It checks endian layout, bounds, aliases, unaligned wire bytes, short
buffers, failure semantics, ring wrap and checksum wrap. CI runs the portable
sources with GCC16 analysis, Cppcheck, CSA, sanitizers and Linux Memcheck.
The manual first-frame policy uses this codec through the opaque bridge.
The codec adds no MMIO, DMA owner or interrupt handling. Physical one-frame
encoding and independent decode remain pending.

```sh
CC=gcc-16 CFLAGS=-fanalyzer CIXVPU_COMMAND_ANALYZE=1 \
    sh ember/tools/cixvpu-command-contract.sh
```

## Closed buffer descriptor codec

The pure `cixvpu_buffer` codec emits fixed 72-byte FRAME and 40-byte BITSTREAM
payloads. The first FRAME profile is progressive, positive even NV12 geometry,
with separate Y/UV strides and capacity spans. It checks each full plane span,
wide stride products and disjoint capacities. Unused fields remain zero.
This is a closed local profile, not a statement that firmware rejects other formats.
BITSTREAM serialization creates an empty output descriptor for a known slot.

Return decoders require the expected handle, tag, firmware VA and capacity.
For BITSTREAM, the caller supplies the correlated outstanding INPUT
`user_data_tag` as the expected tag. It need not equal the empty output
submission tag. The output slot still determines the handle, VA and capacity.
FRAME also checks every geometry and unused byte, except its raw flags.
The result preserves raw flags and separate rejected/corrupt indicators.
BITSTREAM bounds offset and filled length by the remaining capacity, without
wrapping their sum. Flags, frame type, transform and remaining kilobytes are
numeric observations. Parsing success does not mean consumption, a complete
access unit, successful encoding or permission to release an exposed owner.

The aperture checks do not prove mapped backing or authorize an address.
The [stopped slot layout foundation](#stopped-slot-layout-foundation) excludes
existing RPC logical reservations. An explicit CPU constructor checks owner
binding; native queue authorization remains separate. No physical geometry or buffer capacity is
selected here. The module does not access pixel/encoded data, modify page tables,
synchronize DMA, submit native queues or issue GO.

Tests use independent literal payloads and actual TX checksum/RX plain ring
helpers. They cover identity, reserved bytes, aliases, full capacity spans,
wide-product overflow and returned-length overflow. Target GCC16 static assertions
check the pinned reference v2/v3 layouts. The VPU workflow configures GCC16 analysis,
Cppcheck, CSA, sanitizers and Linux Memcheck for the portable module and contract.
These source and layout checks do not qualify a hardware encoder.

```sh
CC=gcc-16 CFLAGS=-fanalyzer CIXVPU_BUFFER_ANALYZE=1 \
    sh ember/tools/cixvpu-buffer-contract.sh
```

## Stopped slot layout foundation

`cixvpu_fw_plan_build_frame_slots` adds an inactive one-core protocol3.5 layout.
The caller supplies even NV12 geometry, both strides and BITSTREAM capacity.
The planner computes required plane bytes and rounds each storage span to pages.
Y starts at firmware VA `0x70000000`; UV follows its rounded storage.
BITSTREAM starts at `0x24400000`. Each plane and BITSTREAM remains bounded to
4 MiB; the contiguous Y+UV range uses one or two 4 MiB FRAME leaves as needed.
Leaf selection follows each page VA, including UV crossing a leaf boundary.
These addresses are local layout policy, not bus addresses or hardware defaults.

The full first RPC reservation `[0x20000000,0x24000000)` stays excluded,
including invalid holes. The second 16 KiB RPC object also stays separate.
The selected bootstrap's 390 owners and 385 mappings become `391+L+P` owners
and `385+P` mappings, where `P` counts all slot pages and `L` is the required
one or two FRAME leaves. ROOT and leaves occupy `6+L` tables; BITSTREAM retains
its separate leaf. The unchanged 2048-owner quota includes every table and data
owner. Other images use their validated base count.

The stopped source mechanism can represent 2048×1500 NV12 with strides 2048:
Y occupies 750 pages and UV 375, starting at `0x702ee000`. UV shares the first
FRAME leaf, then uses 101 pages of the second at `0x70400000`. With an explicit
64 KiB test output, this image uses 1534 owners, 1526 mappings and eight tables.
That output size is a contract fixture, not qualified encoder capacity. The
existing native 128×128 and CPU-only profiles retain their layouts and counts;
this mechanism does not select a larger native experiment.

Slot accessors return checked VA spans and unique backing indices. They do
not allocate, copy pixels, build live PTEs, synchronize DMA or grant ownership.
Ordinary plans keep their existing indices and mappings with slots disabled.
The allocator accepts the explicit slot roles. Ordinary CPU constructors still
reject all slot metadata before page borrows, writes or ownership movement.
Only the explicit slot constructor described below accepts this layout. A manual
CPU-only probe selects it. Command submission, output refill and one-frame encoding
remain outside that probe. The bridge below supplies the transport for the
separate manual first-frame source policy. Physical frame acceptance is pending.

Actual-source tests compose MMU tables with discontiguous synthetic bus pages.
They check literal slot spans, zero tails, full reserve exclusion, exact quotas,
invalid metadata and ordinary constructors' refusal before side effects.

```sh
CC=gcc-16 CFLAGS=-fanalyzer CIXVPU_PLAN_ANALYZE=1 \
    sh ember/tools/cixvpu-fw-plan-contract.sh
```

## Stopped CPU slot preparation

`cixvpu_prepare_frame_slots_cpu` takes a validated slot plan, the trusted full
firmware pin and two immutable CPU plane spans. Each span must have the exact
required length, including row padding. Both source planes are copied before
seal. Storage tails and the complete BITSTREAM backing stay zero.

The constructor checks known input/handle/owner spans and all loaded page
metadata before writing. It builds and verifies seven or eight stopped page tables,
seeds the existing PING, then seals and moves the owner. Errors preserve both
handles but can leave dirty CPU pages; free that stopped allocation instead of
retrying it. Success exposes no mutable page view.

Initial sync publishes each unique data page before leaves and root. Input
planes use PREWRITE. Output uses PREWRITE|PREREAD, so bounce memory receives its
initial zeros. Pre-exposure cancellation matches those masks. Every free remains
forbidden after recorded exposure. No runtime output-byte getter or native
submission is added.

Actual-source contracts check distinct source patterns, page tails, PTEs,
poisoned bounce memory, aliases, failures before effects, last-fallible seal and
retained exposure. No firmware-active profile, pixel format conversion, hardware
frame completion or output capacity is qualified.

## Closed active queue bridge

The opaque prepared owner has typed MESSAGE, INPUT and OUTPUT queue
operations. `cixvpu_prepared_queue_open` requires this exposed slot owner's
acknowledged SWITCHED_IN/core0 and PONG. It binds immutable frame/output handles
and one input tag. Geometry and storage come from the checked constructor.
The bridge inherits the seeded TX checksum and consumed RX cursor. It never
resets a queue. Legacy boot receive returns EBUSY after handover; closed RPC
stepping retains its existing three tuples and sixteen DEBUG replies.

Typed commands use the existing payload codecs. Setup ordering, confirmation
matching, deadlines and total budgets remain caller responsibilities. A successful
publication requires one caller notification before another queue or RPC step.
These APIs perform no MMIO and select no profile, runtime policy or allocation.

One notification gives one matching buffer-return credit. Input submits once.
Output return validates the fixed handle, correlated input tag, VA, capacity
and subtraction bounds. All maps and caller spans are checked before output
POSTREAD. Pagewise copy to caller-owned storage completes before BUFFER ACK.
Short capacity preserves the buffer, credit and caller outputs. Refill rearms
every output page with PREREAD before descriptor publication. EOF blocks refill.
The destination is not retained and survives hold or reuse of device storage.
No mutable view, DMA address getter or extraction service is exposed.

Malformed identities, shapes, checksums, cursors and retained maps fail closed.
A parsed fatal/option-failed event is returned once, then work stays held.
Hold is irreversible. STOP, FLUSH and IRQ masking do not permit exposed-owner
free. Unknown RPC tuples gain no allocation, PTE change or retry path.

Actual-source contracts use independent literal descriptors, poisoned bounce
pages, consumer/producer interleavings, cross-page partial-tail copy, queue
wrap/full conditions and fault injection. Only the separate manual first-frame
policy selects active operations. The CPU-only probe and strict bootstrap
profiles keep their existing behavior.
Native active queue traffic, device DMA data correctness, one HEVC frame and
host decode remain unqualified.

## Manual first-frame experiment

[EMBER64_CIXVPU_HEVC_FRAME_PROBE](../../../arch/evbarm/conf/EMBER64_CIXVPU_HEVC_FRAME_PROBE)
is a source-only manual experiment. It inherits the accepted RPC/UART2 profile,
including SCMI service 128, and selects VPU flags 0xa. The default remains inactive.
Its complete kernel build, native trial and host decode remain pending. Preserve working
default and recovery entries. Do not reuse a boot-long held owner.

The selected synthetic fixture is 128×128 progressive NV12 with both strides 128.
Y uses `16+28*((x/16+2*(y/16))%8)`; UV pairs use 90,160. The 24576-byte fixture
SHA256 is `30e6c0ed6b3eba1891dd011bfdc98641689a21c5ed40b731c6e589229fc66e40`.
The checked plan has 414 owners,407 mappings and 7 tables, with a 65536-byte OUTPUT
slot. Firmware and pixel staging are freed after typed preparation and before
initial DMA sync. CPU-only mode 9 retains its separate 2×4 padded fixture.

A fresh boot policy waits for both acknowledged SWITCHED_IN/PONG and all three
committed bootstrap RPC replies. It does not reset those budgets. It then sends
JOB_ONE; NALU start codes, HEVC Main/token13, P0, B0 and QP26; GO; PING;
FRAME_RATE 30<<16; RC_OFF; one FORCE_IDR+EOS input; and one empty output slot.
Only one option waits for CONFIRM. An indexed CONFIRM must match it.
Every committed publication gets one notification before another operation.

The active phase allows 5001 visits, 5 seconds, 128 messages, 32 publications,
65536 received protocol bytes, 4 output returns and 262144 accumulated host bytes.
Each visit allows 16 protocol actions and 4096 bytes. RPC work runs first.
At publication credit zero, a fresh read-only gate permits bounded final RX
drain under FREE/RETURN. A new PARAM is held without response or notification.
The gate observes one advisory header copy and the private MESSAGE consumer;
it does not ACK, store, notify or create a transaction-stability guarantee.

Output copy completes before ACK. CODECCONFIG bytes are retained. Up to three
refills rearm output with PREREAD before publication. Completion requires an
accepted input return and correlated EOF/BSEOF with a nonempty result.
A JOB_DEQUEUED, STATE or PONG event alone is not frame completion.
Unknown RPC tuples, malformed metadata, timeout and clock/IRQ faults fail closed.
All exposure-success outcomes hold owners, claims and handlers permanently.
No automatic STOP, reset, free, new allocation or PTE growth occurs.

One successful constructor consumes the sole frame-export claim for that boot.
Constructor failure removes its owned sysctl nodes before freeing host storage.
After terminal freeze, `hw.cixvpu0.frame_status` exports 96 bytes: 24 LE32 words.
They are version 1, length 96, primary, cleanup, last stage, action, option,
transport-complete, byte count, output-return count, input/output flags, visits,
messages, received bytes, host publications/notifications, RPC commits/notifications,
IRQ seen/unexpected masks, and RPC state/call/size. Every byte is initialized.
This status is not a native struct or an address getter.

`hw.cixvpu0.hevc_frame` exports only the immutable host copy. Before freeze,
reads return EAGAIN. An empty result returns ENODATA. A failed nonempty prefix
remains readable; inspect status before using it. Owned child discovery preserves
native scalar metadata and clears pointer fields. Discovery supports current
ABI flags with node version zero. Nonzero node versions and emulation overlays
return ENOTSUP. It copies the request once; concurrent user changes cannot
redirect a second read into native versioned-query handling. Generic root/hw metadata retains its native sysctl ABI.
The callback permits a short copy; the user wrapper returns ENOMEM with required
size after a successful short copy. EFAULT does not guarantee required size.

For a reviewed physical trial, extract with `sysctl -b hw.cixvpu0.hevc_frame`.
Check exit status, complete length and SHA256 separately from encoder status.
Transfer the exact bytes to the development host. Require a real HEVC decoder
to produce exactly one 128×128 frame, then measure per-plane error and spatial
variation against the retained fixture. Lossy QP26 does not imply byte equality.
The bounded synthetic host contracts do not prove that real firmware accepts
this setup, tag policy, output capacity or post-GO closed RPC reserve.

Full 2048×1500 NV12 needs 4608000 bytes. The stopped planner/preparer can map
its two FRAME leaves, but this 128×128 experiment does not qualify team geometry,
two streams, 9 fps,
sustained operation, runtime DMA teardown or the public M2M interface.

## Manual CPU-only slot probe

[EMBER64_CIXVPU_SLOT_PREPARE_PROBE](../../../arch/evbarm/conf/EMBER64_CIXVPU_SLOT_PREPARE_PROBE)
uses plain EMBER64, manual UART2 console selection and VPU `flags 0x9`.
It selects no SCMI service. Keep the working default and recovery entries.
This manual diagnostic passed a full GCC16 kernel build and one physical warm
boot on CIX P1 CD8180 with PHECDA UEFI 1.5 and EC 1.15.
The tested source is `d956044f7fe434e535b5ab6044f1c9627d73f3af`.
The running revision was checked through UART and authenticated SSH; FFS was clean.

The existing mountroot callback reads, closes and hashes the same local
user-provided firmware file. The synthetic CPU fixture uses width 2, height 4,
strides 4097/2051 and output capacity 4097 bytes. These are test values, not an
encoder geometry or capacity recommendation. Its plan has 401 owners,
394 mappings and 7 stopped page tables.

The callback audits every allocated page is zero. It keeps both source planes
and firmware staging alive through the typed constructor. It then frees staging,
performs one initial sync, and checks prepared cleanup with matching cancellation.
A failed cleanup retains its owner and reports the error. No automatic retry
occurs. Modes 0 through 8 keep their existing behavior.

No root address is published or printed. The probe makes no VPU MMIO access,
IRQ registration, clock/power request, firmware command or GO submission.
It does not qualify device coherency, firmware-visible pixel bytes or encoding.
Actual-source orchestration tests check no hardware calls, exact source lifetime,
all 401 zero-page views, once-only callbacks, sync ordering and retained failures.
The separate IC tests check copied bytes, PTE words and poisoned bounce buffers.

The native run checked all 401 zero pages, 394 mappings and 7 page tables.
Preparation, initial sync, primary/close results and both cleanup results were zero.
Neither DMA nor prepared owners remained. The kernel gzip SHA256 is
`18e3e872d0d631f413f313512941054c99b0a8dec906a3e98ad7376782819f11`.
This accepts native CPU preparation and pre-exposure cancellation only.
It does not qualify device-visible pixels, an encoded frame or a cold/default boot.

## Manual CPU-only allocation probe



[EMBER64_CIXVPU_DMA_PROBE](../../../arch/evbarm/conf/EMBER64_CIXVPU_DMA_PROBE)
includes ordinary EMBER64 and selects VPU `flags 0x4`, without an SCMI service.
Select it manually and retain the working default and recovery kernels.
After root mounts, one `config_mountroot` callback calls
`firmware_open("cixvpu", "hevcenc.fwb", ...)`.
Supply a user-provided local file, normally at
`/libdata/firmware/cixvpu/hevcenc.fwb`, under its applicable terms.
The profile provides no downloader, package or firmware blob.
The applicable individual firmware licence remains unestablished.

The callback requires exactly 420736 bytes and the SHA256 recorded below.
It reads and closes the file, hashes all bytes, and calls the actual parser
and layout planner. The one-core fixture has 256 unique backing owners for
this image; it is not a claim about runtime geometry on the four-core device.
Staging is freed before native `bus_dma` allocation. The callback checks each
CPU page for zeros, reports role counts, then performs checked cleanup.
A cleanup failure retains the owner.
It prints no DMA addresses. It does not call VPU controls, access VPU MMIO,
copy firmware into DMA pages, build PTEs, synchronize or expose addresses.
Allocation has a bounded page quota; WAITOK is not a wall-clock deadline.

Source `61c007019a992103eced1adead758c881b14822a` passes 2194 actual-attachment
checks with GCC16 path analysis, Cppcheck, CSA and host ASan/UBSan.
Firmware, hash and DMA services are modeled; the parser and planner are real.
The separate actual-source allocator passes 3276792 checks, including 665
acquisition failures. The accepted physical manual kernel below links with GCC16.2.
The physical CPU preparation fixture below also exercises native allocation.
Device DMA data correctness, firmware readiness and video queues remain unqualified.

## Fixed bootstrap memory reserve

`cixvpu_fw_plan_build_boot_rpc` selects a separate one-core protocol-3.5 plan.
The ordinary builder keeps `boot_rpc` zero. The selected plan uses value one.
It maps 128 zeroed `RPC_DATA` pages at firmware VA `0x20000000` and reserves
`[0x20000000,0x24000000)` for the observed 64 MiB logical capacity.
The initial ALLOC remains 128 KiB. The measured RESIZE backing is 512 KiB.
Two extra leaves link root slots128 and144. The second object has four zeroed
SHARED_RW/WRITE pages at `[0x24000000,0x24004000)`, after the first logical reserve.
This host placement follows the reference's capacity cursor inside the v3
protected VA region. It is not a firmware-specified or physically accepted VA.
The observed image shape has 390 owners, 385 mappings and five tables in this
plan. The ordinary plan remains 256 owners/253 mappings.

Unused leaf and root entries stay invalid zero. This differs from the vendor
NOACCESS dummy-page encoding; identical fault behavior is not claimed.
Memory between 128 and 512 KiB is RW before exposure. The reference instead grows
backing and changes live PTEs. This closed profile does neither after exposure.
CPU preparation rebuilds the selected plan and compares its semantic fields
before writes. It checks all table entries, zeroed data and unique loaded bus
addresses. Initial data PREWRITE|PREREAD precedes leaf and root PREWRITE.
Never-exposed cleanup and irreversible exposed hold keep their existing rules.

The planner contract passes 682182 checks. Actual preparation passes 15724748
checks with GCC16 path analysis, configured Cppcheck/CSA and ASan/UBSan.
Causal runtime mutations cover the new boundaries.
Only the separate manual RPC profile selects this plan. Default and strict
manual profiles keep the ordinary plan. The second-object extension still
needs a target build and physical acceptance. General allocation, resize/free,
live PTE changes and physical coherency remain unqualified.

## CPU preparation and opaque ownership

`cixvpu_prepare_cpu` takes an existing CPU-only allocator owner, an immutable
image and plan, and a trusted SHA256 pin. It hashes the full image, rebuilds
the plan and compares semantic fields. It validates all loaded bus addresses,
CPU spans and handle storage before writes. It then zeroes pages, copies only
planned text bytes, builds leaf entries and root links with the real MMU helper,
and verifies every table slot, including unmapped holes.

Success moves the allocation into an opaque prepared owner. The caller's
allocator handle becomes NULL. No mutable getter exists. This ordinary constructor
does not enable the separate boot root capability described below.
Failure preserves caller handles, but CPU bytes may be partially changed.
The caller must free that allocation. It must not retry or publish those bytes.
`cixvpu_prepared_free` retains the complete owner if cleanup fails.
The allocator seals ownership before the move. Public page access and free
reject old aliases which still name that live sealed allocation.
Raw saved CPU views and pointers to freed objects remain invalid by contract.
The caller must serialize access and end all mutable borrows before preparation.

Source `75c59de2e3d76ae51fc1c3de564719749dab0c99` passes 13057225 checks
with GCC16 path analysis, host Cppcheck/CSA and ASan/UBSan. The tests execute
actual preparation, allocator, parser, planner and MMU source. Native services,
including SHA, are modeled; this is not cryptographic or physical acceptance.
Five compiled causal mutations are detected. The native GCC16 object and CSA
have no findings. Native Cppcheck retains 403 header-baseline diagnostics and
two exported-API unused-function hints in the isolated source unit.
No synchronization, MMIO, address publication or firmware execution occurs.

## Initial DMA synchronization and lifetime guard

`cixvpu_prepared_sync_initial` offers one initial synchronization of a prepared
owner. It validates all maps, loaded segments, roles, plan records and allocation
capacity before the first native sync. Every unique data map is synchronized
once, followed by all leaves and the root last. Offsets are map-relative zero;
each operation covers the complete 4096-byte page.

Text, host-written pages and tables use PREWRITE. Device-written data pages
also use PREREAD. PREWRITE initializes possible bounce backing, including zeros.
No DMA address is returned. The CPU-only preparation callback does not call this
API. The separate manual firmware trial invokes it through the boot context.

Cleanup of a synchronized, never-exposed owner validates all receipts before
matching POST operations and release. A private irreversible exposure marker
closes every release path. There is no reset, timeout or caller-boolean bypass.
The typed root publication API uses this marker. Physical handoff is unverified.
The guard does not implement a global stale-pointer registry or internal locking.

Source `ea410dcd9b33ecf57fee7d86594454799ac776b3` passes 13349515 preparation
checks with GCC16 path analysis, host Cppcheck/CSA and ASan/UBSan. The tests
execute actual allocator, preparer, parser, planner and MMU source. Native
services are modeled, with separate poisoned bounce backing. Ten causal mutations
are detected, including an ASan reproduction of an allocation-capacity overrun.
Native GCC16 and CSA pass. Native Cppcheck reports no warning, performance or
portability findings in the changed DMA/preparer scope. These checks do not
qualify physical cache behavior, routing or DMA.

## Typed boot preparation and receive

`cixvpu_prepare_boot_cpu` shares the ordinary constructor and accepts one core
with protocol3.5/r0p0-sum0005. The caller supplies the trusted full-image pin.
It seeds fixed zero-payload PING1006 through the actual checksum-ring helper
before seal, ownership transfer and initial synchronization. It does not start
firmware or accept an arbitrary message, queue or mutable page request.

After initial synchronization, `cixvpu_prepared_publish_root` checks output
storage against known prepared, DMA, page and native-map storage. It marks
irreversible exposure before returning the encoded root word. Failed validation
or marking leaves output unchanged. Every later free remains EBUSY, even if
no MMIO write occurs. This is a conservative capability boundary, not a claim
that hardware received the address. No general kernel-pointer authentication exists.

`cixvpu_prepared_boot_receive` copies one SWITCHED_IN2001/core0 or PONG2009 reply.
It checks the fixed RPC header and brackets a MESSAGE-page acquisition with
independent control acquisitions before and after. All three control tuples must
match, with at most four attempts. Native services acquire/return MVE ownership
through POSTREAD/PREREAD. There is no stale MVE PREWRITE.
The real ring parser checks framing and checksum before the HOST cursor commit.
Only that two-byte HOST field uses POSTWRITE/store/PREWRITE.

`cixvpu_prepared_boot_rpc_header` returns the cached 12-byte RPC observation.
It uses the existing context and output-storage guards without device I/O.
The boot report prints raw state, call ID, size and a separate getter error.
Reported size never authorizes an arbitrary read or copy length.
Validity means one copy completed, not an atomic firmware transaction.
Sticky retries retain the observation. A new read or retained-map fault
invalidates it until another copy; argument and alias errors preserve it.
Unsupported RPC still stops with ENOTSUP before any response or message ACK.

Exact PARAM1/call3/size8 also permits a fixed eight-byte diagnostic copy.
A matching header recheck sets `resize_valid`; the cached VPU address and
new size remain advisory uint32 values. The native report prints them without
new device I/O. Header drift invalidates the fields while preserving ENOTSUP.
This adds no resize response, memory allocation, PTE change or notification.

Empty or unstable snapshots return EAGAIN without output or cursor advancement.
Malformed data, unsupported RPC and retained-map faults stop further protocol I/O.
Argument and output-alias errors before I/O preserve state. Caller serialization
and normal C object lifetime rules remain required. No post-exposure reclamation,
reset, clock release, GO, framebuffer, dynamic RPC or IRQ operation is implemented.

The actual-source preparation suite passes 13499552 checks, and the standalone
allocator passes 2338334 checks with 629 acquisition failures. GCC16 path analysis,
host Cppcheck/CSA and ASan/UBSan cover the production bodies and modeled services.
Ten causal mutations are detected. Native GCC16/CSA pass; all-category Cppcheck
retains header, informational and isolated unused-API findings without suppression.
These results do not establish a hardware firmware boot or physical DMA.

## Inactive DEBUGPARAM response

`cixvpu_prepared_boot_debugparam` is a closed primitive for a fresh published
boot owner. The boot coordinator does not call it. Legacy receive and
the strict `flags 0x7` profile still reject nonzero RPC state with sticky ENOTSUP.
The primitive cannot resume a failed boot epoch.

The helper acquires two fixed 12-byte RPC headers, with at most four pairs.
FREE and RETURN return IDLE and PENDING without writes; stale call/size fields
are ignored. Stable PARAM with call ID 1 and size at most 1012 returns REPLIED.
Only its aligned little-endian state word changes to RETURN. No payload is read
or logged. Other calls and malformed headers stop further protocol I/O.
Unstable observations return nonsticky EAGAIN. Errors preserve caller output;
an existing sticky error prevents all native I/O.

The accepted response ends the prior PREWRITE phase for state with POSTWRITE.
It re-arms unchanged call/size words before the release-ordered state store,
then synchronizes only the four-byte state with PREWRITE|PREREAD.
The payload and page tail remain untouched. Cached diagnostics retain the
observed request. No IRQ notification, message ACK, allocation or release occurs.
A future caller must notify firmware after REPLIED before another step.
Matching observations rely on committed PARAM remaining immutable until RETURN;
they do not prove an atomic transaction or physical cache correctness.

Actual-source suites pass 13647963 preparation checks, 28357 composed boot checks
and 2338334 allocator checks, including 629 acquisition-failure positions.
GCC16 path analysis, configured Cppcheck, CSA and ASan/UBSan cover the change.
Seven compiled runtime mutations are detected. Native GCC16 and CSA have no
diagnostics. All-category Cppcheck retains its baseline and adds one isolated
unused-export hint in each changed native unit; it is not reported clean.
The poisoned bounce model checks that all 4092 bytes after state stay unchanged.
Complete RPC service and firmware readiness still require physical acceptance.

## Closed bootstrap RPC service

`cixvpu_prepared_boot_rpc_step` and `cixvpu_prepared_boot_receive_rpc` are
separate plan1-only APIs. They cannot resume an owner with a sticky error.
Fresh bounded observations accept two exact MEM_ALLOC tuples, one fixed RESIZE
and at most sixteen DEBUG calls. RESIZE requires the accepted first allocation,
base `0x20000000` and new_size 524288. Its backing is prepared before exposure.
The second allocation requires committed RESIZE, size=max_size 16384, region 0
and log2_alignment 12. It returns VA `0x24000000`; its backing and leaf already
exist before exposure. A consumed but uncommitted resize cannot authorize it.
Each allocation or resize slot is consumed before its first response store.
The fixed VA and payload size 4 become visible before the release RETURN store.
Partial DMA synchronization preserves unchanged request and neighboring bytes.
No runtime allocation, live PTE change, heap view, general resize or free occurs.
Wrong tuples fail closed; a duplicate allocation or resize returns EBUSY.

FREE and RETURN do not repeat a response. Receive permits their ordinary queue
progress; a newly observed PARAM defers without reading or advancing that queue.
The manual native coordinator services RPC before messages and issues exactly
one IRQHOST notification after each commit. Reports count commits, notifications
and first allocation, resize and second allocation replies separately.
The maximum is 19 committed replies.
Unexpected IRQs before or after notification
retain ownership and stop the epoch. Transient EAGAIN waits for the next visit
within the original one-second/1001-visit bound. Terminal errors are not retried.

Actual-source preparation passes 15724748 checks. The composed native contract
passes 39952 checks, including 13510 attachment checks, with GCC16 path analysis,
configured Cppcheck/CSA and ASan/UBSan. Causal runtime mutations detect missing
or repeated notification and faults around commit. Native analysis uses modeled
bus-DMA, ACPI and IRQ services. These are models, not physical firmware readiness.

## Dedicated native boot epoch source

`cixvpu_acpi_boot.[ch]` implements a separate opaque manual boot context.
The manual firmware profile calls it after root mounts. It accepts a fresh
SEALED owner from the typed boot constructor. Initial DMA synchronization occurs before
ownership transfer or resource acquisition. A failed synchronization preserves
the caller's handle and consumes the attempt without automatic cleanup.

The fixed profile validates both resource windows, identity, all four core
power references and the native clock67 provider. It reserves the exclusive
initially masked IRQ before activation or main reset. Handler state is ready
before unmask. Only the LSID0 cause is acknowledged; other causes become a
held fault. No parser, DMA synchronization or AML runs in the handler.

Core power API success and raw repair words are advisory. Cached ON can skip
AML, and firmware repair methods can return after a timeout. This source does
not prove fresh repair, electrical power, functional clock271 or exclusive
ownership against unknown firmware. Saved IRQ polarity/sharing is a manual
profile qualification; the common resource object retains number/trigger only.

The source issues the root capability before MMIO publication and immediately
records irreversible exposure. All later outcomes retain pages, maps, claims
and handler for the rest of the boot. Terminal outcomes mask the owned IRQ
once. Before exposure, checked rollback can release ownership; a rollback
failure retains the remaining resources. There is no qualified hot detach,
runtime teardown, suspend, retry or warm-reboot reclamation protocol.

TERMINATE polling is limited to 100 ms and 10001 reads. Reply polling is limited
to one second and 1001 visits, with message limits and finite counter fallbacks.
These limits cover device polls. Inherited native GIC and AML waits do not have
those wall-clock guarantees. Fixed SWITCHED_IN and PONG are separate results;
no GO, codec job, framebuffer or arbitrary RPC is sent.

Run `sh ember/tools/cixvpu-boot-contract.sh` from the repository root.
`CIXVPU_BOOT_ANALYZE=1` adds Cppcheck and host CSA. `CC`, `CFLAGS` and
`TEST_RUNNER` select compilation and execution. `CIXVPU_BOOT_EVIDENCE` preserves
a fresh output directory, and `CIXVPU_BOOT_SOURCE` selects a source-copy mutant.
The linked fixture executes the actual native control and boot bodies plus
actual DMA, preparation, MMU and ring helpers. Native platform services alone
are modeled. It passes 28357 reported checks, including 9094 attachment checks;
the imported DMA fixture also has fail-fast internal assertions.

GCC16 path analysis, host CSA and ASan/UBSan pass. Seven compiled runtime
mutations are detected. Native GCC16 passes; native CSA retains four inherited
bitops findings per unit. All-category Cppcheck retains explicit header,
isolated export and native-service signature findings. No category is disabled.
These are source results. The separate physical diagnostic boundary below
records the limited IRQ observation and an incomplete firmware handshake.

## Manual firmware handshake trial

[EMBER64_CIXVPU_BOOT_PROBE](../../../arch/evbarm/conf/EMBER64_CIXVPU_BOOT_PROBE)
includes the native diagnostic parent and selects VPU `flags 0x7` with SCMI
`flags 0x80`. Build it with GCC16. Select its kernel manually and preserve the
working default and recovery kernels. Qualify the physical resource profile,
DMA address limits and CPU preparation before this trial.

When the observed firmware loses SPCR, the separate
[EMBER64_CIXVPU_UART2_BOOT_PROBE](../../../arch/evbarm/conf/EMBER64_CIXVPU_UART2_BOOT_PROBE)
adds only the [exact-board UART2 console gate](../../../stand/efiboot/docs/cix-spcr.md#manual-kernel-console-profile)
to that boot profile. It does not change the RPC policy or exposure lifetime.

[EMBER64_CIXVPU_RPC_UART2_BOOT_PROBE](../../../arch/evbarm/conf/EMBER64_CIXVPU_RPC_UART2_BOOT_PROBE)
selects the separate fixed RPC policy with VPU `flags 0x8` and SCMI `flags 0x80`.
It preserves the UART2 console gate and all boot-long exposure rules.
Its source checks pass, including both exact ALLOCs, fixed RESIZE, notifications
and the modeled SWITCHED_IN/PONG exchange. The physical diagnostic below also
completes those three replies and receives SWITCHED_IN and PONG.
Runtime codec operations and safe post-exposure teardown remain unqualified.
The complete firmware handshake remains unqualified.

The once-only mountroot callback reuses the fixed local image pin, exact size,
one-core plan and zero-page audit. It calls `cixvpu_prepare_boot_cpu`, then frees
the staging image before creating and invoking the dedicated boot context.
Only that context performs initial DMA synchronization and hardware handoff.
An error before context invocation uses checked, never-exposed cleanup.
After invocation, the callback retains both a preserved caller handle and any
context-owned resources. It never infers that a failed attempt permits release.

Saved reports distinguish attempt, ownership transfer, exposure, held resources,
stage, primary and cleanup errors, identity, core power status, IRQ observations,
SWITCHED_IN and PONG. They do not print a root address. A message or interrupt
alone does not establish successful encoding.

This is a boot-long diagnostic profile. After exposure, keep the current boot
and resources intact while collecting results. Use a full power cycle before
reusing them. Warm reboot, hot detach and runtime reclamation are unqualified.
No GO, frame, video stream or general RPC is submitted. The physical trial below
reaches exposed ownership and an initial IRQ, then stops before a valid handshake.

## Initially masked native IRQ registration

The ARM PIC API `intr_establish_xname_masked` reserves an exclusive native
source with one initial mask reference. It blocks before source publication
and skips the initial direct or multiprocessor enable operation. A caller must
publish its returned cookie and ready context before `intr_unmask` releases
that reference. A pending line can dispatch immediately during that unmask.
An occupied source is rejected without changing the existing owner.

The controller configure callback must preserve the mask. The inspected GICv3
wired SPI path meets that requirement. Other controllers and all-CPU PPI
unmask affinity are not qualified. Existing establish APIs retain their event
order; mask counting and native teardown remain unchanged. Establish and
teardown still require caller serialization. This is not firmware ownership,
DMA drain or a generic replacement for pending-dispatch synchronization.

Run the actual-source ordering contracts from the repository root:

```sh
PIC_ESTABLISH_ANALYZE=1 sh ember/tools/pic-establish-contract.sh \
  sys/arch/arm/pic/pic.c /tmp/pic-establish-new
sh ember/tools/pic-teardown-contract.sh \
  sys/arch/arm/pic/pic.c /tmp/pic-teardown-new
```

Use fresh output paths. `CC`, `CFLAGS` and `TEST_RUNNER` select the establish
fixture compiler, flags and execution wrapper. UP418 and MP420 checks pass,
as do ASan/UBSan and the unchanged teardown contract. Five causal mutations
are detected. Native GCC16 passes; all-category Cppcheck and native CSA retain
explicit inherited diagnostics. The manual VPU boot context uses this API.
The physical diagnostic boundary below records an initial IRQ observation.
Complete interrupt behavior remains unqualified.

## Manual CPU preparation probe

[EMBER64_CIXVPU_PREPARE_PROBE](../../../arch/evbarm/conf/EMBER64_CIXVPU_PREPARE_PROBE)
includes ordinary EMBER64 and selects VPU `flags 0x5`, without an SCMI service.
Build it with the common GCC16 toolchain. Select its kernel manually and retain
the working default and recovery kernels.
It uses the same local firmware path, exact size, SHA256 and one-core fixture
as the allocation probe. It preserves that profile's zero-page audit.

This mode keeps staging until `cixvpu_prepare_cpu` returns. Successful
preparation moves ownership before staging is freed. The callback then calls
`cixvpu_prepared_free`. Failed preparation keeps the allocator handle for cleanup.
Separate output fields report the primary, close, preparation and cleanup errors,
and both retained-owner states. A cleanup error retains the corresponding owner.
The callback runs once. It performs no VPU control, MMIO, DMA synchronization,
address publication or firmware execution.

Source `1d68984bbe4be838906d34af68e4bd92f46cb533` passes 4254 attachment checks
with GCC16 path analysis, Cppcheck, CSA and host ASan/UBSan. Six compiled causal
mutations are detected. The callback test models the preparation service and
checks its inputs and ownership transfer; it does not emulate page-table work.
The separate actual-source preparation contract checks that work.
On the observed board, GCC16 kernel
`07f2ddf493d1b701de8163e51d76bc7cd9c9ff9f` allocates and zero-checks all
256 pages, prepares the pinned image and tables, and releases both owners.
Primary, close, preparation and both cleanup results are zero.
With the firmware file naturally absent, the same kernel reports ENOENT,
zero pages, no preparation and no retained owner. These are CPU-side checks;
the profile performs no device-visible DMA or firmware execution.

## Source and firmware provenance

The OS baseline is `7fac11898f3451472b6288d84ea3fdc87ca4f808`.
The maintained reference is Sky1 Linux 6.18 LTS at
`57e018a398248d7e5e4d798610df79a557c0629f`, principally its CIX codec patch.
The retired DKMS repository is historical evidence only.
The helpers are original BSD implementations of functional wire formats.
No GPL implementation was imported or relabelled. Arm China/CIX reference
copyright and licence terms remain with their original sources.

The locally inspected firmware is Sky1-Linux/sky1-firmware at
`dd81690747ddb092bcc2a221daa0f8f679ee4bc4`, `firmware/hevcenc.fwb`:

```text
SHA256 2f745394cc8700233448d74cf4fb14d18eba547d2259de5e0bbf825a1efef56d
size 420736 bytes; protocol 3.5; version r0p0-sum0005
text 103 pages; BSS 34 pages; shared 6 pages; private 9 pages
```

The parser accepts this actual image's layout. It does not execute the image.
Upstream describes proprietary redistributable firmware, but the applicable
individual licence has not been established. No blob is included in this
repository or authorized for a new package by these source checks.

## Reproduce the source checks

From the repository root, use GCC 16.2.0 on a supported build host:

```sh
CC=gcc CFLAGS='-O2 -g -fanalyzer' sh ember/tools/cixvpu-fw-test.sh
CC=gcc CFLAGS='-O2 -g -fanalyzer' sh ember/tools/cixvpu-ring-contract.sh
CC=gcc CFLAGS='-O2 -g -fanalyzer' sh ember/tools/cixvpu-mmu-contract.sh
CC=gcc CFLAGS='-O2 -g -fanalyzer' sh ember/tools/cixvpu-resources-contract.sh
CC=gcc CFLAGS='-O2 -g -fanalyzer' sh ember/tools/cixvpu-fw-plan-contract.sh
CC=gcc CFLAGS='-O2 -g -fanalyzer' CIXVPU_DMA_ANALYZE=1 sh ember/tools/cixvpu-dma-contract.sh
CC=gcc CFLAGS='-O2 -g -fanalyzer' CIXVPU_PREPARE_ANALYZE=1 sh ember/tools/cixvpu-prepare-contract.sh
```

The firmware script prints its build directory and retains its CLI and fixtures.
Run `cixvpu-fw-check /absolute/path/to/hevcenc.fwb` to inspect an independently
obtained image. Missing files, truncated input and directories must fail.
No check downloads or redistributes firmware.

The resource contract removes its temporary directory on exit.
Set `CIXVPU_RESOURCES_OUTPUT` to an explicit path to retain the executable.
Set `CIXVPU_COMPILE_ONLY=1` when cross-compiling without an execution runner.
On macOS, pass `-isysroot` with an installed macOS SDK to the host GCC16 compiler.
For an AArch64 target executable, pass the EmberBSD sysroot through `CFLAGS`.

For cross-builds, compile each test with its actual `sys/dev/ic/cixvpu_*.c`
implementation. Select the common GCC16 cross compiler and an explicit EmberBSD
sysroot. For example, set `TARGET_CC` and `TARGET_SYSROOT`, then build:

```sh
"$TARGET_CC" --sysroot="$TARGET_SYSROOT" -std=c11 -O2 -g \
  -Wall -Wextra -Werror -Isys/dev/ic \
  sys/dev/ic/cixvpu_ring.c ember/tools/cixvpu-ring-contract.c \
  -o cixvpu-ring-contract
```

Execute target programs on EmberBSD; do not install build tools on the target.
The [source CI](../../../../.github/workflows/cix-vpu.yml) uses a pinned official GCC16
image. Its host contracts and memory analysis do not establish kernel attachment.

## Recorded verification and limits


The reproducible source checks above execute the production firmware, ring, page-entry, resource, layout and ownership code.
They cover malformed data, wraparound, transactional failure, descriptor rejection, duplicate bus pages and acquisition rollback.
Mutation controls must fail before the corrected implementation passes.

Kernel-mode builds use actual EmberBSD/AArch64 headers and GCC16 arguments. GCC path analysis, CSA and Cppcheck supplement host ASan/UBSan and Linux Memcheck.
Target Cppcheck retains inherited header findings and callback-const suggestions incompatible with the autoconfiguration signature.
Address-specific standard-header suppressions cover only `__zeroll`, `__zeroull` and `__sputc`; no project diagnostic category is disabled.
Host memory analysis does not establish live kernel DMA, cache behavior or firmware execution.

The planner's mapping list may alias backing. Free allocation owners, never mappings. Synthetic bus addresses remain fixture data.
The allocator validates loaded bus segments independently of CPU physical addresses and child-tag bounds.
A coherent CPU mapping hint does not change `_CCA` or prepare a bounce buffer for device use.
No runtime reclamation exists for exposed owners.

[Ports diagnostics](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/common-media/cix-vpu) own FFmpeg/GStreamer consumer checks and their retained findings.
A VM's missing-device rejection or software fallback cannot establish a usable codec.
See [DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/cix-p1-orangepi6plus/interfaces.md) for resource, identity, native control and closed-bootstrap hardware boundaries.

## Remaining driver work

The reference ACPI identifier is `CIXH3010`. Read the target's actual ACPI
resources; do not assume a Linux DTS register range or interrupt number.
Establish clock/reset and power ownership before touching the block.
The [native CIX clock/reset providers](cix-platform.md) now have checked source
contracts and passive registration. The default query consumes and releases
untouched provider claims. The optional identity probe uses native power
ownership and checked cleanup. Neither path implements full SCMI support.
The VPU has its own MMU. Use bus_dma for any platform translation; do not
assume IOMMU bypass or infer a mandatory external SMMU from another device.

Replace the diagnostic attachment with an operational driver only after power,
clock and reset ownership is established. Implement firmware page loading,
MMIO, interrupt handling, bounded waits and DMA synchronization.
Add native M2M input/output queues, controls, buffers, poll and stream lifetime.
Reset and close during a frame must stop DMA before releasing resources.
Late completion must not reach a closed or reused session.
Then connect the real device to FFmpeg/GStreamer and qualify the complete packages.
Device DMA data correctness and codec execution require separate board acceptance.

## Physical diagnostic boundary


Current hardware evidence and its applicability belong to the [DevBoards profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/cix-p1-orangepi6plus/interfaces.md).
The accepted closed bootstrap performs no GO or codec frame. Exposed prepared owners, power claims and IRQ registration remain held with IRQ masked.
A full power cycle is required before another epoch. Warm retry, DMA drain and post-exposure reclamation remain unqualified.
The closed allocation/resize policy above is not a general allocator, FREE service or video queue.

## Physical acceptance and recovery

Record board revision, firmware, source, selected kernel/modules and all hashes.
Encode two concurrent 2048x1500, 8-bit streams at 9 fps, at most 4 Mbit/s each,
for 30 minutes under parallel CPU load. Record frame counts, timestamps,
per-frame latency and independently decoded output.
Exercise absent firmware, block reset and close in the middle of a frame.
H.264 and decoding require separate acceptance records.

Use a separately selected test kernel and preserve the accepted boot choice.
Keep original boot artifacts and a working UART recovery path.
Do not overwrite firmware or persistent application data.
A repeatable boot, working memory and a result transport are prerequisites.

## References

- [Maintained Sky1 kernel patches](https://github.com/Sky1-Linux/linux-sky1/tree/57e018a398248d7e5e4d798610df79a557c0629f/patches).
- [CIX codec reference](https://github.com/Sky1-Linux/linux-sky1/blob/57e018a398248d7e5e4d798610df79a557c0629f/patches/0010-media-cix-Add-Sky1-video-codec-VPU-driver.patch).
- [Firmware provenance](https://github.com/Sky1-Linux/sky1-firmware/tree/dd81690747ddb092bcc2a221daa0f8f679ee4bc4).
