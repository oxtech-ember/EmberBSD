# BCM2712 display interrupt provider

This native provider implements the L2 interrupt domains in the
[display resource contract](bcm2712-display-contract.md).
It is an explicit lifecycle API. Boot attachment does not call it.
It does not implement connector detection, DDC, KMS, or HDMI output.

## Ownership and interface

The display lifecycle owner keeps power, clocks, and the mapped resource alive.
A firmware clock snapshot does not grant that ownership.
The owner supplies one validated resource and an exclusive parent interrupt.
It serializes provider creation and destruction.
Do not create overlapping providers for a domain or its parent GSI.
The API never maps registers or changes clocks and resets.

`bcm2712_display_irq_create` takes a subset of declared leaf bits.
The provider rejects an unmasked foreign declared leaf at startup with `EBUSY`.
Contract v1 does not describe every implemented mask bit. Reserved bits may read zero.
An asserted unknown source also makes startup fail without register writes.
It writes only the selected leaf mask. It preserves other bits and PCI-side registers.
An unexpected active foreign leaf stops the parent route and reports `EIO`.
The provider does not clear that foreign source or claim a connection.

| Domain | Resource index | Parent GSI | Layout |
| --- | --- | --- | --- |
| DISP | 9 | 129 | BCM2711 level, W1C acknowledgement |
| AON | 12 | 271 | BCM2711 level, W1C acknowledgement |
| BSC | 13 | 274 | BCM7271 level, no acknowledgement register |

The hard handler reads one status and mask snapshot.
It masks selected pending leaves and acknowledges them only where the register exists.
It queues process-context work. It does not poll or call DRM in hard interrupt context.
The callback receives the pending bit mask and a possible provider error.
For BSC, it must clear the peripheral level before returning success.
A callback error leaves the route stopped and remains available through `bcm2712_display_irq_error`.
A repeated level after completion produces another event.
Events can coalesce; consumers must query the current connector state.

Destroy masks owned leaves and the parent before draining work.
It synchronizes and removes the interrupt, then waits for queued and running work.
Queued work after stop does not call the consumer.
The owner may release its mapping and callback state after destroy returns zero.
Destroy from its own callback returns `EDEADLK`.
Creation errors restore only the owned mask bits and release acquired services.

## Verification

Run `sh ember/tools/bcm2712-display-irq-contract.sh` from the OS root.
The test compiles the production core and native adapter with controlled kernel services.
It covers all three layouts, foreign bits, repeated levels, work coalescing, failure cleanup,
queued teardown, callback self-destruction, and work queued during a callback.
Causal mutants must compile and then fail a behavioral assertion.
Use `CC`, `CFLAGS`, `L2_COMPILE_ONLY`, and `L2_OUTPUT` for cross compilation.
`L2_EVIDENCE` preserves the generated source inputs.
`TEST_RUNNER` selects a runtime checker. `L2_SKIP_MUTANTS=1` limits an instrumented run.
Host service tests do not prove interrupt delivery or hardware ownership on a board.

## Remaining display path

A lifecycle owner must establish clock and power control before it activates the provider.
Native HPD detection and the BCM2712 DDC controller must query the physical connector and EDID.
The KMS driver still needs HVS, its separate display IOMMU, PixelValve and HDMI PHY control.
It also needs atomic modesetting, framebuffer import, DMA synchronization, vblank and page flips.
A controlled console handoff must retain or replace the firmware framebuffer safely.
V3D rendering and this IRQ provider do not supply those scanout interfaces.
Only a registered KMS connector can then feed the native DRM event path to KWin.

Register facts come from Raspberry Pi Linux
[`irq-brcmstb-l2.c`](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/irqchip/irq-brcmstb-l2.c)
and [`bcm2712.dtsi`](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/arch/arm64/boot/dts/broadcom/bcm2712.dtsi).
The native implementation uses register facts. It does not copy Linux driver code.
