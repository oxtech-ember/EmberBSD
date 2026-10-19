# Private native MMU4 backing preparation

This helper prepares private memory for the fixed MMU4 table encoder. It does
not install an MMU, access registers, acquire a shared-cache lease, publish a
frame or prove scanout. No default caller or attachment is selected.

The caller supplies a referenced native BCMV3D device and an expected DEBUG
encoding. DEBUG is compatibility metadata, not a fresh hardware observation.
The helper calls `bcmv3d_hvs_prepare(sc, sc->dmat, ...)`. It uses the actual GPU0
parent tag. It does not create a new tag or use a nonexistent `aa_dmat32` member.
The frame BO parent map and its second consumer map remain separate. Neither
`bo->gpu` nor a DMA segment is assumed to be a CPU physical address.

Four additional native DMA allocations hold the top table, two L2 tables and
the default page. The plan and image use heap storage. Nothing places a16KiB
image or PA vector on the kernel stack. The fixed logical frame has8294400 bytes,
2025 read-only4KiB entries, pitch7680 and IOVA0 in a256MiB translated aperture.
Native allocation sizes are explicit:

|Native PAGE_SIZE|Frame allocation|Frame padding|Each control allocation|
|---:|---:|---:|---:|
|4096|8294400|0|4096|
|65536|8323072|28672|65536|

Other native page sizes return EOPNOTSUPP before allocation. A64KiB model in the
contract test is not a tested64KiB kernel kit. Only the first2025 frame pages
enter the table; native padding remains unmapped. Every control uses its first
4096 bytes as the hardware image. The rest of each allocation stays zero.

Before commitment, the helper checks every4KiB fragment of all five complete
allocations. `pmap_extract_coherency` must resolve both the first and last byte.
Physical addresses must progress contiguously and cover the complete allocation
below2^32. The exclusive end2^32 is allowed. Control bases must be nonzero.
The frame may start at PA0. Full allocation spans and CPU storage cannot alias.
The narrower32-bit bound belongs to this native parent-tag slice; the pure table
encoder has a conservative36-bit bound.

Both frame maps must have one complete segment with the independently extracted
physical base. Each control map has the same requirement. A DMA offset, actual
bounce flag, non-NULL IOMMU cookie, malformed geometry or changed correspondence
is refused. `_ds_paddr` is never used as PA authority. A bounce cookie alone is
not the actual bouncing flag. These ARM-specific map checks require the real
selected headers; the public API does not expose these private fields.

`create` commits its output only on success. `get_config` and cached `status`
preserve their outputs on invalid calls. `prepare` is the exact sequencer
callback signature. It validates cfg and rechecks the complete native mappings,
invokes the actual fixed encoder, zeros the whole frame and all four control
allocations, and copies all four images. It then completes full PREWRITE for
all four control maps and the frame consumer map. No allocation, MMIO, firmware
request, address publication, lease change or CPU mutation follows those syncs.
Preparation is one-shot, including failure.

After zeroing the frame, preparation fills the logical 1920 by 1080 XRGB8888
image with eight fixed vertical bars. Each bar is 240 pixels wide. From left to
right, the colors are white, yellow, cyan, green, magenta, red, blue and black.
Each pixel stores B, G, R and a zero X byte. The 7680-byte pitch is unchanged.
The fill completes before every control-map and frame-map PREWRITE. Native
allocation padding stays zero. This immutable experimental initial frame makes
scanout distinguishable from a black output. It is not a Plasma compositor frame
and does not establish a compositor, display ownership or physical scanout.

All errors are positive BSD errno. Invalid arguments, aliases and geometry use
EINVAL; unsupported geometry/debug or translation/bounce uses EOPNOTSUPP;
physical span overflow uses ERANGE; failed pmap extraction uses EFAULT; changed
physical correspondence uses EAGAIN. Native allocation/map errors are preserved.
The first preparation error and its stage stay cached. A void native sync cannot
report or recover a stall/abort. PREWRITE is mandatory for both values of the
CPU mapping's coherency result. That result does not prove SoC fabric coherence.

Construction errors unwind completed operations in reverse order. Unsealed
successful preparation and a cfg mismatch can be destroyed while still
unexposed. A failed repeated mapping proof latches `mapping_uncertain` and
makes destruction EBUSY, even without seal. No fixture or production path
restores unknown map metadata to obtain a successful free.

Before a future call to sequencer program, the owner must irreversibly `seal`
the backing. Destroy then returns EBUSY, including after a pre-retain refusal.
The owner keeps the frame, control pages, maps, callback text and render/device/
tag references alive. There is no unseal, retry or release-after-publication API.
The [private native adapter](bcmv3d-mmu4-native.md) checks seal itself.
Its source-only composition does not establish hardware access or drain.

Calls need process context and external serialization. Sleeping paths refuse
interrupt/softinterrupt context and use ASSERT_SLEEPABLE. The caller excludes
all other CPU/device writers. These requirements do not grant firmware handoff,
shared MMU2/4/5 ownership, power, interconnect access, HVS fetch bounds or drain.
A private contiguous first frame is not the Plasma SG/fence/cache contract.

Run the actual-source contract from a complete checkout:

```sh
ruby ember/tools/bcmv3d-mmu4-backing-contract.rb
```

The runner copies unchanged backing, frame-helper and encoder sources and real
public headers into separate translation units. Only kernel DMA/pmap/GEM/BO
services are controlled. The BO allocator implementation itself has separate
contracts. Tests cover4KiB/64KiB geometry, every pmap callback failure, first/last
bytes, complete patterned rows and their boundary pixels, zero padding, full map
identities, alias/range errors, exact bytes/sync order,
reverse cleanup, cached outputs, process context, one-shot preparation and
retention. Uncertain objects stay allocated in isolated child processes until
exit. The actual PREWRITE callbacks check every frame row against an independent
literal reference and verify zero padding. Pattern controls remove the fill, skip
the last row, move a band boundary, swap a channel or defer the fill until after
sync. Mutants must compile and reach a CHECK before behavioral rejection counts;
pattern controls require their specific pixel or padding CHECK. A shortened
failure sweep is used only for mutants and the second sealed-failure invocation.
The full positive and sanitizer sweeps retain every failure position.

Use CC/CFLAGS and BACKING_COMPILE_ONLY=1 for GCC16 cross compilation. TEST_RUNNER
can execute target tests on a suitable test host. BACKING_EVIDENCE preserves
inputs and commands. BACKING_SKIP_MUTANTS=1 runs positives only; BACKING_MUTANTS
selects named controls. Target compile-only does not prove execution or DMA.
Native TU checks must use the selected kernel plus its actual BCMV3D per-file
DRM flags and matching generated headers. Do not replace them with generic flags.

Source facts are pinned in the existing
[MMU4 table](bcm2712-mmu4-table-core.md) and
[sequencer](bcm2712-mmu4-init-core.md) guides. Native PA extraction uses
`sys/arch/aarch64/aarch64/pmap.c:pmap_extract_coherency`. DMA translation and
sync use `sys/arch/arm/arm32/bus_dma.c`; the actual frame helper is
`sys/external/bsd/drm2/bcmv3d/bcmv3d_hvs_prepare.c`. All are EmberBSD sources.
The underlying numeric IOMMU facts derive from Linux pin
`a1a601d4857978d810175365550c4536701df4ed`; no GPL implementation is copied.
