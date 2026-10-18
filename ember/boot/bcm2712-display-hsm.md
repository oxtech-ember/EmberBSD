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
The [shared native adapter](bcm2712-native-display-clocks.md) also provides explicit BVB14 ownership.
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

The HSM test compiles the actual [shared clock engine and HSM facade](bcm2712-display-clock-core.md),
and the shared native adapter. The public HSM structures and signatures are unchanged.
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
These source checks do not establish physical HSM control or a display access lifetime.

## Finite opt-in module

[`bcm2712_hsm_probe`](../../sys/modules/bcm2712_hsm_probe) runs one explicit
native-provider cycle at module load. It is not part of a default kernel or
module set. Cross-build it with GCC16 and the selected kernel's build tools.
Set `EMBER_KERNEL_OBJDIR` to that kernel's configured object directory.
The Makefile reads its real preprocessing and DRM flags, generated option
headers, source include paths, and warning policy. Generic DRM module defaults
can select different architecture and legacy configuration definitions.
Record matching source, headers, options, build settings and strong symbols.
Buildinfo alone does not prove ABI. The optional [ABI sample](../tools/bcm2712-hsm-probe-abi.c)
checks three configuration definitions and selected DRM, ACPI, HSM, BVB, DVP and bus_space layouts.
It does not replace source, option or symbol audits. Use the selected profile.

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

The default HSM-only mode does not access display MMIO.
The explicit [BVB0 clock probe](bcm2712-bvb-clock-probe.md) also sends only native firmware-clock requests. The explicit
[HPD0 sample](bcm2712-display-dvp.md#finite-hpd0-sample) reads one controller register.
The explicit [EDID0 transfer](bcm2712-display-edid-probe.md) requires
`BCM2712_DISPLAY_DDC`. The separate [HPD IRQ0 probe](bcm2712-hpd-irq-probe.md) waits for an IRQ-driven
HPD level cycle through the native AON owner. No mode resets hardware. The framebuffer gate does not establish
a general firmware clock lifetime. Run the actual-source host contract with:

```sh
sh ember/tools/bcm2712-hsm-probe-contract.sh "$PWD"
```

The fixture uses the real decoder, HSM/DVP cores/providers, module and libfdt.
It controls the BVB and DDC API boundaries; separate contracts test the native DDC and I2C code.
Controlled kernel services test early refusal, malformed trees, zero rate,
inherited state, failures, retained ownership, cleanup and exact HPD/DDC MMIO order.
`CC`, `CFLAGS`, `NM`, `HSM_PROBE_COMPILE_ONLY=1`,
`HSM_PROBE_OUTPUT` and `TEST_RUNNER` support cross-built and memory-checked tests.
`HSM_PROBE_SKIP_MUTANT=1` runs only the positive source contract. Both DDC feature
configurations run by default. `HSM_PROBE_FEATURES=off` selects the empty fixture
option header; it checks early `EOPNOTSUPP` and absence of DDC symbols.
For cross compile-only checks, set `CC` to GCC16 and `NM` to the matching
`aarch64--netbsd-nm`; the host nm cannot inspect target ELF symbols.
Host execution does not establish physical acceptance.

### Explicit shared-DVP modes

Absent `clock-mode`, `dvp-mode`, `hpd-mode` and `ddc-mode` properties keep the HSM-only cycle above.
Empty, unknown, non-string or conflicting modes fail before HSM, mapping and MMIO.
`dvp-mode=observe` and `dvp-mode=gate0` use the same headless, framebuffer,
render and selected-kernel guards. Both acquire and hold a verified HSM lease
throughout the DVP operation. A completed earlier HSM cycle is not a held lease.

First request `observe` on a controlled cold-headless boot:

```sh
modload -b cold-headless=true \
    -s "expected-buildinfo=EmberBSD:${kernel_commit}" -s dvp-mode=observe \
    /absolute/path/bcm2712_hsm_probe.kmod
```

This mode maps only resource 10, `0x107c700000/0x10`, and calls native DVP
create/status/destroy before unmapping. Create reads SW_INIT and MISC_CONFIG.
Status is cached. Observe performs two DVP reads and no DVP write. It still
uses the HSM provider, which can change firmware clock state or rate.

Review and accept that separate physical observation before an explicit
`-s dvp-mode=gate0` invocation. No mode automatically advances to gate0 or
falls back to observe. Gate0 acquires and releases port 0 through the native
DVP provider, with status at each stage. It performs at most ten DVP reads
and two gate writes. Both DVP modes reject an asserted port 0 reset with
`EBUSY`; neither mode reports successful observation in that state.
Reset bits and the per-controller DVP apertures are never written or mapped.

Both modes use a normal-active-SoC prior-art assumption, as described in the
[DVP access boundary](bcm2712-display-dvp.md#power-and-reset-limits). The first
create is controlled physical MMIO acceptance, not pre-proven ACPI accessibility.
Native bus_space cannot recover a stalled transaction or synchronous abort.
These modes do not claim an ACPI power contract or a general display lifetime.

Successful cleanup destroys DVP, unmaps resource 10, then releases and destroys
HSM. No freed provider is read. Any uncertainty after HSM acquisition stops the
cycle and retains the remaining HSM owner/lease, mapping and native DVP owner.
The log identifies the failed stage and remaining mapping/owner. Retained
samples carry an explicit validity flag and their last successful status stage.
DVP samples are labeled pre-failure; HSM samples are labeled cached. The current
fault and mutation state are unknown after a failed call. A failed status call
is never retried. No native
provider retains a callback into module text. Unload does not clean up retained
state; controlled reboot is required. No failure prints finite-cycle success.

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

The same cold-headless boot then passed separate shared-DVP `observe` and
`gate0` cycles with module source `3d4420f3`. Before loading, 346 source
dependencies, 30 generated files, 40 sampled configuration/layout values and
all strong external symbols matched the selected kernel. Observe read the
shared SW_INIT and MISC_CONFIG registers. Both gate and reset samples were
zero. Gate0 acquired port 0 with lease mask 1, then released it with mask 0.
The gate and reset samples remained zero, with no fault or mutation.
Both modes held HSM at 120 MHz until DVP destruction and unmapping completed.
Both HSM owners were then released and destroyed, and both modules unloaded.

The firmware had already enabled the gates. These cycles made no DVP writes
and do not accept the physical gate-write path. The temperature after gate0
was 50.469 C, without reported throttling or undervoltage. This accepts finite
shared-resource access and inherited-state ownership on this boot. It does
not establish per-controller accessibility, power/reset control, a general
display lifetime, HPD, DDC, KMS, hotplug, or a physical Plasma session.

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

The separate [HD observation mode](bcm2712-hd-observe-probe.md) samples shared HD
and both controller clock-stop words while inherited HSM and both DVP leases stay held.
It does not bootstrap a zero HSM rate or write controller registers.
