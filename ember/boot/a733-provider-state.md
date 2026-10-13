# A733 read-only provider observations

These interfaces prepare a firmware-ready GPU identification consumer. They
observe the existing configuration; none enables the GPU, changes a supply,
programs a PLL, releases reset, or establishes hardware acceleration.

## Supply

Use the acquired supply's
[regulator state query](regulator-state.md). AXP8191 reads its enable bit and
preserves I2C errors. Its voltage query returns the programmed setting,
not a physical measurement. Querying or acquiring a regulator does not reserve
it against another consumer.

## Hardware oscillator

`sunxi_rtc_dcxo_query(rtc_phandle, &state)` reads A733 RTC `+0x160`
twice under the RTC mutex. It uses the existing mapping and registers before
the clock-less RTC attachment returns. Only the native A733 compatible, a
mapping covering the register, and a unique phandle can register. Missing
providers return `ENXIO`, invalid arguments return `EINVAL`, and different
samples return `EBUSY`. Errors leave the output unchanged. The result contains
both raw words and the hardware-classified oscillator rate: status bits 15:14
map 0/3 to 24 MHz, 1 to 19.2 MHz, and 2 to 26 MHz. This is a hardware status
classification, not an independent frequency measurement. No register is written.

The CCU's local `netbsd,dcxo-source = <&rtc>` binding requires exactly one
phandle referencing the native A733 RTC. It is an EmberBSD observation binding,
not an upstream clock-tree replacement. Absent/malformed references fail with
`EINVAL`; a different compatible fails with `EOPNOTSUPP`. A kernel without RTC
support returns `ENXIO`. There is no fixed-clock fallback. Existing global
`hosc` providers and their consumers are unchanged.

The status table comes from the pinned BSP's `ccu-sun60iw2-rtc.c`, revision
`2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f`, SHA256
`484ef6c6cbdac224484f22ca57db8063e2f83404bfe18c18e32c12ab1bc0a3ea`.
Linux commit [`0a136efc0fc2d53b1b1389b3c3fb0f76edca38bf`](https://git.zx2c4.com/linux-rng/commit/?id=0a136efc0fc2d53b1b1389b3c3fb0f76edca38bf)
describes automatic oscillator detection and uses these bits as a read-only
divider selector, including the 0/3 alias. RTC attachment prints one bounded
observation independently of GPU readiness.

## Power domain

`fdtbus_powerdomain_is_enabled_index(phandle, index, &enabled)` is a strict
query of one `power-domains` entry. Missing providers return `ENXIO`; providers
without the optional `pdc_get` callback return `EOPNOTSUPP`. Invalid arguments
or malformed specifiers return `EINVAL`; a missing index returns `ENOENT`.
Errors leave the result unchanged. The automatic attach firmware fallback does
not apply to this query, and no enable callback is called.

PCK600 reads policy, emulation and status twice under its existing mutex.
Only matching, stable static ON (8) and OFF (0) produce a boolean result.
An inconsistent snapshot returns `EBUSY`; dynamic policies, emulation and
other modes return `EOPNOTSUPP`. A previously failed transition remains
quarantined and returns `EIO` without MMIO. A locked static policy can be
observed. Queries never alter timing registers or clear quarantine.
At successful PCK attachment, an internal observation of GPU_CORE domain 6
prints two complete PWPR/PMER/PWSR samples and their classification. This uses
the same locked reader as the strict state API. An unknown or unstable mode
is reported with its error, never as OFF. The diagnostic neither changes the
GPU consumer's TOP5 binding nor proves GPU readiness; it performs no transition.

## GPU register clock path

`sun60i_a733_ccu_gpu_ready(gpu_clock, &core_hz, &bus_hz)` accepts the native
A733 GPU module clock handle. Both results are published only on success.
It verifies two equal snapshots of the eleven required registers, then checks the GPU module gate
and update bit, bus gate, deasserted GPU reset, and GPU AHB master gate.
Missing handles or the oscillator provider return `ENXIO`; a foreign clock
provider returns `EOPNOTSUPP`. Changing or inactive required hardware returns
`EBUSY`. Invalid or aliased output pointers return `EINVAL`.

`sun60i_a733_ccu_gpu_inspect(gpu_clock, &state)` separates observation from
readiness. A nonzero return leaves `state` unchanged. Zero means a complete
observation: `readiness_error` still determines whether the path is ready.
The result includes two samples of thirteen CCU registers, their changed-bit
mask, two fixed-hosc rates, bracketing hardware DCXO observations, and the
first failed condition. Derived core and
bus rates are valid only when `readiness_error` is zero; otherwise they are
zero. The existing `gpu_ready` wrapper retains its errors and output rules.

After handle/provider validation, inspection rejects an FDT mapping shorter than
`0x198c`, queries RTC, performs exactly 26 CCU reads, then queries RTC again: four RTC reads in total. Both queries
must succeed. A failure of either query leaves the complete inspection output
unchanged, including after the CCU reads. Different required CCU samples take priority,
followed by changed DCXO raw status/rate, changed fixed-hosc diagnostic rate,
and the existing gate/reset/PLL checks. There are no retries or writes.
Matching endpoints do not reserve either provider or exclude intervening changes.

The two additional registers are `PERI0PLL_GATE_EN` at `+0x1908` and its
status at `+0x1988` (A733 User Manual V1.00, sections 4.1.6.249/258).
Bits 27:16 decode configured and effective branch gates; control bits 11:0
report automatic-gating bypass. In the normalized masks, 400M is bit 1,
400M_ALL bit 2, 600M bit 9 and 800M bit 10. Both raw samples and decoded
masks are printed. These additional observations are diagnostic only: their
changes remain in `changed`, but do not alter the previous eleven-register
readiness or reservation comparisons. They do not establish power readiness.

PLL_REF normalization uses the hardware-classified 19.2/24/26 MHz DCXO value,
never fixed-hosc. Exact integer arithmetic must prove a 24 MHz reference
before any derived rate is published. For example, the physical #5 REF
`0xf8675f00` describes N=96, M=104, P=1: 26 MHz normalizes to 24 MHz;
24 MHz does not. The matched #6 boot on Zero 3W reports stable RTC status
`0x183fb0f7` before and after CCU reads, classifying its DCXO as 26 MHz.
This confirms the input classification for that snapshot, not an independent
frequency measurement. Fixed-hosc remains diagnostic only.

It decodes integer PLL_GPU0 and PLL_PERIPH0 input and
output dividers, lock/enables, and the selected output gate. Both SDM enable
locations must be clear for each required PLL. All six GPU parents and the
five documented GPU dividers are supported. AHB may use SYS24M or the decoded
peripheral 600M branch; LOSC/IOSC readiness is unsupported. Unsupported modes
or fractional-Hz results return `EOPNOTSUPP`; rates beyond `u_int` return
`ERANGE`. Nominal clock names do not determine the returned frequencies.

The register facts come from the [pinned BSP and Linux v5 submission](a733-accelerator-clocks.md#sources-and-validation).
In particular, PLL_PERIPH0's input divider and separate output gates are
included here; the generic clock tree's rate calculation is not used.

CCU observations do not serialize existing generic clock/reset writers.
Equal snapshots cannot detect every intervening change or reserve resources
against a later change. A future consumer must complete all supply, power and
clock checks before accelerator MMIO and must not use this result to justify
active resource ownership or cleanup. MBUS/DMA, IRQs, firmware, GPU_CORE policy,
and cold-start sequencing remain separate work.

## Software checks

Run `a733-rtc-contract.sh`, `regulator-state-contract.sh`, `a733-power-contract.sh`,
`fdt-power-attach-contract.sh`, and `a733-accelerator-clock-contract.sh` from
`ember/tools`. They execute the production bodies with fake I2C/MMIO and
check success, failure propagation, state preservation, stable reads, and
absence of writes during queries. RTC checks execute the full production
driver, including registration before its clock-less early return. CCU checks
bracket ordering, query errors, normalization for all supported rates, and a
build without RTC support. The CCU contract also checks each refusal
reason, simultaneous failures, complete differing samples, unchanged outputs
on acquisition errors, and the compatibility wrapper. Existing transition and
attach tests remain controls. Passing these contracts and focused
cross-compilation is separate
from physical identification or accelerator workload acceptance.

The post-PLL extension passes 18,245 clock assertions and 1,212 consumer
checks on macOS, including ASan/UBSan. Fourteen compiled preparation mutants
are rejected. The no-RTC build rejects inspection before MMIO. These are
software contracts; the cross-built fixtures also pass on Zero 3W CPU.
The [physical experiment](a733-gpu-identification.md#physical-result) confirms
open post-PLL gates before preparation and after CORE timeout. GPU identity
and accelerator execution remain unverified.
