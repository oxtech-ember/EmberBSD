# A733 display resource ownership core

The core in `sun60i_a733_display_owner.c` supplies native writer arbitration
and a retained reservation boundary. The core performs no I/O. The native
CCU adapter uses this core under the same mutex as native writer admission
and masked RMW. It can prepare one fixed local DE0 clock profile. It does
not access DE/TCON/HDMI apertures or establish scanout readiness.

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

The native CCU API uses the decoded `CLK_DE0` handle and one coordinator
cookie. The caller first reserves PCK domains 1/10, then calls
`sun60i_a733_ccu_display_reserve(de0, power_phandle, cookie)`. This operation
requires the matching PCK reservation and fresh strict ON samples. It checks
the documented borrowed ancestors before accepting the reservation. It uses
fault-aware provider reads, validates the full CCU aperture before acquisition,
and brackets the two CCU samples with RTC observations. Lock order is CCU
then PCK. No PCK callback acquires CCU. No mutex spans recursive clock dispatch.
The ordinary DE0 enable/rate/parent paths fail without touching hardware.

`sun60i_a733_ccu_display_begin_de0(de0, cookie, &retained)` first retains PCK.
It then invokes the pure core's fresh before-state and running-ancestor checks.
The returned flag means that resources are retained, including a failure after
PCK retention but before a CCU write. The local sequence has four masked steps:

1. Configure DE0 selector 3 and divider 1 at `0x0a00`, while gate31 is off.
2. Release DE0 reset16 at `0x0a04`.
3. Open DE0 bus gate0 at `0x0a04`.
4. Open DE0 module gate31 at `0x0a00`.

Each changed field has a bounded masked readback. The provider preserves
unrelated fields. It does not write PLL_DE, shared ancestors, master gates,
power controls, TV0 or HDMI. The pinned BSP `sunxi_de.c` reset/clock admission
at lines 889–939 supplies the reset-before-bus/module order. Manual sections
4.1.6.104–105 define the DE0 mux, divider, gate and reset fields. The 400 MHz
choice is an explicit native profile, not a claim about the BSP's selected rate.

Before retention, release the CCU claim first, then the PCK claim. A failed
release keeps both claims. After retention, no release or rollback is permitted.
`sun60i_a733_ccu_display_fault` records the first error without any hardware
write. A fresh `ready` call checks the matching retained PCK lease, running
ancestors and completed local profile. Status returns cached ownership, fault
and last verified rates. It is not a fresh readiness probe.

Native mutation guards protect the documented local display fields, shared
REF/PERIPH/AHB state, DE/VO1 AHB bits5/4 and auto-gating bit31, and DE_SYS MBUS
bit11. GPU/NPU local masks remain independent. Fresh post-state comparisons
ignore their disjoint AHB/MBUS fields. The initial pure-core full snapshot
comparison remains unchanged. These guards do not exclude autonomous firmware.

This boundary proves only the reviewed CCU/PCK local profile. A separate
retained memory claim must protect IOMMU/NSI/MBUS dependencies before their
MMIO or DMA publication. The consumer must use `netbsd,consumer-managed-power`
to prevent automatic FDT power changes. Output preparation, PLL_DE start,
physical timing, supply/Q-channel constraints and a stop contract remain
separate acceptance boundaries.

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
The existing clock and power contracts also compile the complete native
adapter. They exercise every provider peek fault, local write/readback error,
PCK-before-write retention, pending writers, protected native masks, sticky
faults and complete snapshot publication. A host fixture does not validate
physical timing or autonomous firmware.
