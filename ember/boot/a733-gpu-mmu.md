# A733 Rogue GPU MMU encoding

The [native helper](../../sys/arch/arm/sunxi/sun60i_a733_gpu_mmu.c) encodes
Rogue GPU page-table entries and splits GPU virtual addresses into indices.
It uses the 36-bit physical, 40-bit virtual, 4 KiB page profile of BVNC `36.56.104.183`.
These tables are separate from the [MIPS firmware tables](a733-gpu-mips.md).
The helper performs no allocation, DMA synchronization, MMIO, or table publication.
It does not establish GPU translation, command submission, rendering, or display output.

## Table format

Each table occupies one 4096-byte backing page.

| Level | Entries | Entry width | Virtual address index | Coverage per entry |
| --- | --- | --- | --- | --- |
| Page catalogue (PC) | 1024 | 32 bits | 39:30 | 1 GiB |
| Page directory (PD) | 512 | 64 bits | 29:21 | 2 MiB |
| Page table (PT) | 512 | 64 bits | 20:12 | 4 KiB |

Virtual address bits 11:0 select a byte within the leaf page.
The valid bit is bit zero at every level. An all-zero entry is invalid.
The 4 KiB page-size field in a PDE is zero.
PCE bits 31:4 contain the PD physical address shifted right by twelve bits.
PDE and PTE address fields retain physical address bits 39:12.
This profile requires bits 39:36 to be zero.
All physical page inputs must be 4096-byte aligned and fit the full 36-bit range.
The helper validates 64-bit inputs before narrowing a PCE to 32 bits.

## API and limits

The [header](../../sys/arch/arm/sunxi/sun60i_a733_gpu_mmu.h) provides four functions:

- `sun60i_a733_gpu_mmu_indices()` accepts a byte VA and returns PC, PD, PT, and byte-offset indices.
- `sun60i_a733_gpu_mmu_pce()` encodes one valid entry for a PD page.
- `sun60i_a733_gpu_mmu_pde()` encodes one valid entry for a PT page.
- `sun60i_a733_gpu_mmu_pte()` encodes one valid leaf page with explicit options.

All functions return positive `EINVAL` for malformed arguments.
Errors leave the complete output unchanged.
Output pointers must be non-null, naturally aligned, and non-wrapping.
Encoded outputs are native integers. A later table owner must serialize them little-endian.
The caller must own the output storage and serialize access.
A representable physical address, including zero, does not prove that RAM exists there.
The future allocator must validate ownership, accessibility, and table/page relationships.

The leaf encoder accepts exactly these hardware options:

| Option | PTE bit | Meaning |
| --- | --- | --- |
| `A733_GPU_MMU_PTE_READ_ONLY` | 1 | Reject GPU writes |
| `A733_GPU_MMU_PTE_COHERENT` | 2 | Set the GPU coherency attribute |
| `A733_GPU_MMU_PTE_SLC_BYPASS` | 3 | Request System Level Cache bypass |
| `A733_GPU_MMU_PTE_PM_FW_PROTECT` | 62 | Restrict writes to PM or firmware |

The coherency option only encodes a bit. It does not establish CPU/DMA cache coherence.
The caller must justify cache attributes for the actual interconnect and mapping.
The pinned upstream map path leaves read-only and coherency options clear.
The encoder can represent all four documented options without selecting a policy.
Pending, PM source, virtual-physical page fields, and all unknown flag bits fail.
Larger page sizes and another address-width profile are outside this helper's scope.
No erratum-specific behavior is inferred from an unverified BRN number.

## Reproduce the contract

[The contract](../tools/a733-gpu-mmu-contract.sh) compiles the real helper.
Its default oracle uses independent literal encodings and reconstructs virtual addresses.
An optional directory supplies exact pinned upstream sources for an additional oracle.
The script checks both source hashes before extracting upstream function bodies.
It executes the upstream PCE, PDE, PTE, flag, and three index functions.
Only their kernel type and `WRITE_ONCE` adapters are replaced for the host.

```sh
sh ember/tools/a733-gpu-mmu-contract.sh
MMU_UPSTREAM_DIR=/path/to/pinned-linux-pvr \
    MMU_TEST_CFLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all' \
    sh ember/tools/a733-gpu-mmu-contract.sh /path/to/rogue_36.56.104.183_v1.fw
```

The upstream directory must contain `pvr_mmu.c` and `pvr_rogue_mmu_defs.h` from the pin below.
The optional firmware must match the [metadata parser's pinned SHA256](a733-gpu-fwinfo.md#provenance-and-licensing).
The script downloads nothing and changes neither reference input.
The real metadata parser verifies the pinned image's 36-bit PA, 40-bit VA, and 4096-byte page values.
This is metadata compatibility, not a measurement of hardware translation.

Tests cover every misalignment within one page, all out-of-profile address bits,
all unsupported flag bits, all sixteen supported option combinations, null and misaligned outputs,
wrapping output spans, deterministic preservation on errors, and virtual index boundaries.
They also compare 4096 deterministic generated addresses against both oracles.
Host execution does not test cache maintenance, MMU faults, hardware errata, or a GPU page walk.

On 2026-10-12, ASan/UBSan passed 271129 checks with the upstream oracle and pinned firmware.
The independent default mode passed 189120 checks without external reference files.
Four deliberate mutations failed: wrong PCE shift, PA truncation, unknown flags, and error-output clobbering.
GCC 16.2 compiled the source with actual EmberBSD AArch64 kernel headers.
Its default-budget `-fanalyzer -Wanalyzer-too-complex` run completed without diagnostics.
Clang 21 Static Analyzer and Cppcheck 2.22 found no diagnostics in the target source or host contract.
Host analysis included the extracted upstream functions and the real metadata parser.
No analyzer category was suppressed. Valgrind is unavailable on the macOS/arm64 host;
the sanitizer result is not a Valgrind result.

## Provenance and licensing

The format and reference bodies come from Linux PowerVR revision
[`7b63ef2d55f24519e7e9e5f4d15dbea03f126e40`](https://github.com/torvalds/linux/tree/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination).
The sources offer `GPL-2.0-only OR MIT`; this implementation selects MIT.
The native files preserve Imagination Technologies attribution and the complete MIT notice.
The checked API, regression contract, and documentation are AI-assisted EmberBSD work.

| Source | SHA256 |
| --- | --- |
| [pvr_mmu.c](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_mmu.c) | `f637771e335223c947b1d294f9f84bfc7d60bf91279d07a175f49470f5d2d9f1` |
| [pvr_rogue_mmu_defs.h](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_rogue_mmu_defs.h) | `123701eb6a9bb2ce408e2f2da2ad8a52c654dac9c78c46ec64426bca9c5ea6d7` |

The pinned [BRN enum](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_rogue_fwif_dev_info.h) contains `62269`; it has no entry for `62307`.
Neither the pinned MMU implementation nor its definitions require a `62307` encoding change.
This absence is not evidence that every hardware erratum is handled.
The future owner must establish any additional profile restrictions before publication.
