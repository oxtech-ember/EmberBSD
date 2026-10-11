# BCM2712 V3D MMU fault recovery experiment: handoff design

Status on 2026-10-11: **implemented and contracted**; the first physical
boot delivered the fault (PTI and WRV bits, recorded as `0x30`) and ran the
pinned recovery, but the following good job faulted again with the same
bits instead of completing — the experiment honestly reported `good job
never completed after recovery` and the board was restored. Hardware
fault-recovery acceptance is **open**. The failure path now records
MMU_CTL, TFU_CS, VIO_ID and VIO_ADDR and rejects a busy TFU before
resubmission. The next physical run must distinguish a stuck-busy TFU
after an aborted job from a W1C clear that does not stick on this silicon.
These remain hypotheses, not an established cause.
The [acceptance guards](bcm2712-v3d-validation.md) add regressions for
unexpected completion, fault identity, duplicate events and collateral
DMA writes. These checks and richer diagnostics do not establish physical
recovery; the failed hardware result remains open.
This is stage C after the accepted [translated DMA](bcm2712-v3d-dma.md),
[bin/render queue](bcm2712-v3d-binrender-plan.md) and
[interrupt](bcm2712-v3d-irq-plan.md) experiments: a deliberately faulting
GPU job must fail cleanly and leave the GPU usable for a following good
job on the same boot.

## Pinned facts (raspberrypi/linux 43c132e, v3d_irq.c/v3d_mmu.c/v3d_regs.h)

- HUB interrupt bits: TFUC bit1, **MMU_CAP bit3, MMU_PTI bit4, MMU_WRV
  bit5**; handler acks exactly the handled set through HUB `INT_CLR`.
- On an MMU fault Linux reads `MMU_VIO_ID` (0x122c) and `MMU_VIO_ADDR`
  (0x1234; address in the high `va_width-32` bits), maps `axi_id >> 5`
  through the v4.1+ client table (L2T, PTB, PSE, TLB, CLE, **TFU**, MMU,
  GMP), and clears the latched fault by writing `MMU_CTL` back verbatim
  (`V3D_WRITE(V3D_MMU_CTL, V3D_READ(V3D_MMU_CTL))` — the fault bits are
  write-1-to-clear through this reflection).
- Full MMU flush after the fault: `MMUC = ENABLE|FLUSH`, bounded wait for
  FLUSHING clear, then `MMU_CTL` `TLB_CLEAR` with a bounded wait — in this
  fork always as the established full-value overwrite (`0x060d0c01` with
  the one bit set), never a read-modify-write of unowned bits.
- Line identity is by wire evidence from the accepted IRQ experiment:
  the HUB line is `_CRS` index 0 / GSI 282; handlers self-mask under a
  level-line storm.

## Scope and design

A separate opt-in `BCM2712_V3D_FAULT_PROBE` and `EMBERV3DFAULT`
configuration, after a completed takeover and with no other V3D probe
enabled. One HUB handler (established first, storm-protected), the proven
TFU payload, six objects: page table, scratch, actual and alias source
and destination images (alias entries per the accepted DMA experiment).

Flow:

1. Preconditions as before, plus a clean HUB status. Unmask only
   `TFUC|MMU_PTI|MMU_WRV|MMU_CAP` on the HUB line.
2. Publish the MMU with the source alias translated to the actual source
   (valid, read-only) and the **destination alias deliberately invalid**
   (zero PTE).
3. Submit the TFU copy. The destination write must fault: expect exactly
   one HUB interrupt with a fault bit (PTI/WRV/CAP), no TFUC, `MMU_CTL`
   fault bit latched, and `VIO_ID`/`VIO_ADDR` read for the receipt
   (client expectation: TFU, address inside the destination alias page
   range). The faulted job never completes; the wait is bounded and the
   experiment does not retry it.
4. Recover exactly as pinned: acknowledge the handled interrupt bits,
   clear the fault latch through the verbatim `MMU_CTL` write-back, then
   the bounded MMUC flush and full-value TLB clear. `MMU_CTL` must read
   back `0x060d0c01` with no fault bits.
5. Job 2 (good): rewrite the destination alias PTEs to the actual
   destination pages, sync the page table, repeat the bounded flush,
   verify the destination still holds its canary (job 1 must not have
   written it), and submit the identical TFU copy again. Expect TFUC as
   an interrupt within the bound, then verify the copied image word by
   word, both sources, canaries and scratch, exactly as the DMA
   experiment.
6. PASS requires both halves: a clean fault (interrupt + latch + no
   completion + untouched destination) and a verified good job afterwards
   on the same GPU, same MMU, same boot. Every exit path remasks the HUB
   line to the takeover state, disestablishes the handler, and retains
   everything GPU-exposed until reboot.

## Bounds

One fault kind per boot (invalid destination PTE, whichever fault bit the
silicon raises first), one recovery, one good job. Not covered: binner/
render queue faults, OUTOMEM spill work, fault while multiple jobs are
in flight, GMP violations, sustained fault rates, and any userspace
interface. Stage D (DRM render interface) builds on the recovered-GPU
guarantee this experiment establishes.

## Contracts and review

Actual-source host contract with the fake GPU faulting the first job:
the fixture's fake MMU raises the latched `MMU_CTL` fault bit and the
unmasked HUB interrupt bit when the fake TFU engine touches the invalid
alias entry, then accepts the good job after the pinned recovery
sequence. Cases: the full PASS; no-fault anomaly (job 1 completes —
invalid entry unexpectedly honoured); fault without clean latch (no
interrupt, timeout); recovery that leaves fault bits set; job 2 failing
to complete; job 2 image mismatch; destination canary touched by the
faulted job; storm self-masking; remask and disestablish on every exit;
retention. Mutants: skipped fault-latch clear, skipped flush, write-back
that drops bits, PTE fixup without sync, unmapping the good alias,
retry of the faulted job, release after publication.
