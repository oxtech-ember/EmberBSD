# BCM2712 shared firmware-clock core

The [pure core](../../sys/arch/arm/broadcom/bcm2712_display_clock.c) is a source foundation for four fixed profiles:
HSM firmware clock 13, BVB firmware clock 14, DISP firmware clock 16 and CORE firmware clock 4.
It shares the EmberBSD HSM packet validation, two-port minimum leases and fault policy.
The HSM facade is the first native consumer.
The [HSM compatibility facade](../../sys/arch/arm/broadcom/bcm2712_display_hsm.c) now calls this engine.
The [native HSM API](bcm2712-display-hsm.md), both public HSM structures and their headers remain unchanged.
The kernel selects the shared engine and native adapter with `bcmv3d_acpi`. No profile attaches automatically.

## Private interface

The private types and functions are in
[`bcm2712_display_clockvar.h`](../../sys/arch/arm/broadcom/bcm2712_display_clockvar.h).
Only fixed `BCM2712_CLOCK_HSM`, `BCM2712_CLOCK_BVB`, `BCM2712_CLOCK_DISP` and `BCM2712_CLOCK_CORE` profiles are valid.
There is no arbitrary firmware clock-ID or property-tag interface.

The caller supplies an exchange callback and its argument.
The callback finishes before each operation returns.
The caller serializes all operations and keeps the core and callback storage alive.
The native adapter supplies a kernel-resident callback and fixed per-profile owner slots.
This pure core does not implement native locking, a registry, IRQ-context checks or resource validation.
It performs no MMIO, reset, power-domain operation or automatic attachment.

`bcm2712_clock_init` reads state, rate and the current firmware maximum.
BVB, DISP and CORE also read the firmware minimum. Creation sends no SET.
An invalid profile, malformed reply or failed initialization leaves the output unchanged.
An off HSM, BVB or DISP clock with a zero initial rate is valid before acquisition. CORE requires a positive initial rate in either state.

`acquire` adds one nonzero minimum for port 0 or 1.
`update` changes an active port. `release` removes an active port.
Each operation preserves the other port's minimum and rechecks the current maximum.
The requested target is the maximum of the profile floor and both active minima.
The core sets the rate before it enables an off clock.
Firmware rounding must remain at least the target and no greater than the current maximum.
The core verifies state and rate before it changes a lease.
The caller must stop dependent hardware before release or finalization.

## Profile policy

| Profile | Unleased floor | Inherited high rate | Finalization |
|---|---|---|---|
| HSM13 | max(120 MHz, initial rate) | Preserved until the owner has changed the rate | Restores off only if initially off and enabled by this owner |
| BVB14 | Cached positive `GET_MIN_CLOCKRATE(14)` | First acquisition can reduce it to the effective minimum | Never sends `SET_CLOCKSTATE(0)` |
| DISP16 | Cached positive `GET_MIN_CLOCKRATE(16)` | First acquisition can reduce it to the effective minimum | Restores off only if initially off and enabled by this owner |
| CORE4 | max(positive firmware minimum, positive initial rate) | Frozen as the owner lifetime baseline | Never sends `SET_CLOCKSTATE(0)` |

CORE also serves CSI and bus consumers. Its creation requires a positive inherited
rate, including when the initial state is off. Its frozen floor is the maximum
of this rate and the positive firmware minimum. The floor must not exceed the
firmware maximum. A zero rate fails with `ERANGE`; no minimum-only fallback is
used. Create sends no SET. Acquisition activates an off clock after rate
verification. Update, release and finalization cannot lower it below this
baseline, and finalization never sends `SET_CLOCKSTATE(0)`. This conservative
BSD policy does not arbitrate unregistered consumers or prove that the clock
rate is sufficient for a display mode. The other profile policies are unchanged.

HSM creation and operation order match the existing BSD HSM core.
Its create transcript is GET_STATE, GET_RATE, GET_MAX.
BVB adds GET_MIN after those reads. A zero minimum or minimum above maximum fails before SET.
There is no guessed 75 MHz unleased floor and no initial-rate floor for BVB.
The BVB minimum is cached for the owner lifetime; the maximum is read again on each operation.
The lifecycle caller must exclude firmware and unregistered OS consumers before reducing an inherited rate.

BVB uses explicit lazy activation. Creating an off object does not enable the clock.
The first acquisition enables it after rate verification.
The last release requests the firmware floor and leaves it enabled.
Finalization never restores BVB's initial off-state.
An off BVB object with no acquisition can be finalized without any SET.
A positive firmware minimum is a local fail-closed policy; Linux does not reject a zero BVB minimum explicitly.

The pinned Linux provider gets BVB's MIN/MAX from firmware and marks it critical.
CCF enables critical clocks at registration and keeps a permanent reference.
Consumer minimum requests are combined with the provider floor.
Unlike CCF registration, this source API defers activation until explicit acquisition.
These are lifecycle policies, not evidence that firmware clock 14 has been controlled on hardware.

## Errors and status

Every reply must preserve size, response code, tag, capacity, returned length, clock ID and terminator.
Clock state bit 1 means the clock does not exist. Other reserved state bits are rejected.

A GET failure before the current operation's first SET does not poison the core.
A failure after a SET attempt or its readback latches a permanent fault.
A failed operation does not publish a new lease or remove an existing lease.
Finalization rejects leases and faults. It does not retry or restore an uncertain state.
The future native adapter must retain its registry entry and dependent resources on such failure.

`status` returns the cached floor, last verified state/rate, minima, lease mask, fault and mutation flag.
It sends no request and remains available after a fault.
It does not claim that stale verified state is current after an error.
The reported configured rate is not a measured frequency or a display-access guarantee.

## Source checks

```sh
sh ember/tools/bcm2712-display-clock-contract.sh "$PWD"
CLOCK_SKIP_MUTANTS=1 CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
    sh ember/tools/bcm2712-display-clock-contract.sh "$PWD"
```

The test compiles the actual shared core, HSM facade and a [frozen EmberBSD HSM reference](../tools/fixtures/bcm2712-hsm-reference.md).
The reference retains its exact source hash and is never selected by the kernel.
It removes only kernel includes and the RCS declaration for the portable fixture.
The firmware boundary is controlled. It does not model MMIO or native ownership.
Both implementations must match the reference return values, cached fields and complete request bytes.
The comparison covers transport errors and each packet-envelope field at every operation boundary.
The facade copies each field without structure casts. It exports fault and mutation state even after failure.
It checks inactive objects before it imports their fields. Failed initialization leaves the output unchanged.
Checks include initialization failures, both minima, critical BVB cleanup, current maximum changes,
rounding, malformed packet fields and every SET/readback failure boundary.
Causal mutants remove the corresponding guards.
`CLOCK_MUTANTS='facade-fault facade-floor facade-flags facade-ready'` selects only the facade controls.

`CORE4_ONLY=1` selects the focused CORE cases. `CLOCK_EVIDENCE` preserves portable input snapshots.
`CC`, `CFLAGS`, `CLOCK_COMPILE_ONLY=1` and `CLOCK_OUTPUT` support a cross-built fixture.
`TEST_RUNNER` can invoke a supported memory checker.
The actual kernel translation unit must also compile with the selected kernel's GCC16 flags and headers.
A portable fixture does not replace that check.
Native adapter checks, a linked coherent kernel, physical BVB control and PHY/KMS acceptance have separate boundaries.

## Sources

- Raspberry Pi Linux [CORE policy](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/clk/bcm/clk-raspberrypi.c#L83-L103).
  CORE4 is a fixed critical firmware clock. The frozen inherited-rate baseline above is BSD policy.
- Frozen EmberBSD [HSM reference](../tools/fixtures/bcm2712-hsm-reference.md).
- Raspberry Pi Linux a1a601d4857978d810175365550c4536701df4ed:
  [firmware clock policy](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/clk/bcm/clk-raspberrypi.c#L157-L161),
  [firmware MIN/MAX](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/clk/bcm/clk-raspberrypi.c#L375-L417),
  [CCF critical activation](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/clk/clk.c#L4050-L4069).
  Linux is a reference for ABI and policy facts. Its implementation is not copied into this BSD core.

DISP16 uses the same mechanism with a noncritical finish policy. Its two slots
are logical HVS clients, not HDMI ports. The [retained DISP module](bcm2712-disp-clock-probe.md)
imports the [builtin adapter](bcm2712-native-display-clocks.md), which has one shared
transport lock and typed HSM, BVB, CORE and DISP slots. It compiles no private clock
engine. All existing public layouts remain unchanged.
