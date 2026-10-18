# Finite BCM2712 HVS IOMMU observer

`bcm2712_iommu_observe` is an explicit finite diagnostic module. It has no
attachment and changes no IOMMU register. It does not allocate a frame, follow
page tables, create an IRQ, acquire a clock lease or authorize HVS DMA.
The synchronous firmware GET transport uses the existing kernel mailbox DMA
buffer and mailbox writes. The observer introduces no SET, reset or DMA API.

Require `cold-headless=true`, exact `expected-buildinfo`, and
`iommu-mode=observe4`. Conflicting `clock-mode`, `dvp-mode`, `hd-mode`,
`hpd-mode`, `ddc-mode` or `phy-mode` properties return `EINVAL` before effects.
The existing framebuffer/FDT descriptor and registered native render/GPU0 ACPI
guards remain mandatory. The render minor reference covers the entire cycle.

The operator must exclude concurrent display and firmware configuration writers.
This is an external policy, not a condition that this module can prove. A
completed static retained PHY experiment can coexist if that policy holds.
Do not unload or acquire another experiment's owners to run this observer.
No power, clock lifetime or MMIO stall/abort recovery is established here.

## Finite operation

The existing `bcm2712_display_acpi_resources` validates the complete canonical
GPU0 contract. The existing `bcm2712_display_read_clocks_report` then performs
state/rate GETs for IDs13 (HSM),14 (BVB),4 (CORE),16 (DISP), in that order.
All four states must be1 and all rates positive. HSM must be at least120MHz.
Any error, including a validated `EBUSY` clock refusal, prevents mapping.
Only a wire-validated failure record prints the failing ID/state/rate/check.
No clock fallback, retry, rate selection or enable operation occurs.

Map only resource4, exactly `0x1000005200/0x80`. Read these seven32-bit offsets
in the stated order, twice. There are exactly14 reads on a complete sample.

| Index | Offset | Primary name |
|---:|---:|---|
|0|0x38|MMMU_DEBUG_INFO|
|1|0x00|MMMU_CTRL|
|2|0x04|MMMU_PT_PA_BASE|
|3|0x14|MMMU_ADDR_CAP|
|4|0x1c|MMMU_BYPASS_START|
|5|0x20|MMMU_BYPASS_END|
|6|0x24|MMMU_MISC|

The first DEBUG must have version nibble>=4, VA and PA width nibbles>=6,
and bypass support bit29. Width is nibble+30 bits. Unsupported DEBUG stops
after one read with `EOPNOTSUPP`. No other resource or offset is read.

Unmap before the post-clock report. A complete cycle uses at most16 GETs.
Compare both register passes and the four pre/post ID/state/rate records.
A difference returns `EAGAIN`; there is no resample. Release the render reference
before logging raw words, clocks, errors or completion. Partial register evidence
has an explicit valid-word count. A failed or unstable observation has no
completion line. All synchronous failures use normal cleanup; FINI returns0.
Mailbox timeout quarantine retains the kernel's copied request, not module text.

These counts do not guarantee an absolute wall-clock return deadline. Mailbox
lock/allocation, scheduling, and a stalled or aborting native MMIO access are
outside the mailbox polling deadline. No IOMMU barrier or flush is issued.

## Diagnostic candidate only

A stable equal snapshot is a low-bypass candidate only when:

- DEBUG passes the access guard above.
- CTRL has only named bits (`0x0f1f1f8f`), ENABLE0 set, command bits2/3,
  TLB_CLEARING7, global BYPASS8 and sticky error bits12/20/27 clear.
- MISC SINGLE_TABLE31 is clear. BYPASS_START is exactly0.
- BYPASS_END31 and ADDR_CAP31 are set.
- `end = (uint64_t)(BYPASS_END & 0x7fffffff) << shift`, where shift is22
  for DEBUG28 set, otherwise28.
- `cap = ((uint64_t)(ADDR_CAP & 0x7fffffff) + 1) << 28` and
  `0 < end <= cap <= 2^36`.

Only fixed shifts22/28 are used after conversion to64 bits. Malformed DEBUG
never supplies a shift count. Unknown, disabled, global-bypass and other patterns
remain unclassified. A complete unclassified snapshot returns0 with candidate0.
PT_PA_BASE is raw evidence only. No table address is dereferenced.

Even candidate1 proves no DMA tag, mapping, physical RAM visibility, ownership,
coherency or future stability. A future fixed-frame owner must validate its whole
DMA span and retain mappings, storage, UPM and clocks through a proved stop/drain.
The current ACPI render attachment selects a32-bit DMA tag; a future allocation
plan must respect that source limit. This observer does not inspect a DMA tag.
A contiguous8294400-byte CPU-filled first frame does not provide the full Plasma
GBM/SG/cache/fence contract. V3D `bo->gpu` is a separate device virtual address.
See the [display contract](bcm2712-display-contract.md#fixed-first-frame-design-boundary)
and [pure HVS encoder](bcm2712-display-hvs-core.md).

## Build and source contract

Build the opt-in module with GCC16 and the selected configured kernel's real
`EMBER_KERNEL_OBJDIR`, `DESTDIR` and DRM preprocessing flags. Use the existing
[HSM module build and ABI audit method](bcm2712-display-hsm.md).
Audit strong imports, source closure and generated headers against the exact
installed kernel before load. `expected-buildinfo` alone is insufficient.
`ember/tools/bcm2712-iommu-observe-abi.c` samples the layouts this module uses.
No kernel option, default attachment or kernel API change is introduced.

Run `sh ember/tools/bcm2712-iommu-observe-contract.sh` from a complete source
checkout. Set `TMPDIR` to a disposable build directory. The fixture executes the
actual observer, actual clock reader and bundled libfdt. It doubles resource
validation, mailbox exchange, bus-space, device and property services. Their
kernel implementations have separate contracts. The fixture does not prove
physical access or HVS DMA. `CC` and `CFLAGS` select the compiler. Set
`IOMMU_OBSERVE_COMPILE_ONLY=1` for GCC16 cross compilation. Host ASan/UBSan uses
`IOMMU_OBSERVE_SKIP_MUTANTS=1 CFLAGS='-fsanitize=address,undefined -g'`.

Register facts come from unchanged primary
[bcm2712-iommu.c at Linux a1a601d](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/iommu/bcm2712-iommu.c#L24-L85),
[initialization and bypass bounds](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/iommu/bcm2712-iommu.c#L147-L232)
and [aperture setup](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/iommu/bcm2712-iommu.c#L655-L685).
This BSD observer was written from those facts. GPL implementation is not copied.
