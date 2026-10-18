# BCM2712 DVP gate ownership

The DVP provider owns the two HDMI 108 MHz gates in the shared DVP resource.
It reads reset state but never changes it. The API has no automatic boot attachment.
It does not initialize HDMI, read HPD or EDID, publish DRM events, or provide KMS.

## Resources and lifecycle

Use the version 1 [display resource contract](bcm2712-display-contract.md).
Resource 10 is `dvp-clock-reset`, at `0x107c700000`, with a length of `0x10`.
`DVP_HT_RPI_MISC_CONFIG`, offset `0x08`, has gate bits 3 and 4 for ports 0 and 1.
A set gate bit disables its clock. A clear bit enables it.
`DVP_HT_RPI_SW_INIT`, offset `0x04`, has active-high reset bits 1 and 2 for those ports.
Other bits are not assigned to this API.

The native entry points are in `sys/arch/arm/broadcom/bcm2712_display_dvp.h`.
The pure operation core is `bcm2712_display_dvp.c`.
The native adapter is `bcm2712_display_dvp_acpi.c`.

1. Establish exclusive ownership and accessibility of the mapped DVP resource.
2. Call `bcm2712_display_dvp_create` with the validated resource contract and mapping.
3. Acquire one lease per port with `bcm2712_display_dvp_acquire`.
4. Quiesce the port before `bcm2712_display_dvp_release`.
5. Quiesce API callers, then call `bcm2712_display_dvp_destroy`.

Keep the mapping, clocks and power prerequisites valid until destroy returns zero.
Create reads state but does not write any register. It rejects a second shared owner.
Acquire refuses an asserted reset. It enables only that port's gate and checks readback.
Release restores a gate only if that lease enabled it. An inherited enabled gate stays enabled.
The provider serializes both ports with one sleepable mutex and preserves other register bits.
It does not expose a generic reset pulse or a rate-setting operation.

Destroy returns `EBUSY` while leases remain. Callers must not race new API calls with destruction.
An error before a write leaves the lease state unchanged and permits a read retry.
An attempted write followed by an error or invalid readback faults the provider permanently.
The provider retains ownership and existing leases. It does not retry or restore unknown state.
An unexpectedly gated or reset leased port also faults the owner.
A faulted owner requires a separately controlled recovery; ordinary destroy does not free it.

`bcm2712_display_dvp_status` copies the last verified register values, leases and fault under the mutex.
It performs no MMIO. A snapshot does not prove current hardware accessibility.
Core transport callbacks can report errors. Native `bus_space` cannot recover a stalled bus transaction
or synchronous external abort. Readback checks do not provide such recovery.

## Power and reset limits

The pinned BCM2712 declarations do not assign a separate `power-domains` property to DVP or HDMI.
They assign the V3D power domain to V3D, not to the HDMI controllers.
This is not evidence that arbitrary DVP MMIO is safe during every boot or suspend state.
The explicit lifecycle caller must establish accessibility before create, including its initial reads.
Resource decoding and an [HSM status snapshot](bcm2712-display-hsm.md) cannot establish that condition.
No production display lifecycle caller is connected yet.

The pinned Linux HDMI resume path enables HSM, checks its nonzero rate, then enables the DVP audio gate.
This sequence is prior art, not physical validation of this native provider.
Its DVP reset provider leaves `reset_us` zero. `reset_simple_reset` then returns `ENOTSUPP`.
The HDMI reset callback ignores that return value. This does not establish a usable reset pulse width.
This implementation therefore leaves `SW_INIT` unchanged, including when reset is already asserted.

## Finite HPD0 sample

The [finite module](bcm2712-display-hsm.md#finite-opt-in-module) also accepts
`-s hpd-mode=sample0`. Apply its selected-kernel, render and cold-headless gates.
First accept the separate shared-DVP observation and inherited-gate cycle.
Any `dvp-mode` property conflicts with this mode. Empty, unknown or non-string
HPD values fail before all HSM, mapping and MMIO calls. No mode advances itself.

This mode holds HSM at least 120 MHz, creates DVP, and requires port0 reset bit1 clear,
gate bit3 already clear, no leases, no fault and no mutation. It acquires port0
and rechecks lease1, gate/reset state, fault0 and mutation0 before controller map.
It assumes no concurrent firmware writer. If an external actor closes the gate
after the initial sample, native acquire can write. Any observed mutation then
stops before controller access and retains the owners and shared mapping.

The only controller mapping is resource16, `0x107c701400/0x300`.
A log before the first read names the normal-active-SoC prior-art assumption
and the unrecoverable bus_space stall/abort boundary. Exactly one 32-bit read
at `HDMI_HOTPLUG`, offset `0x1c8`, records the raw word and connected bit0.
Other bits do not determine the connection. The module then unmaps resource16,
calls DVP release/status/destroy, unmaps resource10, then calls HSM release/status/destroy.
Every controller map/read/unmap occurs under both leases. No reset, PHY, per-controller DVP,
VID_CTL or other controller register is written. HPD errors retain remaining
owners/maps and label any saved sample as cached. Unload performs no cleanup.

This is a bounded first-controller-access probe, not a complete ACPI power or
display-lifetime contract. It does not arm AON interrupts, read DDC/EDID, or
provide KMS. A disconnected sample does not accept hotplug delivery.
Linux uses the VC6 core register table for BCM2712. Its detect path holds
runtime PM; resume also invokes a controller-reset callback. That prior art
does not by itself prove physical acceptance of the native read-only sequence.

## Physical HPD0 boundary

One CM5 Rev1.0 with BCM2712D0 passed finite HPD0 samples after a controlled
cold boot without HDMI. The kernel was `0562cd54`, firmware source was
`c90c2a6c`, and module source was `c2e7cdca`. Source/header, option, sampled
layout and strong-symbol checks preceded the normal module load.
The disconnected raw word was zero. After a live HDMI0 connection on the same
boot, the raw word was `0x1`, with connected bit0 set. HSM remained at
120 MHz and DVP lease0 remained held throughout the controller access.
DVP gates and resets were zero, with no fault or mutation. The controller
and DVP mappings were released, both owners were destroyed, and the module
unloaded after each cycle. The temperatures afterward were 49.920 C and
51.569 C, without reported throttling or undervoltage.

This accepts the two sampled levels on that boot. It does not accept
IRQ delivery, DDC, reset control, a general power or display
lifetime, KMS, hotplug, or a physical Plasma session.

## Finite DDC0 snapshot

The finite module accepts `-s ddc-mode=observe0` after the same selected-kernel,
render and cold-headless checks. Any `dvp-mode` or `hpd-mode` property conflicts
with this mode. Empty, unknown or non-string values fail before HSM, mapping
and MMIO. No mode advances itself to a transfer.

It holds HSM at least 120 MHz and a DVP port0 lease throughout the snapshot.
Before either new mapping, it requires inherited gate bit3 clear, reset bit1
clear, no fault and no DVP mutation. It assumes no concurrent firmware writer.
An unexpected gate change can make native acquire write; any observed mutation
then stops before BSC or DDC access and retains the owners and shared mapping.
The normal-active-SoC prior-art assumption remains explicit. Native bus_space
cannot recover a stalled transaction or synchronous external abort.

The only new mappings are BSC resource13, `0x107d508380/0x10`, and DDC0
resource14, `0x107d508200/0x58`. Exactly five 32-bit reads occur in this order:
BSC status `0x00`, BSC mask status `0x04`, DDC CTL `0x28`, ENABLE `0x2c`,
and CTLHI `0x50`. Logs precede the first MMIO in each resource.
CTLHI is a raw diagnostic value and has no acceptance predicate.
The module reports `candidate_quiescent` only when all five values are valid:

```c
(mask & 2) != 0 && (status & 2) == 0 &&
    (ctl & 0x40) == 0 && (enable & 3) == 0
```

These bits describe masked leaf1, no pending leaf1, interrupt enable clear,
and transfer-enable/completion bits clear. The reads are not atomic.
They do not establish SCL/SDA levels, a free bus, a working clock, or a safe
transfer. Other ENABLE bits do not change this candidate predicate.
A busy candidate still takes the normal cleanup path. A zero return means
only that the snapshot and cleanup completed; it does not mean idle.

Cleanup unmaps resource14, then resource13, releases and destroys DVP,
unmaps resource10, then releases and destroys HSM. Native status is sampled
before each owner is destroyed. Errors retain the remaining owners and maps.
Partial and retained samples carry validity flags and cached-value labels.
This mode sends no BSC, DDC or IRQ write. It performs no transfer, EDID read,
poll, interrupt activation, reset or HPD sample. It does not establish an ACPI
power or general display-lifetime contract.

The module fixture uses the actual decoder, HSM/DVP cores and native providers.
It checks exact ranges, offsets, read count, held leases, inverse unmap order,
busy cleanup, invalid properties, partial validity and retained failures.
Causal mutants test those boundaries. Host checks do not accept physical DDC.

## Source validation

```sh
sh ember/tools/bcm2712-display-dvp-contract.sh "$PWD"
DVP_SKIP_MUTANTS=1 CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
    sh ember/tools/bcm2712-display-dvp-contract.sh "$PWD"
```

The fixture compiles the actual resource decoder, operation core and native adapter.
Only kernel services and MMIO are replaced. It uses real host pthread synchronization.
It covers both gate polarities, every foreign register bit, both ports, inherited gates,
reset refusal, uncertain writes, readback failures, retained leases, cleanup and concurrent use.
Unsafe mutants must compile and fail at runtime.
`CC`, `CFLAGS`, `DVP_COMPILE_ONLY`, `DVP_OUTPUT` and `DVP_EVIDENCE` support cross-built checks.
`TEST_RUNNER` can invoke a supported memory checker.

The host fixture passes 3024 conditions, 13 unsafe mutants and 2000 concurrent lease cycles.
GCC 16.2 compiles both native AArch64 units with the real kernel headers and passes path analysis.
Clang Static Analyzer reports no findings in the new units or host fixture.
Cppcheck 2.22 reports only inherited kernel-header findings in the new units; the fixture has none.
The comparison uses the existing HSM provider with the same source headers and compilation flags.
The fixture passes ASan and UBSan. These checks do not include a complete kernel build.

Host tests and analysis do not establish hardware gate control or safe display MMIO.
Valgrind is unavailable on the Darwin AArch64 development host; ASan and UBSan are separate checks.
Physical acceptance needs the declared firmware resources and a coherent kernel.
The next controller steps are HPD sampling through AON and a separate BRCMSTB DDC master.
HVS, display IOMMU, PixelValve, PHY, scanout, PRIME import, atomic KMS and console handoff remain separate work.
The existing render-only V3D device and virtual Wayland outputs do not provide physical HDMI output.

## Sources

The interface facts use Raspberry Pi Linux revision `a1a601d4857978d810175365550c4536701df4ed`:

- [DVP gate/reset registers](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/clk/bcm/clk-bcm2711-dvp.c).
- [CM5 clock connections](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/arch/arm64/boot/dts/broadcom/bcm2712-rpi-cm5.dtsi).
- [BCM2712 resource and reset declarations](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/arch/arm64/boot/dts/broadcom/bcm2712.dtsi).
- [HDMI runtime resume](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hdmi.c).
- [Simple reset semantics](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/reset/reset-simple.c).

This implementation uses register and interface facts. It does not copy Linux driver code.

The HPD register facts use the same pinned Linux revision:
[VC6 register tables](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hdmi_regs.h#L437-L477)
and [connected bit0](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_regs.h#L768).

The DDC/BSC register facts use the same pinned Linux revision:
[register layout and control bits](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/i2c/busses/i2c-brcmstb.c#L24-L78)
and [level status and mask registers](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/irqchip/irq-brcmstb-l2.c#L45-L51).
Only interface facts are used; Linux driver implementation is not copied.
