# A733 Rogue GPU VM memory preparation

`sun60i_a733_gpu_vm.c` prepares one CPU-owned GPU address space.
It allocates five separate 4 KiB pages through native `bus_dma`.
It does not submit work or demonstrate hardware address translation.

## Scope and layout

The initial profile is BVNC `36.56.104.183`, PA36, VA40, and 4 KiB pages.
The caller supplies metadata from the real
[firmware metadata parser](a733-gpu-fwinfo.md).
Other profiles return `EOPNOTSUPP`.

| Page | Contents |
| --- | --- |
| PC | 1,024 little-endian 32-bit page catalogue entries |
| PD | 512 little-endian 64-bit page directory entries |
| PT | 512 little-endian 64-bit page table entries |
| Scratch | One zero-filled GPU read/write page |
| FW context | One 32-byte `rogue_fwif_fwmemcontext`; the remaining bytes are zero |

These allocations total 20 KiB, plus the owner and native DMA bookkeeping.
Only one PC entry, one PD entry, and one PT entry are valid.
The caller selects one aligned scratch address in the pinned general heap:
`0x8000000000 <= VA < 0xa000000000`.
The [MMU encoding helper](a733-gpu-mmu.md) checks each encoded address.
All unused table entries remain zero and invalid.

The scratch PTE uses read/write access with option bits zero.
CPU coherence and GPU PTE cache attributes are separate properties.
Each CPU mapping requests `BUS_DMA_COHERENT` before `bus_dmamap_load()`.
The loaded map must report `_BUS_DMAMAP_COHERENT` without
`_BUS_DMAMAP_IS_BOUNCING`.
The code checks each loaded address before encoding it.
Allocation segment addresses are not substituted for loaded DMA addresses.

The FW context contains the loaded PC address at byte zero.
Its `page_cat_base_reg_set` at byte eight is `ROGUE_FW_BIF_INVALID_PCSET`.
This matches the pinned upstream context initializer.
The code assigns no firmware virtual address to that page.
It does not read or change the separate MIPS firmware page tables.

## Ownership contract

Call `sun60i_a733_gpu_vm_prepare()` from a sleepable context.
Serialize access to the owner and keep input metadata valid during the call.
Pass a separate owner slot containing `NULL`.
The function returns positive errno values.

Preparation checks all five CPU spans and loaded DMA spans for overlap.
It also checks CPU alignment, pointer wrap, map size, segment count,
segment length, PA36 bounds, and overlap with the caller's input or output.
The native allocator must return independently owned allocation and map handles.
The checks do not turn a corrupt allocator into a safe resource manager.

All 20 fallible DMA operations and all validation finish before page writes.
An error releases new resources in reverse order.
The input metadata and caller's owner slot remain unchanged.
Successful preparation publishes only the CPU owner pointer to its caller.
It performs no DMA synchronization, MMIO, or device address publication.

Call `sun60i_a733_gpu_vm_destroy()` to release a prepared CPU-owned object.
It clears the owner slot after releasing the pages.
A `NULL` argument or a slot containing `NULL` is accepted.
The owner slot must not overlap the owner or its pages.

Call `sun60i_a733_gpu_vm_retain()` before any future publication attempt.
This sets a sticky lifetime guard.
It does not transfer ownership or prove device acceptance.
After retention, `destroy()` returns `EBUSY` and preserves all five pages.
There is no release or reset API for retained objects in this scope.
The host fixture can dismantle its simulated allocations because no device exists.
That fixture teardown is not a runtime recovery procedure.

## Contract checks

Run the real production implementation with simulated native DMA operations:

```sh
sh ember/tools/a733-gpu-vm-contract.sh
VM_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/a733-gpu-vm-contract.sh /path/to/rogue_36.56.104.183_v1.fw
```

The optional input must have SHA256
`1db1c399c17401d1f79d46c880db81c724d748c784d4639433b076aba2f9c0d2`.
The script performs no downloads and does not alter the firmware.
It links the real metadata parser and MMU encoders.
It substitutes only kernel service declarations and simulated DMA operations.

The checks exercise every DMA failure position and verify complete cleanup.
They reject noncoherent, bouncing, overlapping, misaligned, and out-of-range maps.
They inspect every table entry, walk the three-level hierarchy,
and compare raw FW context offsets with independent bytewise expectations.
Allocation and loaded DMA addresses deliberately differ in the fixture.
Failure cleanup checks that allocated page bytes remain unchanged.
Retention checks preserve the owner and all five page images.

The pinned-input ASan/UBSan run passes 1,152,543 checks on macOS arm64.
Valgrind Memcheck is not available on that host; this is an ASan/UBSan result.
Five executable mutations fail: allocation-address substitution, omitted
coherence validation, premature page writes, incorrect PCSET initialization,
and removal of the retention guard.

GCC 16 target compilation and Clang Static Analyzer pass.
GCC `-fanalyzer` reaches its default graph limit in allocation and cleanup paths.
It passes with `analyzer-bb-explosion-factor=100` and
`analyzer-max-enodes-per-program-point=256`, with diagnostics still enabled.
Cppcheck reports two existing `constParameterPointer` suggestions in `sys/bus.h`.
It reports no new production finding.
The host fixture and real helper translation units pass Cppcheck and Clang analysis.
The fixture preserves native mutable pointer signatures with four local,
explained `constParameterPointer` suppressions.

These results validate CPU preparation and simulated resource lifetime.
They do not validate a GPU job, firmware context registration, MMU cache
maintenance, context switching, DMA publication, or translation on a board.
PDS and USC mappings, firmware mapping, and job submission remain separate work.

## Provenance

The layout follows the Linux PowerVR driver at
`7b63ef2d55f24519e7e9e5f4d15dbea03f126e40`:

- [`pvr_vm.c`](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_vm.c):
  `fw_mem_context_init()` and the separate firmware memory context allocation.
  SHA256 `efaeea9a1f2ef56d998c75642898d43bee5dbe1dbe9e08ed66b1f4aed5072b04`.
- [`pvr_rogue_heap_config.h`](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_rogue_heap_config.h):
  the general heap range.
  SHA256 `5e73c39a6abc48f81db22e98b58ad7c5038bba24d5116d84f6f2539bfc78e2a4`.
- The pinned native `powervr/pvr_rogue_fwif.h` supplies the real FW context layout.
  Compile-time checks verify its size and the two initialized field offsets.
- Native ARM `bus_dma` defines CPU mapping flags and loaded map state.

The MIT license option is selected for upstream-derived definitions.
The new native source preserves the complete MIT notice and upstream attribution.
The owner, failure checks, and contract fixture are AI-assisted EmberBSD work.
