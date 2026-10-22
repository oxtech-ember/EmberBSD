# Native CM5 OpenGL and KMS

The opt-in native V3D driver supports CL bin/render jobs, CSD compute, a staged
TFU subset, binary syncobjs and same-device PRIME. The common Ports Mesa
package uses this driver for hardware OpenGL on CM5. Native fixed-mode KMS
presents the same GEM objects through HVS/PV0. Rendering, allocation, display
and a complete accelerated desktop have separate acceptance boundaries.

## Native rendering and buffer ownership

The [render interface](bcm2712-v3d-render.md) owns native scatter/gather DMA
storage, per-file GEM handles, UVM mappings and GPU virtual ranges. Its
256 MiB per-BO ceiling is also limited by the remaining device storage budget.
That budget is 1/16 of physical memory, with a 64 MiB floor and a 256 MiB cap.
The renderer's page table and scratch storage count against it. The HVS
consumer retains a separate 16 MiB limit for each scanout BO.

CL and CSD use the [bounded object-list owner](bcmv3d-submit-lists.md).
They accept one to 4096 handles per submission. This is a software resource
policy, not a hardware or UAPI maximum. On a 64-bit target, the GEM pointer
vector and temporary handle vector use at most 48 KiB per request. Native
`kheapsort` uses fixed workspace for O(n log n) duplicate checks. Metadata
allocation can fail with `ENOMEM`. These vectors are separate from BO storage
accounting.

The [TFU admission contract](bcmv3d-tfu.md) accepts selected Mesa 26.2.4 modes,
file-owned BO aliases and interior addresses. It maps complete unique BOs with
read-only inputs and a read/write output or alias. This is MMU containment,
not validation of a complete tiled image or mipmap footprint.
`DRM_V3D_PARAM_SUPPORTS_TFU` remains zero. TFU mipmap execution is not physically
accepted; successful GL mipmap readback through a fallback path does not qualify it.

A device lock serializes execution. Each job retains all submitted GEM refs.
Both CL input fences and the output syncobj are captured before any wait or
output replacement. Range checks cover the command lists, tile allocation
and tile state within the referenced BOs. Unsupported flags, extensions and
formats fail before GPU exposure. See [binary synchronization](bcm2712-v3d-sync.md)
and [CSD computation](bcm2712-v3d-csd.md) for those interfaces.

Successful completion requires the expected IRQ evidence, idle state and
clean MMU status. The driver revokes PTEs and completes the bounded MMU flush
before returning DMA ownership and releasing job refs. An uncertain execution,
timeout or failed revoke/flush poisons the engine. It retains the vector,
GEM refs, mappings and busy BOs until reboot. Later jobs fail without replacing
that ownership. Automatic overflow allocation, general job recovery and hot
removal are unsupported.

Same-device PRIME preserves the native BO, its GPU VA and DMA storage.
Foreign-device import is rejected. Native DMA-BUF mmap validates byte offsets,
page alignment, length and descriptor read/write rights. It permits shared
mappings only and rejects execute protection. A mapping retains its GEM ref
after the originating handles and descriptors close.

## Display and event completion

The [fixed-mode KMS layer](bcmv3d-kms.md) adds a primary plane, CRTC, TMDS
encoder and HDMI-A connector to the same DRM device. The explicit
`WSDISPLAY_BOOTFB_HANDOFF` profile binds the existing native HVS/PV0 owners
after the boot console handoff. Ordinary profiles do not select that display
ownership path.

The supported output is 1920x1080 at 60 Hz. The primary plane accepts
XRGB8888/LINEAR, pitch 7680, offset zero and full-screen placement. It publishes
that exact pair in `IN_FORMATS`. Scaling, rotation, other timings, auxiliary
planes, async flips and foreign GEM objects are rejected.

Scanout uses real GEM reader leases and consumer MMU4 mappings. A real PV0
active-list latch proves publication. A later frame epoch proves old-list
retirement; the process worker then completes consumer POST/unload before
signaling the reader fence. KMS waits for reclaim and old-plane cleanup before
reporting a successful flip. A post-swap failure cancels the pending event and
signals OUT_FENCE with an error. It does not report a successful page flip.

Disable submits the reserved black list. HDMI timing and clocks continue.
This logical BLANK operation does not establish complete HDMI/PHY power-down,
suspend or DPMS. Connector polling uses the retained HDMI0 HPD owner;
persistent HPD IRQ/DDC integration and EDID mode selection remain open.
Rapid disable/enable/disable has source regressions and needs separate physical
acceptance. See the [native scanout owner](bcmv3d-scanout.md) for lease and
retained-fault rules.

## Accepted physical scope


Current hardware evidence and its applicability belong to the [DevBoards profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/bcm2712-cm5/graphics.md).
The native checker does not execute Mesa or scanout. Allocation of a large BO does not qualify rendering at that extent. Physical list coverage and tiled-format limits are recorded in DevBoards.
Current physical Plasma evidence is separate from these earlier finite native checks; do not restate an old pre-Plasma milestone as current board status.

## Reproduce the native CL check

Use a clean pinned source checkout and the matched kernel/module bundle.
Cross-build `EMBERV3DRENDER` with GCC16. Select the recorded EmberBSD sysroot
and build the static checker:

```sh
CC=/absolute/cross/bin/aarch64--netbsd-gcc SYSROOT=/absolute/emberbsd-sysroot \
    sh ember/tools/bcm2712-v3d-cl-check-build.sh /absolute/source /absolute/new-output
```

The builder extracts the existing queue-probe packet encoder. Record the
compiler, source, header/library provenance and output hashes. On the device,
run `./cl-check /dev/dri/renderD128` with access to the native `v3d` render node.
Keep an external bounded controller; the checker adds no alarm around ioctls.

The checker verifies unique handles, non-overlapping GPU address ranges,
every output pixel and all requested command, tile and unrelated-BO guards.
Its largest phase requests 91 BOs of 20 KiB: 1,863,680 live bytes. A kernel
with 64 KiB allocation pages can round these BOs to 5,963,776 bytes. The bounds
exclude metadata, page tables and other clients. They do not assert that
padding outside the requested 20 KiB is checked.

A kernel with the older 64-handle limit rejects the first 72-BO submit with
`EINVAL`. The checker exits once without retrying or changing the limit.
Successful phases close all handles, unmap every BO and check descriptor close.
Error exit releases process descriptors and mappings. Compare native BO/job
counters before and after execution; userspace close alone does not prove
kernel reference or PTE retirement.

## Source and package checks

```sh
ruby ember/tools/bcm2712-v3d-cl-check-contract.rb --source /absolute/source
ruby ember/tools/bcmv3d-cl-reject-contract.rb /absolute/source
ruby ember/tools/bcmv3d-resv-contract.rb /absolute/source
ruby ember/tools/bcmv3d-job-resv-contract.rb /absolute/source
sh ember/tools/bcm2712-v3d-cl-contract.sh /absolute/source
sh ember/tools/bcm2712-v3d-csd-contract.sh /absolute/source
sh ember/tools/bcm2712-v3d-job-contract.sh /absolute/source
```

These contracts compile actual driver/checker functions with controlled kernel
and DRM services. They cover 64, 65, 72, 91, 4096 and rejected 4097-entry lists,
partial lookup, capacity-sized metadata cleanup, duplicate ordering, retained
poison across CL/CSD, and full-vector PTE mapping/revocation. The checker fixture
covers exact workload counts, distinct guards and successful/error cleanup.
The job reservation contract checks 51 TFU, CSD and CL caller cases. It checks
one producer publication for a TFU role alias and two for distinct BOs. Named
controls detect missing TFU deduplication and duplicate-reference release.
Compiled controls must reach their expected failed checks; a crash or timeout
is not a passing control. Host services do not execute GPU commands.

The [KMS contracts](bcmv3d-kms.md#source-checks) cover plane registration,
`IN_FORMATS`, commit/event ownership and boot binding. Run the
[BO size contract](bcm2712-v3d-render.md#reproduce-the-userspace-check) for
allocation, accounting, mmap and final-page bounds. Allocator checks cover
4 KiB and 64 KiB CPU pages; renderer PTE checks retain the existing 4 KiB
GPU-page contract.

Use actual target preprocessing for Cppcheck, GCC Analyzer and Clang Static
Analyzer. Classify diagnostics against the recorded baseline; a successful
compiler exit does not imply clean analysis. ARM64 host Valgrind support is
unavailable for the recorded checks. ASan/UBSan covers controlled host execution
and does not verify native MMIO, DMA cache behavior or kernel execution.

The common Mesa package, provider versions, installed-library hashes and
EGL/GL/PRIME commands belong to the
[Ports graphics receipt](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/mesa-cm5.md).
The [KWin session guide](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/probes/plasma-mobile/toolkit/cross/cm5-kwin.md)
records the separate virtual-output compositing boundary. A compiled driver,
virtual-output session or successful native checker does not establish an
accelerated HDMI desktop.
