# BCM2712 first DMA/TFU experiment: handoff design

Status on 2026-10-10: **implemented** in the opt-in `EMBERV3DDMA`
configuration with actual-source contracts; see
[the experiment page](bcm2712-v3d-dma.md) for the implementation and
the physical acceptance guide. Physical acceptance is pending.
The prerequisite [native reset probe](bcm2712-v3d-takeover.md#physical-cm5-acceptance)
passed once on physical CM5. That result establishes neither DMA nor rendering.
This design incorporates the review corrections below and supersedes earlier
scratch notes that suggested preserving an unknown MMUC bit or using
POSTWRITE alone to check unchanged source memory.

## Scope and prerequisites

Use a separate opt-in `BCM2712_V3D_DMA_PROBE` and proposed `EMBERV3DDMA`
configuration, after successful takeover. Neither exists yet. Reuse the
claimed HUB/CORE mappings and retain masked interrupts. Do not add DRM,
user-submitted commands, an IRQ handler, frequency changes or reset retries.

Pass GPU0's own ACPI `aa_dmat` to the probe. GPU0 lies directly under `\_SB`;
the mailbox/SOCB DMA alias is not its DMA window. `_CCA=0` requires explicit
cache synchronization. The first experiment uses the 32-bit DMA tag.
Observed MMU_DEBUG=0x20804664 decodes as PA36/VA36 in the pinned Linux code;
that does not authorize 40-bit DMA. A 4 MiB table covers the used 32-bit VA
subset, not the complete advertised VA36 space.

Before hardware writes, review the actual ACPI DMA tag and native
`bus_dma` implementation again. Allocate six distinct objects:

| Object | Size |
| --- | --- |
| Page table | 4 MiB |
| Illegal-address scratch | 4 KiB |
| Actual source and destination | 20 KiB each |
| Alias source and destination | 20 KiB each |

Use `bus_dmamem_alloc`, map, `bus_dmamap_create` and load for each object.
Hardware addresses come only from loaded `dm_segs`, never the CPU address.
Require one segment, 4 KiB alignment, complete length, no overflow or pairwise
overlap, and an end address at or below 2^32 for every object.
Use cached mappings with truthful noncoherent synchronization.

## Unresolved BYPASS semantics

The physical reset inventory has BYPASS_START=0 and BYPASS_END=0xfff.
The pinned Linux source defines offsets but does not program these registers
or establish their units and activation conditions. Do not invent a disable
write or claim that invalid PTEs already provide isolation.

For a bounded translation experiment, choose TFU virtual addresses equal to
the DMA addresses of the two owned alias buffers. Their PTEs point at the
separate actual source and destination. Both the tested identity path and
the intended translated path then address memory owned by this probe.
This constrains those two mechanisms; it does not prove general DMA safety
against undocumented behavior or future firmware work.

Initialize all 1,048,576 little-endian PTEs to zero. Populate five pages for
each used source/destination range: source VALID, destination
VALID|WRITEABLE, with PFNs from actual buffer bus addresses. Do not map the
page table or scratch as ordinary GPU data. Do not use big/superpages.

Fill actual and alias sources with different deterministic patterns. Give
both destinations different canaries. Each source's fifth page includes the
64-byte read-ahead allowance required by Mesa's V3D device information.
Compare results against the originally defined pattern, not a source that
the GPU might have modified. The extra destination page is a canary.

## Publication, synchronization and execution

Before device access, sync all four data buffers and scratch with
PREWRITE|PREREAD. After confirmed completion use POSTREAD|POSTWRITE before
checking their contents. **POSTWRITE alone is a no-op in this backend and
can conceal an unexpected GPU write in the CPU cache.** The page table uses
PREWRITE/POSTWRITE unless unchanged-table verification is also claimed.

Mark every allocation permanently exposed before the first publishing MMU
write. From that point, retain every allocation, mapping and owner until
reboot on success, timeout, error, mismatch or questionable readback.
Only pre-publication preparation failures may release DMA allocations.
Do not disable/reset the GPU merely to justify freeing memory.

Follow the pinned MMU setup with owned scratch and page-table PFNs and
MMU control 0x060d0c01. Use only the documented MMUC writes ENABLE=1 and
FLUSH|ENABLE=3. **Do not copy the observed, undefined MMUC bit31 into writes.**
Validate defined readback fields, faults, address fields and interrupt masks.
Bound each cache/TLB clearing wait to 100 ms. Do not blindly reflect W1C
fault bits through read-modify-write, or program BYPASS/ADDR_CAP.

Submit one 64x64, 32-bit raster-to-raster copy. Mesa's exact-copy path uses
generic R32F format 29 for this texel width. For V7.1 HUB registers:

- IIA/IOA use the two alias virtual addresses; IIS=64.
- IOC=0x00400000; IOS=0x00400040; ICA, IUA and COEF0..3 are zero.
- Write ICFG=0x001d0001 **last**, following Linux's TFUC request.
- Do not add an SU write; the pinned submission path does not require one.

Keep IRQs masked and observe raw TFUC. Completion needs CVTCT to increase
exactly once modulo 256, BUSY clear, expected TFUC and no faults, within
500 ms. A fault wins over simultaneous completion. No retry is allowed.
After synchronization, check all 16 KiB of output, both source buffers,
all padding/canaries, the other destination and scratch.

Translation PASS means actual-destination contains the actual-source pattern
and alias-destination and other protected test contents remain unchanged.
Identity or mixed results are separate outcomes, never translation PASS.
Guard-page isolation, invalid-PTE fault recovery, IRQ delivery, firmware
exclusion and Mesa remain outside this first experiment.

## Required implementation and review

Add the separate configuration/option, DMA-tag plumbing, bounded native probe,
actual-source fault/ordering contracts and a physical acceptance guide.
The contracts must cover all allocation/load/sync/MMIO failures, translated
DMA addresses, range overlap/overflow, PTE permissions, counter wrap,
fault-before-success priority and retention after any publishing write.
Require independent review and a complete clean-commit kernel/module build
before installing. Preserve and verify the ordinary CM5 recovery bundle.

Primary references: Raspberry Pi Linux
[`43c132e8863c3bff3647033b6a7d2bf87b15501c`](https://github.com/raspberrypi/linux/tree/43c132e8863c3bff3647033b6a7d2bf87b15501c),
especially [`v3d_mmu.c`](https://github.com/raspberrypi/linux/blob/43c132e8863c3bff3647033b6a7d2bf87b15501c/drivers/gpu/drm/v3d/v3d_mmu.c),
[`v3d_sched.c`](https://github.com/raspberrypi/linux/blob/43c132e8863c3bff3647033b6a7d2bf87b15501c/drivers/gpu/drm/v3d/v3d_sched.c),
`v3d_regs.h`, `v3d_drv.c` and `v3d_gem.c`; Mesa 26.2.4's
`src/gallium/drivers/v3d/v3dx_tfu.c`, `src/broadcom/common/v3d_device_info.h`
and `src/broadcom/cle/v3d_packet.xml` from the
[original source release](https://archive.mesa3d.org/mesa-26.2.4.tar.xz).
Native cache behavior belongs to this fork's `sys/arch/arm/arm32/bus_dma.c`;
the ACPI tag setup is in `sys/arch/arm/acpi/acpi_machdep.c`.
