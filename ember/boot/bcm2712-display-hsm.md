# BCM2712 HSM clock ownership

The HSM provider controls firmware clock ID 13 for the two HDMI controllers.
It has an explicit lifecycle. It does not attach at boot or access display registers.
It does not implement HPD, DDC, power domains, reset control, or KMS.
The [resource contract](bcm2712-display-contract.md) remains a prerequisite.

## Resource and clock interfaces

`bcm2712_display_acpi_resources` validates the versioned GPU0 ACPI resources.
It preserves the `_DSD`, `_STA`, `_CCA`, and `_CRS` checks.
It can return valid resources when HSM has a zero rate after boot without HDMI.
It neither reads nor changes firmware clocks. Failure leaves the output unchanged.
The existing `bcm2712_display_acpi_query` still requires resources and an enabled clock snapshot.

The native API is declared in `bcm2712_display_acpi.h`.
Its status type is declared in `bcm2712_display_hsm.h`.

1. Call `bcm2712_display_hsm_create` from the GPU0 process-context owner.
   Creation validates resources and reads the initial state, rate, and maximum.
   Creation does not change firmware state. Only one native owner can exist.
2. Call `bcm2712_display_hsm_acquire` for port 0 or 1 with a nonzero minimum in Hz.
   The floor is at least 120 MHz and never below the initial firmware rate.
   The provider also preserves the minimum requested by the other active port.
3. Call `bcm2712_display_hsm_update` to change an existing port's minimum.
   Requests above the current firmware maximum fail before any write.
4. Stop that port's dependent hardware before `bcm2712_display_hsm_release`.
   Release preserves the other port's floor and does not disable HSM.
5. Stop all consumers and calls before `bcm2712_display_hsm_destroy`.
   Destroy rejects active leases. It never disables an inherited enabled clock.
   If this owner enabled an originally disabled clock, destroy restores its off-state.
   It preserves a nonzero safe rate instead of restoring an initial zero rate.

All operations reject hard and soft interrupt context before sleeping.
One sleepable mutex serializes ownership, both ports, and property exchanges.
Callers must keep the provider lifetime valid. A pointer is invalid after successful destroy.
Do not use another OS writer for HSM while this owner exists.
These leases do not prevent an external firmware actor from changing the clock.

## Verified state and errors

The transport calls the existing bounded, serialized `bcmmbox_request` API.
SET_RATE uses a 12-byte value buffer; its response contains 8 bytes.
All replies must preserve the packet length, tag, buffer capacity, clock ID, and end tag.
State bit 1 means that the clock does not exist. It is not a power-state wait flag.

The provider sets the requested rate before enabling HSM.
It checks both the SET_RATE response and GET_RATE before SET_STATE.
It checks state and rate again before publishing a lease.
Firmware rounding is allowed only when the result meets the requested floor and maximum.
No error path publishes a new lease or removes an existing lease.

A GET failure before the current operation's first SET does not imply a clock mutation.
It returns the error without faulting the owner. The caller can retry that read-only failure.
After any SET attempt, an error can leave firmware state uncertain.
The provider records a permanent fault and retains ownership, including after failed destroy.
It does not restore state blindly, clear mailbox quarantine, or allow another owner to retry.
Recovery from this state requires a separately controlled reboot.

`bcm2712_display_hsm_status` copies the last verified state and the fault under the mutex.
Its `mutation_attempted` field records whether any SET was attempted during the owner lifetime.
A failed readback does not update the last verified state.
Status is diagnostic data, not proof that display MMIO is safe.
GET_RATE reports firmware's configured rate, not a measured frequency under throttling.
Power, reset, and actual measured clock requirements need their own validation.

## Source validation

```sh
sh ember/tools/bcm2712-display-contract.sh "$PWD"
sh ember/tools/bcm2712-display-hsm-contract.sh "$PWD"
HSM_SKIP_MUTANTS=1 CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
    sh ember/tools/bcm2712-display-hsm-contract.sh "$PWD"
```

The HSM test compiles the actual core and native adapter.
Only ACPI, mailbox, allocation, and kernel synchronization boundaries are replaced.
The adapter test uses a real host pthread mutex and concurrent port acquisition.
The resource test separately compiles the actual ACPI parser.
The tests cover cold zero rate, inherited state, two port minima, malformed replies,
transport errors before and after SET, readback failure, retained fault ownership, and cleanup.
Mutants remove safety checks to prove that the tests detect their absence.

`CC`, `CFLAGS`, `HSM_COMPILE_ONLY=1`, and `HSM_OUTPUT` support a cross-built fixture.
`TEST_RUNNER` can invoke a supported memory checker.
The portable kernel dispatcher runs the resource, IRQ, and HSM contracts.
The build-orchestration fixture checks their dispatch and failure cleanup.
GCC 16.2 compiles the affected AArch64 kernel translation units with the real kernel headers.
GCC path analysis reports no findings in those units.
Clang Static Analyzer and Cppcheck retain inherited header findings; the HSM source adds none.
The host fixture passes ASan and UBSan. Valgrind is unavailable on the tested Darwin AArch64 host.
Host execution does not establish physical clock control or HDMI operation.
These source checks do not establish physical HSM control or a display access lifetime.

## Finite opt-in module

[`bcm2712_hsm_probe`](../../sys/modules/bcm2712_hsm_probe) runs one explicit
native-provider cycle at module load. It is not part of a default kernel or
module set. Cross-build it with GCC16 and the selected kernel's build tools.
Set `EMBER_KERNEL_OBJDIR` to that kernel's configured object directory.
The Makefile reads its real preprocessing and DRM flags, generated option
headers, source include paths, and warning policy. Generic DRM module defaults
can select different architecture and legacy configuration definitions.
Record matching source, headers, options, build settings and strong kernel
symbols before loading. A buildinfo string comparison alone does not prove ABI
compatibility. The optional [ABI sample](../tools/bcm2712-hsm-probe-abi.c)
compares three configuration definitions and selected structure layouts. It
does not replace the source, option or symbol audit. Do not load a module built
against a different kernel profile.

The operator must first shut down, remove HDMI, and perform a controlled cold
boot. A disconnected cable on an already running system is not this prerequisite.
Load the selected module only after the V3D render device has initialized:

```sh
modload -b cold-headless=true \
    -s "expected-buildinfo=EmberBSD:${kernel_commit}" \
    /absolute/path/bcm2712_hsm_probe.kmod
```

Set `kernel_commit` to the verified selected kernel commit before this command.
Use the exact selected kernel buildinfo without its final newline. The module
rejects a mismatch before any HSM call. It also rejects an early framebuffer,
any genfb or wsdisplay device, a missing or invalid FDT, and every framebuffer
descriptor, including a disabled descriptor. These refusal paths send no HSM
request. The module holds a reference to the registered V3D render object and
checks that its device is the ACPI `\_SB.GPU0` device.

The cycle creates the native owner, records initial status, acquires port 0 at
120 MHz, records acquired status, releases port 0, records status before destroy,
and destroys the owner. Status is the last verified cached state, rate, lease
mask, fault and mutation flag. Successful destroy frees the owner; the module
does not read it afterward. An error after any SET attempt or active lease
retains the native owner and requests a controlled reboot. It never retries,
restores an unknown clock state, or clears mailbox quarantine. Empty unmutated
owners can be destroyed during failure cleanup. Module unload performs no HSM
operation and cannot clear a retained owner.

The module does not access display MMIO or perform HPD, DDC or reset operations.
Its framebuffer gate is conservative; it does not establish a general firmware
clock lifetime. Run its actual-source host contract with:

```sh
sh ember/tools/bcm2712-hsm-probe-contract.sh "$PWD"
```

The fixture uses the real HSM core, native provider, module body and libfdt.
Only kernel services are controlled. It checks refusal before HSM calls,
malformed trees, zero rate, inherited state, every transaction failure, retained
ownership and cleanup. `CC`, `CFLAGS`, `HSM_PROBE_COMPILE_ONLY=1`,
`HSM_PROBE_OUTPUT` and `TEST_RUNNER` support cross-built and memory-checked tests.
`HSM_PROBE_SKIP_MUTANT=1` runs only the positive source contract.
Host execution does not establish physical acceptance.

## Physical acceptance boundary

One CM5 Rev1.0 with BCM2712D0 passed the finite module cycle after an
operator-confirmed cold boot without HDMI. The kernel was `0562cd54`,
the firmware source was `c90c2a6c`, and the module source was `faa8e933`.
The selected kernel headers, options, sampled layouts and strong symbols
were checked before loading. The connected-console control returned `EBUSY`
before all HSM calls. Both loads kept the module compatibility checks enabled.

| Last verified stage | State | Configured rate | Leases | Fault |
| --- | --- | --- | --- | --- |
| Initial | 1 | 0 Hz | 0 | 0 |
| Acquired | 1 | 120000000 Hz | 1 | 0 |
| Released, before destroy | 1 | 120000000 Hz | 0 | 0 |

Destroy returned zero. The module was then unloaded without an HSM operation.
No provider access occurred after destroy. The temperature immediately after
the cycle was 49.370 C, without reported throttling or undervoltage.
This is one finite request/readback check. It does not measure the clock,
validate display MMIO accessibility, or establish a general display lifetime.
It does not accept suspend, HPD, DDC, scanout, KMS, or a physical desktop session.

## Sources and next hardware boundary

The hardware floor and shared-clock facts come from Raspberry Pi Linux
[`a1a601d4857978d810175365550c4536701df4ed`, `clk-raspberrypi.c`](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/clk/bcm/clk-raspberrypi.c).
The firmware ABI follows the [mailbox property interface](https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface#clocks).
The implementation uses those interface facts. It does not copy Linux driver code.

Further hardware acceptance must use the declared firmware resources and a coherent kernel.
Implement and validate controller accessibility, power, reset, HPD, and DDC ownership separately.
The [L2 interrupt provider](bcm2712-display-irq.md) supplies a separate interrupt interface.
The [DVP gate owner](bcm2712-display-dvp.md) manages the separate per-port gates without reset writes.
Runtime KMS, scanout, IOMMU, and firmware-console handoff remain separate work.
A733 uses a different clock and power backend. Do not reuse firmware clock ID 13 there.
