# A733 PowerVR CPU-owned firmware memory

[The native memory owner](../../sys/arch/arm/sunxi/sun60i_a733_gpu_fwmem.c)
prepares firmware buffers with EmberBSD `bus_dma`.
It combines the [ELF image helper](a733-gpu-fwimage.md) and
[MIPS encoders](a733-gpu-mips.md).
A successful call produces CPU-owned code, data, and page-table allocations.
It does not execute firmware or prove GPU rendering.

## Ownership and initial scope

[`sun60i_a733_gpu_fwmem_prepare()`](../../sys/arch/arm/sunxi/sun60i_a733_gpu_fwmem.h)
uses a valid DMA tag, stable firmware bytes, and the expected live BVNC.
The caller serializes the operation and calls it from sleepable context.
The helper allocates one contiguous DMA segment per buffer.
Each code/data allocation must be page-aligned and no larger than 256 KiB.
The page table has four 4 KiB pages.
Loaded DMA addresses must be page-aligned and fit the 36-bit physical bus scope.
Code, data, and table DMA ranges must not overlap.
That last check rejects malformed backend results; no defect in the native
`bus_dma` allocator has been established.

The helper checks metadata and the complete ELF image before creating MIPS mappings.
The MIPS heap starts at `0xc0000000` and covers 16 MiB.
Code starts at heap offset zero.
Data starts at the private-data virtual address from the metadata.
The pinned input uses `0xc0032000`.
The code and data allocations must fit disjoint heap ranges.

The first wrapper setup has 4 KiB remap windows.
This preparation path accepts exactly one page for each of these layouts:

| Layout | Virtual address |
| --- | --- |
| Boot code | `0xbfc00000` |
| Boot data | `0xbfc01000` |
| Exception code | `0x9fc02000` |
| Stack | `0xcf600000` |

Their allocation offsets must also be page-aligned.
The entry must select microMIPS at the boot-code base.
Larger layouts require a separate supported mapping strategy.
The firmware metadata's `max_size` does not change these actual allocation limits.

The code/data heap uses little-endian, uncached 4 KiB MIPS PTEs.
The 64-byte boot structure receives the stack DMA address, GPU register base,
and four page-table DMA addresses.
The GPU register base is `0x01800000`.
The helper uses addresses from loaded DMA maps, not opaque allocation segments.
The initial encoders reject physical pages in the wrapper's sensitive remap window.

No GPU register or device descriptor receives any prepared address.
There is no DMA synchronization or ownership transfer at this stage.
A later execution stage must perform the required cache synchronization and
track device ownership before publishing addresses.

On failure, the output pointer stays unchanged.
Every successful DMA map load is unloaded before map destruction.
CPU mappings are unmapped before their backing memory is freed.
All earlier allocations are released when a later operation fails.
`sun60i_a733_gpu_fwmem_destroy()` accepts `NULL`.
Destroying a prepared object is legal only while the CPU still owns every buffer.

## Explicit runtime preparation

After [runtime stages 1 through 12](a733-gpu-identification.md#runtime-staged-diagnostics-with-a-watchdog-bound)
have succeeded, stage 13 rereads the firmware through `firmload`.
It applies the complete ELF and memory checks to that fresh input.
It closes the file and frees the input before publishing `memory_state`.
A close failure destroys the prepared allocations and remains an error.
A successful stage retains the three CPU-owned buffers for a later bootstrap.
Normal observe-only boot performs none of these allocations.

```sh
sysctl -w hw.sun60igpuid0.stage=13
sysctl hw.sun60igpuid0.result hw.sun60igpuid0.memory_state
```

The stage is single-use per boot. The returned sizes and DMA addresses describe
prepared memory. They do not prove a GPU transfer or firmware execution.

## Physical preparation result

On 2026-10-12, stage 13 passed on a 4 GiB Orange Pi Zero 3W.
The GCC 16.2 `EMBER64 #5` bundle used source `30b1549f61cb9ba607d2a39def9a9d2f68717883`.
All stages 1 through 13 returned zero with PLL_GPU0 selected.
The native allocator prepared 102400 code bytes, 20480 data bytes,
and a 16384-byte MIPS page table. Five ELF loads passed validation.
The entry was `0xbfc00001`; boot-code and stack DMA addresses were
`0x4011c000` and `0x40121000` in this run.
These addresses are observations, not fixed allocation requirements.

The four modules came from the same build. The existing observe-only DTB,
boot firmware and startup configuration were preserved. Wi-Fi/SSH returned;
the existing NPU probe still reported stage 4 and errno zero.
The explicit sequence took under one second at the log's timestamp resolution.
Its 16-second hardware watchdog did not fire and was disarmed afterwards.
No firmware execution, device DMA, display output, or long run was tested.

| Artifact | SHA256 |
| --- | --- |
| ELF kernel | `29c14cbe458d2266b563b26b225002093310e68bb0cffd5837f8aeedc4171880` |
| Native image | `201796fd9112955fe4d3300f191eeace51c055ba448022b50aadaeaff609ff69` |
| Preserved board DTB | `6e3e92e251a617066a54634394e65d3463df96fdd5920753056cb0c2e4e78d4e` |

## Reproduce the contract



[The contract](../tools/a733-gpu-fwmem-contract.sh) executes the production
owner functions with simulated `bus_dma` and `kmem` callbacks.
It links the real metadata parser, ELF preparer, and MIPS encoders.
Only kernel include lines and `__KERNEL_RCSID` are removed for the host harness.
No hardware allocation or GPU execution is simulated as a successful physical test.

```sh
sh ember/tools/a733-gpu-fwmem-contract.sh
CC=clang FWMEM_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/a733-gpu-fwmem-contract.sh /path/to/rogue_36.56.104.183_v1.fw
```

The optional firmware path must match the SHA256 in the
[firmware provenance record](a733-gpu-fwinfo.md#provenance-and-licensing).
The script does not download or alter that input.
Tests cover all twelve fallible allocation/map/load operations,
malformed segment results, unsupported layouts, sensitive physical addresses,
DMA aliases, unchanged input/output pointers, and complete cleanup.
They compare encoded PTEs and the boot structure with independent expected values.

The larger exception/boot-data layouts and table/code alias regressions fail
against the saved pre-fix owner and pass after the validation changes.
The alias fault deliberately violates the backend contract.
It is a defensive check, not a reproduction on native hardware.

On 2026-10-12, the pinned-input host contract passed 1195 checks with ASan/UBSan.
GCC 16.2 compiled the owner with actual EmberBSD AArch64 kernel headers.
Its preparation frame was 1072 bytes; the page-map helper used 528 bytes.
Clang 21 Static Analyzer found no diagnostics in the target owner or host harness.
GCC path analysis exceeded its default graph budget.
It completed without diagnostics with `-fanalyzer -Wanalyzer-too-complex`,
`--param=analyzer-bb-explosion-factor=50`, and
`--param=analyzer-max-enodes-per-program-point=64`.

Cppcheck 2.22 found no remaining diagnostics in the changed owner or harness.
The target run still reports two pre-existing `constParameterPointer` findings
in `sys/sys/bus.h`, at lines 46 and 61; its overall exit status is therefore nonzero.
The previous `pages` finding assumed an unreachable zero-page call.
A local count guard now expresses the allocator's nonempty-size contract.
Host suppressions cover only const suggestions in two mock `bus_dma` hook
signatures. Those signatures intentionally preserve the real API pointer types.
No category is disabled globally.
Valgrind is unavailable on the macOS/arm64 host; ASan/UBSan was used instead.
These tests do not establish native DMA correctness or firmware execution.

## Provenance

The native allocation and cleanup implementation is BSD-2-Clause.
MIPS heap placement and boot-data requirements were checked against Linux PowerVR,
revision `7b63ef2d55f24519e7e9e5f4d15dbea03f126e40`:

| Reference | SHA256 |
| --- | --- |
| [pvr_fw.c](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_fw.c) | `c55b994aa005f727f6708879162c83f86d8c35201f2cda036795cbd39470daea` |
| [pvr_fw_mips.c](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_fw_mips.c) | `2e4a9a791a41d9f9e2d2bc713904e7fc3a081ea7e5e556002641c03f8e0ad6d0` |

Those references offer `GPL-2.0-only OR MIT`; the referenced option is MIT.
The separate [MIPS encoding source](a733-gpu-mips.md) preserves its MIT notice
and Imagination Technologies attribution.
The native owner, tests, and documentation were developed with AI assistance.
The external firmware retains its separate `LICENSE.powervr` terms.
