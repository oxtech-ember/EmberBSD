# AMD GPIO controllers without ACPI events

ACPI 6.6 section 5.6.5.1 makes GPIO-to-ACPI event mappings optional.
A controller can provide peripheral `GpioInt` resources without an `_AEI` object.
See the [ACPI event model](https://uefi.org/specs/ACPI/6.6/05_ACPI_Software_Programming_Model.html#declaring-gpio-controller-devices).

`acpi_event_create_gpio` walks `_AEI`. Its `AE_NOT_FOUND` result must not stop
AMD GPIO attachment. The driver must still establish its interrupt, attach the
GPIO child and register the ACPI GPIO provider. Other event-creation errors
keep the existing diagnostic and abort path.

This is an AI-assisted EmberBSD correction to the NetBSD driver at
[`b644118641c7cc03cebcec227cb89cc7ef0590ad`](https://github.com/NetBSD/src/commit/b644118641c7cc03cebcec227cb89cc7ef0590ad).
The [OpenBSD driver](https://github.com/openbsd/src/blob/4401108cce3a58e8f9335d6744f07fc25e9728e4/sys/dev/acpi/amdgpio.c)
also registers its GPIO provider without requiring an event map.
Its [ACPI registration helper](https://github.com/openbsd/src/blob/ec875f0e6c743bef3095e3d151fb39f85048d450/sys/dev/acpi/acpi.c)
handles the GeneralPurposeIO region before it checks `_AEI`.
No OpenBSD implementation code is copied.

Run `ember/tests/amdgpio-optional-aei-contract.py` with explicit `HOST_CC`
set to host GCC 16. It executes the production attach tail with counted helpers.
Nine cases cover present and absent events, real errors, interrupt failure,
and a missing GPIO child. Two controls restore the old gate or ignore all errors.
Both controls must exit 1 with the expected diagnostic.
The model accesses no registers and evaluates no AML methods.
Full kernel builds, GPIO interrupts and peripheral operation need separate acceptance.
