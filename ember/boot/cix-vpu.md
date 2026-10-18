# CIX P1 H.265 codec foundations

Status: diagnostic ACPI attachment and checked source foundations.
The hardware driver is incomplete.
Origin: EmberBSD contributors, AI-assisted.

EmberBSD identifies the CIX P1 codec and validates its ACPI descriptions.
The default resource query does not activate the codec or establish its power state.
There is no video device or M2M interface.
The manual allocation and preparation profiles read an image into CPU memory.
No mode executes firmware or encodes frames.
The target is Orange Pi 6 Plus with CIX CD8180 and stock ACPI firmware.
The OS owns firmware, DMA, interrupts, power/reset and the application API.
Ports owns FFmpeg and GStreamer consumers.

## Implemented scope

The independent BSD source in `sys/dev/ic` implements bounded foundations:

| Source | Contract | Remaining caller responsibility |
|---|---|---|
| `cixvpu_fw.[ch]` | Validate the protocol 3 firmware header, text extent, BSS bitmap and shared-region layout. Reject invalid input without publishing partial output. | Select an authorized blob, allocate pages and load/boot firmware. Structural validity is not authentication. |
| `cixvpu_ring.[ch]` | Pack and unpack bounded message rings, including the selected `-sum` firmware's asymmetric checksum framing. | Own coherent snapshots, locking, DMA synchronization, barriers and index publication. |
| `cixvpu_mmu.[ch]` | Encode 40-bit bus addresses and checked two-level page entries. Reject invalid permissions, occupied entries and stale removals. | Allocate/map DMA memory, stop the device and manage caches/TLBs. Removal is not safe during active DMA. |
| `cixvpu_fw_plan.[ch]` | Plan protocol-3 backing owners and firmware VA mappings without allocation. Check the text offset, BSS bounds, aliases, table coverage and page quota. | Allocate validated DMA backing, copy data, synchronize and publish only after stopped-device checks. |
| `cixvpu_dma.[ch]` | Allocate one native `bus_dma` owner per backing page. Seal moved ownership, synchronize unique maps and reject release after recorded exposure. | Keep addresses hidden. Qualify the hardware handoff, runtime synchronization and eventual teardown separately. |
| `cixvpu_prepare.[ch]` | Recheck the image pin and plan, verify CPU tables and move ownership. Offer initial synchronization, fixed boot messages and a guarded root capability. | End all mutable borrows before preparation. Serialize access and retain every exposed owner until a qualified teardown exists. |

The attachment selects the header parser, layout planner and CPU-only allocator.
The kernel also links CPU preparation, MMU and ring helpers. Only the explicit
manual preparation profile calls ordinary CPU preparation. No attachment calls
the typed boot constructor, root publication or receive helpers.
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
adds seven fixed raw state reads under the same controls. Source checks pass;
physical acceptance remains pending.
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

## Manual CPU-only allocation probe

[EMBER64_CIXVPU_DMA_PROBE](../../sys/arch/evbarm/conf/EMBER64_CIXVPU_DMA_PROBE)
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
The separate actual-source allocator passes 2338334 checks, including 629
acquisition failures. The full manual kernel links with GCC16.2.
No physical allocation result is established. Device DMA, firmware execution
and video queues remain unqualified.

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
No DMA address is returned. The manual preparation callback does not call this
API yet, and no attachment calls the typed root publication API.

Cleanup of a synchronized, never-exposed owner validates all receipts before
matching POST operations and release. A private irreversible exposure marker
closes every release path. There is no reset, timeout or caller-boolean bypass.
The typed root publication API uses this marker; hardware handoff is unfinished.
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

Empty or unstable snapshots return EAGAIN without output or cursor advancement.
Malformed data, unsupported RPC and retained-map faults stop further protocol I/O.
Argument and output-alias errors before I/O preserve state. Caller serialization
and normal C object lifetime rules remain required. No post-exposure reclamation,
reset, clock release, GO, framebuffer, dynamic RPC or IRQ operation is implemented.

The actual-source preparation suite passes 13464524 checks, and the standalone
allocator passes 2338334 checks with 629 acquisition failures. GCC16 path analysis,
host Cppcheck/CSA and ASan/UBSan cover the production bodies and modeled services.
Ten causal mutations are detected. Native GCC16/CSA pass; all-category Cppcheck
retains header, informational and isolated unused-API findings without suppression.
These results do not establish a hardware firmware boot or physical DMA.

## Manual CPU preparation probe

[EMBER64_CIXVPU_PREPARE_PROBE](../../sys/arch/evbarm/conf/EMBER64_CIXVPU_PREPARE_PROBE)
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
Physical allocation and preparation tests remain pending.

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
The [source CI](../../.github/workflows/cix-vpu.yml) uses a pinned official GCC16
image. Its host contracts and memory analysis do not establish kernel attachment.

## Recorded verification and limits

GCC 16.2.0 source contracts pass on Linux and EmberBSD/AArch64:
442 firmware assertions, 229165 ring assertions and 12394 page-entry assertions.
Tests exercise real shared source, malformed data, wraparound and transactional
failure behavior. Mutations of plain and checksum ring rules are detected.
The actual encoder image's layout also passes in the AArch64 guest.

The three core units compile in freestanding `_KERNEL` mode against actual
EmberBSD headers. GCC path analysis, Clang target analysis and Cppcheck were run.
Cppcheck's target run excludes only three unused inline standard-header APIs
at exact file/line locations; the initial findings are retained.
Linux Memcheck runs with origins, all leak kinds and a failure exit status.
The core tests and firmware CLI have zero reported memory errors or leaks.
These checks are not a full kernel build, kernel memory analysis or DMA test.

The pure layout planner passes 669449 actual-source checks with GCC16 and
ASan/UBSan. Its kernel-mode source also compiles with actual AArch64 GCC16
arguments and path analysis. Cppcheck and CSA report no findings in that scope.
The planner calls the unchanged header parser, then rejects mapping overlaps
from the text's additional 0x1000 VA offset. Its page list describes unique
allocation owners; its mapping list can alias text and shared backing across
cores. Callers must never free from the mapping list. Getters recheck bounds
and counts before indexing. Synthetic bus addresses in the MMU composition
tests are fixtures; no DMA address or device operation occurs in the planner.
The selected layout has seven common pages, 128 print pages and at most four
contiguous cores. Other layouts need separate support. Linux source CI at
`09331e9443d32bf061b781f24e2fe9d47c8b25a3` passes these planner checks under
Memcheck and ASan/UBSan, without reported memory errors or leaks.

The layout planner also passes all 669449 checks as a static GCC16/AArch64
program on the physical board, with kernel
`522474e4de9f597153b5192ad579d900fff273ca`. This userspace execution does not
access VPU resources or test native DMA allocation.

The native allocation-only helper passes 2338334 actual-source checks,
including all 629 native acquisition failure positions. The harness models
kernel services and executes the production functions. It tests translated,
discontiguous bus addresses, duplicate bus-page rejection, partial rollback,
child-tag lifetime and unchanged error outputs. A separate build rejects
non-4-KiB kernel pages before calling native allocation services.

The allocator chooses the supplied 64-bit ACPI tag, or the supplied 32-bit
tag when no 64-bit tag exists. It validates loaded bus segments independently
of allocation physical addresses and the child tag's bounds. Common and print
CPU mappings use a coherent mapping hint without changing `_CCA`. All pages
remain CPU-owned. Zeroed CPU mappings do not prepare a possible bounce buffer
for hardware. Allocation alone performs no DMA sync, PTE publication or device
access. The separate initial-sync helper and lifetime guard are described above.
No runtime reclamation exists for an exposed owner.

The optional identity probe from
`a6a21a0820a54fac683a53b4980d296ae6a94177` also booted on the board and
firmware described above. The firmware returned `0xfffffffc` (`-4`, SCMI
`NOT_FOUND`) for both clock ID 67 enable and cleanup disable. The probe
reported primary error 5, cleanup error 5 and unreleased claims. The source
path reached native power ON and its status check, then asserted main reset
with readback during cleanup. It retained power ownership and provider
claims after the rejected clock disable. It did not map VPU registers,
read a hardware identity, allocate DMA or execute firmware.

The default query had separately received the same status from GCLK for
clock ID 67. These failures do not identify their firmware-side cause or
prove the final electrical power state. Returning to an accepted kernel
passed a clean FFS boot and network reconnection. The later native-backend receipt above qualifies a separate control and
identity sequence. That result does not change this earlier AML failure.

The resource validator passes 50 assertions with GCC 16.2.0 on macOS/AArch64,
including GCC path analysis and a separate ASan/UBSan run.
Six raw-descriptor policy cases include extra `ExtendedAddress64`, `Address16`,
unknown and I/O types, and dependent alternatives. These exercise the same
classification function as the ACPICA callback, with literal descriptor types.
Its real source and contract cross-compile for EmberBSD/AArch64 with GCC16.
Cppcheck 2.22.0 uses those actual target arguments and headers.
It reports no findings after three exact unused-inline standard-header suppressions:
`__zeroll`, `__zeroull` and `__sputc`. No project diagnostics are suppressed.
These host results do not establish physical attachment or codec support.

On 2026-10-17, `EMBER64` from `293e5f1166569113072a1deac12012e025056ce0`
booted on the CIX CD8180 board described above. Both host and target compilers
were GCC 16.2.0. The ELF kernel SHA256 is
`5afb9ee12a359d9007f03d8746d699bdaa34d61fba359c861b137ab1cf3924d2`.
The physical diagnostic attachment reported MEM0 `0x14230000`, MEM1 `0x14240000`,
two `0x10000` spans, level IRQ 358 and `_CCA = 0`. It left the codec disabled.
All 50 resource assertions also passed as a cross-built program on that kernel.
The kernel sources compile with their actual target arguments and GCC `-fanalyzer`.
Cppcheck on those kernel arguments reports inherited header style findings and
two callback-const suggestions incompatible with the fixed autoconfiguration API.
No project diagnostic category was disabled. Native host ASan/UBSan and the
GCC16 Linux CI with Memcheck cover the portable source, not live kernel memory.
See the [board profile](../boards/orange-pi-6-plus.md) for the boot and recovery limits.

[Ports diagnostics](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/common-media/cix-vpu)
cross-build FFmpeg 9.0.2 M2M modules and the actual GStreamer 1.28.8 V4L2 plugin.
All 58 selected AArch64 ABI values match between native and bundled headers.
In a compiler-free EmberBSD VM with no video device, FFmpeg registers HEVC M2M
and refuses eight real initialization attempts. Four semaphore lifetime cases
also pass. A real missing-device SIGSEGV found during this work was fixed.
The actual GStreamer plugin loads and creates no HEVC hardware factory.
The VM uses the accepted base/kernel revision
`6dd57144183678938164b4c2dcfc370cdbde8473`, which contains no CIX attachment.
No software fallback, simulated device or firmware execution establishes hardware support.

Ports records inherited analyzer findings and GStreamer's retained GLib globals.
The complete revised FFmpeg package and its installed consumers are not yet requalified.

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
Physical attachment, DMA and codec execution require separate board acceptance.

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
