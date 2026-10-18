# Native BCM2712 display clock owners

The [native adapter](../../sys/arch/arm/broadcom/bcm2712_display_clock_acpi.c)
uses one kernel-resident mailbox callback and one sleepable mutex.
Two fixed, typed slots own firmware HSM clock13 and BVB clock14.
The [shared core](bcm2712-display-clock-core.md) supplies their lease mechanism.
There is no arbitrary clock-ID or transport-callback API.
No clock framework, display MMIO, power operation or automatic attachment is added.

## Ownership and lifecycle

All operations require process context. Hard and soft interrupt calls fail
before sleeping or sending a firmware request. Creation validates the GPU0
ACPI resource contract before clock initialization. It assigns the output
only after success. A second owner of the same ID gets `EBUSY`; both IDs can
coexist. One mutex serializes both IDs, their leases and mailbox exchanges.

GPU0 ACPI contract v1 declares HSM13. It does not declare a BVB14 property.
BVB14 is this BCM2712 driver's fixed, known firmware mailbox clock ID from
the [pinned Raspberry Pi sources](bcm2712-display-clock-core.md#sources).
Initialization validates reply IDs, existence and rate bounds. This optional
driver capability adds no ACPI schema or firmware change.

The caller owns the display handoff. It must exclude firmware and
unregistered OS clock writers. A native registry slot or valid ACPI resource
contract does not prove this exclusion, power availability or safe display use.
Stop dependent hardware before release. Quiesce API callers before destroy.
A provider pointer is invalid after successful destroy. Slot identity is
checked before any provider storage is read.

The existing [HSM API and status layout](bcm2712-display-hsm.md) are unchanged.
The BVB API and cached status are in
[`bcm2712_display_bvb.h`](../../sys/arch/arm/broadcom/bcm2712_display_bvb.h).
Use `bcm2712_display_bvb_create`, `acquire`, `update`, `release`, `status`
and `destroy` with ports0/1 and nonzero minimum rates in Hz.

BVB create reads state, rate, maximum and minimum without a SET.
The firmware minimum must be positive and no greater than the maximum.
The cached minimum is its unleased floor. Activation is lazy.
The first acquisition can reduce an inherited high rate to the effective
floor and active port minima. Last release requests the firmware floor.
BVB never sends `SET_CLOCKSTATE(0)`, including destroy. An unacquired off
provider can be destroyed without any SET. HSM retains its existing floor
and restore-off policy.

## Errors and observations

A GET failure before a SET leaves the current lease unchanged without a
sticky mutation fault. A failed SET or its verification retains that
profile's native owner and fault. Later mutation, destroy and replacement
cannot retry it. The other profile's software slot remains independent.
Fault retention does not establish the state of the shared firmware service.

Status is cached and sends no firmware request, including after a fault.
BVB reports both minima, floor, last verified rate/state, leases, fault and
mutation-attempted state. Failed create and status leave output unchanged.
A successful status fills its complete output storage before copying fields;
this is adapter behavior, not a general C structure-padding ABI guarantee.

## Source checks

```sh
sh ember/tools/bcm2712-native-clock-contract.sh "$PWD"
NATIVE_CLOCK_SKIP_MUTANTS=1 \
    CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
    sh ember/tools/bcm2712-native-clock-contract.sh "$PWD"
```

The fixture compiles the actual adapter and common core. It reuses the
shared-core test's firmware service. Kernel synchronization, allocation,
mailbox and ACPI resource-call boundaries are controlled. Separate contracts
check resource decoding and the core's complete clock policy.
It checks both IDs, concurrent entries, IRQ refusals, unknown pointers,
query-free cached status, retained faults, cleanup policies and HSM layouts.
Causal mutants test these boundaries. Earlier HSM/probe contracts remain valid.

`CC`, `CFLAGS`, `NATIVE_CLOCK_COMPILE_ONLY=1` and `NATIVE_CLOCK_OUTPUT` support
cross builds. `TEST_RUNNER` can invoke a supported memory checker.
These checks and an actual `_KERNEL` translation-unit compile do not prove a
linked coherent kernel, physical BVB clock control, PHY/KMS output or Plasma.
