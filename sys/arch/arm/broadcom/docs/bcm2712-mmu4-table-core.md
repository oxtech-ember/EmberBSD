# Fixed MMU4 table encoder

This pure BSD mechanism encodes one full-translation profile. The aperture is
[0, 256 MiB), with no bypass. Frame IOVA0 covers a 1920x1080 XRGB8888 image with
7680-byte pitch and 8294400 bytes. It uses 2025 read-only 4 KiB leaves. This is a
source contract. It performs no allocation, DMA synchronization, MMIO, shared
cache operation or device activation. It has no native caller or default wiring.

## Input and images

`bcm2712_mmu4_encode()` accepts one fixed `bcm2712_mmu4_pages` object. It contains
raw expected DEBUG, top/two L2/default physical page addresses and exactly 2025
frame-page physical addresses. The caller owns these pages. Numeric metadata
does not prove CPU PA authority, parent DMA address translation or ownership.
A V3D GPU virtual address is not an HVS-visible address.

Require DEBUG version 4, VA and PA width nibbles at least 6 (width=nibble+30), and
bypass capability bit 29. The capability requirement is conservative BSD policy;
this profile does not use bypass. Other DEBUG page-mode fields do not select
large pages. Unsupported DEBUG returns `EOPNOTSUPP`.

Every PA is 4 KiB aligned. Its complete 4096-byte span must fit below 2^36.
An exclusive end of 2^36 is valid. All four control-page PAs are nonzero and
mutually distinct. None can equal a frame-page PA. Since all pages have the
same alignment and extent, this also rejects partial physical overlap.
Rejecting control PA 0 is allocator policy, not a claimed hardware restriction.
Frame PA 0 and repeated immutable frame pages are representable. This fixed SG
vector does not complete GBM allocation, BO fences or the Plasma buffer contract.

The output has four 4096-byte images, in top/L2[0]/L2[1]/default order. Each table
word uses explicit little-endian bytes. No host-endian word store is assumed.

| Image | Nonzero words |
|---|---|
|Top|0/1 = VALID28 OR the corresponding L2 PA shifted right12|
|L2[0]|0..1023 = VALID28 OR frame PA[0..1023] shifted right12|
|L2[1]|0..1000 = VALID28 OR frame PA[1024..2024] shifted right12|
|Default|None|

All other bytes are zero, including the final 23 L2 entries. Leaf WRITEABLE 29
and page-size 31:30 remain clear. A 36-bit PA produces a 24-bit PPN below the flags.
For L2 PAs 0x01001000/0x01002000, top words are 0x10001001/0x10001002.
Sequential frame pages from 0x02000000 give leaf 0=0x10002000,
leaf 1023=0x100023ff, leaf 1024=0x10002400 and leaf 2024=0x100027e8.
The first leaf bytes are 00 20 00 10. The fixed frame's exclusive IOVA end is 0x7e9000.

## Errors and lifetime

Supply real accessible, aligned input and output objects. The function first
rejects NULL, CPU-object range overflow, misalignment and overlap with `EINVAL`.
It then validates DEBUG, control PAs and frame PAs in that order. PA alignment,
zero control PAs and physical aliases return `EINVAL`; full-span range errors
return `ERANGE`. All errors are positive BSD errno values.

Every validation precedes the first output write. Rejection preserves all
output bytes. Success writes directly into caller-owned images without a large
kernel-stack temporary. The caller excludes input writers, output readers and
device access during encoding. This error-preserving commit is not concurrent
16 KiB publication. Do not place either large object on the kernel stack.

The encoder does not prepare real DMA pages, prove address/image correspondence,
synchronize allocation padding or authorize table publication. A later native
owner must prepare all tables, default and frame backing before any MMIO. It
must retain typed shared-cache member 4 before local writes, finish the shared
flush before MMU ENABLE, and retain all owners/pages on uncertain mutation.
Power/interconnect access, all MMU4 masters, shared MMU2/4/5 ownership, firmware
exclusion and stop/drain remain separate gates. No disable or release protocol
is supplied. Encoding success is not working translation or scanout.

## Source checks

Run `sh ember/tools/bcm2712-mmu4-table-contract.sh`. It compiles the unmodified
production C and fixture as separate translation units. Both use the real header.
Independent literal word/byte oracles cover all leaf positions, flags, table boundaries, zero tail/default, SG/repeated/zero/max PAs,
DEBUG fields, all control/frame physical aliases, CPU overlap in both directions,
exclusive PA end, range overflow, poisoned outputs, canaries and repeat calls.
`CHECK` remains fatal with `NDEBUG`. Compiled mutants change used production facts
or validation; a compile failure is not a behavioral rejection.

`CC`, `CFLAGS`, `TEST_RUNNER`, `MMU4_TABLE_COMPILE_ONLY=1`, `MMU4_TABLE_OUTPUT` and
`MMU4_TABLE_EVIDENCE` support cross checks. `MMU4_TABLE_SKIP_MUTANTS=1` runs only
the positive. `MMU4_TABLE_MUTANTS` selects controls. Use the common GCC16 cross
compiler with the EmberBSD sysroot for target objects. Host ASan/UBSan checks
source execution; it is not Valgrind or physical acceptance.

Pinned primary facts come from Raspberry Pi Linux
`a1a601d4857978d810175365550c4536701df4ed`:
[flags, widths and page geometry](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/iommu/bcm2712-iommu.c#L70-L116),
[top-level and leaf entries](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/iommu/bcm2712-iommu.c#L258-L348),
and [default page initialization](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/iommu/bcm2712-iommu.c#L205-L232).
Only numeric facts inform this independent BSD implementation.
