# A733 IOMMU1 inactive-peer lease

This is an original BSD-2-Clause native CCU/PCK mechanism with AI assistance.
Reservation performs no clock, reset, power or IOMMU writes. A separate
fixed operation can request domain 2 ON. Neither operation establishes DMA
readiness. Use them before a separately admitted managed DE memory operation.

## Fixed admission

The caller already owns DISPLAY domains 1/10, CCU and the seven-word memory
claim. The new subclaim must precede retention and any attempted local write.
It reserves PCK domain 2 VE_DEC with the same cookie, without power writes.
Domain 2 must remain static ON or OFF, policy/status equal, PMER zero, and no
dynamic mode. Two complete samples must match the reserved state. A changed
state, quarantine, read fault, wrong owner or malformed provider refuses use.

The CCU mutex excludes pending generic writers before reservation. The fixed
peer fields must be zero in both samples. These masks stay protected against
future generic dispatch and direct native masked RMW for the display lifetime.

| IOMMU1 master | CCU module gate | Bus reset and gate | Extra gate |
| --- | --- | --- | --- |
| 0/1 VE_MBUS0/1 | 0xa88 bit31 | 0xa8c bits18/2 | 0x5e4 bit18 |
| 3 DI | 0xa20 bit31 | 0xa24 bits16/0 | None |
| 4 G2D | 0xa40 bit31 | 0xa44 bits16/0 | None |
| 5 EINK | 0xa60 and 0xa64 bit31 | 0xa6c bits16/0 | None |

Zero reset bits mean asserted. Zero gate bits mean closed. VE encoder fields
in 0xa8c and other 0x5e4 masters remain available. No peer MMIO is read.

`begin_de0` first retains the existing display power lease, then domain 2.
Any failure after the first retention preserves all claims and the first
error. The fresh isolation check precedes the existing DE0 writes.
`display_verify_locked` rechecks the subclaim for ready/HDMI/TV0 phases.
The raw state getter publishes only after complete successful capture.
A clean pre-retention release frees domain 2 before dropping CCU masks.
No global IOMMU operation is authorized by this read-only subclaim.

## Retained peer register access

`sun60i_a733_ccu_display_begin_peer_access` holds the CCU mutex. It requires
prepared DE0, retained same-cookie base power, memory and inactive-peer claims.
It checks the complete current profile before calling the fixed PCK helper.
The lock order is CCU, then PCK. No peer clock or reset is changed.

`sun60i_a733_pck_display_peer_on` is an internal provider operation. Call it
only through the CCU wrapper. It requires retained slots 1, 10 and 2 with the
same owner. It checks both base domains and the exact reserved domain-2 state.
Static ON needs no write. The first OFF profile requires PWPR/PWSR/PMER all
zero. Locked, dynamic, emulated or unknown OFF bits refuse the request.

The OFF operation writes the existing five PCK delay values, then requests
static ON. Every write uses fault-aware poke, a bus barrier and exact readback.
It rechecks the power profile after the delay writes. Policy denial, emulation,
unknown status or an access error stops the operation. Status polling has a
10 ms limit. A complete new two-sample ON state replaces the old expectation
only after final parent and domain checks. The CCU wrapper then rechecks its
full retained profile. No peer or IOMMU MMIO is part of this operation.

Any failed write attempt quarantines domain 2. The wrapper keeps the first
error and all claims. The code never powers the domain OFF, reverses a partial
transition or releases retained resources. This mechanism permits a later
admitted all-MIC access; it does not authorize global IOMMU initialization.
The existing master-2 local operation does not call it.

## Required global-init continuation

1. Hold this peer lease, retained base/memory ownership and cold DE internal
   reset/gates with no allocated or published DMA. Refuse an active peer.
2. Capture IOMMU1 only after the existing exact CCU aperture/clock proof.
   Check version 0x16, global enable, all six micro resets/bypass/error states,
   global errors and register values needed by the selected init profile.
3. Establish the exact managed sequence before native writes. A bypass-only
   profile may omit tables or invalidation only with separate evidence. The
   available BSP init does not prove flush completion. No fallback DMA tag
   or unproved global-enable shortcut is admitted.
4. Retain any table allocation before publishing its address. Recheck owned
   state before each attempted operation and preserve claims on any failure.
5. Only after actual completion and strict memory admission may DE internal
   reset/gates and the existing RGB/RCQ path start.

Unresolved hardware facts: IOMMU v0x16 global/system reset cache semantics;
exact full-flush completion/status; safe initialized TTB alignment/extent and
all-bypass operation. The BSP flush and local invalidation functions each
wait 10 us, write the control back to zero, and return 0 without a status poll.
These functions cannot serve as a successful-completion oracle. An asserted
peer reset is an isolation condition, not a proved bus-drain event.

## Provenance

Allwinner A733 manual v1.00 SHA256
cdd77e5779d3c69d41b5f237f3db3db51c67fdf0f3aca9a2eedc77d9420f248d:
chapter 10 master map; sections 4.1.6.84 and 106-116 leaf fields/reset meanings.
The pinned [BSP](https://github.com/orangepi-xunlong/linux-orangepi/tree/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f)
uses commit `2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f`.
`bsp/configs/linux-5.15/sun60iw2p1.dtsi` maps IOMMU1 masters 0/1 to
VE_DEC power and masters 2-5 to DE_SYS power. The DTS is GPL-2.0+ or MIT.
`bsp/drivers/iommu/sunxi-iommu-v2.c` supplies offsets, table-base shift,
bypass/reset and flush/invalidation facts. Its SPDX identifier is
GPL-2.0-or-later. No GPL implementation is copied.
New native code is independently written BSD-2-Clause, AI-assisted.

## Checks

Run `sh ember/tools/a733-accelerator-clock-contract.sh` and
`sh ember/tools/a733-power-contract.sh`. The existing actual-provider fixtures
include inactive-peer acquisition, conflicting native writers, incomplete
samples, cleanup failure, retained ownership and changed peer/power state.
The fixed ON controls cover every read and write failure, exact write order,
denial, timeout, post-transition proof and sticky retention.
Use `CLOCK_TEST_CFLAGS` or `POWER_TEST_CFLAGS` for host sanitizer flags.
The checks remain active with `-DNDEBUG`. They do not enable a board device.
