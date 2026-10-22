# A733 display provider observer

The A733 observer records CCU and PCK state at provider attachment. It does
not enable a display pipeline, map DE/TCON/HDMI registers, or expose KMS.
Equal samples do not establish power, clock, reset, memory or firmware ownership.
This step has no physical HDMI acceptance.

## Read contract

The implementation uses the existing native CCU and PCK mappings. It adds no
clock references, resets, power requests, interrupts, DMA or polling loops.
The PCK provider already enables its own programming clock at attachment;
this observer does not change that behavior.

The CCU adapter validates the entire register aperture before any read.
It takes two ordered samples of this allowlist, for 46 32-bit reads:

| CCU offset | Register |
|---|---|
| `0x02e0` | PLL_DE_CTRL |
| `0x05c0` | AHB_MASTER_GATE |
| `0x05e0` | MBUS_MASTER_GATE |
| `0x0a00` | DE0_CLK |
| `0x0a04` | DE0_BGR |
| `0x0a74` | DE_SYS_BGR |
| `0x1604`, `0x160c` | TCONTV0_BGR, TCONTV1_BGR |
| `0x1684`, `0x168c`, `0x1690` | HDMI_TV_CLK, HDMI_BGR, HDMI_SFR_CLK |
| `0x16cc`, `0x16ec` | DPSS_TOP1_BGR, VIDEO_OUT1_BGR |
| `0x1928`, `0x19a8` | DEPLL_GATE_EN, DEPLL_GATE_STAT |
| `0x02e8`, `0x02ec` | PLL_DE_PAT0, PLL_DE_PAT1 |
| `0x0580` | NSI_CLK |
| `0x0a80`, `0x0a88` | VE_ENC0_CLK, VE_DEC_CLK |
| `0x0b00`, `0x0c00`, `0x1840` | NPU_CLK, DRAM0_CLK, CSI_CLK |

These registers have no documented read-to-clear fields. The observer does
not interpret reserved bits or change the PLL, gate or reset registers.
The table is a bounded observation contract, not the full HDMI clock tree.

The PCK adapter takes two passes over DE_SYS (domain 1) and VO1 (domain 10).
Each pass reads PWPR, PMER and PWSR at domain-relative offsets `0`, `4` and `8`.
Domain windows are 4 KiB. The existing provider lock protects each triple
from native writers. A quarantined domain returns `EIO` before its reads.
No display-domain transition is requested.

Both adapters use `bus_space_peek_4`. A synchronous access fault returns
`EFAULT`. A complete snapshot is published only after every read succeeds.
Failures print an unavailable diagnostic and leave the provider attached.
There are no retries or fallback reads. Fault-aware access cannot bound a
hardware transaction that stalls indefinitely.

The reports contain both raw samples and a changed-register mask. CCU mask
bits follow table order. PCK mask bits follow domain order, then PWPR, PMER,
PWSR. A zero mask means only that these samples compare equal. Samples are
not atomic across the providers, and the locks do not stop autonomous hardware.
Neither report authorizes an access to a display block.

The callback-based acquisition and commit-after-success pattern also serves
the [BCM2712 resource observer](../../broadcom/docs/bcm2712-display-contract.md). A733 uses its own
provider interfaces, register map and power domains. It does not use the
BCM2712 mailbox, ACPI resources or register definitions.

## Decoded clock observations

The pure decoder accepts only equal samples with a zero changed mask. It
compares the samples itself and leaves the output unchanged on error.
It performs no I/O. Documented PLL control and pattern fields remain raw;
post-PLL gate control/status is separate from PLL enable and lock. Open
post-PLL gates can coexist with a disabled, unlocked PLL. No rate is inferred.

The selected-source and module-gate bitmaps use this order:

| Bit | Consumer | CCU register | DE3X selector | DE4X selector |
| --- | --- | --- | --- | --- |
| 0 | DE0 | `0x0a00` | 0 | 1 |
| 1 | NSI | `0x0580` | 5 | none |
| 2 | VE_ENC0 | `0x0a80` | 5 | none |
| 3 | VE_DEC | `0x0a88` | 5 | none |
| 4 | NPU | `0x0b00` | 6 | none |
| 5 | DRAM0 | `0x0c00` | 3 | none |
| 6 | CSI | `0x1840` | none | 1 |
| 7 | HDMI_TV | `0x1684` | none | none |
| 8 | HDMI_SFR | `0x1690` | none | none |

Selectors occupy bits 26:24. Module gates use bit 31. The decoder reports
selection even when that gate is off. Reserved selectors set a separate
unknown mask. NSI/DRAM0 update bit 27 and NSI dynamic mode bit 28 remain
separate observations; equal samples do not mean an update completed.
DRAM0 bit 16 also selects the external memory clock path. The decoder does
not infer memory ownership or an idle DRAM path from that bit or its gate.

DE0 and HDMI_TV divisors are linear fields plus one. HDMI_TV has two such
fields (4:0 and 12:8); its second divisor is not a power of two. HDMI_SFR
has no divider. Bus-gate and reset bitmaps use snapshot register indices;
HDMI reset bits 18:16 are reported separately as bits 2:0. DE_SYS and VO1
registers contain reset bit 16, not a bus gate at bit 0.

This census is a list of potential consumers, not a lease or proof that all
firmware consumers are known. DEPLL3X can feed DRAM0, NSI, VE and NPU; DEPLL4X
can feed CSI. The source must not be treated as a dedicated display PLL.
No new clock/reset providers, FDT consumers or power requests are added.

## Sources and license boundary

The code is an original BSD-2-Clause implementation. Register facts come from
Allwinner's A733 User Manual V1.00 (2025-08-14), sections 4.1.6.61, 82, 83,
63, 64, 74, 104, 105, 113-115, 119, 123, 225, 226, 230-232, 235, 237,
244, 256 and 265. The public
[manual source](https://gitlab.com/api/v4/projects/71118510/repository/files/A733%2FHardware%E7%A1%AC%E4%BB%B6%E7%B1%BB%E6%96%87%E6%A1%A3%2F%E8%8A%AF%E7%89%87%E6%89%8B%E5%86%8C%2FA733_User%20Manual_V1.00.pdf/raw?ref=d0dc4d615be00b76e3a0818d0818eff8f69ffa15)
is pinned to repository revision `d0dc4d615be00b76e3a0818d0818eff8f69ffa15`.
Its PDF SHA256 is
`cdd77e5779d3c69d41b5f237f3db3db51c67fdf0f3aca9a2eedc77d9420f248d`.
The manual retains Allwinner copyright; it is not BSD-licensed source code.
This repository does not redistribute it or copy its implementation prose.

PCK register facts and access semantics come from Arm DEN0051E. The
[existing provider documentation](a733-power-domains.md) records that source.
The [A733 power binding](../../../../external/gpl2/dts/dist/include/dt-bindings/power/allwinner,sun60i-a733-pck-600.h)
uses the `GPL-2.0-only OR BSD-2-Clause` license; the BSD option permits reuse
of its IDs.
The Allwinner BSP at
[`2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f`](https://github.com/orangepi-xunlong/linux-orangepi/tree/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f)
provides a cross-check for platform topology. Its GPL driver implementations
are not copied into this BSD driver.

## Software checks

```sh
sh ember/tools/a733-display-observer-contract.sh
sh ember/tools/a733-accelerator-clock-contract.sh
sh ember/tools/a733-power-contract.sh
```

The snapshot fixture compiles the production acquisition code. It checks
exact read order, aperture rejection before the first access, every callback
failure, unchanged error output, raw reserved values and each changed bit.
The existing clock and power fixtures compile the actual provider adapters.
They check zero added writes, faults at each access, quarantine, lock release,
and continued attachment without partial diagnostic output. Decoder checks cover
every mux selector, divider and gate, stale or inconsistent change masks,
reserved bits, pending updates, SDM fields and disabled PLLs with open gates.

Use `DISPLAY_OBSERVER_CFLAGS`, `CLOCK_TEST_CFLAGS` and `POWER_TEST_CFLAGS`
with `-fsanitize=address,undefined -fno-omit-frame-pointer` for host sanitizer
checks. These tests use fake MMIO. They do not validate hardware timing,
physical display power, HDMI output, GPU rendering or KMS.

## Next minimum: one RGB plane and vblank

The next display milestone is one CPU-filled, linear XRGB8888 scanout buffer,
one fixed progressive mode, one connector and measured vblank interrupts.
Do not enable it from these raw observations. It first needs explicit resource
ownership and a reviewed initialization and failure sequence:

1. Add exact A733 DE, TCON_TV0, VO1 and HDMI FDT resources and dependencies.
   Implement their missing CCU clocks/resets and display power ownership.
   Preserve unrelated clock/reset fields. Confirm supplies, pinctrl and the
   selected HDMI PHY clock source before accessing a display block.
2. Establish the DE3.52 register contract for one RGB layer: channel routing,
   stride/address width, format, blender output, update/latch semantics and
   interrupt acknowledgment. Disable unused enhancement/compression/scaling
   blocks through a documented sequence. Do not assume the existing DE2
   `sunxi_mixer` register layout applies.
3. Allocate and synchronize the buffer through `bus_dma`. Specify the display
   IOMMU stream, address translation and memory ownership. Keep the buffer
   pinned until scanout and pending register updates have stopped. Reject
   unsupported addresses and geometry before programming hardware.
4. Program TCON_TV0 timing, VO1 routing and the HDMI PHY/transmitter for the
   declared mode. Confirm sink connection and a supported mode through DDC.
   Manual section 15.2 gives VO1 at `0x05510000`, TCON_TV0 at `0x05730000`,
   and TCON_TV1 at `0x05731000`. The BSP name `tcon3` identifies TV0, not TV1.
5. Implement a bounded vblank wait and IRQ lifecycle. TCON_TV `GINT0` at `+4`
   has vblank enable bit 30 and flag bit 14. The flag is **write-zero-to-clear**,
   according to manual section 15.2.10.2. Preserve unrelated interrupt state;
   do not apply a generic W1C acknowledgement. Confirm which pipeline event
   completes a DE register update before implementing page flips.

The manual's section 14.3 describes DE features, but does not supply the
DE3.52 plane register map. The missing table must specify the DE3.52 channel
register bases, linear RGB layer address/stride/format fields, channel-to-output
routing, blender size and background, register-update/latch protocol, and
completion/status/interrupt-clear semantics. Record each fact with a pinned
source and its provenance before implementing that contract. A complete
permissive driver is not required: documented register facts can support an
independent BSD mechanism. GPL vendor C must not be copied as implementation.
A reusable permissive DE3.52 register header has not been established.
The existing BSD-2-Clause sunxi DE2/TCON/HDMI code can supply framework patterns;
each A733 register and sequence still needs independent evidence.

Acceptance must retain the exact kernel/DTB cohort, register prerequisites,
mode, buffer checksum, observed image and vblank count/timing. Include timeout,
unplug, invalid-buffer and teardown checks. A static RGB image plus interrupts
is a separate milestone from DRM atomic modesetting and GPU-backed scanout.
KMS, buffer import/export and explicit synchronization remain necessary for
accelerated Plasma. A firmware framebuffer is usable only with an actual
validated framebuffer handoff and reserved memory; it must not be inferred
from a connected HDMI cable or offscreen GPU success.

## Boundary before the first initialization write

The [display ownership core](a733-display-ownership.md) defines a write-free
reservation contract. Its native adapters remain to be connected. Use
the native provider arbitration pattern: count in-flight CCU writers before
acquisition, protect shared ancestor mutations, and reserve DE_SYS/VO1 under
the PCK lock. Existing GPU reservations must continue to work. The generic
FDT power API does not reference-count these shared domains. A future
consumer must use `netbsd,consumer-managed-power` to prevent automatic power
changes before its own validation.

Before a separately reviewed DE0 clock/reset initialization can run, the
consumer must hold those reservations and take fresh stable snapshots. Both
power domains must remain strict static ON with no emulation, dynamic policy,
quarantine or pending transition. DE0 must be gated and held in reset. The
chosen parent must have a separately proven running rate and a retained lease;
no parent, NSI, DRAM or shared-domain reset is retuned or toggled. Unknown or
pending muxes fail closed. Any attempted write retains the reservations until
reboot unless a separate stop/rollback contract proves release safe.

These conditions permit only the reviewed local clock/reset experiment.
They do not permit DE/TCON/HDMI MMIO, DMA or scanout. That next boundary still
needs Q/clock sequencing, VO1/PHY/supply ownership and the RGB/RCQ contract
above. If no already-running parent can be borrowed, a PLL_DE start needs a
separate complete consumer and firmware handoff audit. A zero census mask
cannot authorize it.
