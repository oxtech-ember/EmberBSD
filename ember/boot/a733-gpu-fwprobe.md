# A733 PowerVR first KCCB command

The [native probe](../../sys/arch/arm/sunxi/sun60i_a733_gpu_fwprobe.c)
submits one `LOGTYPE_UPDATE` command after a successful firmware boot.
It checks the return word and the KCCB read offset.
A successful probe proves one firmware command completed. It does not establish graphics,
DRM, interrupts, recovery, or accelerated Plasma.

## Preconditions

Call `sun60i_a733_gpu_fwprobe()` with a register tag and handle,
the prepared memory owner, its successful bootstrap result, and a persistent
[probe result](../../sys/arch/arm/sunxi/sun60i_a733_gpu_fwprobe.h).
Use the unchanged outputs of [fwmem](a733-gpu-fwmem.md),
[fwinit](a733-gpu-fwinit.md), and [fwboot](a733-gpu-fwboot.md).

The caller must hold an adaptive mutex at `IPL_NONE` throughout the call.
It must retain power and clocks and map the full 64 KiB register bank.
It must exclude queue users, interrupt handlers, reset, suspend, and reclamation.
The probe does not establish these hardware prerequisites itself.

The work and config buffers must have coherent CPU mappings before bootstrap.
The probe requires each area's recorded `coherent` flag.
It also checks native `_BUS_DMAMAP_COHERENT` and rejects `_BUS_DMAMAP_IS_BOUNCING`.
It never changes a live mapping or its cache attributes.
All five loaded DMA spans and all 19 FWIF object regions are validated.
The regions must match the fixed native initializer layout and ABI sizes.

For noncoherent ARM DMA ranges, `BUS_DMA_COHERENT` creates an uncached CPU mapping.
AArch64 uses the Normal-NC memory attribute for this mapping.
Native DMA sync operations still supply ordering barriers.
Do not substitute `volatile` or whole-buffer writeback for this contract.
The 16-byte connection structure and KCCB return words share cache lines
between CPU and firmware writers. A stale writeback can corrupt another writer.
Bounce copies also violate this shared-memory requirement.

## Runtime validation

Structural failures leave the result unchanged and perform no I/O.
The probe then sets `attempted` before its first runtime observation.
A repeated call returns `EBUSY` without changing the result.
Keep the result for the owner's entire lifetime. Do not clear it to retry.

The probe requires the current raw `firmware_started` byte to equal one.
It waits for raw `compchecks.updated` to become one.
Another nonzero representation is a protocol error.
A second config `POSTREAD` follows the publication-flag load before payload reads.
The native DMA acquire barrier prevents stale BVNC or layout observations.
The two layout versions must equal `ROGUE_FWIF_COMPCHECKS_LAYOUT_VERSION`.
The hardware and firmware BVNC records must equal the prepared A733 profile,
`36.56.104.183`. This is a strict diagnostic policy for this one profile.
The processor version, DDK version, build, options, and OS count are cached.
No guessed DDK value or container-version comparison is used.

KCCB must be in its initial state: read offset zero, write offset zero,
wrap mask 127, and command size 88 bytes.
FWCCB must have valid offsets, wrap mask 31, its native command size,
and no pending command. The probe observes FWCCB but never consumes it.
The trace log type must remain zero.
Firmware fault and restart indications cause failure.
The power state must be `ROGUE_FWIF_POW_ON` or `ROGUE_FWIF_POW_IDLE`.
The probe never writes a firmware power state or connection token.

Compchecks readiness and command completion share one monotonic one-second
interval. There are at most 10,001 observations, with delays at most 100 us.
The probe checks the remaining budget before publication.
A late compatibility update cannot trigger a command after that budget expires.
The observation cap can end the wait before the time deadline.
Scheduling, cache operations, and an interconnect stall can extend return time.

## Publication and completion

The probe writes only these host-owned regions:

1. KCCB command slot zero: 88 zero-initialized bytes with opcode `0x2abc00ce`.
2. KCCB return word zero: clear it before publishing the command.
3. KCCB control `write_offset`: set it to one after command and return syncs.

The command has zero flags and zero payload.
`LOGTYPE_UPDATE` asks firmware to refresh its cached log setting.
The existing shared log type remains zero, so the probe does not enable tracing.

Range-specific native DMA syncs order these stores.
The result's `published` flag is set before the write-offset store.
Firmware can observe that store before a later MMIO kick.
The probe then sets `kick_attempted`, applies an ordering barrier,
and writes 32-bit zero to `MTS_SCHEDULE` at offset `0x0b00`.
Zero selects the upstream GP data master.
A successful register write and following barrier set `kicked`.
These flags do not claim firmware execution.

The probe accepts only return word one and read offset one,
with write offset still one. Return bits other than `CMD_EXECUTED` fail.
Read-offset progress alone does not prove completion.
A stale return word cannot satisfy the probe because publication clears it.
The executed-command counter is cached, including natural 32-bit wraparound.

The completion loop observes shared buffers through read-only
`POSTREAD`/`PREREAD` cycles. It performs no further shared-memory writes.
It does not require an interrupt handler or clear pending IRQs.
An unexpected FWCCB command is cached and causes failure without acknowledgement.
The result records non-atomic snapshots before publication and at the last poll.

Success, error, timeout, and a faulting kick all retain the bootstrap's DMA ownership.
The probe never frees, unmaps, resets, reinitializes, or reuses firmware memory.
There is no retry or recovery protocol in this helper.

## Software checks

Run the [production-source contract](../tools/a733-gpu-fwprobe-contract.sh):

```sh
FWPROBE_TEST_CFLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all -g' \
    sh ember/tools/a733-gpu-fwprobe-contract.sh
```

The contract compiles the real probe bodies and links the real FWIF initializer.
Its independent oracle uses pinned ABI offsets and command bytes.
It models shared coherent memory and firmware updates between CPU operations.
It checks every byte outside the three allowed write ranges.
It also checks publication barriers, a stale return word, MMIO faults before
and after possible execution, delayed compatibility, deadlines, malformed layouts,
coherence and bounce rejection, foreign fields, and one-shot behavior.

These checks do not perform hardware MMIO, native cache maintenance, or firmware execution.
Target compilation and static analysis cover the native kernel source separately.
Physical command acceptance requires a separate board record.
ASan/UBSan is used where Valgrind Memcheck does not support the host platform.
That run is not a Valgrind result.

## Sources and license

The command path follows Linux PowerVR revision
[`7b63ef2d55f24519e7e9e5f4d15dbea03f126e40`](https://github.com/torvalds/linux/tree/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination).
`pvr_fw_trace.c:update_logtype()` sends this command and waits for its return slot.
`pvr_ccb.c:pvr_kccb_send_cmd_reserved_powered()` defines publication order.
`pvr_fw.c:pvr_fw_mts_schedule()` defines the GP kick and barriers.
`pvr_kccb_wait_for_completion()` tests `CMD_EXECUTED` in shared memory.
The native boot path does not require host writes to connection states or tokens.

The source retains the MIT license option and upstream attribution.
The native helper and regression contract are AI-assisted EmberBSD work.
