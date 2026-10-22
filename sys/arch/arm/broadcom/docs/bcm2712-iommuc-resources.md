# BCM2712 shared IOMMU cache descriptors

This source-only contract adds a separate ACPI `\_SB.ICAC` description for the
cache shared by IOMMU2, IOMMU4 and IOMMU5. It preserves GPU0 v1, its 32 MMIO
resources and seven IRQs. GPU0 gains only an `_DEP` object reference.
The descriptor does not add a mapping, readiness result, permission,
clock/reset/power operation, MMIO or default attachment.

## Fixed firmware contract

The canonical [firmware series](../../../../../ember/firmware/README.md) applies
`bcm2712-iommuc-resources.diff` to pinned edk2-platforms. The new device has:

- `_HID="PRP0001"`, `_UID=0`, `_STA=0x0f`.
- `compatible="brcm,bcm2712-iommuc"` and
  `emberbsd,iommuc-contract-version=1` in the device-properties UUID package.
- One fixed, positive-decode, non-cacheable, read/write QWord Memory
  ResourceConsumer: `0x1000005b00/0x80`, with no translation or source string.
- No IRQ or DMA description. `_CCA` is not a cache-access grant.

`_STA=0x0f` is this profile's exact present/enabled/UI/functioning status. It
is not D0, power ownership or an MMIO accessibility guarantee.

## Native descriptor queries

[bcm2712_iommuc.h](../bcm2712_iommuc.h) and the
[resource query source](../bcm2712_iommuc_acpi.c) are selected
by explicit display components or the optional [native ICAC owner](bcm2712-iommuc-native.md).
The [GPU dependency query](../bcm2712_iommuc_dependency.c)
remains GPU-only. Both queries require process context.

`bcm2712_iommuc_acpi_resources(cache, out)` requires the exact `\_SB.ICAC`
Device handle and validates the identity, version, status and full resource
profile. It commits a zero-initialized descriptor only after complete success.

`bcm2712_iommuc_acpi_dependency(gpu, cache_out, out)` first resolves the exact
`\_SB.GPU0` Device handle and validates its existing canonical display contract.
It then accepts 1–32 distinct `_DEP` Device object references. Each external object
must be `LOCAL_REFERENCE` with Device ActualType and a non-NULL Device handle.
ANY, STRING and INTEGER are invalid even though the generic reference helper
accepts some of them. Exactly one reference must name canonical ICAC. Other valid
Device dependencies are allowed; this does not mean those devices are attached
or ready. The 32-reference limit is an explicit v1 implementation boundary.
The caller supplies valid, disjoint outputs. Both outputs remain unchanged on
failure. The separate native owner supplies strict software lookup, not readiness.

The native ACPI enumerator scans `_DEP` devices first. It can still attach a
consumer after an unmatched or failed dependency. A future cache consumer must
require the real provider's successful registration. `_DEP` supplies only order,
not readiness, power or access authority.

## Source checks

The [actual-source fixture](../../../../../ember/tools/bcm2712-iommuc-contract.c) compiles both
native query sources, the existing display decoder/adapter, real ACPICA public
type headers and the unchanged bounded `acpi_eval_reference_handle` function.
Only namespace/kernel service boundaries are controlled. There is no provider
model. It checks every query error, fixed geometry/flags, canonical path/type,
strict reference forms, mixed dependencies, output padding/canaries and cleanup.
A copied GPU contract on another handle must fail before the resource query.

```sh
sh ember/tools/bcm2712-iommuc-contract.sh /absolute/os/source
```

`ACPICA_INCLUDE` can select the matching source tree's public ACPICA headers.
`CC`, `CFLAGS`, `ICAC_COMPILE_ONLY`, `ICAC_OUTPUT` and `TEST_RUNNER` support cross
checks and a single executable wrapper. `ICAC_SKIP_MUTANTS=1` runs the positive;
`ICAC_MUTANTS` selects focused compiled causal cases. `ICAC_EVIDENCE` saves exact
extractions, provenance, dependencies and rejected mutant inputs.

The existing [AML component checker](../../../../../ember/tools/bcm2712-display-firmware-contract.rb)
uses Ruby 2.7 or later and iasl. With `IOMMUC_REQUIRED=1` it also requires ICAC,
validates its compiled descriptor and GPU0's object-reference package. It records
GPU0 resource-program and `_DSD` byte hashes for baseline comparison. The actual
OS display decoder still validates all 32 MMIO resources and seven IRQs. This is
a component check, not a packaged firmware, live namespace or coherent kernel.
The [ICAC AML regression](../../../../../ember/tools/bcm2712-iommuc-firmware-contract.rb) runs the
actual checker and six checksum-valid malformed AML controls.
`ICAC_BASELINE_PLATFORM` compares the GPU0 resource-program and `_DSD` bytes
against the earlier prepared platform source. The optional output is separate
from both source inputs.

The ordered-series checker uses only a small export of pinned original files.
Do not edit the prepared firmware cache or reapply the series there.

## Open access and lifetime gate

Pinned Linux describes the separate cache phandle and one serialized cache
operation. It requests no cache clock, reset or power-domain property. That
absence is not an always-on guarantee. HSM/BVB/DISP leases, CORE GET replies,
framebuffer absence and a prior MMU4 snapshot do not establish cache accessibility
or exclude firmware writers. Do not invent a power resource or reuse V3D's SMS
power owner for the cache.

A later access bridge in the native provider must own the cache mapping and serialization for
all cooperating MMU2/4/5 users. Its lifecycle must retain faults and all exposed
translation obligations after uncertainty. GPU0 cannot claim exclusive ownership
of this shared block. Resolve the actual platform access/power contract and
firmware-writer exclusion before any cache MMIO. No physical cache acceptance,
HVS translation, frame publication or DMA drain is established here.

Primary register/relationship facts are pinned Raspberry Pi Linux
`a1a601d4857978d810175365550c4536701df4ed`:
[DTS](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/arch/arm64/boot/dts/broadcom/bcm2712-ds.dtsi#L274-L305),
[cache](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/iommu/bcm2712-iommu-cache.c#L15-L59),
[IOMMU dependency](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/iommu/bcm2712-iommu.c#L628-L640).
[ACPI 6.6 §6.5.8](https://uefi.org/specs/ACPI/6.6/06_Device_Configuration.html#dep-device-dependencies)
defines the ordering limit. No GPL implementation is copied into this BSD code.
