# A733 PowerVR initial firmware structures

The native [initializer](../../sys/arch/arm/sunxi/sun60i_a733_gpu_fwinit.c)
prepares CPU-owned work and configuration buffers for the first MIPS boot.
It uses the [exact native FWIF declarations](../../sys/arch/arm/sunxi/powervr/README.md).
It performs no allocation, DMA, MMIO, clock change, queue submission, or execution.
Successful initialization does not establish firmware boot or GPU acceleration.

## Interface and ownership

Include [sun60i_a733_gpu_fwinit.h](../../sys/arch/arm/sunxi/sun60i_a733_gpu_fwinit.h).
Call `sun60i_a733_gpu_fwinit_work_size()` before allocating the work buffer.
The current layout requires 139264 bytes, or 34 pages.

```c
int sun60i_a733_gpu_fwinit(void *work, size_t work_size,
    void *config, size_t config_size, uint64_t work_phys,
    uint64_t core_hz, uint32_t clusters, uint32_t slc_kib,
    struct sun60i_a733_gpu_fwinit_layout *out);
```

Both CPU buffers require 4096-byte alignment.
Work capacity must be a page multiple between the required size and 256 KiB.
Configuration capacity must be exactly 12288 bytes.
The result requires its native alignment and retains no CPU pointers.
The three spans must not wrap or overlap, including unused work capacity.
The caller owns valid buffers and serializes all access during initialization.
No device may access these buffers during the call.

`work_phys` describes one contiguous physical allocation for the claimed capacity.
It must be page-aligned. The entire range must fit 36 bits.
The range must avoid the MIPS wrapper remap window `[0x1fc00000, 0x1fc04000)`.
Physical address zero is numerically valid; the caller must verify RAM ownership.
Configuration physical pages are not inputs to this helper.
Their allocation, physical separation, mappings, and lifetime belong to the caller.
Neither successful range checks nor initialized bytes establish DMA reachability.

Pass the observed core clock and validated firmware feature values.
The clock must be nonzero and fit `uint32_t`.
The cluster count and SLC size in KiB must be nonzero `uint32_t` values.
These checks validate representability; they do not identify hardware capabilities.
The initializer neither selects nor changes clock frequency or voltage.

All checks run before the first output write.
On success, only the required work prefix and all configuration bytes change.
Spare work capacity remains untouched. The result is published last.
Every error leaves both buffers and the result unchanged.
`EINVAL` reports invalid arguments, alignment, ranges, sizes, or overlap.
`EOPNOTSUPP` reports intersection with the wrapper remap window.
The imported FWIF compile-time assertions reject unsupported host layouts.
The implementation requires 64-bit pointers and little-endian byte order.

## Fixed allocation directory

The result contains `total_work_size` and an indexed object directory.
Each entry records its buffer, byte offset, size, and firmware virtual address.
Work objects use base `0xc0100000` plus their offset.
Object starts have 64-byte alignment. The fault page has 4096-byte alignment.

| Work object | Offset | Size |
| --- | --- | --- |
| KCCB control | 0 | 144 |
| KCCB commands | 192 | 11264 |
| KCCB return slots | 11456 | 512 |
| FWCCB control | 11968 | 144 |
| FWCCB commands | 12160 | 2816 |
| Power sync | 14976 | 4 |
| HWR information | 15040 | 2336 |
| MMU cache sync | 17408 | 4 |
| System data | 17472 | 3624 |
| Fault page | 24576 | 4096 |
| GPU utilization | 28672 | 10280 |
| Runtime configuration | 38976 | 72 |
| Trace buffer 0 | 39104 | 48000 |
| Trace buffer 1 | 87104 | 48000 |
| Trace control | 135104 | 864 |
| OS data | 136000 | 584 |

The three configuration pages have separate firmware addresses.
Their contiguous CPU offsets do not imply contiguous firmware addresses.

| Configuration object | CPU offset | Structure size | Firmware VA |
| --- | --- | --- | --- |
| Connection control | 0 | 16 | `0xc0fd0000` |
| OS initialization | 4096 | 104 | `0xc0fe0000` |
| System initialization | 8192 | 544 | `0xc0ff0000` |

The caller must map these exact objects before any future firmware start.
The [MIPS page-table helper](a733-gpu-mips.md) does not establish DMA ownership.
No pointers to unallocated workload estimation, coremem, performance, or
register-configuration objects are fabricated.

## Initial values

KCCB has 128 slots. FWCCB has 32 slots. Both command records are 88 bytes.
Their controls start with zero read/write offsets and wrap masks 127 and 31.
Commands, return slots, sync words, HWR, and unused fields start at zero.
Connection states and tokens remain zero; a later handshake must set them.
OS initialization contains the allocated queue, HWR, and OS-data addresses.
Both compatibility records start with layout version 3 and BVNC zero.

Runtime configuration records the clock and cluster count.
APM latency persistence is true. APM latency and watchdog period are zero.
System data disables DM overlap when SLC size is below 4 KiB.
GPU utilization starts in IDLE. Every fault-page word is `0xdeadbee0`.
System initialization contains the physical fault-page address and allocated
runtime, trace-control, system-data, and GPU-utilization firmware addresses.
PDS and USC execution bases are `0xda00000000` and `0xe000000000`.
The initial clock is recorded and the marker is 1.
`firmware_started` remains false.

Both trace buffers contain 12000 dwords and start at zero.
Trace control records their firmware addresses and the shared dword count.
Logging is disabled initially. Trace pointers, flags, and assertion records are zero.
The native `trace_buffer` pointer remains NULL in each shared trace record.
Upstream labels that field for host readers; firmware uses `trace_buffer_fw_addr`.
A native reader must use the result directory and its own retained CPU mapping.
Writing a kernel CPU address into shared data provides no firmware address.

## Contract and validation limits

Run the [real-source contract](../tools/a733-gpu-fwinit-contract.sh):

```sh
sh ember/tools/a733-gpu-fwinit-contract.sh
FWINIT_TEST_CFLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all -g' \
    sh ember/tools/a733-gpu-fwinit-contract.sh
```

Set `TMPDIR` to the selected external build directory when required.
The contract compares complete initialized byte images against independent
upstream offsets and values. It resolves all 14 initialized firmware pointers
to their expected allocated objects and checks directory bounds and separation.
It checks 64-bit addresses, both SLC paths, spare capacity, sentinels,
physical limits, alias rejection, alignment, and unchanged error outputs.
The real FWIF headers retain their upstream ABI assertions during compilation.

Target compilation, static analysis, and host ASan/UBSan do not test DMA,
cache coherency, firmware consumption, queue processing, or GPU execution.
Valgrind is unavailable on the tested Darwin arm64 host.
The CPU helper owns no allocation or resource lifetime.

## Provenance

Initialization rules come from the Linux PowerVR driver at revision
`7b63ef2d55f24519e7e9e5f4d15dbea03f126e40`.
The source files offer `GPL-2.0-only OR MIT`; this adaptation selects MIT.
Imagination attribution and the MIT notice remain in the native source and header.
The implementation and tests were prepared with AI assistance.

| Upstream source | SHA256 |
| --- | --- |
| [pvr_fw.c](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_fw.c) | `c55b994aa005f727f6708879162c83f86d8c35201f2cda036795cbd39470daea` |
| [pvr_ccb.c](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_ccb.c) | `bbb078805620159c96adbaaa73d66cee36090882ea5f002631a3ccf47beb2f57` |
| [pvr_fw_trace.c](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_fw_trace.c) | `fcafbf8e86a3f0a54a613560e39a5b46ff10b2e7ee5fb8e50abb8f25c8344359` |
| [pvr_fw.h](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_fw.h) | `bb38d16e0397b8b48606e6b5380a8ac327fd64a9bc5d038fd6671b0f689cdb06` |
| [pvr_rogue_heap_config.h](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_rogue_heap_config.h) | `5e73c39a6abc48f81db22e98b58ad7c5038bba24d5116d84f6f2539bfc78e2a4` |
