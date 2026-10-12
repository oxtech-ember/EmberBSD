# A733 PowerVR firmware bootstrap

The [bootstrap helper](../../sys/arch/arm/sunxi/sun60i_a733_gpu_fwboot.c)
hands prepared native DMA buffers to the GPU. It then observes the firmware
boot flag for a bounded interval. The helper does not submit GPU commands.
A successful firmware boot does not establish rendering or DRM support.

## Inputs and lifetime

Call `sun60i_a733_gpu_fwboot()` with the register tag, register handle,
prepared memory owner, and persistent result. See the
[public contract](../../sys/arch/arm/sunxi/sun60i_a733_gpu_fwboot.h).
The memory owner must contain the unchanged successful outputs of
[`fwmem_prepare` and `fwmem_interface`](a733-gpu-fwmem.md).
The interface must use the [native FWIF initializer](a733-gpu-fwinit.md).

The caller must hold an adaptive mutex at `IPL_NONE` throughout the call.
The caller must check fresh hardware readiness before this call.
This includes power, clocks, the strict PCK handshake, and the expected GPU ID.
The register handle must cover the full 64 KiB register bank.
No other caller, interrupt handler, or device may access these DMA buffers.

Zero the result once. Keep it with the memory owner for its entire lifetime.
Do not clear or replace the result to retry a bootstrap attempt.
The helper sets `attempted` and `device_owned` before the first DMA sync.
A repeated attempt returns `EBUSY` without changing the result or performing I/O.
A preflight failure leaves the result and buffers unchanged.

The preflight checks all five loaded DMA maps, their single segments,
page alignment, 36-bit physical spans, and pairwise disjoint ranges.
It rejects the reserved physical wrapper remap window.
It checks the three config objects against their native FWIF sizes,
page offsets, and firmware virtual addresses.
It also checks the code, data, table, work, and config allocation sizes.
It does not rebuild the page table or reparse the prepared firmware image.

## DMA handoff

The helper uses these initial full-buffer sync operations:

| Buffer | Operation |
| --- | --- |
| Code and page table | `BUS_DMASYNC_PREWRITE` |
| Data, work, and config | `BUS_DMASYNC_PREWRITE \| BUS_DMASYNC_PREREAD` |

All five sync operations finish before
[`sun60i_a733_gpu_fwstart()`](a733-gpu-fwstart.md) performs MMIO.
The helper never frees or unmaps DMA memory.
It never writes firmware buffers after handoff.

If `fwstart` fails without any possible hardware access, the helper pairs
the initial operations with POST syncs and returns CPU ownership.
The result still records an attempt, so a retry remains forbidden.
If `fwstart` might have touched hardware, all buffers remain device-owned.
This includes a fault during reset release, even when `released` is false.
An inconsistent leaf-helper result also fails closed.

Success, timeout, and an invalid firmware flag all retain device ownership.
While `device_owned` is true, the caller must keep every DMA allocation,
map, and backing page. It must not call the memory destructor or write the buffers.
A cached error does not prove that the GPU stopped accessing memory.
A separate verified device-stop protocol is required before reclamation.
This helper does not implement that protocol.

## Observation

The helper observes the config allocation through complete
`POSTREAD`, read-only observation, and `PREREAD` cycles.
This includes the final successful or failing observation.
Native DMA sync operations handle cache visibility and bounce buffers.
The helper does not rely on cache coherence or a `volatile` read alone.

The firmware boot flag is read as a raw volatile byte.
Zero means pending. One means the firmware reported that it started.
Any other value returns `EPROTO` without releasing DMA ownership.
The helper does not interpret an untrusted DMA byte through a C `bool` lvalue.

The wait uses a five-second monotonic deadline, with at most 5,001 observations.
Busy delays are at most 1,000 microseconds each and respect the remaining interval.
The observation limit also bounds execution if the test clock does not advance.
Scheduling and individual cache operations can extend the actual return time.
The helper does not print to the console during the wait.

The result caches the boot byte, timestamp, connection states, alive tokens,
BVNC records, layout versions, processor version, and DDK compatibility fields.
It also records the MMIO start result, observation count, error, and ownership.
These fields form a non-atomic diagnostic snapshot.
They do not establish compatibility acceptance or a live connection handshake.
Reading the cached result later performs no DMA or MMIO.

## Verification

Run the [real-source contract](../tools/a733-gpu-fwboot-contract.sh):

```sh
FWBOOT_TEST_CFLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all -g' \
    sh ember/tools/a733-gpu-fwboot-contract.sh
```

The contract includes the production bootstrap function bodies and links the
real FWIF initializer. It replaces only native DMA, time, and the MMIO leaf helper.
Separate CPU and device byte arrays model noncoherent visibility.
Device writes become CPU-visible only through `POSTREAD`.
The contract checks all five initial syncs before MMIO, retained ownership,
paired syncs after an untouched failure, and unchanged preflight outputs.
It also checks raw invalid flag bytes, 64-bit snapshots, deadline boundaries,
the observation cap, malformed layouts, and allocation lifetime.

The host contract does not execute physical DMA, GPU instructions, or MMIO.
Native target compilation and static analysis check a different boundary.
Physical firmware execution requires a separate board acceptance record.
ASan/UBSan is used on hosts where Valgrind Memcheck is unavailable.
Do not describe that run as a Valgrind result.

On 2026-10-12, the Apple Clang 21 ASan/UBSan contract passed 152234 checks.
Five causal mutations failed: missing POSTREAD, premature ownership return,
late attempt guard, invalid DMA bool access, and truncated 64-bit BVNC.
GCC 16.2 compiled the native AArch64 source and completed path analysis.
Clang Static Analyzer reported no diagnostics in the target or host fixture.
Cppcheck 2.22 covered 121 of 189 checkers with no confirmed defect.
Remaining messages concern KNF prototype names, variable scope, unused
standalone functions, and inherited bus-header const suggestions.
The host's full preprocessed FWIF initializer reaches an unsupported Darwin
`errno_t` parse path. The project-only preprocessed analysis retains that limitation.
No diagnostic categories were suppressed. Valgrind does not support this macOS/arm64 host.

The integrated consumer passed 6548 ASan/UBSan checks, including 1111 for
interface preparation and 2953 for bootstrap. All twelve consumer mutations failed.
GCC target compile and path analysis were clean. Clang retained four old
`bitops.h` dead-store findings. Cppcheck added two false `%zu` reports for
`size_t` work/config sizes; no new confirmed consumer defect was found.

## Sources and license


The five-second boot wait follows the Linux PowerVR driver at
[`7b63ef2d55f24519e7e9e5f4d15dbea03f126e40`](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_fw.c).
Native FWIF types retain the upstream layout assertions.
The DMA handoff uses the EmberBSD native `bus_dma` contract and ARM sync semantics.
The Linux wait does not provide the native cache ownership implementation.

The helper retains the MIT license option and upstream attribution.
The native adaptation and regression contract are AI-assisted EmberBSD work.
