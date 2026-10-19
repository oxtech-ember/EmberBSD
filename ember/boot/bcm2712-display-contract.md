# BCM2712 display resource contract

This interface defines the firmware resources for a native BCM2712 display driver.
The current code validates resources and reads a firmware clock snapshot.
It does not implement HDMI hotplug, DDC, modesetting, scanout, or framebuffer takeover.
A successful snapshot does not authorize display register access.
The separate [firmware display notification](bcm2712-display-handoff.md) is a
source-only one-attempt protocol. It has no default caller and is not an AXI
drain, power or shared-MMU/cache exclusion grant.

## Kernel building blocks

`BCM2712_DISPLAY_COMPONENTS` links the explicit ICAC descriptor queries, typed
shared-cache core, MMU4 table encoder and initialization sequencer, and firmware
display notification. `EMBERV3DRENDER` selects it when `bcmv3d_acpi` is present.
It adds no attachment,
mailbox transaction or MMIO invocation. Linking these APIs does not grant display
ownership or enable scanout. `BCM2712_DISPLAY_DDC` remains a separate option.

## Firmware opt-in

GPU0 keeps `_HID = "BCM2712"`, `_CID = "BCM2850"`, `_STA = 15`, and `_CCA = 0`.
The standard Device Properties UUID in `_DSD` contains this integer property:

```text
emberbsd,display-contract-version = 1
```

Version 1 uses the ordered `_CRS` resources below.
It preserves the first ten memory ranges and first four interrupt indices.
Existing firmware without the property remains valid for the V3D render driver.
The display observer reports `unavailable` and sends no display clock request in that case.
Unknown versions and incomplete resources are rejected.
The firmware series adds this property through `bcm2712-display-resources.diff`.
The builder applies the series once to a fresh pinned source checkout.
A source patch alone does not prove that a running firmware implements this contract.

Each memory descriptor is QWordMemory with these attributes:

- ResourceConsumer, positive decode, and fixed minimum and maximum addresses.
- ReadWrite, NonCacheable, AddressRangeMemory, and TypeStatic.
- Zero granularity and translation offset.
- No resource-source reference.
- Exact base and length from this table; maximum equals base plus length minus one.

Each interrupt is an ExtendedIRQ ResourceConsumer descriptor.
It uses Level, ActiveHigh, Exclusive, and NotWakeCapable.
It has no resource-source reference.
One descriptor can contain several ordered interrupt values.
The adapter checks these flags before the generic ACPI parser can discard them.

## Memory and ownership

Addresses are CPU physical addresses. Lengths are hexadecimal byte counts.
Each range occurs once. An owner is a future driver role, not an active resource claim.

| Index | Resource | Base | Length | Owner |
|---:|---|---:|---:|---|
| 0 | V3D hub | `0x1002000000` | `0x4000` | Render |
| 1 | V3D core | `0x1002008000` | `0x6000` | Render |
| 2 | V3D SMS | `0x1002030800` | `0x700` | Render |
| 3 | HVS | `0x107c580000` | `0x1a000` | Scanout |
| 4 | HVS IOMMU | `0x1000005200` | `0x80` | Scanout |
| 5 | PixelValve 0 | `0x107c410000` | `0x100` | Scanout |
| 6 | PixelValve 1 | `0x107c411000` | `0x100` | Scanout |
| 7 | MOP | `0x107c500000` | `0x28` | Scanout |
| 8 | MOPLET | `0x107c501000` | `0x20` | Scanout |
| 9 | Display interrupt controller | `0x107c502000` | `0x30` | Scanout |
| 10 | DVP clocks and resets | `0x107c700000` | `0x10` | Shared DVP provider |
| 11 | HD | `0x107c720000` | `0x100` | Shared HD provider |
| 12 | AON interrupt controller | `0x107d510600` | `0x30` | Shared AON domain |
| 13 | BSC interrupt controller | `0x107d508380` | `0x10` | Shared BSC domain |
| 14 | DDC 0 | `0x107d508200` | `0x58` | DDC 0 |
| 15 | DDC 1 | `0x107d508280` | `0x58` | DDC 1 |
| 16 | HDMI 0 core | `0x107c701400` | `0x300` | HDMI 0 |
| 17 | HDMI 0 DVP | `0x107c701000` | `0x200` | HDMI 0 |
| 18 | HDMI 0 PHY | `0x107c701d00` | `0x300` | HDMI 0 |
| 19 | HDMI 0 RM | `0x107c702000` | `0x80` | HDMI 0 |
| 20 | HDMI 0 packet | `0x107c703800` | `0x200` | HDMI 0 |
| 21 | HDMI 0 metadata | `0x107c704000` | `0x800` | HDMI 0 |
| 22 | HDMI 0 CSC | `0x107c700100` | `0x80` | HDMI 0 |
| 23 | HDMI 0 CEC | `0x107d510800` | `0x100` | HDMI 0 |
| 24 | HDMI 1 core | `0x107c706400` | `0x300` | HDMI 1 |
| 25 | HDMI 1 DVP | `0x107c706000` | `0x200` | HDMI 1 |
| 26 | HDMI 1 PHY | `0x107c706d00` | `0x300` | HDMI 1 |
| 27 | HDMI 1 RM | `0x107c707000` | `0x80` | HDMI 1 |
| 28 | HDMI 1 packet | `0x107c708800` | `0x200` | HDMI 1 |
| 29 | HDMI 1 metadata | `0x107c709000` | `0x800` | HDMI 1 |
| 30 | HDMI 1 CSC | `0x107c700180` | `0x80` | HDMI 1 |
| 31 | HDMI 1 CEC | `0x107d511000` | `0x100` | HDMI 1 |

A future HDMI child must obtain shared HD and DVP access from their owners.
It must not map a second copy of those ranges.
HVS, PixelValve, MOP, and the display interrupt domain belong to the scanout owner.
The DDC registers implement `brcmstb-i2c`; they are not compatible with `bcm2835bsciic`.

## Interrupt domains

The seven `_CRS` interrupt values are GIC GSIs, in this order:

| Index | Source | GSI |
|---:|---|---:|
| 0 | V3D hub | 282 |
| 1 | V3D core | 281 |
| 2 | PixelValve 0 | 133 |
| 3 | PixelValve 1 | 142 |
| 4 | Display parent | 129 |
| 5 | AON parent | 271 |
| 6 | BSC parent | 274 |

The original firmware symbols name GSI 282 as core and 281 as hub.
The native V3D driver instead assigns hub completion to 282 and core completion to 281.
That assignment follows the observed TFU interrupt in `bcm2712_v3d_irq.c`.
This contract uses the native names and preserves the original numeric order.

Child values below are bits in their parent controller, not additional GSIs.

| Domain | Memory index | Parent IRQ index | Children |
|---|---:|---:|---|
| Display | 9 | 4 | MOPLET 0; MOP 1; HVS EOF 2, 9, 16 |
| AON | 12 | 5 | HDMI 0 CEC 1, 2, 3; HPD 7, 8; HDMI 1 CEC 11, 12, 13; HPD 14, 15 |
| BSC | 13 | 6 | DDC 0 bit 1; DDC 1 bit 2 |

AON and BSC have different status, mask, and clear layouts.
A shared interrupt provider must preserve that distinction.
This change neither establishes interrupts nor clears their status.

## Clock observation

The observer uses the existing bounded and serialized property-mailbox transport.
It sends GET_CLOCKSTATE and GET_CLOCKRATE for each of these firmware IDs:

| Role | Firmware ID |
|---|---:|
| HDMI HSM, firmware M2MC clock | 13 |
| HDMI BVB | 14 |
| HVS core | 4 |
| HVS display | 16 |

Each clock must report state 1 and a nonzero rate.
HSM must report at least 120 MHz, the minimum in the pinned firmware clock provider.
The observer validates the complete response header, length, tag, echoed ID, and terminator.
It sends at most eight requests and stops on the first failure.
The observer reports resource validation separately from the clock snapshot.
A rejected snapshot does not invalidate a successful version-1 resource check.
For `EBUSY` after two valid clock responses, it reports the clock ID, state,
rate, and failed state, nonzero-rate, or HSM-minimum check. The printed tag
identifies that predicate; it does not identify a transport failure.
Transport and malformed-response failures do not expose clock values.
The existing query and clock-output APIs retain all-or-nothing results.
It does not set rates, enable clocks, release resets, or claim a power domain.

This snapshot is not a clock lifetime guarantee or a mode-specific rate check.
A native display driver still needs shared clock ownership and minimum-rate arbitration.
Audio uses DVP gates 0 and 1, with a 108 MHz parent; CEC uses a 27 MHz clock.
DVP reset IDs 1 and 2 are separate from the firmware clock IDs.

## Implementation and checks

The [decoder](../../sys/arch/arm/broadcom/bcm2712_display.c) validates the immutable contract.
The [ACPI adapter](../../sys/arch/arm/broadcom/bcm2712_display_acpi.c) checks raw resource flags.
The V3D finalizer invokes the observer once, independently of render initialization.
The observer cannot turn an unsupported display contract into a successful KMS probe.

Run the source contracts on the developer machine:

```sh
sh ember/tools/bcm2712-display-contract.sh "$PWD"
sh ember/tools/bcm2712-v3d-contract.sh "$PWD"
DISPLAY_SKIP_MUTANTS=1 CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
    sh ember/tools/bcm2712-display-contract.sh "$PWD"
```

The first test compiles the actual decoder and adapter with fake ACPI and mailbox services.
It tests malformed resources, version rejection, error propagation, and unchanged outputs on failure.
It also rejects deliberately unsafe changes to the production source.
`CC`, `CFLAGS`, `DISPLAY_COMPILE_ONLY=1`, and `DISPLAY_OUTPUT` support a cross-built test executable.
`TEST_RUNNER` can execute the test through a supported memory checker.
These checks do not establish physical HDMI, HPD, or KMS support.

## Firmware component test

The firmware owner is `ember/build-firmware.sh` and `ember/firmware/series`.
The builder checks out UEFI commit `246e70acd939d62b5a79f895984afd0a93bd83e9`.
Its edk2-platforms submodule is `c4b5d05de1f2ef633bdb4c175b5c118fcb2666ed`.
Do not apply the series to that checkout before invoking the builder.
The builder creates and patches its own source checkout.

For a source check, use a separate export of that pinned submodule.
Apply the ordered series once to the export with `patch -p1 -F0`.
The component test reads the patched export without changing it:

```sh
IASL=/path/to/current/iasl \
    ruby ember/tools/bcm2712-display-firmware-contract.rb /path/to/patched-export "$PWD"
```

The test extracts the real GPU0 ASL block and the production resource macros.
Use Ruby 3 or newer and a current iASL compiler.
It compiles them with iASL and treats warnings as errors.
It checks the generated AML flags, constants, field offsets, property UUID, and version.
It feeds those resources to the actual OS decoder.
`CC`, `CFLAGS`, `DISPLAY_COMPILE_ONLY`, `DISPLAY_OUTPUT`, and `TEST_RUNNER` support cross-built decoder tests.
`HOST_CC` selects the host ASL preprocessor. `IASL` selects the host ASL compiler.
`DISPLAY_EVIDENCE` saves the AML, generated test inputs, hashes, and command arguments.
This component check does not link a complete UEFI image or load an SSDT namespace.

Set `DISPLAY_COMPILED_AML` to a full build's `Dsdt.aml` to validate its GPU0
package through the same resource checks and OS decoder. The tool still compiles
the source component. It reads the selected DSDT without changing it and records
its SHA256, size, and checked bytes in the evidence directory. The DSDT must have
a valid signature, length, checksum, and exactly one GPU0 device declaration.
This check does not prove that an FD contains the DSDT or that firmware loads it.

The RPi5 `AcpiTables.inf` uses its local DSDT. Older platform GPU ASL modules are separate inputs.
The new diff changes only GPU0 and named register constants.
Thermal references to `\_SB.GPU0` keep the same path.
The default ACPI parser appends memory and interrupt entries in descriptor order.
Existing V3D consumers keep their original memory and interrupt indices.

The [HSM clock provider](bcm2712-display-hsm.md) adds explicit ownership for firmware clock ID 13.
Its separate resource query does not require an enabled clock snapshot.
It has no automatic boot attachment and does not authorize display MMIO.

The [native L2 interrupt provider](bcm2712-display-irq.md) uses these domain and leaf indices.
It requires explicit lifecycle ownership and has no automatic boot attachment.

## Source facts and remaining boundaries

The register facts come from Raspberry Pi Linux commit
[`a1a601d4857978d810175365550c4536701df4ed`](https://github.com/raspberrypi/linux/tree/a1a601d4857978d810175365550c4536701df4ed):
[`bcm2712.dtsi`](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/arch/arm64/boot/dts/broadcom/bcm2712.dtsi),
[`bcm2712-rpi-cm5.dtsi`](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/arch/arm64/boot/dts/broadcom/bcm2712-rpi-cm5.dtsi), and
[`clk-raspberrypi.c`](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/clk/bcm/clk-raspberrypi.c).
The legacy ACPI indices come from edk2-platforms commit
[`c4b5d05de1f2ef633bdb4c175b5c118fcb2666ed`](https://github.com/eotics-com/edk2-platforms/tree/c4b5d05de1f2ef633bdb4c175b5c118fcb2666ed).
The implementation uses hardware facts; it does not copy Linux driver code.

The pinned Linux HDMI driver rejects zero HSM clock before register access.
It also prohibits clearing `VID_CTL_ENABLE` on BCM2712 because that operation can lock the bus.
A future native implementation must retain these hardware constraints.

## Fixed first-frame design boundary

The following facts define a future static-frame stage. They do not establish
working scanout or authorize register writes. The path is HVS channel0/output0
through BCM2712 PixelValve0 to HDMI0. MOP and MOPlet serve TXP writeback to memory;
they are not part of this HDMI path. Use scanout resources 3, 4 and 5 for HVS,
its IOMMU and PV0. Resources 7 and 8 are not needed for this path.
See the pinned [PV mapping](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_crtc.c#L1330-L1395)
and [writeback mapping](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_txp.c#L545-L570).

Select the HVS layout from its version low byte: C0 is `0x53`, D0 is `0x54`.
The channel register offsets and opaque-alpha encoding differ. Reject an unknown
version before writes. PV0 uses one pixel per clock for progressive modes.
Its HDMI0 clock-select index is 0; do not substitute the legacy HDMI selector 1.
See the [HVS register layouts](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_regs.h#L552-L763)
and [alpha rules](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_plane.c#L1305-L1354).

The proposed first frame is progressive 1920x1080 at 60 Hz, with a 148.5 MHz
TMDS character rate and RGB8 output. Use one immutable XRGB8888 linear buffer,
with pitch 7680, offset 0 and 8294400 bytes. In little-endian memory its bytes
are B, G, R, X. Disable scaling, rotation, crop and blending. The fixed CEA mode
has horizontal values `1920/2008/2052/2200`, vertical values
`1080/1084/1089/1125`, and positive sync polarities.
A fresh EDID must validate the current sink and selected mode.
See the [mode entry](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/drm_edid.c#L843-L847)
and [VC6 plane path](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_plane.c#L1930-L2357).

Unity RGB avoids scaling kernels, LBM and YUV conversion, but still needs UPM.
The pinned two-line UPM calculation gives 15616 bytes for this pitch, aligned
to 256-byte units. Reserve dlist, UPM and COB storage without overlap.
The dlist lives in HVS SRAM at resource3 plus `0x4000`; it is not a framebuffer
DMA allocation. Use 32-bit writes and the selected C0/D0 layout.
See [UPM sizing](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_plane.c#L901-L926)
and [HVS publication](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hvs.c#L1176-L1258).

The framebuffer must have one continuous HVS-visible DMA span. Physical SG
pages require a suitable HVS IOMMU mapping. The descriptor has a 40-bit address
field, but the pinned driver selects a 36-bit DMA mask. Keep that conservative
bound for the complete first-frame range; never truncate addresses.
A contiguous allocation alone does not prove the current IOMMU translation or
pass-through state. See the [DMA mask](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_drv.c#L320-L345),
[continuous-span check](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/drm_gem_dma_helper.c#L461-L483)
and [IOMMU aperture policy](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/iommu/bcm2712-iommu.c#L87-L116).

The current [native BO](../../sys/external/bsd/drm2/bcmv3d/bcmv3d_bo.c) permits SG
storage. Its `bo->gpu` belongs to the V3D MMU; it is not an HVS DMA address.
The [render driver](../../sys/external/bsd/drm2/bcmv3d/bcmv3d_drv.c) supports
PRIME self-import and CPU mapping, but has no foreign SG export/import hooks.
The proposed native KMS integration should share that DRM/GEM owner and preserve
the render ABI. This avoids adding foreign PRIME solely for local scanout.
It still needs separate V3D and HVS mappings, DMA tags, producer completion and
cache synchronization. Do not replace the render DMA domain with the HVS domain.
This is an EmberBSD design choice; Linux uses separate display and V3D devices.

HVS also needs ownership of firmware clocks 4/core and 16/disp, beyond the
[PHY clock prerequisites](bcm2712-display-phy-core.md#errors-and-lifetime-boundary).
Linux enables both HVS clocks. The observation API above does not establish
their lifetime, power or mode-specific rate policy. No current HVS rate is
accepted here. See the [CM5 clock bindings](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/arch/arm64/boot/dts/broadcom/bcm2712-rpi-cm5.dtsi#L264-L267)
and [HVS clock acquisition](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hvs.c#L2168-L2203).
Firmware handoff, shared-HD/DVP quiesce and exclusion of other HVS users must
hold through all controller and IRQ cleanup.

The [enable order](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_crtc.c#L658-L703)
places HVS publication before HDMI preparation, PV configuration, PV enable,
HDMI pre-enable, PV video enable and HDMI post-enable. PHY programming alone
does not provide this path. PV0 vblank uses GSI133 and `VFP_START` bit7 at
INTEN `+0x24` and W1C INTSTAT `+0x28`. HVS EOF0 is a separate display-domain
leaf2 event. Initial evidence needs fresh PV interrupts, the expected active
HVS dlist, advancing frame counts and the visible known static pattern.
Counters alone do not prove correct pixels or a working link.
See [PV vblank and active-list checks](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_crtc.c#L801-L898).

Keep the BO and internal allocations pinned until stop and DMA drain are proved.
Linux clears PV video enable, waits for it to clear, delays 20 ms, flushes PV
and stops HVS; VC6 checks the disabled mode. This sequence does not establish
our native DMA-drain or owner-release predicate. Retain buffers, mappings,
clocks and owners when cleanup is uncertain. Never clear BCM2712
`VID_CTL.ENABLE`. HVS mapping, clock/power ownership, exclusive handoff, IRQ
lifetime and stop/drain/release remain gates before native first-frame access.
See the [stop sequence](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_crtc.c#L503-L551)
and [VC6 stop check](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hvs.c#L1097-L1123).

The [pure fixed HVS plane encoder](bcm2712-display-hvs-core.md) documents the
ten-word C0/D0 payload and its separate DMA/UPM metadata contract.

The separate [pure HDMI0 timing encoder](bcm2712-hdmi0-timing-core.md) describes
fixed controller timing and packing payloads. It performs no I/O or activation.

### Pure PV0 timing payload

The [fixed encoder](../../sys/arch/arm/broadcom/bcm2712_display_pv0.c) produces
the nine named fields in [`bcm2712_pv0_timing`](../../sys/arch/arm/broadcom/bcm2712_display_pv0var.h).
`bcm2712_pv0_encode_1080p60` returns positive `EINVAL` for NULL output.
A valid output receives the complete fixed payload. The function has no other
failure case, mutable state, callbacks or native integration.

| Field | PV0 offset | Word |
|---|---:|---:|
| `control` | `0x00` | `0x00177002` |
| `v_control` | `0x04` | `0x20000002` |
| `vsyncd_even` | `0x08` | `0x00000000` |
| `horza` | `0x0c` | `0x0094002c` |
| `horzb` | `0x10` | `0x00580780` |
| `verta` | `0x14` | `0x00240005` |
| `vertb` | `0x18` | `0x00040438` |
| `mux_cfg` | `0x34` | `0x00000020` |
| `pipe_init_ctrl` | `0x94` | `0x00000111` |

These offsets are source facts, not a register-write API or application order.
The words use PV0 clock-select 0, one pixel per clock, RGB24 format and FIFO
threshold 46. Progressive VC6 timing includes `ODD_TIMING` despite its name.
`PV.EN` and `VIDEN` are clear. `FIFO_CLR` and pipe-init enable remain future
payload commands that require quiesce. Do not apply this table to a live PV.
Sync polarity belongs to the HDMI controller stage.
See the pinned [PV fields](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_regs.h#L145-L236),
[FIFO threshold](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_crtc.c#L239-L307)
and [configuration facts](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_crtc.c#L346-L480).

The caller owns output storage and serializes access to it. Encoding does not
validate EDID, clocks, power, HVS, mappings, IRQ lifetime, enable, stop or DMA drain.
It does not establish a first frame, PLL lock or a working link.

```sh
sh ember/tools/bcm2712-pv0-timing-contract.sh "$PWD"
PV0_SKIP_MUTANTS=1 CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
    sh ember/tools/bcm2712-pv0-timing-contract.sh "$PWD"
```

The contract compiles the actual source. It checks the fixed words, NULL input,
output canaries and repeated encoding over poisoned storage. Its causal mutants
change production facts or packing. `CC`, `CFLAGS`, `PV0_COMPILE_ONLY`,
`PV0_OUTPUT` and `TEST_RUNNER` support cross builds and supported memory checkers.

## Physical resource observation

A CM5 Rev 1.0 with BCM2712 D0 booted the complete GCC 16.2 firmware build from
`c90c2a6cee52208fbc1fbe794a72c597f2bd9fd3`. Its live DSDT passed the actual
OS decoder: contract version 1, 32 memory ranges, and seven interrupts.
Kernel `0562cd54544965f1405fa09dd8cbf395212b352a` then identified the clock
failure separately. Both HSM replies passed wire validation. Clock ID 13
reported state 1 and rate 0 Hz, so the observer returned `EBUSY` without
display MMIO. A firmware console and the native V3D render device remained available.
This accepts resource discovery, not clock control or physical KMS.

Native interrupt and DDC providers, power and clock ownership,
HVS IOMMU support, atomic KMS, buffer import, synchronization, and console handoff remain separate stages.
The firmware framebuffer descriptor does not supply those interfaces.
An accelerated virtual output does not display a frame on HDMI.

The explicit [finite IOMMU4 observer](bcm2712-iommu-observe.md) samples inherited
clock and translation registers. Its diagnostic candidate never authorizes DMA.

The explicit [HVS state observer](bcm2712-hvs-observe.md) samples only resource3
identity, global control and all three channel control/status pairs. Its finite
initial-inactivity snapshot grants no ownership, translation or stop/drain.

The [typed shared-cache core](bcm2712-iommuc-core.md) source-tests one bounded
transaction and irreversible member retention. It adds no native cache caller,
MMU table programming, power/access grant or DMA-drain predicate.

The [fixed MMU4 table encoder](bcm2712-mmu4-table-core.md) prepares byte images
for one full-translation frame profile. It grants no native DMA, MMIO access,
publication, cache ownership or stop/drain permission.

The [pure MMU4 initialization sequencer](bcm2712-mmu4-init-core.md) source-tests
one retained full-translation transaction. It adds no native caller, MMIO grant,
power/access proof, automatic caller or DMA-drain/release predicate.

The [private native MMU4 backing helper](bcmv3d-mmu4-backing.md) validates CPU
physical pages against complete parent DMA maps. It prepares zeroed backing
and full PREWRITE without MMIO. It does not supply an MMU owner or publisher.
