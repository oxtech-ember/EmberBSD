# A733 PowerVR MIPS memory encoders

The native [helpers](../../sys/arch/arm/sunxi/sun60i_a733_gpu_mips.c)
encode the initial MIPS firmware page table and boot data.
They perform no allocation, file access, DMA, MMIO, clock changes, or execution.
The GPU consumer does not call these helpers yet.
These contracts do not establish firmware boot or GPU acceleration.

## Interface

Include [sun60i_a733_gpu_mips.h](../../sys/arch/arm/sunxi/sun60i_a733_gpu_mips.h).
Both functions return positive errno values and leave outputs unchanged on error.
The caller owns valid buffers and serializes all access for the entire call.
Inputs must remain stable. Output byte buffers may be unaligned.
Input arrays contain native `uint64_t` values and require native alignment.
Input and output buffers must not overlap. Their spans must not wrap.

```c
int sun60i_a733_gpu_mips_map(void *table, size_t table_size,
    uint64_t va, const uint64_t *pages, size_t page_count);
int sun60i_a733_gpu_mips_boot_data(void *out, size_t out_size,
    uint64_t stack_pa, uint64_t register_pa, const uint64_t pt_pa[4]);
```

The caller must validate allocation ownership, device access, and DMA constraints.
A numerically valid physical address does not prove RAM ownership or reachability.
Physical address zero is representable and passes these numerical checks.
The caller must exclude MMIO from stack, page table, and payload allocations.
Cache synchronization, DMA lifetime, wrapper remaps, and MMU flushes belong to
future native integration. The helpers do not replace those operations.

## Page table

Start with a zeroed table of exactly 16384 bytes.
The table has 4096 little-endian 32-bit entries, one per 4096-byte page.
Supply 1 through 4096 physical pages and an aligned firmware virtual address.
The full mapping must fit `[0xc0000000, 0xc1000000)` without wrapping.
Physical addresses must be page-aligned and fit 36 bits.

Each entry contains `(physical_address >> 6) | 0x17`.
Bits 29 through 6 contain the page frame number.
The flags select uncached memory and set dirty, valid, and global bits.
Read-inhibit and execute-inhibit bits remain clear.
Every prior nonzero entry must use this same encoding.
The entire table is checked before any requested entry changes.
A target entry must be zero, including when the requested mapping is identical.

New physical pages must not alias each other or any prior mapped page.
This restriction belongs to this initial-boot helper, not the MIPS ABI.
The caller must use a zeroed table and only serialized helper updates.
The helper validates old encodings but does not search for aliases between
old entries introduced by other writers. There is no replace or unmap API.
Validation uses bounded comparisons and no temporary allocation.
The largest request compares at most 4096 new pages against 4096 entries.

The wrapper redirects physical addresses in `[0x1fc00000, 0x1fc04000)`.
Upstream uses a trampoline for these four pages.
This implementation rejects them until native trampoline support exists.
The rule also applies to prior PTEs, the stack, and page table page addresses.

## Boot data

The output size must be exactly 64 bytes.
The wire layout matches upstream `struct rogue_mipsfw_boot_data`:

| Offset | Size | Value |
| --- | --- | --- |
| 0 | 8 | Stack physical page address |
| 8 | 8 | Register physical address, exactly `0x01800000` |
| 16 | 32 | Four physical page table page addresses |
| 48 | 4 | Page size exponent, 12 |
| 52 | 4 | Page table page count, 4 |
| 56 | 4 | Reserved, zero |
| 60 | 4 | Reserved, zero |

Every field uses explicit little-endian byte writes.
Stack and table addresses must meet the same alignment and 36-bit limits.
The four page table pages need not be physically contiguous.
They must be distinct from each other and from the stack page.
The caller supplies their correct order and keeps the allocation alive.
The helper does not place this structure into a firmware image.

`EINVAL` reports malformed arguments, ranges, prior entries, or buffer overlap.
`EEXIST` reports an occupied target entry or a prohibited physical alias.
`EOPNOTSUPP` reports a physical page in the wrapper remap window.
If several checks fail, callers must not depend on their error precedence.

## Contract tests

Run the [production-source contract](../tools/a733-gpu-mips-contract.sh):

```sh
sh ember/tools/a733-gpu-mips-contract.sh
MIPS_TEST_CFLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all -g' \
    sh ember/tools/a733-gpu-mips-contract.sh
```

Set `TMPDIR` to the selected external build directory when required.
The script compiles the real helper source and removes its temporary binary.
It checks independent wire-byte vectors, full-heap mapping, sparse updates,
36-bit boundaries, alignment, malformed prior PTEs, alias rejection,
wrapper-sensitive pages, span overlap, wrap checks, and unchanged error outputs.
It also checks all boot-data fields and reserved bytes.
ASan/UBSan checks host memory behavior, not DMA or kernel execution.
Valgrind is not available on the tested Darwin arm64 host.

## Provenance

The wire definitions and translation rules come from the Linux PowerVR driver
at revision `7b63ef2d55f24519e7e9e5f4d15dbea03f126e40`:

- [pvr_vm_mips.c](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_vm_mips.c):
  SHA256 `8778f1195def444131d325a4d41ff3ad3c99e5bfd240d54072b65c5b3aff15b9`.
- [pvr_fw_mips.c](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_fw_mips.c):
  SHA256 `2e4a9a791a41d9f9e2d2bc713904e7fc3a081ea7e5e556002641c03f8e0ad6d0`.
- [pvr_rogue_mips.h](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_rogue_mips.h):
  SHA256 `c2a89bd580c94506179c6dccc29dbd04f3bea1994b7788395058a040c69416db`.

These files offer `GPL-2.0-only OR MIT`. This adaptation selects MIT.
The native source and header retain Imagination attribution and the MIT notice.
They have no Linux kernel dependency. The EmberBSD implementation and tests
were prepared with AI assistance.
