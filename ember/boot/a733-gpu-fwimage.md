# A733 PowerVR MIPS CPU image preparation

[The native helper](../../sys/arch/arm/sunxi/sun60i_a733_gpu_fwimage.c)
prepares code and data in caller-owned CPU buffers.
It validates metadata through the [production parser](a733-gpu-fwinfo.md).
It then checks the ELF32 program headers before it writes any output.
This is a preparation component, not evidence of firmware execution or GPU rendering.

## API and supported scope

[`sun60i_a733_gpu_fwimage_prepare()`](../../sys/arch/arm/sunxi/sun60i_a733_gpu_fwimage.h)
accepts the original stable firmware bytes, their size, and the expected live BVNC.
The caller supplies separate writable code, data, and result buffers.
The capacities must cover the metadata allocation sizes.
All declared spans must be disjoint and must not wrap `uintptr_t`.
The caller must supply valid memory for every declared span.

The first implementation supports these conditions:

- ELF32, little endian, `ET_EXEC`, `EM_MIPS`, and ELF version 1.
- A 52-byte ELF header and 32-byte program headers.
- Between 1 and 16 program headers, containing only `PT_LOAD` or `PT_NULL`.
- All six MIPS layout IDs: code, exceptions, boot code, private data, boot data, and stack.
- Equal physical and virtual addresses in each load header.
- Read/execute permissions for CODE and read/write permissions for DATA.
- Each load fits exactly one actual metadata allocation, without virtual or destination overlaps.
- `filesz <= memsz`, nonzero `memsz`, and source ranges inside the metadata payload boundary.
- Alignment is zero, one, or a power of two. Virtual address and file offset must be congruent.
- The entry, with the microMIPS bit cleared, lies inside file-backed boot code.

The permission, address-equality, and header-count restrictions define this
initial loader scope. They are not universal ELF requirements.
Unknown program header types return `EOPNOTSUPP`; their contents are not interpreted.
`PT_NULL` entries are skipped after the complete header table is bounded.
The helper does not inspect instructions, section headers, relocations, or ELF architecture flags.
It does not authenticate firmware or verify that its instructions can execute.

After complete validation, the helper zeros the required code and data allocations.
It copies each load's opaque file bytes to its checked destination.
This initializes BSS, unused allocation space, boot data, and stack to zero.
Capacity beyond the required allocation sizes stays unchanged.
Any validation error leaves the input, code, data, and result unchanged.

The result contains decoded metadata and six layouts in ID order.
Use `section[id - A733_GPU_FW_MIPS_CODE]` to get each layout.
Each layout supplies `alloc_offset`, `alloc_size`, and `base_addr`.
`entry` preserves the microMIPS bit; `entry_addr` clears it.
`entry_offset` locates the normalized entry in the code buffer.
`load_count` counts the accepted `PT_LOAD` entries.

This helper does not allocate memory or access devices.
A later loader must prepare boot parameters, address translations, cache synchronization,
DMA lifetime, interrupts, and firmware startup through separate checked mechanisms.
Zeroed boot data alone is not a complete boot configuration.

## Checks

[The contract](../tools/a733-gpu-fwimage-contract.sh) links the actual image
helper and metadata parser. It has no replacement implementation.
Run it from the source root:

```sh
sh ember/tools/a733-gpu-fwimage-contract.sh
CC=clang FWIMAGE_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/a733-gpu-fwimage-contract.sh /path/to/rogue_36.56.104.183_v1.fw
```

The optional firmware input must match the SHA256 below.
The script does not download or modify that file.
Synthetic tests cover all 16 load slots, truncated input, unsupported headers,
range overflow, allocation boundaries, overlapping segments, permissions, alignment,
BSS, microMIPS entry handling, buffer aliasing, and unchanged outputs after late errors.
They also make 4000 deterministic mutations of project-owned ELF headers.
The real input test compares all prepared bytes with independently recorded load ranges.
It checks zero-filled regions, spare capacity, and the unchanged original input.

On 2026-10-12, the production-source contract passed ASan/UBSan on macOS/arm64.
The pinned firmware produced 102400 code bytes and 20480 data bytes from five loads.
Its entry is `0xbfc00001`; the normalized entry offset is 98304 in CODE.
Exception code starts at CODE offset 94208.
Boot data and stack start at DATA offsets 12288 and 16384.
These values describe this pinned input, not fixed allocation sizes in the helper.

Cppcheck 2.22 and Apple Clang 21 Static Analyzer checked the helper and host contract.
The helper also passed GCC 16.2 compilation and Clang/Cppcheck analysis with actual
EmberBSD AArch64 kernel headers and configuration.
GCC path analysis reached its default per-program-point graph limit in the layout loop.
It completed without diagnostics with `-fanalyzer -Wanalyzer-too-complex`,
`--param=analyzer-bb-explosion-factor=50`, and
`--param=analyzer-max-enodes-per-program-point=64`.
No analyzer category was disabled.
The macOS/arm64 host does not support Valgrind; ASan/UBSan was used instead.
Host preparation and target compilation do not establish DMA or firmware execution.

## Provenance and licensing

The ELF loading reference is Linux PowerVR
[`pvr_fw_util.c`](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_fw_util.c),
revision `7b63ef2d55f24519e7e9e5f4d15dbea03f126e40`.
Its SHA256 is `d871d443a5f8720609d4aa69d724cccb1cd1032eca7d9686282ebaa405e1ea2d`.
The source offers `GPL-2.0-only OR MIT`; this adaptation selects MIT.
The helper preserves Imagination Technologies' 2024 attribution and the MIT notice.
EmberBSD's bounded validation, preparation, and tests were developed with AI assistance.
Metadata provenance is recorded with [the metadata parser](a733-gpu-fwinfo.md#provenance-and-licensing).

The unchanged external test input is Imagination's
[`rogue_36.56.104.183_v1.fw`](https://gitlab.freedesktop.org/imagination/linux-firmware/-/blob/8a58f81883f7be458daa34e418cc4079f995b279/powervr/rogue_36.56.104.183_v1.fw),
revision `8a58f81883f7be458daa34e418cc4079f995b279`, size 131072 bytes.
Its SHA256 is `1db1c399c17401d1f79d46c880db81c724d748c784d4639433b076aba2f9c0d2`.
The firmware uses the separate `LICENSE.powervr` terms; it is not MIT software.
The tests read its published metadata and ELF headers and copy opaque payload bytes.
They do not disassemble or alter executable instructions, or write a modified firmware file.
The firmware binary is not included in the source tree.
