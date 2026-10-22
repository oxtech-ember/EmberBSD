# A733 PowerVR firmware ABI checks

The [native FWIF headers](../powervr/README.md) preserve
Linux PowerVR's exact shared structures and compile-time layout assertions.
They provide the data contract for a future MIPS firmware boot implementation.
They do not allocate memory, access registers, transfer firmware, or start GPU work.
No accelerated display or Plasma session is established by these checks.

## Interface

Include `arm/sunxi/powervr/pvr_rogue_fwif.h` from kernel code.
No Linux compatibility layer is required.
The header selects the exact upstream structure and command layouts.
Every inclusion evaluates 476 upstream offset, size, and array assertions.
Four further upstream assertions check structure and ring constraints.
Native assertions reject incompatible pointer, boolean, integer, and byte-order assumptions.

The current interface requires a 64-bit little-endian host.
Use default enum widths and the declared field alignment.
A host pointer in `rogue_fwif_tracebuf_space` is for host use only.
Do not substitute it for a 32-bit firmware address field.
A correct C layout does not establish DMA coherency, valid GPU addresses,
firmware compatibility, memory ordering, or a working command protocol.

| Shared record | Size, bytes | Alignment, bytes |
| --- | ---: | ---: |
| `rogue_fwif_connection_ctl` | 16 | 8 |
| `rogue_fwif_osinit` | 104 | 8 |
| `rogue_fwif_sysinit` | 544 | 8 |
| `rogue_fwif_sysdata` | 3624 | 8 |
| `rogue_fwif_osdata` | 584 | 8 |
| `rogue_fwif_runtime_cfg` | 72 | 4 |
| `rogue_fwif_gpu_util_fwcb` | 10280 | 8 |
| `rogue_fwif_hwrinfobuf` | 2336 | 8 |
| `rogue_fwif_tracebuf` | 864 | 8 |
| `rogue_fwif_ccb_ctl` | 144 | 8 |
| `rogue_fwif_kccb_cmd` | 88 | 8 |
| `rogue_fwif_fwccb_cmd` | 88 | 8 |

`rogue_fwif_sysinit.firmware_started` is a one-byte boolean at offset 352.
The table describes the pinned ABI. Do not derive offsets from this document.
Use the C fields and keep the upstream assertions active.

## Reproduce the checks

Run the [contract](../../../../../ember/tools/a733-gpu-fwabi-contract.sh) from the source root:

```sh
sh ember/tools/a733-gpu-fwabi-contract.sh
CC=clang FWABI_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/a733-gpu-fwabi-contract.sh
```

The script compiles and includes the production headers directly.
It prints the sizes of the boot-related records.
It verifies that `-fshort-enums` and `-fpack-struct=1` fail the ABI assertions.
It also verifies that the array check rejects a pointer member.
It neither downloads nor opens a firmware binary.

For a target compile, use the current EmberBSD kernel compiler and include paths.
Compile `ember/tools/a733-gpu-fwabi-contract.c` with `PVR_FWABI_COMPILE_ONLY` defined.
This omits the host executable and evaluates the same header assertions.
Keep the configured target headers and flags in the compilation database.

## Validation boundary

On 2026-10-12, Apple Clang 21 compiled and ran the contract on macOS arm64
with ASan/UBSan. All incompatible-ABI cases failed as expected.
GCC 16.2 compiled the contract with actual EmberBSD `EMBER64` kernel headers.
GCC `-fanalyzer` and Clang Static Analyzer reported no diagnostics.
Cppcheck 2.22 reported no diagnostics with the target compilation database.

The unmodified pinned upstream headers were compiled through a separate type
and macro harness. Their printed layouts matched the native headers.
Cppcheck reported no findings before or after the adaptation.
This comparison checks layout adaptation; it does not test a Linux runtime.
The headers contain declarations and one retained inline helper.
Static analysis has no device execution paths to inspect here.
Valgrind does not support the macOS arm64 host used for this check.
The ASan/UBSan run is not a Valgrind result.

Provenance, original SHA256 hashes, and the MIT notice are in the
[header directory](../powervr/README.md).
