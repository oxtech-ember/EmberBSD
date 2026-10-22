# BCM2712 V3D interrupt contract

The opt-in `BCM2712_V3D_IRQ_PROBE` and `EMBERV3DIRQ` configuration
test owned TFUC, FLDONE and FRDONE completion through ACPI interrupts.
They reuse the bounded [TFU](bcm2712-v3d-dma.md) and
[bin/render](bcm2712-v3d-binrender-plan.md) payloads with exclusive job ownership.

## Hardware applicability


Current hardware evidence and its applicability belong to the [DevBoards profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/bcm2712-cm5/graphics.md).
TFUC, FLDONE and FRDONE must each satisfy the owned interrupt/queue guard. Duplicate/error race injections remain source-contract evidence.

## Interrupt identity (pinned)

The recorded CM5 firmware declares four GPU0 interrupts in `_CRS`, all
`ResourceConsumer, Level, ActiveHigh, Exclusive` (live DSDT order, and the
firmware source `Silicon/Broadcom/Bcm27xx/Include/IndustryStandard/Bcm2712.h`
of eotics-com/edk2-platforms `c4b5d05de1f2ef633bdb4c175b5c118fcb2666ed`):

| _CRS index | GSI | Firmware header name | Meaning by wire evidence |
| --- | --- | --- | --- |
| 0 | 282 (0x11A) | `BCM2712_V3D_CORE_INTERRUPT` (GIC_SPI 250+32) | **HUB line** (TFU done arrived here) |
| 1 | 281 (0x119) | `BCM2712_V3D_HUB_INTERRUPT` (GIC_SPI 249+32) | **CORE line** (by elimination) |
| 2 | 133 | `BCM2712_PIXELVALVE0_INTERRUPT` | display; not ours |
| 3 | 142 | `BCM2712_PIXELVALVE1_INTERRUPT` | display; not ours |

The recorded firmware header names are swapped relative to the observed
events. TFUC is a HUB-only event, so the probe registers the HUB handler
on index 0 and the CORE handler on index 1. Keep the resource index and
observed event identity distinct from the firmware header's label.

## Register facts (pinned to raspberrypi/linux 43c132e, v3d_regs.h/v3d_irq.c)

Both blocks carry `INT_STS 0x50 / INT_SET 0x54 / INT_CLR 0x58 /
INT_MSK_STS 0x5c / INT_MSK_SET 0x60 / INT_MSK_CLR 0x64`.

- CORE bits: FRDONE bit0, FLDONE bit1, OUTOMEM bit2, SPILLUSE bit3,
  TRFB bit4, GMPV bit5 (6 on older), CSDDONE bit6 on ver 71.
- HUB bits: MMU_WRV bit5, MMU_PTI bit4, MMU_CAP bit3, MSO bit2,
  **TFUC bit1** (TFU complete), TFUF bit0; ver 71 adds GMPV in HUB.
- Handler protocol (v3d_irq.c): read INT_STS, ack the handled set through
  `INT_CLR`, return; enable is `INT_MSK_SET(~ours)` then
  `INT_MSK_CLR(ours)`; disable is `INT_MSK_SET(~0)`.
- The takeover has already masked all HUB (0x7f) and CORE (0x0fff007f)
  bits and verified the masks read back; the experiment owns unmasking
  only its handled bits and restoring the takeover masks afterwards.

## Host API (pinned to this fork)

Parse `_CRS` with `acpi_resource_parse` and take `acpi_res_irq(&res, 0)`
(hub) and index 1 (core); establish each with
`acpi_intr_establish_irq(dev, irq, IPL_VM, mpsafe=true, handler, arg,
xname)` from `sys/dev/acpi/acpi_util.c`; disestablish via the returned
cookie (`acpi_intr_disestablish`). GSI routing is already proven on this
board (sdhc at acpi0 uses irq 305/306).

## Scope and design

A separate opt-in `BCM2712_V3D_IRQ_PROBE` and `EMBERV3DIRQ`
configuration, after a completed takeover, with **no** other V3D probe
enabled in the same kernel: the experiment owns the job lifecycle end to
end, because a shared polling probe would race the interrupt handler for
the same latched status bits. It reuses the two accepted job payloads
byte for byte (TFU copy and clear-and-store bin/render) but with its own
allocations and its own MMU publication, exactly as those experiments.

Flow:

1. Preconditions as before (completed takeover, MMU geometry, clean CORE
   and HUB status). Establish both handlers; no bits are unmasked yet.
2. Publish the MMU; submit the proven TFU copy. Unmask only
   `HUB_INT_TFUC` through HUB `INT_MSK_CLR`. Wait on a condition
   variable with a 500 ms timeout; the handler must observe exactly one
   TFUC, ack it through HUB `INT_CLR`, and wake the waiter. The CPU-side
   verification of the copied image still runs afterwards. Re-mask TFUC
   through `INT_MSK_SET`.
3. Submit the proven bin/render job. Unmask only `FLDONE|FRDONE` on the
   core line. The handler records each bit, acks it, and wakes the
   waiter; the experiment waits for FLDONE then FRDONE (500 ms each),
   then verifies the stored image and control-list integrity as in the
   queue experiment. Re-mask the core bits.
4. Success requires: both handlers ran at most once per job (exactly
   once in total per bit), no unexpected bits, no interrupt storm
   (spurious handler entries counted and bounded), image and canaries
   verified, no MMU fault. Everything GPU-exposed is retained until
   reboot as before; the two handlers are disestablished and both blocks
   restored to the takeover's full masks on every exit path.

Any timeout, storm, wrong line, unexpected bit or verification failure
ends the experiment with its own verdict string and remasks everything.

## Bounds

This proves interrupt delivery, handler context and wakeup for one TFU
and one bin/render completion on both GPU lines. It does not prove
shared lines, threaded/deferred processing, OOM recovery work, MMU fault
interrupt handling (bits stay masked), MSO/SPILLUSE paths, or any
sustained rate. The separate [stage C](bcm2712-v3d-fault-plan.md) now
accepts one invalid-PTE recovery on the same board.

## Contracts and review

Actual-source host contract as in the previous stages, with a fake
interrupt controller: the fixture invokes the handlers on unmasked bits
at the moment the fake GPU latches them, checks ack-then-wake ordering,
exactly-once delivery, storm detection, remask-on-exit, disestablish on
every path, and the full job verification. Mutants: missing ack (storm),
missing remask, wait without timeout, unmask before establish, handler
that never wakes, ack of bits not handled, and release-after-publication.
Independent review and a clean `EMBERV3DIRQ` build before installation;
the ordinary recovery bundle must still verify.
