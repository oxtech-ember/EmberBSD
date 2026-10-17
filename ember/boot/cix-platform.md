# CIX ACPI clock and reset providers

These native providers support the first bounded CIX P1 VPU resource path.
They are selected by EMBER64. Registration does not activate the codec.
The [VPU attachment](cix-vpu.md) still leaves power, clocks, reset, DMA and
firmware execution disabled until a checked consumer owns these resources.

## Clock provider

`cixclk_acpi` matches CIXHA010 with UID 0. It registers without calling GCLK
or CLKC. One exclusive claim supports clock ID 0x43. The API uses the native
CCLK AML methods rather than a second mailbox implementation.

GCLK returns a little-endian status and a 64-bit rate. CLKC returns a
little-endian status. The implementation checks the returned object, payload
bounds, minimum length and complete status word before reading a value.
Failures preserve the output. Diagnostics identify the method and exception
or firmware status, including timeout statuses 6 and 11, without dumping data.
No operation changes the clock rate. A query still executes AML and is not
passive namespace inspection.

Enable or disable failure can follow a hardware change. The claim stays
uncertain until disable succeeds. An uncertain claim cannot be released or
enabled again. Repeated confirmed enable or disable returns EALREADY without
calling AML. Callers must use thread context and serialize claim lifetime.

## Reset provider

`cixreset_acpi` accepts one exact RST0 profile: CIXHA020, UID 0, non-coherent,
one read/write fixed memory resource at 0x16000000 with length 0x1000. It
rejects extra or unsupported resource descriptors before mapping. Attachment
maps and registers only; it does not read or change reset registers.

Only two reset IDs are supported:

| ID | Offset and mask | Meaning |
| --- | --- | --- |
| 0x0e | 0x400, bit 11 | VPU reset, active low |
| 0x8e | 0x804, bit 19 | VPU RCSU reset, active low |

An exclusive claim does not access MMIO. Assert clears the owned bit;
deassert sets it. A sleep mutex covers the complete read, update, barrier
and readback. Other bits are preserved. Readback failure retains an uncertain
claim. Only a confirmed assert recovers it. Put accepts untouched or confirmed
asserted claims. A state query reads one owned bit without writes or changes
to ownership state; it cannot recover an uncertain claim.

The consumer owns power sequencing and reset timing. These providers do not
choose a VPU RCSU reset sequence, expose arbitrary register access, toggle
other reset IDs or implement general CIX clock and reset support. They are
built in with boot-long lifetime. Hot detach and suspend/resume are not supported.

## Source checks and limits

The [native PowerResource framework](acpi-power.md) now preserves shared
ownership and failed transitions. It is not a complete D-state engine.
The VPU control helper validates the running CLKT, RSTL, _PR0 and _PR3
packages before acquiring these providers. It is compiled with the kernel,
but the diagnostic attachment does not call it yet.

The helper supports a query with no power or reset operation. Its separate
active probe requires an already deasserted RCSU reset. It uses native power
ownership, confirms the power bit, enables the clock, pulses the main reset
and reads identity and geometry. Cleanup asserts reset before releasing
clock and power. Failed cleanup retains its context and reports a separate
error. A power attempt is pending rollback, not proof of a native lease.
An OFF readback fault after lease release requires diagnosis, not OFF replay.
This probe has no DMA or interrupts. Its cleanup cannot serve a DMA driver.
Power-bit confirmation does not establish memory repair or firmware readiness.

Run from the source root with GCC16:

```sh
CC=gcc-16 CFLAGS='-O2 -fanalyzer' CIXCLK_ANALYZE=1 \
  sh ember/tools/cixclk-contract.sh
CC=gcc-16 CFLAGS='-O2 -fanalyzer' CIXRESET_ANALYZE=1 \
  sh ember/tools/cixreset-contract.sh
CC=gcc-16 CFLAGS='-O1 -g -fsanitize=address,undefined' \
  sh ember/tools/cixclk-contract.sh
CC=gcc-16 CFLAGS='-O1 -g -fsanitize=address,undefined' \
  sh ember/tools/cixreset-contract.sh
CC=gcc-16 CFLAGS='-O2 -fanalyzer' \
  sh ember/tools/acpi-power-contract.sh
CC=gcc-16 CFLAGS='-O2 -fanalyzer' CIXVPU_CONTROL_ANALYZE=1 \
  sh ember/tools/cixvpu-control-contract.sh
```

The contracts compile all production functions with modeled OS services.
Clock checks exercise real response parsing, ownership, error recovery and
cause diagnostics. Reset fixtures use actual public ACPICA resource types;
AML byte decoding is outside that model. Two reset threads perform 8000
operations through a real host mutex. These are userspace source contracts,
not kernel execution or evidence of physical reset behavior.

GCC16 host contracts pass 780 clock checks and 383 reset checks. The source
also compiles with actual AArch64 kernel flags and GCC path analysis. Host
Cppcheck and Clang Static Analyzer cover the contracts. Target analysis
retains inherited header style findings and fixed callback-const suggestions;
the clock target CSA also reports four inherited bitops dead stores.
No diagnostic category was suppressed. The [CI workflow](../../.github/workflows/cix-platform.yml)
adds Linux ASan/UBSan and complete Memcheck runs. Check its exact-head result.
The power contract passes 75 assertions; the VPU control contract passes 379.
The power module retains inherited CSA dead stores in unchanged functions.
Those findings are recorded separately; its execution contract does not
claim clean analysis of the whole ACPI power module.

A complete kernel build, provider attachment, real clock transactions,
reset readback and codec execution are separate acceptance boundaries.
No physical provider operation is accepted by these source checks.

## Primary provenance

Hardware facts come from the pinned vendor
[ACPI clock implementation](https://github.com/radxa/kernel/blob/77736302973085a9d0ffb397ec4ee4ef795160f4/drivers/clk/cix/acpi_clk.c),
[reset register table](https://github.com/radxa/kernel/blob/77736302973085a9d0ffb397ec4ee4ef795160f4/drivers/reset/reset-sky1.c)
and [reset IDs](https://github.com/radxa/kernel/blob/77736302973085a9d0ffb397ec4ee4ef795160f4/include/dt-bindings/reset/sky1-reset.h).
The checked board firmware describes CCLK, RST0 and the VPU CLKT/RSTL references.
These original BSD providers do not include or relabel GPL implementation code.
