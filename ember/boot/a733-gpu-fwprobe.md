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
The initial LOGTYPE and MMUCACHE probes require the trace log type to remain zero.
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
The later-command tests execute slots zero through four with the real helper.
They enable trace mode before LOGTYPE in slot three, then submit KICK in slot four.
They reject unknown log bits, unexpected mode changes, and the existing runtime faults.

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

## Later diagnostic commands and tracing

`sun60i_a733_gpu_fwprobe_cmd()` takes an explicit expected log type after the slot argument.
Every runtime observation must match that exact value.
Zero keeps tracing disabled. A nonzero value requires `TRACE` and at least one valid log group.
Only `LOGTYPE_UPDATE` can change the predecessor's mode.
The caller writes and synchronizes the shared setting before it sends that command.
Other commands retain the mode from the successful predecessor.

Stage 21 selects `0x3f`: `TRACE`, `MAIN`, `MTS`, `CLEANUP`, `CSW`, and `BIF`.
It sends LOGTYPE in slot three, then KICK in slot four with the same expected setting.
Stages 16 and 17 keep their strict zero-log requirement.
Tracing does not bypass compatibility, queue, power, fault, or completion checks.
An unexpected firmware response still fails without consuming the response.

## MMUCACHE continuation

`sun60i_a733_gpu_fwprobe_mmucache()` accepts the unchanged successful LOGTYPE result
and a separate persistent result. Both results belong to the same memory owner
and firmware boot. The common publisher, observer, validation, and deadline
serve both entry points. Neither entry point is a general queue API.

The continuation requires current KCCB read and write offsets of one.
It submits one `MMUCACHE` command in slot one, then publishes write offset two.
Its return word must become exactly one, with read and write offsets two.
The first command and its return word remain unchanged.

The 88-byte command has opcode `0x2abc0066` and zero command flags.
Its payload contains these exact pinned upstream values:

- `cache_flags = 0x0400001f`: PC, PD, PT, both TLB flags, and interrupt request.
- `mmu_cache_sync_fw_addr`: the validated existing `MMUCACHE_SYNC` object's address.
- `mmu_cache_sync_update_value = 0`.

`CTX_ALL` is not added. The sync word is observed but never written by the host.
Zero in this word does not prove completion. The pinned `pvr_mmu_flush_exec()`
waits for the KCCB return slot even on its first attempt with `wait = false`.
The native continuation polls the same completion evidence. It does not retry
or adopt the upstream reset path on failure.

Cached `command`, `slot`, `cache_flags`, `sync_fw_addr`, and `sync_update_value`
distinguish both operations. Each snapshot also records the current `mmucache_sync`.
The coherent mapping, lifetime, publication, and fault rules above still apply.
The only host writes are command one, return word one, and KCCB `write_offset`.
No GPU page table, firmware memory context, or job is created or modified.
A successful cache command would prove this firmware operation completed.
It would not prove a GPU page walk, GPU read/write job, or graphics acceleration.

The contract runs the real LOGTYPE operation before testing the continuation.
It checks exact command bytes, the second slot, retained first-command bytes,
invalid or aliased previous results, stale completion, and foreign shared fields.
It distinguishes KCCB completion from the unchanged zero sync value.
It also covers queue regression, late completion, MMIO faults, and timeout retention.
The physical acceptance below confirms both commands on Zero 3W.

## Physical command result

Orange Pi Zero 3W completed LOGTYPE and MMUCACHE on 2026-10-12.
The source was `f1b4b39a84c3d926c03445cc3e76fd2ebee85392`.
The kernel was GCC 16.2 `EMBER64 #9`, with its four matching modules.
Stages 1 through 16 passed at 13:57:09–13:57:10 UTC.
Stage 17 completed at 13:58:59 UTC, within the recorded second.

| Observation | LOGTYPE | MMUCACHE |
| --- | --- | --- |
| Opcode | `0x2abc00ce` | `0x2abc0066` |
| Slot | 0 | 1 |
| Read/write offsets | `0/0` to `1/1` | `1/1` to `2/2` |
| Return word | 1 | 1 |
| Executed counter | 0 to 1 | 1 to 2 |
| Observations | 3 | 3 |

Both results reported error zero and all four attempt/publication/kick flags.
MMUCACHE used flags `0x0400001f`, sync address `0xc0104400`, and update value zero.
The sync word remained zero; completion was established by the return slot and queue offsets.
The cached LOGTYPE result remained intact after stage 17.
FWCCB stayed empty. Power state was `3` (idle), HWR state was `1`, and firmware faults were zero.
Firmware remained started with matching BVNC and DDK build `6976702`.
A 16-second hardware watchdog protected each stage. It was disarmed without a reset.
SSH remained available. The existing NPU stage-4 check stayed successful.
All firmware allocations remain retained until reboot.

| Component | SHA256 |
| --- | --- |
| ELF kernel | `833a795643215a41d8476e2896e68f08876f8fa57354fa96a7d20443b31e77f7` |
| Native image | `8c7e16238c810f94291d82ae837e871125fc6df57b455a0f98fbaad5c6b30a97` |
| Preserved DTB | `6e3e92e251a617066a54634394e65d3463df96fdd5920753056cb0c2e4e78d4e` |

The pinned firmware was unchanged.
This record establishes two firmware commands. No Rogue GPU translation or graphics task was submitted.
GPU read/write completion, IRQ handling, DRM, HDMI presentation, and accelerated Plasma remain unverified.
