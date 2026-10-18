# A733 NPU MMU32 image builder

`sun60i_a733_npu_mmu_build()` encodes one CPU-owned VIP page-table image.
It does not allocate DMA memory, access registers, attach an IRQ or change the
VIP ioctl ABI. The native DMA context calls this helper during CPU preparation.
The NPU device driver does not call the context yet.

The initial native profile uses VA32 and PA32. It maps 1 to 64 consecutive
virtual pages in one 16 MiB MTLB slot. The physical pages can be noncontiguous.
The first and last pages of the slot stay unmapped. This profile is a bring-up
limit. It does not state the maximum physical address width of the device.

The caller supplies trusted kernel pointers and physical pages from its own
DMA allocation. The helper does not prove RAM ownership. The caller must keep
all inputs stable during the call. The plan and page list must not overlap an
output image. Neither output image can overlap the other image. Validation
covers pointer wrap, page count, alignment, address limits, unknown flags,
duplicate physical pages and overlap with either physical page-table range.
The helper snapshots the bounded input before it writes either image.

The MTLB image has 256 little-endian 32-bit entries and occupies 1024 bytes.
Its physical allocation must own a full aligned 4096-byte page. The STLB image
has 4096 little-endian 32-bit entries and occupies 16384 bytes. Its allocation
must align to 4096 bytes. The hardware format only requires 64-byte STLB pointer
alignment; this native profile has the stricter requirement.

A mapped MTLB entry is `stlb_physical | 1`. A read-only page entry is
`page_physical | 3`. A writable page entry is `page_physical | 7`.
Unmapped entries use exception bit 1 and clear present bit 0: their value is 2.
This includes absent MTLB slots. The vendor initializer clears absent MTLB
entries to zero. The native exception choice follows the published MTLB format
bit. Its hardware fault behavior is not yet accepted.

`sun60i_a733_npu_mmu_pd_entry()` encodes the VA32 PD-mode root as
`((mtlb_physical >> 12) << 4) | 1`. It requires 4096-byte alignment and PA32.
Both functions return `EINVAL` on invalid input. An error leaves the output
unchanged. A successful build writes exactly 1024 and 16384 bytes. Extra output
capacity stays unchanged. No cache synchronization or device publication occurs.

## Provenance

This is an original BSD implementation from format facts. No vendor allocator,
MMU implementation or Linux glue was copied. The primary source is the
[VIP2 BSP at 2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f](https://github.com/orangepi-xunlong/linux-orangepi/tree/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/npu/aw_nna_vip/vip2):

- `inc/vip_feature_database.h`: the PID `0x1000003b`, revision `0x9202` profile
  sets `va_bits=32`, `mmu_pd_mode=1` and `remove_mmu=0`.
- `memory/vip_drv_mmu.h`: MTLB/STLB counts, shifts, pointer mask and flag bits.
- `memory/vip_drv_mmu.c`: mapped PTE flags 3/7, free STLB value 2 and table storage.
- `vip_drv_hardware.c`: PD root encoding, PD register `0x3b4` and control `0x388`.

These are source facts. They do not prove that a device fetched a page table.

## Checks and next boundary

Run `python3 ember/tools/a733-npu-mmu-contract.py`. Set `CC` and
`NPU_MMU_CFLAGS` for a host compiler and its analysis flags. Set
`NPU_MMU_SOURCE_ROOT` to test another complete source input. The runner uses
Python standard-library modules only. It compiles the full production helper
and header with only kernel include plumbing removed. Expected byte values and
error checks are independent. The checks stay active with `NDEBUG`.
`python3 ember/tests/a733-npu-observe.py` also runs this contract.

The host contract tests all table entries, mixed rights, high addresses,
noncontiguous pages, guard entries, table/payload aliases, pointer wrap,
input/output overlap and complete output preservation after rejection.
The target translation unit must also pass the common GCC16 build and required
source analyzers. Host sanitizer execution is not hardware MMU acceptance.

The next consumer must own low-address DMA allocations, map and synchronize
both table images, retain allocations after the first hardware attempt, and
prove fresh clock/power ownership before MMIO. IRQ event and command-terminal
evidence must prove execution. An idle bit alone is not that proof.

## Native DMA context

`sun60i_a733_npu_dma_prepare()` owns three low32 allocations: a 4096-byte root,
a 16384-byte STLB and a 4096-byte command page. Its opaque context accepts a
trusted stable kernel command image of 8 to 4096 bytes, in 8-byte units.
It copies the image and zero-fills the remaining command page. It does not
validate command semantics. The sole payload mapping is read-only at device
VA `0x01001000`. Model and data buffers are outside this first profile.

The context uses `bus_dmatag_subregion()`, the native memory/map API and loaded
`ds_addr` values. It requires exactly one allocation segment and one complete
loaded segment per buffer. It rejects bounce maps, non-exact lengths, alignment
errors, high addresses and overlap between any buffer pair. It propagates tag
errors. It has no unrestricted allocation fallback. Tag cleanup uses
`bus_dmatag_destroy()` even when the native API returns a referenced parent tag.
A prepare failure preserves the caller's output and unwinds owned resources in
reverse order. The info API returns values and exposes no mutable CPU pointer.

`sun60i_a733_npu_dma_retain()` makes the lifetime sticky before the first
`BUS_DMASYNC_PREWRITE` operation. It synchronizes the complete three maps.
Call it before any attempt to publish a root address to hardware. A second
retain returns `EBUSY` without more syncs. Destroy also returns `EBUSY` after
retain, including after a failed hardware write or timeout. This code has no
reset, stop or retained-release path. The caller serializes all context access.
The context grants no clock, power, IRQ, MMIO or execution readiness.

Run `python3 ember/tools/a733-npu-dma-contract.py` for the actual-source lifetime
contract. Its variables are `CC`, `NPU_DMA_CFLAGS` and `NPU_DMA_SOURCE_ROOT`.
The existing NPU source dispatcher also runs it. Fault controls cover all
acquisition steps, strict map shape, bounce/range/alias rejection, native tag
references, byte images, sync order and retained cleanup refusal.
