# BCM2712 display resource contract

This interface defines the firmware resources for a native BCM2712 display driver.
The current code validates resources and reads a firmware clock snapshot.
It does not implement HDMI hotplug, DDC, modesetting, scanout, or framebuffer takeover.
A successful snapshot does not authorize display register access.

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

The RPi5 `AcpiTables.inf` uses its local DSDT. Older platform GPU ASL modules are separate inputs.
The new diff changes only GPU0 and named register constants.
Thermal references to `\_SB.GPU0` keep the same path.
The default ACPI parser appends memory and interrupt entries in descriptor order.
Existing V3D consumers keep their original memory and interrupt indices.

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

A complete firmware build and device validation, native interrupt and DDC providers, power and clock ownership,
HVS IOMMU support, atomic KMS, buffer import, synchronization, and console handoff remain separate stages.
The firmware framebuffer descriptor does not supply those interfaces.
An accelerated virtual output does not display a frame on HDMI.
