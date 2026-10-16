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
