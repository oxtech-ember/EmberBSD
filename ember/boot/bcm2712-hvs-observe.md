# Finite read-only HVS state observation

`bcm2712_hvs_observe` is an explicit source-only diagnostic module. It reads
GPU0 resource3 at `0x107c580000/0x1a000`. It does not write HVS, IOMMU or shared
cache registers. It has no IRQ, reset, frame allocation, DMA, dlist or pixelvalve
operation. A snapshot grants no ownership, power, translation or stop/drain.

Require `hvs-mode=observe0`, `cold-headless=true` and exact verified
`expected-buildinfo`. Reject other display-probe properties before effects.
Keep the native registered render/GPU0 and framebuffer/FDT absence guards.
The caller must exclude active native and firmware HVS/MPL/TXP/camera clients.
A completed static retained PHY and a DISP hold can coexist. This observer does
not acquire, release or unload their owners. Render references and snapshots do
not prove exclusion of other writers.

The existing clock reader validates HSM13, BVB14, CORE4 and DISP16 before mapping.
It requires state1 and positive rate for all four, and HSM at least120MHz.
That is the existing register-observation prerequisite. It does not establish
fixed-mode timing rates, an inherited CORE floor or a power-domain operation.
A pre-clock error produces zero HVS maps/reads. No SET, lease or retry occurs.

## Exact bounded reads

Read VERSION first. Its low byte selects C0=0x53 or D0=0x54. Read these eight
words twice, in this order. All offsets are private fixed facts, not a caller
register interface.

| Index | Word | C0 offset | D0 offset |
|---:|---|---:|---:|
|0|VERSION|0x000|0x000|
|1|CONTROL|0x020|0x020|
|2|channel0 CTRL0|0x030|0x100|
|3|channel0 STATUS|0x044|0x118|
|4|channel1 CTRL0|0x050|0x140|
|5|channel1 STATUS|0x064|0x158|
|6|channel2 CTRL0|0x070|0x180|
|7|channel2 STATUS|0x084|0x198|

Unknown first VERSION stops after one read with `EOPNOTSUPP`. A changed low byte
at the second VERSION stops after nine reads with `EAGAIN`, before another
variant offset is touched. A change only in upper VERSION bits completes all16
reads, then gives `EAGAIN`. The ordinary path has exactly16 reads and no polling.
No W1C/status-clear bank, SRAM, LPTR/DL/RUN or other resource is accessed.

Unmap before the one post-clock report. A successful map/read always has this
post report, even after a VERSION error. Pre-clock/map failures have none.
Each report uses at most eight GETs; the total is at most16. Keep any primary
VERSION/raw-mismatch/strict-pattern errno and record `post_error` separately.
A post error replaces an otherwise successful observation only. Require equal
valid pre/post clock records before accepting the snapshot.

The strict sampled pattern requires CONTROL.HVS_EN bit31 clear, every
CTRL0.ENB bit31 and RESET bit30 clear, and every STATUS.MODE bits14:13 equal0.
A stable pattern that fails this check returns `EBUSY`. RESET1 alone does not
prove active DMA. All other fields remain raw data. The result is named
`initial_inactive_snapshot`. It is never an ownership, idle-AXI or free predicate.

Raw valid clock/register evidence follows unmap and render-reference release.
Every register line identifies pass/index/offset/value. Failures emit valid raw
evidence and errno, but no snapshot-complete marker. INIT is synchronous and
finite; FINI has no retained asynchronous state. Mailbox scheduling/allocation
and native MMIO stall/abort are not bounded by the read count. A stalled or
aborted bus-space access is not a recoverable callback error.

## Build and actual-source checks

Build only the explicit module with GCC16 and the selected configured kernel's
real `S`, DRM flags and generated headers. Query the actual expanded `COMPILE.c`
before compiling; do not substitute a hand-built compiler profile. Audit the
kernel's strong symbol closure and all selected source/header inputs before
loading. There is no default attachment, kernel option or API change.
The existing [57-word ABI sample](../tools/bcm2712-iommu-observe-abi.c) covers the
same external device/DRM/resource/clock/bus/iterator layouts. Internal samples do
not cross the kernel/module interface. The sample does not replace the audit.

Run `sh ember/tools/bcm2712-hvs-observe-contract.sh`. The fixture executes the
actual observer, actual four-clock reader and actual bundled libfdt. It reuses
only the existing IOMMU fixture's controlled types and proplib/device/ACPI/FDT
services. The runner adapts the controlled word-array size/property name and
removes its old offsets. HVS MMIO/resource/mailbox boundaries are controlled
separately. No old observer or classifier is reused or modeled as production.

Tests cover C0/D0 offsets, all VERSION low bytes, upper bits,1/9/16 reads,
all channel/mode/reset/global masks, every pre/post GET position, mismatch,
combined primary/post errors, early guards, logging and exact cleanup.
Source mutants must compile before behavioral rejection counts. `CC`, `CFLAGS`,
`TEST_RUNNER`, `HVS_OBSERVE_COMPILE_ONLY=1`, `HVS_OBSERVE_OUTPUT` and
`HVS_OBSERVE_EVIDENCE` support cross checks. Use `HVS_OBSERVE_SKIP_MUTANTS=1`
for host ASan/UBSan. `HVS_OBSERVE_SOURCE` and `HVS_OBSERVE_ONLY=combined-raw` or
`combined-pattern` reproduce the two prior-error regressions on pinned source.
These source/host checks are not physical HVS or scanout acceptance.

Pinned primary facts:
[VC6 register layouts and fields](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_regs.h#L552-L763),
[C0/D0 readable dump sets](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hvs.c#L70-L201),
[variant selection](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hvs.c#L2139-L2148).
The pinned VC6 stop sequence gives no complete AXI-drain/free predicate.
No Linux implementation code is copied into this BSD observer.
