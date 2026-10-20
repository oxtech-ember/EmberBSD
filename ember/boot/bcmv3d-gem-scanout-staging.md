# Private GEM scanout staging

This mechanism prepares three unpublished GEM scanout slots in the existing
native MMU4 backing. It does not activate KMS, write MMIO, change an active
page table, flush ICAC, publish HVS memory or establish content readiness.
There is no default caller. The initial sealed frame and its config, plan,
image, status, maps and contents retain their existing contracts.

The process-context caller holds a live GEM reference when it enters
`bcmv3d_mmu4_native_stage_gem(native, slot, obj)`. The native wrapper takes the
existing local mutex. It does not enter the ICAC mutex across allocations.
The BO reference helper checks the DRM device and actual BO function table
before it casts the object. It takes one real GEM reference. It does not
allocate another frame or reserve the original BO quota a second time.

Slots 0, 1 and 2 cover IOVA windows at 8, 16 and 24 MiB. Each window has
8 MiB of capacity. The logical frame span is exactly 8294400 bytes from BO
offset zero: 1920 by 1080 XRGB8888, pitch 7680. This stage does not validate
pixel contents, framebuffer metadata or producer fences. A future framebuffer
caller must validate its actual format, modifier, dimensions, stride and
source rectangle. Busy rendering does not invalidate an immutable page map.

Each slot holds the actual BO, a separate SG-capable consumer DMA map, two
native L2 allocations, its page vector and PTE staging bytes. The original
two L2 pages cover only the first 8 MiB. Three additional windows therefore
need six additional L2 allocations. Native control allocations use PAGE_SIZE;
only their first 4096 bytes contain hardware table data. The remaining bytes
stay zero. Both supported native page sizes, 4096 and 65536, use 4096-byte
hardware leaves. Each slot maps exactly 2025 read-only leaves. BO allocation
padding and the remaining 23 entries stay outside the frame mapping.

The helper validates complete parent and consumer map geometry independently.
Their segment counts and boundaries need not match. It rejects bounce flags,
IOMMU cookies, DMA offsets, overlapping segments and unsupported physical
correspondence. For every 4096-byte fragment of the complete BO allocation,
`pmap_extract_coherency` must resolve its first and last byte. Both native maps
must identify those same physical bytes. A coherency result does not skip the
proof. The native parent-tag slice requires physical spans below 2^32. The
pure table encoder retains its independent 36-bit address contract.

The helper also proves the original control mappings without changing their
cached status. It rejects physical and CPU aliases with the initial frame,
control allocations and other staged slots. It never uses `bo->gpu`, CMA
`paddr` or `_ds_paddr` as a physical-address authority. The new L2 allocations
must be distinct, aligned and disjoint from every retained frame/control span.

All fallible work precedes the slot commit. A private candidate top image
preserves the original top entries and all other candidate entries. Slot 0
changes entries 2/3, slot 1 changes 4/5 and slot 2 changes 6/7. The active top
allocation is never written or synchronized. New L2 memory receives encoded
bytes but no PREWRITE; publication must later revalidate and synchronize real
resources. Staging does not alter the existing initial-frame seal lifetime.

`bcmv3d_mmu4_native_discard_staged_gem(native, slot)` clears only that candidate
top pair and frees that slot's never-published allocations, consumer map and
GEM reference. It is not a retirement or drain operation. An occupied slot
returns EBUSY. The same object in another slot returns EEXIST. Empty discard
returns ENOENT. Invalid arguments use EINVAL; foreign objects use ENXIO;
map/physical mismatch uses EOPNOTSUPP; pmap failure uses EFAULT; changed page
endpoints use EAGAIN; range overflow uses ERANGE. Native DMA errors are
preserved. Errors leave committed slots, candidate bytes and initial backing
unchanged. All errors use positive BSD errno.

The separate [retained range mechanism](bcmv3d-mmu4-range.md) publishes and
unmaps only never-HVS-latched private windows. Once a table pair is linked,
it stays resident. A later stage reuses that empty pair without changing its
live leaves. Discard then releases only the newly staged BO and consumer map.

Run the joined actual-source contract:

```sh
ruby ember/tools/bcmv3d-gem-scanout-contract.rb
```

The contract uses the actual BO allocator, reference helper, backing, initial
frame helper and both table encoders. Kernel DMA, pmap, VM and GEM services
are controlled through the existing BO fixture. It covers both native page
sizes, independent SG boundaries, all DMA service and pmap endpoint failure
positions, aliases, errors, references, quota, zero L2 padding, candidate bytes,
initial-frame preservation and sealed discard in an isolated boot process.
The normal unsealed run ends with no live maps, allocations or GEM references.
A sealed initial backing remains retained until the isolated boot exits.

CC/CFLAGS select the host compiler and sanitizer flags. TEST_RUNNER can select
an executable test host. SCANOUT_EVIDENCE records source hashes and commands;
SCANOUT_COMPILE_ONLY=1 compiles without execution. SCANOUT_SKIP_MUTANTS=1 runs
positives only. Mutants compile before execution and require a CHECK/SIGABRT
failure. Separate SEGV, bare abort and timeout controls cannot count as causal
rejections. Host fixtures do not establish real DMA, MMU4 translation, rendering,
fence completion, HVS fetch, page flip, safe retirement or physical pixels.
