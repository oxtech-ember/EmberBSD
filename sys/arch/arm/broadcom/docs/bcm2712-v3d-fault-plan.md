# BCM2712 V3D MMU fault recovery contract

The opt-in `BCM2712_V3D_FAULT_PROBE` and `EMBERV3DFAULT` configuration
test one invalid-destination TFU fault and one following good job in the same boot.
The probe requires completed takeover and exclusive ownership of the GPU.
It retains published allocations until reboot. General recovery remains outside this contract.
Current hardware applicability and pinned evidence belong to
[DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/bcm2712-cm5/diagnostics.md).

## Pinned facts

Register and MMU sequences use raspberrypi/linux `43c132e8863c3bff3647033b6a7d2bf87b15501c`.
The generation-specific client map uses
[torvalds/linux v3d_irq.c at a5ebb762](https://github.com/torvalds/linux/blob/a5ebb76233b79db01e82e061173cb73c6d2b5c6b/drivers/gpu/drm/v3d/v3d_irq.c).

- HUB interrupt bits: TFUC bit1, **MMU_CAP bit3, MMU_PTI bit4, MMU_WRV
  bit5**; handler acks exactly the handled set through HUB `INT_CLR`.
- On an MMU fault Linux reads `MMU_VIO_ID` (0x122c) and `MMU_VIO_ADDR`
  (0x1234; address in the high `va_width-32` bits). V3D 7.1 uses the low
  seven ID bits; TFU is `0x7c` or `0x7d`. The old `axi_id >> 5` map
  does not apply to CM5. Linux clears the latched fault by writing `MMU_CTL` back verbatim
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
TFU payload, six objects: page table, guarded three-page scratch, actual and alias source
and destination images (alias entries per the accepted DMA experiment).

Flow:

1. Preconditions as before, plus a clean HUB status. Unmask only
   `TFUC|MMU_PTI|MMU_WRV|MMU_CAP` on the HUB line.
2. Publish the MMU with the source alias translated to the actual source
   (valid, read-only) and the **destination alias deliberately invalid**
   (zero PTE).
3. Submit the TFU copy. The destination write must fault: expect exactly
   one HUB interrupt with a fault bit (PTI/WRV/CAP), `MMU_CTL`
   fault bit latched, and `VIO_ID`/`VIO_ADDR` read for the receipt
   (client expectation: TFU, address inside the destination alias page
   range). Freeze ISR evidence with a controller mask and barrier.
   The conversion retires with CVTCT delta 1 and idle TFU; its terminal
   TFUC must be observed either in that ISR or under the mask within
   a bounded wait. Retirement is not successful copying. All destination
   canaries and protected image buffers must remain unchanged. Invalid
   writes redirect to the middle scratch page. For this fixed payload,
   require the final 128 source bytes at its start, zeros elsewhere and
   intact nonzero canaries on both sides; other patterns remain unsupported.
4. Recover exactly as pinned: acknowledge the handled interrupt bits,
   clear the fault latch through the verbatim `MMU_CTL` write-back, then
   acknowledge only the captured residual HUB fault/terminal indications,
   then the bounded MMUC flush and full-value TLB clear. `MMU_CTL` must read
   back `0x060d0c01` with no fault bits.
5. Job 2 (good): rewrite the destination alias PTEs to the actual
   destination pages, reset the verified scratch while CPU-owned,
   sync all buffers and the page table, repeat the bounded flush
   without clearing any new fault,
   verify the destination still holds its canary (job 1 must not have
   written it), reprogram all eleven TFU input fields, then submit the
   identical TFU copy through ICFG again. Expect TFUC as
   an interrupt within the bound, then verify the copied image word by
   word, both sources, canaries and scratch, exactly as the DMA
   experiment. Require a clean HUB/MMU before rearm, exactly one new
   ISR/TFUC and total CVTCT delta 2 afterwards.
6. PASS requires both halves: a clean fault (interrupt + latch + no
   successful writes + untouched destination) and a verified good job afterwards
   on the same GPU, same MMU, same boot. Every exit path remasks the HUB
   line to the takeover state, disestablishes the handler, and retains
   everything GPU-exposed until reboot.

## Bounds

One fault kind per boot (invalid destination PTE, whichever fault bit the
silicon raises first), one recovery, one good job. Not covered: binner/
render queue faults, OUTOMEM spill work, fault while multiple jobs are
in flight, GMP violations, sustained fault rates, and any userspace
interface. Stage D (DRM render interface) must add job ownership, lifetime
and failure handling beyond this bounded recovery result.

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
