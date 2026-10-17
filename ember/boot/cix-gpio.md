# CIX S5 GPIO ACPI events

`cixgpio` supports one bounded ACPI profile: GPI4, HID `CIXH1003`, UID4,
32 pins, bank ID0, memory `0x16004000/0x1000`, GSI404, and one `_AEI`
interrupt on pin6. It does not attach the other GPIO banks or expose a
general-purpose GPIO interface. The hardware is Cadence GPIO r1p02.

EMBER64 selects **armed mode**, `flags 0x1`, for this bounded profile.
Set `flags 0x0` to repeat the passive check before arming another sample.
A passive attachment validates the
resource profile, maps that resource, prints nine register values, and
unmaps it. It performs no register writes, IRQ installation, event method
execution, or clock/reset operation. The passive snapshot excludes
`IRQ_STATUS` at offset0x20 because reading it clears pending status.
Mapping success alone does not prove that firmware left the clock accessible.
The passive physical check below establishes this boundary on one sample.

The selected `flags 0x1` arms the event after the same snapshot.
It requires pin6 to be an input with bypass off and requires all other
interrupts in the bank to be masked. It preserves pin direction, output,
pull, mux, and every unrelated register bit. Setup disables pin6, sets its
level-low interrupt configuration, checks that configuration, installs the
parent interrupt and completion state, and enables pin6 last. A failed
setup leaves pin6 disabled. An armed attachment has boot-long lifetime;
hot detach, suspend/resume and wake-from-sleep are not implemented.

The hard interrupt reads STATUS once and schedules the standard ACPI
event. The parent interrupt stays masked while `_L06` runs in the ACPI
worker. The existing firmware method calls the EC event routine, which
can produce `Notify(PWRB,0x80)`. The existing ACPI button driver passes
that notification to sysmon. Userspace powerd owns the shutdown policy.
No synthetic notification or EC polling replaces the GPIO interrupt.

The optional ACPI event completion callback owns rearming. Queue failure
calls it synchronously, possibly in hard interrupt context. AML completion
calls it in the worker. The callback does not sleep. Pending/fault state
and rearming share an interrupt-safe mutex. A duplicate interrupt cannot
queue a second AML job. Queue or AML failure latches a visible fault and
leaves the parent masked. A high input resets the assertion budget.
32 consecutive completions with a low input latch a fault. This is a
service budget, not a vendor timing guarantee. Existing event users that
do not install this callback retain their previous rearm behavior.

Firmware owns the S0 clock for this stage. The vendor DTS describes the
S5 GPIO APB clock as fixed100MHz and has no S5 GPIO reset. The saved ACPI
CLKT ID0x106 names FCH GPIO APB in the vendor clock header. The driver
does not resolve this discrepancy by modifying a different clock domain.
It performs no CLKT, SCMI, SMC, power-resource or reset calls.

EC AML owns its I2C transactions. Successful AML execution alone does not
prove that an EC transport transaction succeeded or that a button event
was delivered. The firmware contains busy loops; the ACPICA loop timeout
does not provide a short driver deadline. Physical acceptance must record
the GPIO IRQ, AML completion, PWRB/sysmon event, userspace action and final
power state separately.

## Passive physical check

On 2026-10-17, a passive boot on the observed CIX P1 CD8180 sample passed
with source `9571b130e`. All nine snapshot reads completed without a panic
or MMIO fault. BYPASS was `0`, DIRECTION was `0x226826c1`, INPUT was
`0x38783ffe`, and MASK was `0xffffffff`. Pin6 was an input, high and not
bypassed; every bank interrupt was masked. The disk-root fsck was clean,
and native powerd started automatically. No synthetic event was injected.
That passive trial accepts register access only.

With source `522474e4d`, two physical short presses later reached native
powerd, disk synchronization and FFS unmount. The operator reports fan stop,
and the next boot reports clean FFS. The earlier shutdown utilities take about
15–20 seconds. Restart by a short press after shutdown is unresolved: the
operator reports that power must be removed and restored. These observations
do not qualify the final firmware wake-source configuration.

On 2026-10-18, another physical short press with the same kernel and GCC16
utilities from `2231dbb6c` passes sync and FFS unmount. The installed console
wrapper is from `3794f23aa`; the profile head is `df31d0139`. Host UART arrival
timestamps record 4.05 s from the shutdown reason to completed unmount. This
excludes press-to-policy latency and firmware completion. The operator measures
about 5 s from press to fan stop and accepts that duration. Restart by short
press still fails. After DC restoration, the next unattended boot reports clean
FFS and restores WPA2/DHCP. The persistence marker and installed file hashes
match. Electrical rail state remains unmeasured. No shutdown hook or filesystem
step was bypassed.

## Source contracts

Run from the source root with GCC16:

```sh
CC=gcc-16 sh ember/tools/cixgpio-contract.sh
CC=gcc-16 CFLAGS='-O2 -fanalyzer' sh ember/tools/cixgpio-contract.sh
CC=gcc-16 CIXGPIO_ANALYZE=1 sh ember/tools/cixgpio-contract.sh
CC=gcc-16 \
  CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  CIXGPIO_RSMISC_CFLAGS='-O1 -g -fsanitize=address -fno-omit-frame-pointer' \
  sh ember/tools/cixgpio-contract.sh
```

The runner compiles the actual driver and ACPI event functions. It removes
only kernel includes and supplies modeled kernel/namespace/bus services.
It compiles the fork's ACPICA resource decoder and feeds the
saved literal resource bytes through that decoder and the production
resource callbacks. Register behavior includes write-one enable/disable
and clear-on-read STATUS. Tests cover passive access, malformed or foreign
resources, ownership rejection, duplicate/changed event creation, setup
failure, immediate completion, duplicate IRQ entry and masked failures.
`TEST_RUNNER` names one executable, such as a Memcheck wrapper.
`CIXGPIO_COMPILE_ONLY=1` builds without running. `CIXGPIO_TEST_OUTPUT`
retains the test executable. These contracts do not access hardware.

The decoder bounds each header and declared descriptor length before
reading resource fields. Tests call `AcpiBufferToResource` with every prefix
of both resource templates, oversized lengths and truncated large headers
and EndTags. The direct AML walker has the same boundary tests. Whole
descriptors without an EndTag retain the public decoder's existing behavior.

The host parser uses ACPICA's `ACPI_MISALIGNMENT_NOT_SUPPORTED`
configuration for bytewise conversion. This is an explicit platform
configuration difference; it does not make the target parser branch
identical. The unchanged `rsmisc.c` also contains direct unaligned integer
loads. The sanitizer command above instruments only that file with ASan.
All other decoder files, including the changed `rscalc.c` and `utresrc.c`,
and the driver, framework and harness retain full ASan+UBSan. `rsmisc.c` is
not claimed to be UBSan-clean. No sanitizer category is suppressed. Linux Memcheck
also covers the complete contract. Its CI result must be checked separately.

## Primary provenance

Register facts and the HID mapping come from the
[vendor Cadence driver](https://github.com/radxa/kernel/blob/77736302973085a9d0ffb397ec4ee4ef795160f4/drivers/gpio/gpio-cadence.c).
The [vendor DTS](https://github.com/radxa/kernel/blob/77736302973085a9d0ffb397ec4ee4ef795160f4/arch/arm64/boot/dts/cix/sky1.dtsi)
identifies S5 GPIO0, its resource and clock. Original
[Cadence upstream code](https://github.com/torvalds/linux/blob/ffc253263a1375a65fa6c9f62a893e9767fbebfa/drivers/gpio/gpio-cadence.c)
and the vendor implementation both specify clear-on-read STATUS with no
ACK register. No GPL implementation was copied into the BSD driver.
The [CIX firmware memory map](https://github.com/cixtech/edk2-platforms/blob/1a48c6523a3225f3ef01b1c91eb3e3dc0dd1857f/Silicon/CIX/Sky1/Include/MemoryMap.h)
independently identifies GPIO0_S5 at this resource.
