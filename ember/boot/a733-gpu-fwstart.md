# A733 PowerVR MIPS reset-release contract

The native [startup function](../../sys/arch/arm/sunxi/sun60i_a733_gpu_fwstart.c)
implements the PowerVR MIPS register sequence for BVNC **36.56.104.183**.
It uses fault-aware `bus_space_peek` and `bus_space_poke` accesses.
It does not allocate memory, prepare firmware, verify boot completion, or submit GPU work.
The software tests below do not establish physical firmware execution or accelerated Plasma.

## Caller contract

Include [sun60i_a733_gpu_fwstart.h](../../sys/arch/arm/sunxi/sun60i_a733_gpu_fwstart.h).
Call `sun60i_a733_gpu_fwstart(bst, bsh, info, boot_code, boot_data, exception_code, result)`.
The three addresses are physical DMA addresses, in that order.
They refer to separate 4096-byte boot-code, boot-data, and exception-code regions.

The caller must:

1. Own powered hardware and serialize access to it.
2. Map `A733_GPU_FWSTART_REG_SIZE` bytes at the GPU register base.
3. Supply validated metadata for the GPU identified on that device.
4. Keep that metadata stable for the whole call.
5. Prepare firmware code, data, page tables, and all required FWIF structures.
6. Synchronize every buffer for device access before calling the function.
7. Retain every reachable allocation after any attempted register write.

The last requirement includes failed calls.
Freeing memory requires a separately proven hardware reset or quiescence procedure.
This function implements no retry, rollback, shutdown, or ownership recovery.
The [CPU memory helper](a733-gpu-fwmem.md),
[image preparation](a733-gpu-fwimage.md), and
[FWIF structures](a733-gpu-fwabi.md) have separate contracts.
A valid address range does not prove allocation ownership or DMA coherency.

Preflight accepts the identified MIPS/PBE2 core with BRN63553,
a 36-bit physical bus, and 512-bit SLC cache lines.
It rejects `xe_tpu2` and `gpu_multicore_support` configurations.
All remap addresses must be page-aligned, fit the 36-bit bus, and differ.
They must avoid physical addresses `0x1fc00000` through `0x1fc03fff`.
These checks complete before the first register access.

## Sequence and failure state

The pinned upstream sequence is preserved:

1. Clear optional system-bus protection and fence that write.
2. Apply the PBE2 reset mask, then release Rascal and Dust.
3. Keep the firmware processor in reset while releasing the other blocks.
4. Configure the low 32 bits of SLC control. Preserve upper scramble bits.
5. Configure the MIPS wrapper and boot, data, exception, and BRN63553 remaps.
6. Configure Garten idle control, debug, and AXI-ACE attributes.
7. Wait three microseconds, release firmware reset, fence, then wait again.

Each normal write receives a write barrier.
Each read receives a read/write barrier before access.
The default sequence has 17 writes and 22 total register accesses.
System-bus protection adds one write and one read.
BRN71242 sets the SLC lazy-writeback override when present in validated metadata.
A register fault stops the sequence immediately. No later access is attempted.

A non-NULL result is cleared before validation, including an invalid `info` pointer value of `NULL`.
The result contains:

| Field | Meaning |
| --- | --- |
| `last_register` | Offset of the last attempted register access |
| `writes` | Number of attempted writes, including a faulting write |
| `touched` | Set before the first write, including a faulting write |
| `released` | The reset-release write returned success |

`released` does not mean that firmware booted.
A reset-release write can affect hardware and then report a fault.
Therefore, `released == false` does not prove that the GPU is stopped.
`touched == true` requires retaining all firmware memory after any return value.
A successful return only establishes that the register sequence completed.
The caller must separately observe and validate the firmware startup handshake.

The function returns positive errno values:
`EINVAL` for invalid arguments or remap addresses,
`EOPNOTSUPP` for an unsupported metadata configuration,
and `EFAULT` for a fault reported by a register accessor.
Fault recovery does not guarantee bounded latency for an interconnect stall.
A hardware watchdog and an independent recovery path remain board-test requirements.

## Reproduce the software checks

Run the [production-source contract](../tools/a733-gpu-fwstart-contract.sh):

```sh
sh ember/tools/a733-gpu-fwstart-contract.sh
CC=clang FWSTART_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/a733-gpu-fwstart-contract.sh
```

The test records MMIO calls. It does not execute firmware.
Its literal golden trace comes from the pinned upstream start and MIPS-wrapper functions.
It is independent of the native function's successful output.
The tests cover all combinations of optional system-bus reset and BRN71242.
They inject faults before and after each access, including reset release.
They check barrier order, delay placement, upper SLC bits, full 36-bit addresses,
invalid input, and the ownership flags seen by each accessor.

On 2026-10-12, the host ASan/UBSan run passed **39123 checks**.
Six deliberate defects were detected: late `touched`, incorrect AXI configuration,
incorrect remap order, premature `released`, a missing write barrier,
and physical-address truncation.
A failing-before regression also detects stale result flags for `info == NULL`.

GCC 16.2 compiled the production source with actual EmberBSD `EMBER64` headers.
GCC `-fanalyzer` and host/target Clang Static Analyzer reported no diagnostics.
Cppcheck 2.22 reported no startup-source findings.
Two unchanged const-parameter suggestions remain in the included `sys/bus.h`.
The host contract passed Cppcheck after local scope and naming cleanup.
An upstream branch harness has expected constant-condition and const-style findings.
Its hardware access and feature lookups are test boundaries, not a Linux runtime.
No diagnostic category was disabled to obtain these results.

Valgrind does not support the macOS arm64 host used for this check.
ASan/UBSan was used instead. This is not a Valgrind result.
Kernel integration, real cache maintenance, physical startup, interrupts,
firmware queues, graphics jobs, and sustained operation require separate validation.

## Provenance and license

The sequence and constants come from Linux revision
`7b63ef2d55f24519e7e9e5f4d15dbea03f126e40`.
The source files offer `GPL-2.0-only OR MIT`; this adaptation selects MIT.
Imagination Technologies' 2023 attribution and the complete MIT notice are preserved.
The native adaptation and tests were developed with AI assistance.

| Original file | SHA256 |
| --- | --- |
| [pvr_fw_startstop.c](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_fw_startstop.c) | `5c33d75b4101b012cc21ff47e6eb71dbc7187c881d1542e7f533c6ddb62d8130` |
| [pvr_fw_mips.c](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_fw_mips.c) | `2e4a9a791a41d9f9e2d2bc713904e7fc3a081ea7e5e556002641c03f8e0ad6d0` |
| [pvr_rogue_cr_defs.h](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_rogue_cr_defs.h) | `7421bd9524a485e729421fa1452e09d6687d7a34b092c2502a3c1271b0295b7c` |
| [pvr_rogue_defs.h](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_rogue_defs.h) | `7c4c6902f7e456694bd3d64608eae48cdfccc223750773f07648de712e2d4ced` |
| [pvr_rogue_mips.h](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_rogue_mips.h) | `c2a89bd580c94506179c6dccc29dbd04f3bea1994b7788395058a040c69416db` |
| [pvr_rogue_fwif_dev_info.h](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_rogue_fwif_dev_info.h) | `d3d9c39a514a0b90dcd8756e9058e89e059fc51c19670766ed30d9d0cc4c84a5` |
