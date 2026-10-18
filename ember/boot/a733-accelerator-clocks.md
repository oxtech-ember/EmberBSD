# A733 accelerator clock providers

The native main CCU supplies the GPU/NPU module clocks, interface gates
and reset specifiers needed by a future accelerator consumer. It does not
attach a GPU or NPU driver, change a power domain, or start DMA. Registering
these providers does not enable an accelerator clock.

## Implemented interface

Clock and reset IDs use the existing `allwinner,sun60i-a733-ccu` binding.
The implementation is in `sys/arch/arm/sunxi/sun60i_a733_ccu.c` and its header.

| Resource | Register | Behavior |
| --- | --- | --- |
| PLL GPU0 / NPU | `0x0e0` / `0x2a0` | Normal clock API only observes firmware state; the reserved GPU experiment can start PLL_GPU0 at 600 MHz |
| GPU0 module | `0xb20` | Six parents, gate bit 31, update bit 27, fractional M field |
| NPU module | `0xb00` | Seven parent selectors, gate bit 31, divisor 1 through 32 |
| AHB NPU / GPU0 | `0x5c0` | Bits 6 / 7; other gates remain unchanged |
| MBUS GPU0 / NPU | `0x5e0` | Bits 16 / 18; other gates remain unchanged |
| NPU / GPU0 bus | `0xb04` / `0xb24` | Gate bit 0 |
| NPU resets | `0xb04` | CORE 16, AXI 17, AHB 18, SRAM 19 |
| GPU0 reset | `0xb24` | Bit 16 |

GPU frequency is `parent * (16 - M) / 16`. Rate requests select the
integer divisors 1, 2, 4, 8 or 16, encoded as M = 0, 8, 12, 14 or 15.
A firmware fractional setting is reported using its actual formula.
The NPU divider is M + 1. Neither module retunes its parent or silently
selects another parent to approximate a requested rate.

Rate changes require an exactly representable, nonzero frequency. A
changed divider or parent requires the module gate to be off. An already
selected value succeeds without a write. Reserved mux selectors return
no parent; an absent or stopped parent prevents enabling the module.
The VE0, VE1 and DE parents listed by the NPU mux are not implemented by
the current CCU, so those choices fail explicitly.

Gate, mux and divider writes preserve unrelated bits. GPU writes include
the update bit. A barrier and field readback detect rejected writes as
`EIO`; no polling or speculative recovery sequence is used. Reset lines
use the existing NetBSD CCU reset operations. That framework does not
reference-count shared parents. Optional provider guards now arbitrate
mutations. The experimental GPU reservation counts in-flight writers before
acquisition,
then blocks GPU-local/reset mutations and shared source changes. Its dedicated
PERIPH0 prepare writes only the local mux/gates/reset. Shared ancestor enable is a
write-free success while borrowed; recursive writes never run on that path.
Without a lease, existing clock and reset operations retain their behavior.
This is not general GPU/NPU power management.
The separate SRAM reset is exposed but has no automatic sequence.

MBUS gates report no rate because the MBUS clock tree is not implemented
here. The NPU bus gate also reports no rate: its real parent is not
documented in the cited Linux submission. Neither is assigned a fictional
24 MHz parent. Their on/off operations remain available.

## Independent local reservations

GPU and NPU have separate typed local slots. Either acquisition order can
borrow the same running REF/SYS24M/PERIPH0/AHB ancestors. Native writers
cannot change a reserved local mask or borrowed ancestor. Borrowed enable
returns success without a physical write. Native admission counts remain
balanced, and the actual RMW hook rechecks protected masks under its mutex.

The NPU reserve/prepare/ready/release APIs use its fixed profile. Prepare
requires PCK4 retention first. It writes only its mux/divider, three resets
and four gates. It retains its slot before any write attempt, including a
fault. A fresh ready query requires a completed retained sequence and the
same verified parent/local state. PLL_NPU and SRAM reset 19 stay unchanged.
The legacy GPU wrappers keep their readiness, PLL600 and UPDATE policies.
GPU snapshot comparison uses only its own AHB master bit 7, so an NPU bit 6
change does not invalidate the GPU reservation.

## PLL ownership and power

PLL handoff checks enable, LDO, lock detection, lock and output bits, the two SDM pattern
enables, and an integer multiplier of at least 11. It accounts for both
the output divider and the input divider. Disabled, unlocked, SDM or
unrepresentable states report zero and fail enable with `ENXIO`. Retuning
returns `ENXIO`; disable returns `EBUSY` through the normal clock API.
The separate `sun60i_a733_ccu_gpu_prepare_pll` experiment requires a GPU lease,
a stopped PLL and open manual post-PLL gating. It starts PLL_GPU0 at 600 MHz,
checks consecutive lock samples and settling, then prepares local GPU clocks.
It retains ownership after any write attempt. It does not change PLL_NPU or
shared PLL_REF/PERIPH0. See the [runtime selection and bounds](a733-gpu-identification.md#optional-dedicated-pll-experiment).

PLL_NPU can also feed DRAM, MBUS and video engines. The current CCU cannot
account for all these consumers. A future PLL/DVFS implementation needs
shared ownership and board voltage limits before changing that PLL.
The existing peripheral PLL outputs offer alternative module parents;
this change does not alter their existing firmware handoff assumptions.

These clocks do not establish accelerator supply voltage, PCK600 power
state, interrupt handling, DMA coherency, or address translation. The
[PCK600 provider](a733-power-domains.md) is another prerequisite, not a
substitute for a consumer that owns and unwinds the full power sequence.

The pinned BSP NPU sequence deasserts AHB, AXI and CORE resets, then enables
AHB, configures rate, enables MBUS and bus gates, and finally the module.
The GPU clock helper enables parents, releases its bus reset and enables
bus/core clocks. The initial BSP platform setup orders supply, runtime
domain, clocks and rate; its resume path instead orders clocks before
runtime power. These paths are not one interchangeable cold-start recipe.
This is source evidence, not a validated native sequence; the BSP does not
sequence the new SRAM reset.

The [dedicated PLL experiment](a733-gpu-identification.md#physical-pll-result)
passes two short physical Zero 3W runs with GCC16 kernel `498c0028e`.
PLL lock and GPU_CLK UPDATE complete. CCU decodes 600/200 MHz, CORE reaches
strict ON/Q readiness, and the real PBVNC is `36.56.104.183`.
The older PERIPH0-400M experiment timed out and stalled on GPU register reads.
The new path changes both PLL enable and module source/rate; the individual
internal dependency was not isolated. General DVFS, IRQ, DMA, firmware
execution and application rendering remain outside this acceptance.

## Sources and validation

Origin: EmberBSD, AI-assisted native implementation using hardware facts.
No GPL driver implementation is imported or relicensed. NetBSD framework
copyrights and the existing dual-licensed DT bindings are preserved.

The vendor reference is Orange Pi's [Linux BSP commit
2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f](https://github.com/orangepi-xunlong/linux-orangepi/tree/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f):

- `bsp/drivers/clk/sunxi-ng/ccu-sun60iw2.c`
  (SHA256 `1d32bb8555f8ff205d35210f638f9d42cc28aa9295c46963aa1c810d47b06d77`).
- `bsp/drivers/clk/sunxi-ng/ccu_sdm.h`
  (SHA256 `3cfd2a05c597f5aac02dd9cf9293aefd9cb634817fe0c28f7bbfb4d5fde2fa59`).
- `bsp/drivers/npu/aw_nna_vip/vip2/os/linux/platform/allwinner/vip_drv_device_platform.c`.
- `bsp/modules/gpu/img-bxm/linux/rogue_km/services/system/rogue/rgx_sunxi/sunxi_platform.c`.

Junhui Liu's Linux CCU v5 submission, dated 30 September 2026, is the
additional primary reference, not a claim of merged Linux support.
Message IDs are `20260930-a733-clk-v5-N-11175b41cd2d@pigmoral.tech`, where:

- [4/8: PLLs](https://lists.openwall.net/linux-kernel/2026/09/29/2375)
  confirms the input divider and PLL control bits.
- [6/8: modules](https://lists.openwall.net/linux-kernel/2026/09/29/2367)
  describes the GPU formula, integer-divisor table and update bit.
- [7/8: gates](https://lists.openwall.net/linux-kernel/2026/09/29/2372)
  uses individual AHB/MBUS gate bits and records the unknown NPU bus parent.
- [8/8: resets](https://lists.openwall.net/linux-kernel/2026/09/29/2377)
  adds the NPU SRAM reset omitted by the pinned BSP.

The BSP's AHB/MBUS gate helpers OR broad constants into shared registers.
Those constants also turn on sibling gates. This implementation follows
the individual-bit operations in v5 and does not copy those constants.

Run `sh ember/tools/a733-accelerator-clock-contract.sh` on the development
host. It compiles the full production main CCU, generic clock operations,
and production clock/reset dispatch with fake MMIO. It also checks every
local CCU ID against the imported DT binding. The contract covers rejected
writes, reserved and unresolved parents, PLL handoff, exact rates, GPU
encoding/update, reset masks, preservation of siblings and no attach writes.
Use `CLOCK_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'`
for the sanitizer run. No Python or board access is required.

The contract and GCC 12.5/16 AArch64 kernel-object cross-builds have passed.
The extended 15,188-check contract also passed natively on physical Zero 3W
under kernel #7. It executes real CPU instructions with fake MMIO, not GPU
clock hardware. The complete GCC16.2 #8 kernel subsequently booted with the
separate experimental DTB; see the [physical result](a733-gpu-identification.md#physical-result).
That earlier attempt did not reach identification. The current 53,475-check
fixture and physical PLL result above extend it. GPU rendering and NPU
inference have separate acceptance.

## Masked native updates

A733 serializes each accelerator RMW and each generic gate/reset RMW with
one provider mutex. The native writer guard releases that mutex before
recursive parent dispatch. An owner preparation path holds the same mutex
and calls the internal locked update. It does not enter a native callback
while it holds the mutex. A write/readback error returns to its caller.
This mechanism does not add a new reservation or grant resource ownership.

`sunxi_ccu_update_bits` has an optional provider hook. Other CCU providers
keep the previous gate/reset fallback and return behavior when the hook is
absent. This change does not claim new synchronization for those providers.

The accelerator source contract also runs a two-thread control of the
actual A733 RMW body. A forced interleave reproduces a lost GPU/NPU MBUS
bit before serialization. The fixed control preserves both bits and the
unrelated fields. Run only that control with the standard-library runner:

```sh
python3 ember/tools/a733-ccu-rmw-contract.py
```

The existing `CLOCK_TEST_CFLAGS` and `CC` settings select native sanitizer
flags and the host compiler. These host checks do not execute target code,
validate physical timing, or exclude autonomous firmware writers.
