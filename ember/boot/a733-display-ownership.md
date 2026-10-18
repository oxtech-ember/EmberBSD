# A733 display resource ownership core

The core in `sun60i_a733_display_owner.c` supplies native writer arbitration
and a retained reservation boundary. It performs no I/O. No driver invokes
it yet. It does not enable a display clock, change power or reset state,
access DE/TCON/HDMI registers, or establish scanout readiness.

## Resource and lifetime contract

The caller keeps one zeroed state for the provider lifetime. One native
mutex must serialize every call. A callback cannot write or acquire that
mutex again. It returns fresh complete CCU, RTC and PCK observations, or an
error. It must reject a quarantined PCK domain. It must not supply a cached
attach-time observer snapshot.

Every native writer enters before it can mutate hardware and exits after
its last access. This includes operations that begin before a reservation.
The counter prevents reservation while any writer remains active. During a
reservation, writes to DE_PLL, DE0, DE_SYS, VO1, TV0, HDMI, REF24, PERIPH0 and
AHB fail with `EBUSY`. Other writers remain independent but are counted.
Read-only status remains available and does not call a provider.

A reservation validates equal samples directly, in addition to changed
masks. It rejects unknown mux selectors, pending NSI/DRAM updates and NSI
dynamic mode. DE_SYS and VO1 must use static ON policy and report strict ON,
with no emulation. A policy lock on static ON is allowed because this path
borrows the state without a write. DE0 must be gated and held in reset;
DE_SYS and VO1 resets must already be deasserted. DE/VO1 AHB master
gates (bits 5/4 at `0x05c0`) and DE_SYS MBUS gate (bit 11 at `0x05e0`)
must be open, with AHB auto gating (bit 31) off. These conditions do not
establish external firmware exclusion.

PLL_DE can be off. Open DE post-PLL gates do not prove that PLL_DE runs.
The reservation does not start or retune this PLL. The raw consumer census
remains evidence of potential consumers, not an ownership bitmap.

`sun60i_a733_display_begin_de0` obtains another complete snapshot. Every
sample must match the reserved snapshot. It then checks the already-running
borrowed ancestors for one fixed DE0 candidate: PERIPH0-400M, selector 3,
divide by one. Success marks the reservation retained **before** a future
native caller can attempt a local write. This core does not make that write.
It does not permit an access to a display aperture or start DMA.

Before this boundary, a matching owner can release its reservation after a
read failure. After the boundary, release fails with `EBUSY`. The first
reported fault remains sticky. There is no recovery or reset API; a reboot
is required. A caller must not reinitialize retained state or replace the
owner pointer to bypass this restriction. Cached rates are the last verified
rates, not fresh measurements after a fault.

## Borrowed ancestor contract

The inputs contain two CCU samples and two RTC raw reads before and after
them. The provider must bound all register apertures before acquisition.
The additional CCU inputs use this order:

| Input | CCU offset | Required fact |
| --- | --- | --- |
| REF | `0x0000` | Enabled, LDO, lock detection, lock and output; actual RTC-normalized 24 MHz |
| PERIPH0 | `0x00a0` | Same running flags; integer 1.2 GHz base before fixed branches |
| PAT0, PAT1 | `0x00a8`, `0x00ac` | No enabled pattern, fractional/SDM mode or SDM busy state |
| AHB | `0x0500` | SYS24M or PERIPH0-600M source; exact integer divider |
| PERI0 gate control | `0x1908` | Required branches configured open with auto gating disabled |
| PERI0 gate status | `0x1988` | Required branches report enabled |

REF rate uses the actual RTC DCXO selection (24, 19.2 or 26 MHz), N+1,
M+1 and the input divider. PERIPH0 uses N+1, M+1 and its input divider;
N must be at least 11. The core requires exact integer results. A register
label such as `400M` is not sufficient evidence of a running rate.
PAT0 bit 31 and PAT1 bits 31, 27:24 and 20 must be zero. Bias fields with no
fractional enable remain allowed. AHB selectors 1 and 2 are outside this
bounded profile. The AHB register is not `AHB_MASTER_GATE` at `0x05c0`.

PERI0-400M requires branches 400M and 400M_ALL. Their manual-control bits
are 1 and 2, and their configured/effective gate bits are 17 and 18.
An AHB parent of PERI0-600M also requires manual bit 9 and gate bit 25.
These gates and shared PLLs are never changed by this contract. Unsupported
or stopped ancestors fail explicitly; there is no fallback PLL enable.

## Native adapter boundary

The next adapter must connect this state to the CCU writer guard and PCK
reservations for domains 1 and 10. It must coexist with GPU/NPU reservations,
reserve before reading, define one lock order, and reverse only reservations
that have not crossed a write boundary. It must cover all mutation paths,
including recursive parent operations, reset operations and direct native
prepare helpers. A read callback must use fault-aware bounded accesses and
publish only a complete snapshot. The future consumer must use
`netbsd,consumer-managed-power` to prevent automatic FDT power changes.

Only after those adapters pass software review can a separate local DE0
clock/reset experiment be considered. It needs a documented order, field
preservation, bounded readback, supply/Q-channel constraints and retention
on any attempted write. It must leave PLL_DE and shared ancestors unchanged.
If that borrowed path cannot work, a dedicated PLL_DE start needs separate
factor, voltage, lock and complete consumer-handoff evidence. A disabled
PLL_DE is an expected starting state, not a permanent rejection condition.

The [RGB/RCQ format encoder](a733-de352-format.md) supplies checked register
values and descriptor bytes. The following milestone is one CPU-filled
XRGB8888 plane with DE3.52 RCQ updates and measured TV0 vblank. It still needs a validated DMA/IOMMU path,
unused-block disable sequence, TCON/VO1 timing, HDMI PHY, HPD and DDC/EDID.
The [observer document](a733-display-observer.md) defines those boundaries.
No result from this core is KMS, HDMI, PRIME or accelerated Plasma acceptance.

## Sources and checks

This implementation is original BSD-2-Clause code, written with AI assistance.
The [pinned manual and BSP](a733-display-observer.md#sources-and-license-boundary)
supply hardware facts. Manual sections 4.1.6.6, 8, 9, 66, 82, 83, 104, 105, 113,
249 and 258 cover the additional PERIPH0/AHB/gate fields. The pinned BSP
`bsp/drivers/clk/sunxi-ng/ccu-sun60iw2.c` defines REF normalization and DE0
selector 3. The existing native GPU provider supplies the BSD rate-evaluation
pattern. Arm DEN0051E defines PCK policy and state fields. GPL C is not copied.

Run the actual production-core contract on a host with Python 3 and a native
C compiler:

```sh
python3 ember/tools/a733-display-owner-contract.py
DISPLAY_OWNER_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    python3 ember/tools/a733-display-owner-contract.py
```

`CC`, `DISPLAY_OWNER_SOURCE_ROOT`, `DISPLAY_OWNER_CFLAGS` and `TEST_RUNNER`
can select the compiler, input source, analyzer flags and supported execution
wrapper. The runner only replaces kernel include plumbing; it compiles the
complete production bodies. Fixtures cover in-flight and protected writers,
partial or stale observations, fractional and stopped parents, each required
gate, actual RTC normalization, error publication, retention and sticky faults.
A host fixture does not validate physical timing or autonomous firmware.
