# A733 IOMMU1 bounded cold initialization

This is independent BSD-2-Clause native code with AI assistance. It uses
hardware facts from the pinned sources below. No GPL implementation is copied.
It extends the explicit native DE stage. Attach performs no IOMMU operation.
This is a bounded request sequence, not a proved flush-completion mechanism.

## Admission and power

The existing seven-word borrowed CCU memory profile must pass. The consumer
reserves all inactive IOMMU1 peers before retention. It retains DE_SYS 1,
VO1 10, VE_DEC 2 and their protected CCU fields with one cookie. DE internal
module gates stay closed, core0 reset stays asserted, and no pixel or RCQ
object exists. No shared clock is retuned.

The fixed `begin_peer_access` operation must succeed before MIC0/1 reads.
It makes domain 2 accessible without releasing the VE_DEC leaf reset or gates.
The same CCU-to-PCK ownership checks bracket each complete IOMMU capture.
All six MIC instances are inside the mapped IOMMU1 aperture at 0x03910000.

Two complete samples must match. Initial admission requires version 0x16,
global enable 0, no pending global flush or invalidation request, and zero
global and MIC error status. Unknown bypass, mode or interrupt-enable bits
are rejected. Auto-gating must be 0 or 0x7fff0003 and remains unchanged.
Reset bits outside the six known MIC bits are preserved.
The normal already-running master-2 profile does not run this global sequence.

## Owned guard directory

V2 uses 4096 32-bit directory entries: 16 KiB, aligned to 16 KiB. A zero entry
is invalid. The native allocator owns one contiguous segment below 4 GiB,
a loaded map, CPU mapping and stable child DMA tag. It clears every byte.
The allocation never creates a translated page or supplies a fallback tag.

The consumer synchronizes PREWRITE and marks the directory retained before
its first initialization write. TTB receives the DMA address shifted right
by 14. The full extent and alignment are checked before publication.
Any later failure retains the directory, its map and its tag until reboot.
No rollback, table reuse or destructor after publication is implemented.
Allocation failure before publication frees only that private allocation.

## Finite request sequence

Each operation starts with fresh owner, inactive-peer, cold-core and complete
expected-register checks. A direct pre-read must equal the expected word.
Each write has a bus barrier and readback. Only the following fields change:

1. Assert MIC reset bits 0 through 5 in global register 0x10.
2. Mask global IRQ enables at 0x80, bits 1:0. Mask each MIC IRQ enable at
   MIC+0x30, bit 0. Never write error-clear registers.
3. Set all six MIC bypass bits at MIC+4, bit 0.
4. Set TTB 0x20 to the retained directory address shifted right by 14.
5. Set prefetch 0x2c to zero. Set TLB and PTW invalidation modes 0x40/0x60
   to 1. No address-specific invalidation request is issued.
6. Release only MIC reset bits 0 through 5. Peer leaf resets and gates remain
   held by the CCU lease. This reset operation is not a bus-drain proof.
7. Request global flush at 0x30 with value 3. Request each MIC flush at
   MIC+0x18 with value 1. Delay 10 us, then write global flush control 0.
8. Set global enable 0x14 to 1. Preserve auto-gating.

MIC is 0x400 plus 0x200 times the local master index. A MIC request readback
can be 1 or can clear to 0. After the global request writes 3, its readback
can be 3 or 0. A physical v0x16 sample returned 0 after that write. This is
an observed request state. It does not establish a self-clear mechanism or
flush completion. The pinned BSP does not require readback 3.

Only a global 3-to-0 or MIC 1-to-0 request transition is accepted between
stable captures or at the direct pre-read. Global values 1, 2, unknown bits
and an unrequested 0-to-3 transition are rejected. Both samples of each
capture must still match. The final global write of 0 is mandatory, even
when the register already reads 0. It follows all six MIC requests and the
10 us delay. No native `flush_done` flag exists. A changed non-command field,
new error, malformed readback or access fault stops the sequence. No error
is cleared to make admission pass.

The consumer then prepares NSI2 with the existing master-local mechanism.
It repeats strict memory admission before opening the DE internal core.
No DE DMA occurs between directory publication and the full flush request
sequence. Subsequent frames also recheck the complete cold-init state and
all six MIC error words under the retained ownership claims.

## Hardware acceptance boundary

The pinned BSP waits 10 us and returns success without a completion-status
poll. This implementation does not reinterpret that delay as completion.
An authorized first hardware run must require matching RCQ-loaded plane words,
a new RCQ finish, two fresh TV0 observations and unchanged error/owner state.
Visible HDMI, repeated scanout, KMS and accelerated Plasma are separate results.
A failed or uncertain run keeps all tables, frames and claims. Reboot is the
only currently implemented release boundary after publication.

## Primary register facts

The Allwinner A733 User Manual v1.00 has SHA256
`cdd77e5779d3c69d41b5f237f3db3db51c67fdf0f3aca9a2eedc77d9420f248d`.
Chapter 10 maps the six IOMMU1 masters. CCU sections 4.1.6.84 and 106-116
supply the inactive peer fields. It does not supply a full IOMMU register table.

All BSP paths below use commit
`2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f` of
[linux-orangepi](https://github.com/orangepi-xunlong/linux-orangepi/tree/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f).
`bsp/drivers/iommu/sunxi-iommu-v2.c` supplies register offsets, power-aware MIC
access, reset-bit arithmetic, interrupt masks, bypass meaning and flush timing.
Its reset routine selects local master index modulo six and clears/sets that
single bit in register 0x10. The low-six mask is derived from those six indices.
The implementation leaves unrelated reset bits unchanged.
`bsp/drivers/iommu/sunxi-iommu-pgtable.h` and `.c` supply V2 directory geometry,
TTB shift and invalid-zero entry facts. They are GPL sources used as hardware
references only. Their SHA256 values are respectively
`93ff578b4632306c9403f1edd9c5e1841558d465ecc112caf07346d47373eba3` and
`c48d2f4c80f3d15455e811eaddbea0ab485af217654f9823e927fe299f329b76`.
The pinned driver leaves prefetch at its zero-initialized value. It does not
read the DTS prefetch property. Its v0x16 mode selectors are both 1.

## Source checks

Run `python3 ember/tools/a733-de352-native-contract.py`.
Use `DE352_CFLAGS` for host sanitizer flags. Assertions remain active with
`-DNDEBUG`. The actual coordinator and allocator bodies run with controlled
MMIO, power, output and DMA adapters. Controls cover every write fault,
malformed readback, changed error state, capture failure, power admission,
zero-table extent, alignment, retained lifetime and actual RCQ model evidence.
These controls do not constitute a physical hardware run.
