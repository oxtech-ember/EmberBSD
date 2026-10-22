# BCM2712 native compute dispatch

The bounded native `SUBMIT_CSD` path shares GEM and engine ownership
with [TFU](bcm2712-v3d-render.md). Physical applicability belongs to
[DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/bcm2712-cm5/diagnostics.md). A finite shader check does not qualify arbitrary jobs.

## Submission contract

Use the existing serialized engine and per-submission MMU table. Accept
a bounded set of distinct, file-owned BOs under the
[shared submission-list contract](bcmv3d-submit-lists.md); copy the handle array once and retain every
reference until cache cleaning and PTE revocation complete. Map only these
objects. Unlike TFU, arbitrary shader access needs both DMA directions.
Reject unknown flags, extensions and performance objects. Binary input/output
syncobj is now covered by the subsequent [synchronization acceptance](bcm2712-v3d-sync.md).
Keep `SUPPORTS_CSD` zero until the complete Mesa synchronization contract
exists. This experimental path is synchronous and is not a Mesa driver.

Program V3D 7.1 CFG1–6 and unused CFG7 before CFG0 starts execution. Use
the core interrupt at ACPI resource index 1, GSI 281. Require a clean IRQ
boundary, exactly one CSDDONE and a cleared interrupt after acknowledgment.
Invalidate L2T and slice
caches before execution; after completion drain the TMU write combiner and
clean L2T before revoking PTEs and returning DMA ownership. Every poll and
job wait is bounded. Any uncertain failure poisons the shared TFU/CSD engine
and retains all submitted BO references until reboot.

The first test shader performs integer arithmetic and stores 16 lane
results through the TMU. It uses Mesa's V3D 7.1 QPU instruction packer;
record its source/version and the generated instruction disassembly.
Test repeated execution, changed uniforms/data, process
isolation, invalid handles/ranges and interleaving with the accepted TFU.
An empty shader alone cannot establish memory-write correctness.

Actual-source contracts cover user-array copy failure, partial lookup unwind,
duplicate and foreign BO rejection, code/uniform bounds, both DMA directions,
IRQ masking, cache order, timeouts, close while busy, poisoned retention and
TFU/CSD serialization. Causal mutants detect missing ownership and ordering checks.
The userspace tool and shader generator provide reproducible bounded inputs.

Review specifically: cache-clean failures must retain GPU-owned BOs;
duplicate handles must not corrupt reference accounting; user pointers
must never be dereferenced directly; stale core IRQs cannot prove success;
TFU and CSD must not change the shared page table concurrently.

Register facts are checked against Linux's Broadcom V3D driver and Mesa
26.2.4 `src/gallium/drivers/v3d/v3dx_draw.c` and `src/broadcom/qpu/`.
The native implementation preserves EmberBSD bus_dma, GEM and IRQ ownership.
Display/KMS, CL submission, Vulkan and desktop acceptance remain separate.

## Reproduce the checks

Run `sh ember/tools/bcm2712-v3d-csd-contract.sh` and the existing
`bcm2712-v3d-job-contract.sh`. Both compile actual driver sources. The CSD
contract checks 46 register failures and rejects nine causal mutations,
including omitted cache cleaning, a missing CFG7 write and nested IRQ masks.
Run with `CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'` and
`V3D_SKIP_MUTANTS=1` for memory/undefined-behavior instrumentation.

`bcm2712-v3d-csd-shader.h` contains the generated 15-instruction program and
its disassembly. Reproduce it on macOS from the unmodified Mesa 26.2.4
archive (SHA256 `bce5f7fbebb934373b86c999a064d52fb5065878dc57f287f95346648ec832e9`):

```sh
cc -std=c11 -D_GNU_SOURCE -DHAVE_STRUCT_TIMESPEC -DHAVE_PTHREAD \
    -Wno-initializer-overrides -Wl,-dead_strip \
    -I"$MESA/src" -I"$MESA/include" \
    ember/tools/bcm2712-v3d-csd-shader.c \
    "$MESA/src/broadcom/qpu/qpu_pack.c" \
    "$MESA/src/broadcom/qpu/qpu_instr.c" \
    "$MESA/src/broadcom/qpu/qpu_disasm.c" \
    "$MESA/src/util/ralloc.c" "$MESA/src/util/u_printf.c" \
    -o "$OUTPUT/shader-gen"
"$OUTPUT/shader-gen" > "$OUTPUT/csd-shader.h"
cmp "$OUTPUT/csd-shader.h" ember/tools/bcm2712-v3d-csd-shader.h
"$OUTPUT/shader-gen" --check
```

The warning option covers repeated upstream initializer entries; dead-strip
removes unused Mesa utility functions. No Mesa source is modified. Original
MIT licenses remain in the selected source tree. The generator additionally
requires every instruction to survive pack/unpack/repack identically.
`--check` rejects a SINGLE_SEG flag conflicting with the packed last-THRSW
sequence; this shader uses four threads and multiple segments.

Cross-compile `ember/tools/bcm2712-v3d-csd-check.c` with the same flags and
provenance checks as the TFU acceptance tool. On `EMBERV3DRENDER`, run
`./csd-check /dev/dri/renderD128`. Expected: 389 real QPU dispatches, each
writing 16 integers derived from changing uniforms and lane IDs. The test
checks every code/output/guard word, two concurrent processes,
invalid pointers, foreign file handles and CSD/TFU/CSD transitions. It also
runs the existing full TFU acceptance. A register model or a compiled test
binary does not establish that these physical checks have passed.

The register/cache reference is Raspberry Pi Linux
[`43c132e8863c3bff3647033b6a7d2bf87b15501c`](https://github.com/raspberrypi/linux/tree/43c132e8863c3bff3647033b6a7d2bf87b15501c/drivers/gpu/drm/v3d).
The read-only references have SHA256:
`v3d_regs.h`: `ab374d39a47af85b1bef41721f1b68019eb4531253e4b41cc5c94405df8037f9`;
`v3d_gem.c`: `afcb1ea86bd984eb9d2191b5a784d829caf0f5c15d24daab2a7f5a363b94af7e`;
`v3d_sched.c`: `495f13010f1d031582d26bc8566b1d5a1ae3e43a5dd3945130480cf5db7a9f7b`.
`v3d_irq.c`: `fb251b5dbc15247a4eaafb04970e3ff09bd20954c5d274312e72126fe7237064`.
Only register facts/protocol inform the native implementation; Linux's
platform, scheduler and DMA implementation is not copied into this driver.

## Completion identity

The older CSD_STATUS bits 11:4 are not a supported V3D 7.1 completion guard.
The pinned Linux driver retires CSD through its IRQ fence and dependent
TMU drain/L2T cache-clean job. Mesa's V3D 7.1 register header is not public.
Do not infer completion from the older field layout.

Use the pinned IRQ/cache protocol with exclusive ownership from reset and
serialized submission. Retain raw CSD_STATUS only for diagnosis. Keep exact
IRQ count/status, stale/latched IRQ rejection, bounded cache completion,
MMU checks, scratch integrity and PTE revocation before returning buffers.
The actual-source regression reproduces the rejected hardware snapshot,
then verifies acceptance without interpreting undocumented fields. It also
requires rejection of stale and newly latched IRQs. Any failed stage still
poisons the engine and retains all submitted buffers until reboot.

## Hardware applicability


Current hardware evidence and its applicability belong to the [DevBoards profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/bcm2712-cm5/graphics.md).
Finite QPU output, code/output guards and interleaved TFU checks do not qualify arbitrary-job recovery or endurance. No capability bit follows solely from a bounded shader check.
