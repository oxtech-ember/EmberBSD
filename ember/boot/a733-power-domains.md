# A733 PCK-600 power-domain prerequisite

EmberBSD supplies a native FDT power-domain provider for Allwinner A733.
It is a prerequisite for device drivers, including GPU/NPU drivers; it does
not expose an accelerator, submit commands or claim hardware acceleration.
The owner is this OS repository. Provider attachment is verified on Zero
3W; successful physical accelerator-domain transitions remain unverified.

## Interface and limits

The [driver](../../sys/arch/arm/sunxi/sun60i_a733_pck600.c) attaches as
`sun60ipck` to `allwinner,sun60i-a733-pck-600`. The register range is
`0x07060000` through `0x0706afff`, with eleven 4 KiB domain windows.
Its clock is `CLK_BUS_R_PPU` (19), R-CCU register `0x1ac`, bit 0, sourced
from the high-speed oscillator. **A733 has no PPU reset.** The A523 reset
sequence must not be used on A733.

The [A733 DT](../../sys/external/gpl2/dts/dist/arch/arm64/boot/dts/allwinner/sun60i-a733.dtsi)
contains the provider, with `#power-domain-cells = <1>` and its clock.
The imported [binding IDs](../../sys/external/gpl2/dts/dist/include/dt-bindings/power/allwinner,sun60i-a733-pck-600.h)
preserve upstream numbering: NPU 4, GPU_TOP 5 and GPU_CORE 6.
The other IDs are VI 0, DE_SYS 1, VE_DEC 2, VE_ENC 3, PCIE 7, USB2 8,
VO 9 and VO1 10. This does not validate those peripherals.

Attachment enables only the provider's programming clock. It takes the
bounded GPU_TOP/GPU_CORE diagnostic below without writing domain registers.
It publishes no GPU/NPU consumer node.
A later consumer must establish its supplies, device clocks, resets,
interrupts and memory ownership in the required order before using hardware.
Shared-domain lifetime management remains the consumer's responsibility;
this FDT interface is not a runtime-PM reference-counting framework.

On a consumer request the provider:

- Validates the domain ID before accessing registers; rejects GPU_CORE off,
  following upstream's always-on restriction.
- Serializes requests and rejects existing dynamic, locked or emulated
  policy and mismatched policy/status, without writing registers.
- Leaves a domain already at the requested policy/state untouched.
- Programs the five A733 delay values only for a needed transition,
  verifies their readback, and preserves unrelated policy bits.
- Drains the policy write, checks policy and status, and waits at most
  10,000 microseconds of polling delays, in 10-microsecond steps.
- Returns `EIO` for readback or reverted-policy failure, or `ETIMEDOUT`
  when status never reaches the requested state. Further requests to that
  domain return `EIO` until reboot. Other domains remain available.

The poll budget excludes CPU scheduling and MMIO access latency. A bus
access that traps or never completes cannot be converted into a timeout
by this software loop. Readback detects ignored writes, not every bus fault.
After a failed write/transition, a rollback could race an unfinished hardware
transition. The driver therefore makes no speculative rollback writes.
Failed attachment releases its mapping and any clock reference it acquired.

The [FDT power-domain API](../../sys/dev/fdt/fdt_powerdomain.c) now accepts an
optional error-returning `pdc_set` callback. Its errors reach consumers, and
an all-domain request stops at the first error. Earlier successful domains
are not automatically rolled back. Existing `pdc_enable` callbacks retain
their previous void-callback behavior; Apple PMGR is unchanged. Truncated
DT specifiers are rejected before entering either callback.

## FDT attachment failures

The FDT bus checks power-domain errors before attaching a matched consumer.
A missing `power-domains` property retains ordinary attachment. A present
malformed property or registered provider failure prints the node name and
error, and prevents both attach and post-attach callbacks. The
node stays unattached so a later scan can retry it.

Automatic attachment uses `fdtbus_powerdomain_enable_on_attach`. An
unregistered provider retains the firmware-managed state: its DT
`#power-domain-cells` and specifier bounds are validated, then that entry
is skipped. The parser continues through other domains and never suppresses
registered callback errors, including `ENXIO`. This preserves existing
SD/eMMC attachment with the unsupported power controller described by
[RK3399's DT](../../sys/external/gpl2/dts/dist/arch/arm64/boot/dts/rockchip/rk3399.dtsi).
An invalid phandle, missing cell count or truncated entry still fails.

Explicit enable/disable APIs remain strict and return `ENXIO` for an
unregistered provider. A future A733 consumer must explicitly require its
providers before MMIO; automatic attach success is not proof of power.

Previously unmatched nodes get a fresh match search in the default pass,
so providers attached earlier in the same boot can make a driver available.
The new match must pass pre-attach before attaching. Still-unmatched nodes
keep the `not configured` diagnostic without pre-attach pinctrl or power
operations; a new match during that diagnostic cannot bypass pre-attach.
Successfully attached nodes are not powered or attached again on a retry.

This preserves pinctrl-before-power ordering for matched consumers. A
failure does not undo the selected pinctrl state or earlier successful
domains in a multi-domain request. Supply/clock sequencing and shared
resource ownership still need a separate consumer design. The later
[GPU identification binding](a733-gpu-identification.md#fdt-power-opt-in)
explicitly opts out of automatic power changes; normal board DTBs observe only.

Run `sh ember/tools/fdt-power-attach-contract.sh` for the production scan,
pre/post-attach and power API regression. Use
`FDT_ATTACH_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'`
for sanitizer checks. The contract covers quiet/default passes, absent
and malformed properties, RK3399 SD/eMMC firmware fallback, strict explicit
requests, checked and legacy callbacks, mixed domains, errors, retries,
late provider matches, unmatched diagnostics and preserved side effects.

## Read-only GPU PPU diagnostic

The provider reads GPU_TOP (5) and GPU_CORE (6) through its existing mapping,
under its existing lock. Each domain has two ordered samples of this fixed
allowlist, for 80 fault-aware 32-bit reads in total:

| Registers | Domain-relative offsets |
| --- | --- |
| PWPR, PMER, PWSR | `0x000`, `0x004`, `0x008` |
| DISR, MISR, STSR | `0x010`, `0x014`, `0x018` |
| PWCR, PTCR | `0x020`, `0x024` |
| IMR, AIMR, ISR, AISR | `0x030`, `0x034`, `0x038`, `0x03c` |
| EDTR0, EDTR1, DCDR0, DCDR1 | `0x160`, `0x164`, `0x170`, `0x174` |
| IDR0, IDR1, IIDR, AIDR | `0xfb0`, `0xfb4`, `0xfc8`, `0xfcc` |

The diagnostic publishes both domains only after every read succeeds. A
synchronous read fault returns `EFAULT` without publishing partial output;
an already quarantined domain returns `EIO` before any reads. Diagnostic
faults do not quarantine a domain or prevent provider registration. There
are no retries, polling delays, new mappings, W1C writes or GPU accesses.
Reading ISR/AISR preserves events; only writes of one clear their bits.

Output includes both samples and a changed-register mask in table order.
Channel metadata is decoded only for stable Arm PCK-600/PPU v1.1 identity
and a valid channel configuration. Unknown identities remain raw values.
Neither recognized metadata nor equal samples establish GPU readiness.
MISR exposes input levels, not the internal transition phase or PREQ output.
The lock excludes native provider writers, not autonomous hardware changes.
Fault-aware access cannot guarantee completion of a stalled bus transaction.

Ordinary `pdc_get` still reads only PWPR/PMER/PWSR twice and retains its
strict errors. In physical kernel #6, CORE6 had policy `8`, emulation `0`
and status `0`, hence `EBUSY`. The [physical receipt](a733-gpu-identification.md#physical-result)
confirms both as one-Q-Channel PCK-600/PPU v1.1, with PWCR `0x101`,
CORE MISR `0` and TOP MISR `0x100`. Arm DEN0051E section 5.2.8 requires
the requested static mode before changing PWCR.DEVREQEN. The pinned BSP's manual
`PWCR=0; PWPR=8` GPU initialization is therefore not a justified recovery
sequence for that mismatch. The diagnostic does not alter power policy.
A [physical preboot snapshot](a733-preboot-snapshot.md#physical-zero-3w-result)
finds the same `8`/`0` state before the final ARISC-startup SMC and kernel.
It establishes an earlier boundary, not the original writer or reset cause.

## Reproduce the software checks

The contract compiles the actual driver and FDT implementation, with bus,
clock and FDT primitives replaced by deterministic C fixtures. It needs a
host C compiler and shell; it does not need Python or a board.

```sh
sh ember/tools/a733-power-contract.sh
POWER_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/a733-power-contract.sh
```

The 45 scenarios cover enable/disable, delayed completion, idempotence,
preserved policy bits, timeout, denial, each ignored register write,
quarantine, invalid IDs, GPU_CORE protection, firmware policy protection,
attachment failure cleanup, truncated FDT data, checked errors and the
legacy callback path. A separate 1,661-check matrix covers strict read-only
state queries. The diagnostic adds 236 scenarios covering exact read order,
each acquisition fault, unchanged error output, each changed field,
recognized P/Q and unknown identities, quarantine and attachment behavior.
These are software contracts, not physical MMIO tests.

The expanded diagnostic passes on Apple Silicon macOS, including ASan/UBSan.
Its physical register snapshot was accepted in #7. The extended target
contract passes on the real Zero 3W CPU under #7: 133 ownership/wait cases
plus the existing matrices. These native tests use fake MMIO. The combined
GPU preparation contracts reject eleven causal mutants. PCK objects and
target contracts cross-build with the corrected Ports GCC 16.2.

Earlier provider validation used separate `EMBER64` object directories to
cross-build the driver, R-CCU, FDT power-domain code
and unchanged Apple PMGR using GCC 12.5 and the corrected Ports GCC 16.2.
The GCC16 objects retain DWARF5 and CTF, with strict kernel warnings enabled.
Both Zero 3W and Zero 4 DTBs compiled and decoded with the provider present.
The existing `/soc` unit-address warning remains; no new DT warning appeared.

The complete GCC16.2 `EMBER64` kernel, four matching modules and both A733
DTBs cross-built on macOS. Matched #8 booted Zero 3W through vendor
boot0/U-Boot, with `sun60ipck0`, both CCUs and Wi-Fi/SSH available.
This verifies attachment, not successful accelerator power transitions.
The normal GPU DT enables [read-only observation](a733-gpu-identification.md).
The separate #8 clock/reset attempt below failed before identification.
Active supply control, DMA/MMU, IRQs and command execution remain unverified.
Use the [cross-build instructions](cross-build.md) from a clean commit,
including matching modules and DTBs; do not mix these artifacts across builds.

## Experimental GPU reservation

The separate `sun60i_a733_pck_gpu_*` API resolves a registered native provider
by phandle and reserves TOP5/CORE6 without hardware writes. Ordinary `pdc_get`
remains strict; `pdc_set` returns `EBUSY` only for reserved TOP/CORE. Other
operations retain their behavior. The bounded waiter requires stable ON and
Q acceptance. Pending status, faults and unsupported modes remain failures.
Ownership retained before clock preparation lasts until reboot; see the
[experimental consumer](a733-gpu-identification.md#experimental-clock-preparation).

DEN0051E Figure 4-21 places PCSM completion before isolation release,
clock/reset sequencing and Q acceptance. Arm [IHI0068D](https://documentation-service.arm.com/static/617a637283e60c5c768e2241)
sections 2.1.2/2.3 permit reset release from Q_STOPPED/Q_EXIT, but require
Q_STOPPED before clock removal. This justifies failure retention, not a claim
that clocks caused the pending CORE transition. The experiment never changes
PWCR, supplies, PCK delays or power policy, and never requests CORE power-off.

On physical Zero 3W, the current experiment completed GPU_CLK UPDATE and passed local CCU
readiness before the bounded waiter returned `ETIMEDOUT` (60): CORE PWPR
`0x8`, PWSR `0`, MISR `0`. Resources remained reserved until reboot; no GPU
access or rollback followed. Restoring the normal DTB returned observe-only
operation and ended the reservation. The failing PCSM or Q-Channel phase
remains unknown. The separate post-PLL gates are open before preparation and
after timeout. See the
[physical result](a733-gpu-identification.md#physical-result).

`sun60i_a733_pck_gpu_request_on` re-issues the static GPU_CORE ON request for
the reserved owner. The vendor's own writers motivated it: the SCP
standby-resume loop ORs 8 into every PWPR while skipping only the domain-6
wait, and the BSP `pck600_domains.c` power-on writes COMMAND_ON unconditionally
and polls PWSR. The request programs the five A733 delay values, rewrites the
already-requested ON policy with readback, then polls PWSR for at most 10000
microseconds. A readback mismatch or a reverted policy is treated as a denial
and quarantines the domain like a failed transition; a status timeout does
not quarantine, keeping the read-only waiter available. An already-complete
CORE needs no request. Ordinary `pdc_set` restrictions are unchanged.

The first physical boot with this request active hung the board before
network bring-up and reproduced after full power removal; the exact stall is
not localizable without a serial console. See the
[physical result](a733-gpu-identification.md#physical-result-withdraw-the-experimental-boot-until-a-console-exists);
the experimental DTB must not be booted on hardware until a console exists.

## Provenance

Register facts were checked on 2026-10-08 against these primary sources:

- Arm [Power Policy Unit Architecture Specification v1.1, DEN0051E](https://documentation-service.arm.com/static/5f873177f86e16515cdb6d99),
  notably PWPR/PWSR, dynamic/lock/emulation fields and static-policy denial.
- Arm [CoreLink PCK-600 TRM, 101150_0004_00](https://documentation-service.arm.com/static/5f23d3b1da9f9552000f9cf6),
  for the 4 KiB PPU window and common registers. Allwinner's `0xc00`,
  `0xc04` and `0xc10` delay registers come from its BSP, not the Arm TRM.
- Orange Pi Linux BSP commit `2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f`:
  [PCK600](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/pm_domain/pck600_domains.c),
  [R-CCU](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/clk/sunxi-ng/ccu-sun60iw2-r.c),
  and [SoC DT](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/configs/linux-6.6/sun60iw2p1.dtsi).
  The BSP identifies eleven A733 windows, their delay values, and no PPU reset.
- Linux commit `0c2669a9f4a1d607e7591ae50ccf3c432a0aff08`:
  [PCK600](https://github.com/torvalds/linux/blob/0c2669a9f4a1d607e7591ae50ccf3c432a0aff08/drivers/pmdomain/sunxi/sun55i-pck600.c)
  and [A733 IDs](https://github.com/torvalds/linux/blob/0c2669a9f4a1d607e7591ae50ccf3c432a0aff08/include/dt-bindings/power/allwinner,sun60i-a733-pck-600.h).
  These agree with the BSP and add the GPU_CORE always-on restriction.

Linux/Allwinner driver code is GPL-2.0-only and remains credited to
Chen-Yu Tsai/Allwinner respectively. It was consulted for hardware facts;
no Linux driver source is imported here. The native implementation and
fixtures are new BSD-2-Clause code, with AI assistance declared in-source.
The imported upstream ID header retains its original dual license unchanged.

| Downloaded input | SHA256 |
| --- | --- |
| BSP PCK600 driver | `2d9c2b60b3b264d1732538815d12570e45386d30b280a1d0b4f5b9780bdc49ee` |
| BSP R-CCU driver | `9285e1b35bfae975e2c1257d477d843ad277916b9c555c12c5f88e1c7ea02554` |
| BSP A733 DT | `d68616db1d2755fbf13730d34e1edadb474cf7e5deb93ca982295b6e2a5edba4` |
| Linux PCK600 driver | `1e31b18cc776afcbb9ad3c7f52c317f4c2023560671db4690fb51b76cfbf7daf` |
| Imported Linux A733 IDs | `74b1088fbc582ec093096d3876c10cf2eece260e665e89e87ae855c18c7b9f13` |
| Arm DEN0051E PDF | `fa4ec504e4ffb98f5b35bda644e65c08884d5a5f77d2f853e1eb4d55ec1889fb` |
