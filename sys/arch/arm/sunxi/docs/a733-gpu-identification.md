# A733 GPU staged diagnostics

The native `sun60igpuid` consumer identifies an A733 GPU after strict
CORE ON/Q confirmation.
The current A733 DT enables its diagnostic `netbsd,observe-only` stage, which
never maps or reads GPU registers, even when the queried providers are ready.
This default mode provides no GPU initialization, DMA, submission or acceleration.
The [driver](https://github.com/oxtech-ember/EmberBSD/blob/main/sys/arch/arm/sunxi/sun60i_a733_gpu.c)
is owned by EmberBSD. Software contracts, the complete GCC16 kernel build
and physical attachment on Zero 3W are verified. The explicit runtime
PLL600 cold start now identifies the physical GPU; see the
[current hardware result](#physical-pll-result). Normal board DTBs remain
observe-only. The earlier PERIPH0-400M path times out at GPU_CORE and can
stall on GPU register access. The consumer now blocks that access after failed
CORE confirmation; this does not repair power-domain activation.
Explicit stage 15 has separately
[booted the pinned firmware on Zero 3W](a733-gpu-fwboot.md#physical-bootstrap-result).
Stage 16 has [completed one firmware command](a733-gpu-fwprobe.md#physical-command-result).
Stage 17 adds a software-checked MMUCACHE probe. GPU memory isolation
and graphics acceleration remain unverified.

The identification contract includes the production firmware bridge and backend
interfaces. It checks that an adopted bridge blocks every diagnostic stage,
including a failed service. Bridge preparation and publication are explicit
aborting boundaries here; their actual lifecycle runs in
`sh ember/tools/a733-gpu-fwbridge-contract.sh`. Run both contracts when changing
this shared state. These host checks do not establish hardware operation.
The identity contract checks failed REQUEST/WAIT, finalizer errors, direct
identity calls, and the unconfirmed firmware-only boot path without GPU MMIO.
It also checks successful CORE confirmation, retained reservations, and a
REQUEST timeout followed by a successful WAIT.
The HWR diagnostic keeps its full 2,336-byte firmware snapshot in temporary
kernel memory. It releases that memory after formatting or a format error.
GCC16.2 reports a 2,032-byte sysctl frame, below the unchanged 3,584-byte limit.
The target translation unit passes GCC path analysis. Host checks verify
allocation, release, unchanged DMA bounds, and all 16 diagnostic records.
These checks do not establish a complete kernel stack bound or hardware operation.

## Physical result


Current hardware evidence and its applicability belong to the [DevBoards profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/a733-zero3w/interfaces.md).
Normal boot observes firmware-owned state. A reported gate mask is not a frequency measurement, and sampled MISR levels do not identify a transition phase.
Do not infer permission for GPU MMIO or override PWCR from a partial clock/domain snapshot.

## Local binding

The SoC node uses `allwinner,sun60i-a733-gpu`, followed by
`img,img-bxm-4-64` and `img,img-rogue`. The A733 compatible is a local EmberBSD
binding addition. It is not presented as an accepted Linux or NetBSD binding.
The product/family strings appear in the pinned upstream Rogue binding, which
does not list A733. The BSP uses `img,gpu`; neither fallback is matched here.

The local binding requires:

- A single register range at `0x01800000`, covering PBVNC at `+0x20`.
  The DT preserves the BSP range size `0x8ffff`; the consumer maps only `0x28`
  bytes and performs one 64-bit fault-aware read only after CORE ON/Q confirmation.
- One `core` clock, `<&ccu CLK_GPU0>`, from the native A733 CCU.
- One domain, `<&pck600 PD_GPU_TOP>`, with domain ID 5 and one argument cell.
- `gpu-supply` referencing the AXP8191 `dcdc4` regulator node.
- The empty boolean property `netbsd,consumer-managed-power`.
- The current DT also sets empty `netbsd,observe-only`, a local EmberBSD
  diagnostic opt-in. Nonempty values fail with `EINVAL`. It preserves provider
  error handling but exits after completed CCU observation, before OPP checks
  or GPU mapping, including when readiness succeeds.

The hardware description also records GPU reset and SPI interrupt 63, but
the normal identification path does not acquire or operate them. CCU readiness
checks the actual GPU reset bit. The SoC node is disabled by default.
Zero 4 supplies DCDC4 and enables the identification consumer; Zero 3W
inherits those declarations. Use these DTBs with their matching kernel.

## FDT power opt-in

`netbsd,consumer-managed-power` is an explicit EmberBSD extension to FDT
attachment. An empty property skips only automatic power-domain enabling
before attach. Pinctrl initialization and post-attach behavior remain intact.
A nonempty property fails pre-attach with `EINVAL`. Ordinary nodes retain
checked power error propagation and firmware fallback for unregistered
providers.

A consumer carrying this property owns sequencing and error handling through
strict provider APIs. This consumer requires it; the normal path only queries state.

## Readiness and result

The probe runs once through an autoconfiguration finalizer after device setup.
It avoids the parent-specific deferred queue, which misses late `/soc` children.
Unavailable providers are reported without blocking boot or retrying the probe.

The probe validates the exact supply, power-domain and clock references.
It requires the supply enabled, GPU_TOP statically ON, and
[CCU readiness](a733-provider-state.md). It accepts only
400 or 600 MHz at a programmed 800000 microvolts, the two pinned BSP operating
points with the same voltage across all listed speed bins. Other settings
return `EOPNOTSUPP`; they are neither changed nor declared electrically unsafe.
Supply/domain OFF, dynamic power mode, missing providers, unsupported clocks,
or query errors prevent GPU mapping and reads.

The non-preparation boot path cannot prove CORE ON/Q readiness through a retained
PCK owner. It returns `EBUSY` before GPU mapping, even with ready supply and
clocks. This firmware-only path remains unverified on hardware. The explicit
prepared boot path must pass both strict CORE waits. Observe-only mode remains
read-only and does not acquire new reservations.

For a completed CCU observation that is not ready, the consumer reports the
first failed condition, the queried DCDC4 setting in microvolts, both
oscillator rates, and the thirteen raw CCU registers. Differing registers show
both samples. This uses one bounded provider inspection, without a second
query, writes, or GPU MMIO. Acquisition errors do not print an unavailable
snapshot. The reported supply setting is not a physical voltage measurement;
the 800000-microvolt operating-point check follows CCU readiness.

Outside observe-only mode, after all checks pass, `bus_space_peek_8` reads PBVNC. The driver prints the
actual raw value and four 16-bit fields. Expected A733 identity is
`36.56.104.183`, raw `0x00240038006800b7`. Every other value, including zero
and all ones, is reported as unexpected and rejected with `ENODEV`; a bus
fault returns `EFAULT`. No expected-value fallback exists. Mapping and
acquired handles are released on both success and failure. Firmware-owned
hardware state is never unwound by disabling resources.

Queries are observations, not a reservation against later writers. Equal CCU
snapshots cannot exclude every intervening change. Fault-aware reads handle
synchronous faults but do not guarantee that a stalled bus transaction will
finish. The normal consumer has no cold-start path or retry policy; firmware that
leaves the GPU OFF produces an explicit unavailable result. GPU_CORE domain 6
is not requested by the pinned BSP GPU binding or operated by the normal path.
The PCK provider separately reports its raw state at attachment. That earlier
observation is not consumed as a GPU readiness guarantee or power capability.
The pinned DDK selects a live `USE_FPGA` path that operates CORE6 directly;
the experimental paths preserve that handshake instead of copying the write.

## Experimental clock preparation

This experiment has not achieved identification. Its separate, empty property is
`netbsd,experimental-clock-prepare`. It is mutually exclusive with
`netbsd,observe-only`; malformed or conflicting properties fail before any
provider action. The normal board DTS files remain observe-only. Build a
separate Zero 3W artifact, with the matching kernel sources and host tools:

```sh
CPP=/path/to/aarch64--netbsd-cpp DTC=/path/to/nbdtc \
    sh ember/tools/a733-gpu-experimental-dtb.sh /absolute/output
```

The builder produces `sun60i-a733-orangepi-zero3w-gpu-experimental.dtb`;
it neither replaces the normal DTB nor installs anything. Acceptance requires
a recoverable matched kernel/modules/DTB bundle and an explicitly selected
experimental boot. Zero 4 active preparation is not covered by this builder.

The one-shot path requires DCDC4 enabled at 800000 microvolts, TOP statically ON,
the observed PCK-600 v1.1 single-Q configuration and CORE policy ON/status OFF.
Native CCU ownership excludes preexisting writers.
The actual RTC/DCXO and normalized REF must yield an already running 400 MHz
PERIPH0 branch and 200 MHz AHB. With the GPU gates off and reset asserted,
it selects mux 3/M=0, releases bus reset, enables the bus gate and then the
module gate. Only `GPU_CLK` and `GPU_BGR` are written. It never calls recursive
clock enable, changes a PLL/supply or writes a PCK register, including PWCR.

After the fourth write, an UPDATE-completion poll allows at most 1001 reads and
10000 microseconds of requested delay. No extra writes occur. The
[A733 manual](https://gitlab.com/api/v4/projects/71118510/repository/files/A733%2FHardware%E7%A1%AC%E4%BB%B6%E7%B1%BB%E6%96%87%E6%A1%A3%2F%E8%8A%AF%E7%89%87%E6%89%8B%E5%86%8C%2FA733_User%20Manual_V1.00.pdf/raw?ref=d0dc4d615be00b76e3a0818d0818eff8f69ffa15),
V1.00 section 4.1.6.121, defines bit 27 as self-clearing when configuration is valid.
Strict CCU readiness and decoded 400/200 MHz precede the first CORE waiter.
PCK independently allows 1001 observations and 10000 microseconds of delay;
both domains must reach stable ON with QACCEPTn high and QDENY low.
MMIO acquisition time is additional; these bounds cannot bound a stalled bus access.
Supply, domain and clock checks repeat before the single PBVNC read.

A retained timeout reports a fresh terminal CCU snapshot, including UPDATE and
both raw samples, without replacing the original error/stage or retrying preparation.
Failed acquisition reports its error without stale samples. Cleanup remains read-only.

The clock lease is irreversible before its first attempted configuration write.
PCK ownership is retained before that call. Every later error retains resources
until reboot, including rejected writes, timeouts and an unexpected PBVNC.
No gate removal, reset assertion, CORE power-off, detach or automatic retry is
performed. Shared firmware sources are borrowed; native retune/disable and
GPU-local mutations are refused while reserved. External firmware writers
cannot be excluded by these native locks.

The production-body contracts cover reservation, each write failure, retention,
delayed UPDATE clearing, deadline/stuck cases, CCU-before-CORE ordering and
terminal acquisition failure. Its 15,253 clock and 1,156 consumer checks also
pass as AArch64 executables in an isolated VM. This is software-only validation;
it does not establish the cause of the remaining physical CORE timeout. Run the three A733
clock/power/identification contracts and `sh ember/tools/a733-gpu-prepare-mutations.sh`.
The experiment remains disabled in normal board DTBs.

## Experimental domain request

A further opt-in property, `netbsd,experimental-domain-request`, extends the
prepared-clock experiment. It requires `netbsd,experimental-clock-prepare`,
is rejected together with `netbsd,observe-only`, and adds a static CORE request
before the existing waiter.

It re-issues the static GPU_CORE ON request after strict CCU
readiness and before the read-only CORE waiter. Allwinner's own writers do
exactly this without comparing the previous policy and status: the SCP
standby-resume loop ORs 8 into every PWPR (skipping only the domain-6 wait),
and the BSP `pck600_domains.c` power-on path writes COMMAND_ON and polls PWSR.
`sun60i_a733_pck_gpu_request_on` programs the same five delay registers,
writes the already-requested ON policy, verifies the readback, and polls
PWSR for at most 10000 microseconds. A write that does not land or a policy
that reverts is a denial: the domain is quarantined like an ordinary failed
transition. A status timeout does not quarantine, so the read-only waiter
stays usable afterwards. GPU_CORE power-off remains impossible.

A failed boot request or wait preserves its errno and retained reservations.
A retained timeout still reports the terminal CCU snapshot. The error path
never maps or reads the GPU aperture. Fault-aware peeks only catch synchronous
faults; they cannot bound an unresponsive bus transaction.

### Physical result: withdraw the experimental boot until a console exists


Use a working serial console and a tested watchdog recovery route before selecting the experimental domain-request path.
A fault-tolerant peek handles synchronous traps; it cannot bound a bus transaction that never completes.
Keep the ordinary observe-only DTB and a separately recoverable boot path.

### Physical result: the staged ladder localizes the stop to GPU MMIO


The PERIPH0-400M experiment did not establish GPU_CORE readiness. GPU MMIO without the successful dedicated-PLL preparation can stall the board.
Runtime stages must preserve strict ON/Q checks and their watchdog bound. Do not bypass readiness because TOP or CCU access succeeds.

### Runtime staged diagnostics with a watchdog bound

Normal board DTBs do not run this experiment at boot. The live driver exposes
`hw.sun60igpuid0.stage`, `result`, `pbvnc`, `clock_source`, `clock_state`, and the cached firmware state nodes.
Run one stage per sysctl write. Each stage runs once in the following order:

| Stage | Action |
| --- | --- |
| 0 | Test the sysctl path without hardware access; this does not reset the sequence |
| 1 | Check DCDC4 at 800000 microvolts |
| 2 | Check GPU_TOP |
| 3 | Reserve GPU power domains |
| 4 | Reserve GPU clocks |
| 5 | Retain power ownership until reboot |
| 6 | Prepare the selected GPU clock |
| 7 | Inspect clocks and cache the complete result in `clock_state` |
| 8 | Reissue the static GPU_CORE ON request |
| 9 | Wait for CORE ON/Q readiness |
| 10 | Read CORE_ID and PBVNC only after stage 9 confirms CORE ON/Q and supply/TOP/clock checks pass |
| 11 | Read strict PBVNC, only after stage 9 confirmed CORE ON |
| 12 | Load firmware through firmload and validate metadata against the live BVNC; no GPU transfer |
| 13 | Prepare CPU-owned firmware memory and page tables; no GPU execution ([guide](a733-gpu-fwmem.md)) |
| 14 | Prepare the full CPU-owned firmware interface and extend page tables atomically; no device access ([guide](a733-gpu-fwinit.md)) |
| 15 | Revalidate hardware, transfer DMA ownership, release the MIPS reset and wait at most five seconds for firmware startup ([guide](a733-gpu-fwboot.md)) |
| 16 | Revalidate hardware and submit one bounded `LOGTYPE_UPDATE` command after successful firmware startup ([guide](a733-gpu-fwprobe.md)) |
| 17 | Revalidate hardware and submit one bounded `MMUCACHE` command after successful stage 16; no page-table changes ([guide](a733-gpu-fwprobe.md)) |

Stage 14 obtains cluster count and SLC size from the validated firmware metadata.
It uses the observed core clock rate and prepares work/config buffers.
Stage 15 requires the unchanged prepared object and a successful stage 14.
It rechecks supply, GPU_TOP, strict CORE readiness, clocks and live BVNC.
The observed clocks must still match the values used for firmware initialization.
Only then does it map the complete register window and call the bootstrap helper.
The helper synchronizes all five allocations before device access.
After a possible register side effect, it retains all DMA memory until reset,
including on faults or timeout. Releasing the CPU register mapping does not
release any DMA allocation. The sequence remains single-use per boot.

`interface_state` and `boot_state` expose cached results without device reads.
`memory_state` and `interface_state` change to device-owned after handoff.
The bootstrap result includes attempted writes, firmware-start status,
connection fields and firmware compatibility values. The shared-memory
snapshot is not an atomic firmware transaction. Firmware startup alone
does not establish interrupt delivery, command processing or acceleration.

Stage 16 requires successful stage 15 and its unchanged device-owned memory.
It also requires a valid startup snapshot and released MIPS reset.
It repeats the supply, GPU_TOP, strict CORE, clock, and live BVNC checks.
The core and bus clocks must still match the prepared values.
Only then does it map the register bank and call the command probe.
The probe waits for compatibility data and one KCCB command within one second.
It never writes connection states or performs an OS handshake.
Success requires both the command return word and queue progress.
`probe_state` exposes cached publication, kick, queue, and compatibility results.
The driver publishes this report after it unmaps the CPU register window.
It retains all DMA allocations on success, error, and timeout.
A failed stage cannot be retried, including after writing stage zero.
Command completion does not establish rendering, interrupts, or accelerated Plasma.

Stage 17 requires the unchanged successful stage-16 `LOGTYPE_UPDATE` result.
It checks publication, kick, return word one, and read/write offsets one.
The previous command must use slot zero and have no MMUCACHE payload.
It repeats all stage-16 hardware readiness checks before mapping registers.
The helper uses slot one and the existing MMUCACHE synchronization object.
It requires return word one and read/write offsets two within one second.
The synchronization value is diagnostic data, not a completion token.
`cache_state` records this separate result and the command payload.
`probe_state` and its persistent result remain unchanged.
No page table, VM context, connection state, or DMA ownership changes.
All memory stays retained, and the same no-replay rule applies.
This stage has software checks; physical acceptance requires a separate record.

Normal boot never runs these stages. Keep the external hardware watchdog
active during each explicit stage when no serial recovery is available.

Stage 12 is a [firmware metadata preflight](a733-gpu-fwinfo.md). It requires
successful strict identification. It does not load or execute code on the GPU.

`result` records the last errno. Only stage 8 REQUEST returning `ETIMEDOUT`
permits the next, read-only stage 9 WAIT. Any other failure stops the sequence.
Failed WAIT blocks stage 10, including after a stage 0 dry run. Both identity
helpers also reject calls without confirmed CORE ON/Q before GPU mapping.
Reading these sysctls accesses cached data,
not GPU registers. The CORE_ID failure path unmaps immediately. It does not
read PBVNC or print an uninitialized CORE_ID after a failed first read.

A stalled bus transaction cannot be bounded in software. Arm the hardware
watchdog in external-tickle mode around each hardware stage:

```sh
wdogctl -e -p 16 sunxiwdt0
sysctl -w hw.sun60igpuid0.stage=1
wdogctl -d
sysctl hw.sun60igpuid0.result
```

Save and sync a transcript before each stage. Check that no other test uses
the watchdog. After a stall it resets the board into the normal DTB. This
reset recovery was verified on 11 October 2026. No risky stage runs on reboot
unless an operator separately installed an automatic test script.

### Optional dedicated PLL experiment

Before stage 1, set `hw.sun60igpuid0.clock_source=1` to select PLL_GPU0 at
600 MHz for stage 6. The default `0` preserves PERIPH0 at 400 MHz. Selection
changes after the sequence starts return `EBUSY`; stage 0 cannot unlock it.
This selection does not change the boot-time DT experiment.

The new provider API starts only a stopped, reserved PLL. It rejects running
or unstable state, SDM and automatic or disabled post-PLL gating. It preserves
reserved register fields. With the observed 24 MHz PLL_REF, N=75 and M=3 give
600 MHz. The consumer rechecks the 800000-microvolt supply before preparation.
The provider enables LDO, PLL and lock detection in the BSP order. Three
consecutive lock samples and a settling interval precede output enable.
Readback, UPDATE completion and a real CCU observation must confirm 600/200 MHz.
Every failure after the first attempted write retains ownership until reboot.
PLL_REF, PLL_PERIPH0, PLL_NPU, PPU policy and voltage are not changed.

This is an explicit hardware experiment, not GPU acceleration. The pinned BSP
always enables the dedicated PLL, but selects PERIPH0 for rates up to 800 MHz.
The new path tests one remaining clock hypothesis. It does not establish that
PLL selection causes the existing CORE timeout. Do not infer readiness from
a successful clock operation or substitute an expected GPU identity.

The register sequence comes from Orange Pi BSP `2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f`,
`bsp/drivers/clk/sunxi-ng/ccu-sun60iw2.c`, `ccu_gate.c` and `ccu_common.c`.
A733 User Manual V1.00 sections 4.1.6.16 and 4.1.6.252 define the PLL and
manual post-PLL gate fields. The implementation is new BSD-2-Clause code,
with AI assistance. The upstream sources retain their licenses and authorship.

## Source audit: remaining cold-start boundary

In Linux `0c2669a9f4a1d607e7591ae50ccf3c432a0aff08`, the
[PCK probe](https://github.com/torvalds/linux/blob/0c2669a9f4a1d607e7591ae50ccf3c432a0aff08/drivers/pmdomain/sunxi/sun55i-pck600.c#L181)
calls `pm_genpd_init(..., false)` for every domain, including CORE. The
[generic framework](https://github.com/torvalds/linux/blob/0c2669a9f4a1d607e7591ae50ccf3c432a0aff08/drivers/pmdomain/core.c#L2395)
records software ON without reading PWSR or calling the power-on callback.
`GENPD_FLAG_ALWAYS_ON` prevents power-off; it does not complete cold startup.
That bookkeeping cannot establish readiness from the #8 policy/status mismatch.

Vendor U-Boot's [A733 configuration](https://github.com/orangepi-xunlong/u-boot-orangepi/blob/b791be842935b27268ae3d00e943a9075495f30a/configs/sun60iw2p1_a733_defconfig#L75)
at `b791be842935b27268ae3d00e943a9075495f30a` enables `CONFIG_ARISC_DEASSERT_BEFORE_KERNEL`.
Its [handoff](https://github.com/orangepi-xunlong/u-boot-orangepi/blob/b791be842935b27268ae3d00e943a9075495f30a/arch/arm/lib/bootm.c#L408)
calls SMC `0x8000ff10` with the OS FDT before entering the kernel. The receiving
BL31/ARISC implementation, its GPU PCSM role, and correspondence to this board's
firmware remain unverified. This is an evidence gap, not an established missing
enable step or a reason to replace firmware or add speculative DT properties.

## Sources and software checks

Hardware resources, supply and operating points come from Orange Pi BSP
[`2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f`](https://github.com/orangepi-xunlong/linux-orangepi/tree/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f):
`bsp/configs/linux-6.6/sun60iw2p1.dtsi` and the Zero 3W board DTS.
PBVNC offset/fields and the expected tuple come from the same BSP's
`img-bxm` DDK sources. Provider formulas and constraints are documented in
[provider observations](a733-provider-state.md). The upstream product strings
can be checked in the pinned [Rogue binding](https://github.com/torvalds/linux/blob/2a4d91142e538ff5580c6bf48b5e668d8131fd9a/Documentation/devicetree/bindings/gpu/img%2Cpowervr-rogue.yaml).

```sh
sh ember/tools/a733-gpu-identification-contract.sh
sh ember/tools/fdt-power-attach-contract.sh
GPU_ID_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/a733-gpu-identification-contract.sh
```

The contract executes production driver and autoconfiguration bodies with fake providers/MMIO.
It covers resource/provider failures, both operating points,
identity errors, cleanup, the late `/soc` queue hazard and repeated finalizers.
Diagnostics cover acquisition/readiness errors, voltage and raw samples;
observe-only requires zero GPU mappings/reads with ready or unready providers.
These checks do not prove physical GPU identity or acceleration.

## Physical PLL result


Current hardware evidence and its applicability belong to the [DevBoards profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/a733-zero3w/interfaces.md).
The dedicated GPU PLL path reports 600 MHz core, 200 MHz bus and BVNC 36.56.104.183 after strict ON/Q readiness.
PLL lock and UPDATE completion do not measure external clock frequency or the configured regulator voltage. The successful combined source/rate change does not isolate one internal cause of the earlier stall.
Configured source analysis retains CCU path-complexity and inherited header/model findings; it is not a clean-analysis or Valgrind claim.
