# BCM2712 native compute dispatch

Status: the opt-in native CSD path passed physical CM5 acceptance on
2026-10-11: 389 QPU dispatches with verified memory writes and 420 TFU copies
in the same run. This is not yet a Mesa, Vulkan or desktop acceptance.

## Design and acceptance plan

Continue the [accepted native GEM/TFU path](bcm2712-v3d-render.md) with
standard `SUBMIT_CSD`. The target is a real QPU computation with verified
output on CM5, before connecting Mesa. Execute this plan inline.

Use the existing serialized engine and per-submission MMU table. Accept
1–64 distinct, file-owned BOs; copy the handle array once and retain every
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

1. Extend the actual-source job contract: user-array copy failure, partial
   lookup unwind, duplicate/foreign BO rejection, code/uniform address
   bounds, both DMA directions, core IRQ masking, cache order and timeout,
   close while busy, poisoned retention, and TFU/CSD serialization.
   Observe failure before implementation; keep causal mutation tests.
2. Add `bcmv3d_submit_csd`, ioctl routing and a bounded CSD executor in the
   existing native driver. Reuse MMU ownership and the counted IRQ-mask
   discipline. Keep the existing TFU regression suite passing.
3. Add a reproducible userspace C acceptance tool and shader generator.
   Cross-build with the selected GCC16 and recorded EmberBSD ABI inputs.
   Review the complete change before installing the test kernel.
4. Run on CM5 with the recorded ordinary kernel available for recovery.
   Capture source identity, hashes, output, duration and kernel diagnostics.
   Restore the ordinary kernel after the trial. Update README and the
   knowledge base only to the observed level of support.

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

## Correct the retirement assumption

The first CM5 run received exactly one CSDDONE with no hub interrupt, but
rejected CSD_STATUS `0x10002080` after a zero initial value. Linux's older
bits 11:4 decode as eight, not one. The pinned Linux driver does not use
that field to retire CSD: `v3d_irq.c` signals the CSD fence, and the dependent
cache-clean job drains TMU writes and cleans L2T. Mesa's simulator uses a
separate, non-public V3D 7.1 register header. The public older field layout
is therefore not a supported completion guard for this device.

Use the pinned IRQ/cache protocol with exclusive ownership from reset and
serialized submission. Retain raw CSD_STATUS only for diagnosis. Keep exact
IRQ count/status, stale/latched IRQ rejection, bounded cache completion,
MMU checks, scratch integrity and PTE revocation before returning buffers.
The actual-source regression reproduces the rejected hardware snapshot,
then verifies acceptance without interpreting undocumented fields. It also
requires rejection of stale and newly latched IRQs. Any failed stage still
poisons the engine and retains all submitted buffers until reboot.

## Physical acceptance, 2026-10-11

CM5 Rev 1.0, BCM2712 D0, 4 GiB RAM and 64 GB eMMC, on Waveshare CM5-NANO-B.
The existing UEFI was preserved; its source and carrier PCB revisions are
unknown. GCC 16.2.0 cross-built the kernel and static C test on macOS.

At 12:03:15 UTC the check exited 0 in 0.34 seconds. All 389 QPU dispatches
wrote the expected 16 lane values: 6,224 GPU-computed integers. The test
varied uniforms and destination offsets, checked every code/output/guard
word, rejected cross-file handles and ran two concurrent CSD clients.
The interleaved TFU test completed 388 normal/concurrent copies and 32
submit/close copies. Its other 32 close races returned the expected ENOENT;
32 busy observations confirmed submissions in progress. The final CSD run
also passed. No CSD failure diagnostic appeared in the captured kernel log.
This short run is neither a throughput benchmark nor a long-duration test.

| Artifact | SHA256 |
| --- | --- |
| `netbsd-EMBERV3DRENDER` | `d920a8b28c1c65fbcebb36705d73e007cf8eac8c4f980273145a76bbdf16041e` |
| AArch64 Image | `314164955c3355e06536d7de05a5aebc26a92a8fccfd6baf1a7540bd2930cf9f` |
| Check from `a0f5086328ee109bfc445a9a87552d71c4ee4063` | `7b05c7014e919a6f291d2340cae5c2880d4cbf6c7c53b40aa05917abc001dfff` |
| Full successful kernel/test log | `f90d5f48abb47867aaad10842a49820b2cdeed691ed3b4f09478aea9b1449a3f` |
| First rejected kernel/test log | `c560bc2bfdbd128c6d28a93c1ca50cc97c3525a098256e5313a9ac4dd3132357` |

ELF/Image, debuglink, CTF, source/compiler provenance, module symbols and
the kernel builder's host contracts passed. All four board modules remain
byte-identical to the accepted ordinary/TFU bundle. The test's 74 used
base/compiler headers and five library/CRT files match the previously
recorded device ABI inputs. This does not establish complete SDK provenance.
The CSD contract passed 38,502 assertions, 46 injected register failures,
nine causal mutants and ASan/UBSan; the unchanged TFU path passed 28,545
assertions, 37 injected failures and eleven mutants. Independent review
covered ownership, IRQ retirement and the generated shader's thread flags.

The exact kernel source tree is `f7d2ab4b2319d797c88be6b8e50c1adb3a37f529`.
Starting from the [accepted TFU tree](bcm2712-v3d-render.md#source-reconstruction),
apply public commits `573566699a3`, `45a9d5a4a96`, `ee54216da58`, then
`a0f5086328e`. An independent Git index reproduced the complete tree.
Build metadata records local commit
`c415ee4495ed8c815df7eae8af13186872bb5477`; it is not a separate public branch.

The ordinary kernel is restored after the experiment. Binary syncobj/fences
passed a [subsequent physical check](bcm2712-v3d-sync.md). General CL submission,
timelines, PRIME, Mesa/Vulkan execution, display/KMS, arbitrary-job
recovery and an accelerated desktop remain separate steps. No capability
bit was enabled merely because this bounded shader check passed.
