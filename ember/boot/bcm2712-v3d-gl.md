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

The checks below use CM5 Rev 1.0 / BCM2712 D0 with 4 GiB RAM and 64 GB eMMC
on a Waveshare CM5-NANO-B. Each result is finite and applies to its named
workload and recorded kit. It does not qualify the complete firmware/base stack.

| Check | Accepted result | Boundary |
| --- | --- | --- |
| Native CL/TFU | 333 CL pairs, 1,363,968 checked pixels and 32 TFU copies; four jobs each with 72 and 91 distinct BOs; all active pixels and requested guards pass | Native clear/store commands, binary fences, two clients and CL/TFU/CL transitions; no Mesa or display execution in this checker |
| TFU raster | 16 copies and 67,712 checked pixels with IOC1 and IOC0, including 84x52 with stride 96 and byte offsets 256/512; exact two-BO budget return | Fixed raster workload only; no tiled or mipmap qualification |
| BO extent | Nine single allocations through 32 MiB, including 16 MiB plus 4 KiB; three held BOs totaling 17,969,152 bytes; all 12 releases return the exact native budget | Allocation and release only; no mapping, rendering or scanout qualification for a 32 MiB BO |
| Fixed-mode KMS | Atomic modeset, 240 consecutive double-buffer flips in eight seconds, normal BLANK teardown and returned BO memory | About 30 presentations/s in the fixed 1080p60 mode; no complete DPMS or HDMI Plasma acceptance |
| Mesa V3D | EGL 1.5, GLES 3.0 and desktop OpenGL 3.1 on V3D 7.1.10.2; real shaders and checked pixels, including four 1280x800 lifecycles per discovery path | Short context/render lifecycle; no desktop frame-rate or long-run claim |
| Same-device PRIME | 80 cross-process 1 MiB lifecycles, SCM_RIGHTS, duplicate imports, partial mappings and retained mappings after handle/fd close; four GBM/EGLImage/GLES pixel cycles | Same-device linear sharing; no foreign-device DMA or arbitrary modifiers |

The CL result uses kernel `1cc306cd0df17fa135320eb0668f092bfc46f6d8` and
checker source `faff91f1015f194cf8d100adc5c09ba1827158c4`. Native counters record
185 BO creates and releases, exact budget return, zero allocation failures and
no new execution fault. The checker SHA256 is
`f896dbf8c8999753b6678d371bb3d1b6a280313ff17dd04cf373f14b3b48b14c`.
The installed kernel SHA256 is
`772e16dd4c3c68f634d1e4530e9b012151d97eaf0dedbafd8e2a66572bd61e4f`.
Physical job-list coverage stops at 91 handles. The inclusive 4096-entry limit
and larger failure cases are covered by source contracts.

The BO-extent result uses kernel `62bf77f25bebf7c362121589fd41d4b65405c6d5`.
The KMS result was also repeated with the explicit format/modifier pair on
kernel `29b51eb8073755849f22ccdfdedf487c8be49870`. Mesa and PRIME receipts retain
their own provider and kernel pins in the
[Ports graphics receipt](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/mesa-cm5.md).

The TFU raster result uses kernel `cff738af94a53a1c70cd07c7a9f3cc18f6cb88f9`
and the 16-job checker from source `ab61bcb4606655bdd368c8d8854d3a918a55ca6d`.
Its ELF SHA256 is
`3268a9fbc2b0c989084361e064364ce6bc4f90d52c9387b2e65409188858e3d5`.
The installed kernel SHA256 is
`5710c3a922efdb737a8d18fce8e6cc2831616225df94d24ce1414518dcf076c0`.
All 64x64 and 84x52 raster jobs pass, with no new execution or admission fault.
The same ELF rejects the first IOC0 request on the older fixed-payload kernel.
This result qualifies the raster version only. The later
[raster/tiled checker](bcm2712-v3d-tfu-check.md) requires its complete 24-job
acceptance; the 16-job result does not qualify its tiled phases or mipmaps.

The existing native TFU ownership, CSD, binary-syncobj and CL checkers also
pass on kernel `cff738af94a53a1c70cd07c7a9f3cc18f6cb88f9`. Their 305 BO creates
and releases return the exact budget, with no new execution fault. Intentional
invalid flags and closed-handle tests exhaust the eight-sample TFU admission
log budget. Absence of later rejection lines is not proof of no later rejection.

A complete physical HDMI KWin/Plasma session is not accepted. Larger physical
CL/CSD lists, general job recovery, foreign-device sharing, Vulkan, sustained
allocation pressure and long-duration graphics operation remain unqualified.
The device ABI inputs used by these consumers are not a fully qualified SDK.

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
sh ember/tools/bcm2712-v3d-cl-contract.sh /absolute/source
sh ember/tools/bcm2712-v3d-csd-contract.sh /absolute/source
sh ember/tools/bcm2712-v3d-job-contract.sh /absolute/source
```

These contracts compile actual driver/checker functions with controlled kernel
and DRM services. They cover 64, 65, 72, 91, 4096 and rejected 4097-entry lists,
partial lookup, capacity-sized metadata cleanup, duplicate ordering, retained
poison across CL/CSD, and full-vector PTE mapping/revocation. The checker fixture
covers exact workload counts, distinct guards and successful/error cleanup.
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
