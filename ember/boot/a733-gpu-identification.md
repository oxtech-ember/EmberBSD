# Firmware-ready A733 GPU identification

The native `sun60igpuid` consumer can identify an already prepared A733 GPU.
The current A733 DT enables its diagnostic `netbsd,observe-only` stage, which
never maps or reads GPU registers, even when the queried providers are ready.
This default mode provides no GPU initialization, DMA, submission or acceleration.
The [driver](https://github.com/oxtech-ember/EmberBSD/blob/main/sys/arch/arm/sunxi/sun60i_a733_gpu.c)
is owned by EmberBSD. Software contracts, the complete GCC16 kernel build
and physical attachment on Zero 3W are verified. Physical GPU identification
has not yet been accepted. Read-only provider observations are verified;
the current experiment completed GPU_CLK UPDATE with the observed post-PLL
gates open, but GPU_CORE still
failed its strict ON/Q readiness check. No GPU register was accessed.

## Physical result

On 2026-10-09, a 4 GiB Orange Pi Zero 3W booted the complete GCC16.2 kernel
and matched modules from `f85ffd420f6aff450bac2709aa76f0da5a537551`, using the
separate experimental DTB. The board revision was not recorded. Vendor
boot0/U-Boot and the boot script were preserved; a previous matched bundle
was backed up before installation.

GPU_CLK UPDATE completed before the CORE waiter. The terminal CCU observation
reported readiness with `GPU_CLK=0x83000000`, `GPU_BGR=0x00010001` and
`AHB=0x03000002`. Bit 27 was clear. The decoded core/bus rates were 400/200 MHz;
neither clock frequency nor the configured 800000-microvolt supply was
independently measured. Before preparation and after timeout, both samples
of PERI0PLL_GATE_EN/STATUS were `0x8fff0fff`/`0x0fff0000`: configured,
no-auto and effective masks were all `0xfff`. This includes the 400M,
400M_ALL, 600M and 800M branches; gate status is not a frequency measurement.

CORE still returned `ETIMEDOUT` (60), with last `PWPR=0x8`, `PWSR=0`, `MISR=0`.
There was no GPU mapping or PBVNC read. The clock/domain leases remained
reserved until reboot. Restoring the normal DTB and rebooting returned
observe-only operation and SSH; GPU_CLK/GPU_BGR were zero again. Kernel,
normal DTB and retained recovery image hashes matched the expected bundle.

| Artifact | SHA256 |
| --- | --- |
| Kernel ELF | `529fe5130070131bbc6b6f59881b0430129ed92bde37d3d0023caa46fd604bca` |
| Native kernel image | `66880ad9d1a2ec06cdf411de17a489c3f3b28c7b62ef9e704ff7053c8c4db143` |
| Experimental DTB | `7a840baec0c4c76455b1ce8b2252e63bfea7a121e88c38c43c413ab1575dab7c` |
| Restored normal DTB | `a2bd07430948db61140040a1a5c0d5872f5797a4719d7af6ff00b6c0d8d88113` |

This run excludes unfinished UPDATE and closed observed post-PLL gates.
It does not distinguish a stalled PCSM phase from Q-Channel exit.
MISR contains sampled input levels, not transition phase or request outputs.
The observation does not justify a PWCR override or relaxed readiness.
The preceding #8 attempt on 2026-10-08 did not wait for UPDATE; its same CORE
timeout cannot retroactively establish that clocks were configured then.

The normal baseline agrees with the earlier #7/#8/#11 observations:

- RTC status `0x183fb0f7` classifies DCXO as 26 MHz. PLL_REF `0xf8675f00`
  normalizes it to 24 MHz. The fixed-hosc DT provider remains unchanged.
- Both PCK GPU domains identify as PCK-600, PPU v1.1, one Q-Channel:
  IDR0 `0x10130101`, IDR1 `0x2`, IIDR `0x0b61143b`, AIDR `0x11`.
- GPU_TOP has PWPR/PWSR `0x8` and MISR `0x100`; GPU_CORE has PWPR `0x8`,
  PWSR `0`, DISR `1`, MISR `0`. Both PWCR values are `0x101`.
- GPU_CLK/GPU_BGR are zero, PLL_GPU0 is `0x41104500`; the normal consumer
  reports `GPU module gated (error 16)` before any GPU access.

These are short boot observations, not sustained operation or acceleration.
The [read-only PCK diagnostic](a733-power-domains.md#read-only-gpu-ppu-diagnostic)
and CCU observations preserve firmware-owned state.

## Local binding

The SoC node uses `allwinner,sun60i-a733-gpu`, followed by
`img,img-bxm-4-64` and `img,img-rogue`. The A733 compatible is a local EmberBSD
binding addition. It is not presented as an accepted Linux or NetBSD binding.
The product/family strings appear in the pinned upstream Rogue binding, which
does not list A733. The BSP uses `img,gpu`; neither fallback is matched here.

The local binding requires:

- A single register range at `0x01800000`, covering PBVNC at `+0x20`.
  The DT preserves the BSP range size `0x8ffff`; the consumer maps only `0x28`
  bytes and performs one 64-bit fault-aware read only outside observe-only mode.
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
the experimental path below preserves that handshake instead of copying the write.

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
is rejected together with `netbsd,observe-only`, and adds two stages only.

The first stage re-issues the static GPU_CORE ON request after strict CCU
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

The second stage is a bounded TOP-only identification after any failed
request or wait. Vendor kernels keep GPU_CORE outside the GPU binding, so
this register bank is read with only GPU_TOP managed. After re-verifying the
800000 microvolt supply, the statically ON TOP domain and strict 400/200 MHz
CCU readiness, the consumer maps the identification window and reads
`RGX_CR_CORE_ID` (0x18) and `RGX_CR_CORE_ID__PBVNC` (0x20) through
fault-tolerant peeks. A successful read publishes the real PBVNC explicitly
marked "GPU_CORE not confirmed ON"; a fault or readiness change only prints.
This diagnostic never authorizes further GPU access, mapping or resets.

### Physical result: withdraw the experimental boot until a console exists

Kernel `EMBER64 #6` from `c3fbeceb0e27bb7fced4cb7a2faae083797b2bc5` with the
normal observe-only DTB boots and passes the full driver diagnostics. The
same kernel with the experimental domain-request DTB reproducibly hangs the
board before network bring-up, including after a full power removal; no
panic output is reachable without a serial console. The stall is one of the
two new hardware actions of this experiment — the GPU_CORE PPU delay/policy
writes or the GPU MMIO identification peeks — and cannot be localized from
software alone. A fault-tolerant peek bounds synchronous traps, not a bus
transaction that never completes.

Do not boot the `netbsd,experimental-domain-request` DTB on hardware until
a serial console is attached to the board. The vendor-derived request path
stays in the tree behind the opt-in; recovery of the hung board was done by
restoring the normal DTB on the card from a host VM.

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
