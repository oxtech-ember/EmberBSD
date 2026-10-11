# BCM2712 native compute dispatch

## Design and acceptance plan

Continue the [accepted native GEM/TFU path](bcm2712-v3d-render.md) with
standard `SUBMIT_CSD`. The target is a real QPU computation with verified
output on CM5, before connecting Mesa. Execute this plan inline.

Use the existing serialized engine and per-submission MMU table. Accept
1–64 distinct, file-owned BOs; copy the handle array once and retain every
reference until cache cleaning and PTE revocation complete. Map only these
objects. Unlike TFU, arbitrary shader access needs both DMA directions.
Reject unknown flags, extensions, synchronization and performance objects.
Keep `SUPPORTS_CSD` zero until the complete Mesa synchronization contract
exists. This experimental path is synchronous and is not a Mesa driver.

Program V3D 7.1 CFG1–6 and unused CFG7 before CFG0 starts execution. Use
the core interrupt at ACPI resource index 1, GSI 281. Check exact CSDDONE,
idle state and one completion-counter increment. Invalidate L2T and slice
caches before execution; after completion drain the TMU write combiner and
clean L2T before revoking PTEs and returning DMA ownership. Every poll and
job wait is bounded. Any uncertain failure poisons the shared TFU/CSD engine
and retains all submitted BO references until reboot.

The first test shader performs integer arithmetic and stores 16 lane
results through the TMU. It uses Mesa's V3D 7.1 QPU instruction packer;
record its source/version and the generated instruction disassembly.
Test repeated execution, changed uniforms/data, counter wrap, process
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
