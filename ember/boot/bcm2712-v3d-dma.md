# BCM2712 V3D first DMA/TFU experiment

Status on 2026-10-10: implemented in the opt-in `EMBERV3DDMA`
configuration and **physically accepted once** on the stand CM5:
the translated copy passed (see the result below). This realizes the reviewed
[handoff design](bcm2712-v3d-dma-plan.md) after a completed
[takeover](bcm2712-v3d-takeover.md). It is the first GPU DMA and
command execution on EmberBSD CM5; it is not DRM, not a driver for
applications, and not a demonstration of rendering.

## Scope

`BCM2712_V3D_DMA_PROBE` extends the takeover-only build. After the
takeover completes and retains the PM claim and the HUB/CORE/SMS
mappings, one bounded experiment runs from the same autoconfiguration
finalizer:

1. Decode `MMU_DEBUG` and require at least 32-bit PA and VA widths;
   the experiment uses the 32-bit ACPI DMA window of GPU0 only.
2. Require the takeover's HUB interrupt masks to still be in force.
3. Allocate six objects through `bus_dmamem_alloc` on GPU0's own
   `aa_dmat`: a 4 MiB page table, a 4 KiB illegal-address scratch, and
   five-page actual and alias source and destination images
   (16 KiB of image data plus one canary/read-ahead page each). Every
   load must be one segment, full length, 4 KiB aligned, inside the
   32-bit window, with no pairwise overlap. The allocations are
   mapped cacheable; cache coherency is explicit `bus_dmamap_sync`
   operations, not a coherent mapping.
4. Fill deterministic word patterns from definitions held in the
   driver, never from DMA memory that the GPU could modify. The
   fifth source page exists for the 64-byte read-ahead allowance
   Mesa documents; the extra destination page is a canary.
5. Publish the MMU like the pinned Linux `v3d_mmu.c`: PT base, control
   `0x060d0c01` (enable plus abort/interrupt on PT-invalid,
   write-violation and capability-exceeded), illegal-address scratch,
   MMUC writes `ENABLE=1` and `FLUSH|ENABLE=3`, then a TLB clear
   written as a full value so read-modify-write can never reflect
   W1C fault bits. Each clearing wait is bounded to 100 ms. No
   BYPASS, ADDR_CAP, SMS or power register is touched.
6. Submit one 64x64 generic-R32F raster copy through the TFU in the
   pinned `v3d_sched.c` order with ICFG (`0x001d0001`) written last.
   The input and output addresses are the alias virtual addresses;
   their PTEs point at the separate actual source and destination
   pages. Source PTEs are read-only, destination PTEs writable, no
   big or super pages, and the page table and scratch are not mapped
   as ordinary GPU data.
7. Watch raw TFU status with interrupts masked for at most 500 ms.
   Completion is the converted-jobs counter moving exactly once
   modulo 256 with the busy bit clear; a pending MMU fault wins over
   simultaneous completion.
8. Synchronize with POSTREAD|POSTWRITE (POSTWRITE alone is a no-op in
   this backend) and verify: the actual destination holds the actual
   source pattern, every protected image keeps its defined pattern,
   and the scratch stays zero.

Translation PASS requires exactly that outcome. Identity addressing
(the alias source copied to the alias destination), mixed results,
faults, timeouts and mismatches are distinct failures. After the
first publishing MMU write every allocation, mapping and the takeover
claim are retained until reboot, on success and on every failure;
only preparation failures before publication release DMA memory.

## Bounds

The experiment constrains one translated copy through addresses this
probe owns. It does not establish guard-page isolation, invalid-PTE
fault recovery, IRQ delivery, exclusion of firmware work, cache
correctness beyond the synced ranges, Mesa operation or any render
interface. `EMBERV3DDMA` is an experiment kernel: it is not the
ordinary CM5 kernel and must not become one until a real DRM driver
owns this hardware.

## Checks

Run the actual-source host contract:

```sh
sh ember/tools/bcm2712-v3d-dma-contract.sh "$PWD"
CFLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all' \
    sh ember/tools/bcm2712-v3d-dma-contract.sh "$PWD"
```

The harness substitutes host services, executes the production probe
body, and models noncoherent memory as separate CPU and device views.
It covers allocation/load/sync failures, rejected layouts (split,
short, misaligned, above 4 GiB, overlapping), unsuitable MMU
geometry, lost interrupt masks, stuck MMUC/TLB waits, MMIO faults
before and after publication, PTE permissions, the pinned submission
order, counter wrap, fault-before-completion priority, identity and
mixed outcomes, protected-image and scratch modification, one-shot
behavior and retention. Eight causal mutants (including POSTWRITE
without POSTREAD and release-after-publication) are rejected.

On 2026-10-10 the contract passed 35 cases and 209 checks on macOS
with ASan/UBSan. These tests substitute hardware services; they do
not access physical V3D.

## Physical CM5 result

The complete `EMBERV3DDMA` kernel from clean commit `5d7278c3bab4`
(Kernel SHA256 `93f0741ec93247e020e3c17172463c87898d06d191550f90176215f5473d745e`)
booted on physical Compute Module 5 Rev 1.0, BCM2712 D0, 4 GiB, on
2026-10-10. The observer identified V3D 7.1, the reset takeover
completed a second time with identical identification fields, and the
DMA experiment printed:

```text
MMU_DEBUG=0x20804664 PA36 VA36; using the 32-bit DMA window only
translated DMA PASS: TFU copied the actual source pattern through alias addresses; CVTCT 0->1; allocations retained until reboot
```

This establishes the first GPU DMA and command execution on EmberBSD:
one TFU raster copy translated through the published page table with
both sources, the alias destination, canary pages and the scratch
verified unchanged. It does not establish interrupts, fault recovery,
bin/render queues, DRM, Mesa or any application path. After recording,
the board was returned to the ordinary `EMBER64` kernel; the passive
observer and SSH were rechecked. The source boot log SHA256 is
`27e9685bd409a4985f9444a00937d8a9423b96a550283e94108b1e6fc224a99e`.

## Physical acceptance guide

1. Confirm the board identity: Compute Module 5 on the stand recorded
   in the wiki, and the ordinary `EMBER64` kernel booting with the
   passive observer lines (clock state 1, nonzero rate, `V3D 7.1`).
2. Verify the recovery bundle from the takeover acceptance still
   matches its recorded SHA256 before installing anything.
3. Build the complete `EMBERV3DDMA` kernel from a clean commit with
   the cross-build guide; keep the artifacts, contracts log and
   provenance verifications.
4. Install the experimental kernel next to the ordinary one without
   removing the recovery path, then boot it once by explicit boot.cfg
   selection. The expected new lines after `reset takeover complete`
   are `MMU_DEBUG=... PA.. VA..`, then either
   `translated DMA PASS ... CVTCT x->y` or
   `DMA experiment stopped at <stage>: error <n>; <verdict>`.
5. Record the outcome: board, firmware note, kernel revision and
   SHA256, the complete new log lines, uptime, and the SHA256 of the
   saved log. Any retained allocation failure keeps its state until
   reboot; power-cycle after recording.
6. Return the board to the ordinary kernel and verify SSH and the
   observer lines again before any other work.

Primary references: the [reviewed design](bcm2712-v3d-dma-plan.md);
Raspberry Pi Linux `43c132e8863c3bff3647033b6a7d2bf87b15501c`
(`v3d_mmu.c`, `v3d_sched.c`, `v3d_regs.h`); Mesa 26.2.4
`src/gallium/drivers/v3d/v3dx_tfu.c`; this fork's `sys/arch/arm/
broadcom/bcm2712_v3d_dma.c` and `ember/tools/bcm2712-v3d-dma-contract.c`.
