# Fixed private HVS/PV0 publication mechanism

The [helper](../../sys/external/bsd/drm2/bcmv3d/bcmv3d_hvs_publish.c) publishes
one sealed initial test frame through HVS0 and PV0. The manual
`EMBERV3DCONSOLE` profile calls it from the same retained ACPI boot owner after
[actual native MMU4 completion](bcmv3d-mmu4-native.md). Ordinary profiles do not
select this caller. The mechanism has no attachment, clock control, mapping or
allocation. This source slice does not establish physical scanout, DRM/KMS or a
Plasma compositor frame.

## Hardware boundary

The complete manual owner produced the correct eight-bar frame on BCM2712
hardware. The same owner completed the [unused MMU4 range check](bcmv3d-mmu4-range.md)
while keeping the initial IOVA0 frame. Four native V3D regression programs passed.
Only the fixed test frame has physical display acceptance. DRM/KMS, mutable HVS
submission, page flips, active scanout retirement and physical Plasma remain open.

## Input and ownership

The caller supplies the actual opaque MMU4 native owner, synchronous register
callbacks, the existing typed `bcm2712_hvs_frame` reservation and fresh zeroed
[status storage](../../sys/external/bsd/drm2/bcmv3d/bcmv3d_hvs_publish.h).
The helper calls actual native cached status. It requires successful COMPLETE,
valid sequence/shared/ICAC/backing snapshots, retained member 4 only, successful
cache flush, and sealed/prepared backing with five completed PREWRITE syncs.
The source is the fixed MMU4 IOVA 0 with span 8294400. Neither a frame PA nor
`bo->gpu` is accepted. The existing HVS encoder validates the concrete UPM
range and slot. Metadata does not acquire that range or prove capacity.

The same resident boot owner must hold canonical mappings, normal-active fabric,
all real clock lifetimes, successful CPU/firmware handoff and writer exclusion.
Before this call it must own SRAM words 32..41, the supplied UPM reservation and
the inherited COB partition. Disabled MODE, EMPTY, firmware ACK and matching
registers do not prove a free pool, ownership or AXI drain. No new permission
token or allocator facade replaces these obligations. The helper and its fixture
do not construct a native reservation. The fixed native policy below owns it.

The callback windows map canonical resources 3, 5, 6, 11, 16, 17, 18, 19 and 22:
HVS, PV0, PV1, shared HD, HDMI0 core, DVP0, PHY0, RM0 and CSC0. Only HVS and PV0
are written. HDMI VID_CTL.ENABLE remains intact. HDMI timing, PHY, CSC, AVI,
quantization and sink policy remain inherited. All backing, storage, callbacks,
mappings, reservations and clock references remain alive after every outcome.
There is no retry, rollback, free, unmap, stop or drain interface.

## Fixed native boot owner

The selected profile permits one canonical GPU0 boot owner and one HVS writer.
It excludes other display drivers, manual probes, suspend and detach until reboot.
The same owner retains the exact CLOSED issuer lease, successful CPU close/drain,
real held serial route, firmware ACK, four clock leases, both stopped PV/HVS pipes,
actual renderer tuple and programmed MMU4 native object. These are required before
the attempt. The helper queries the actual full native cached status before any
callback, map or register read. No copied success flag replaces that query.

This first and only writer reserves SRAM words 32..41 and UPM bytes 0..15615 in
slot 0. It retains the inherited COB partition. The backing is the actual sealed
fixed IOVA 0/span 8294400 object. The HVS encoder bounds the UPM range against the
pinned 262144-byte pool and 32 handles. The native callback reads CXM_SIZE at +4
after a recognized matching VERSION and requires at least 42 list words. This
bounds the concrete list; it does not measure a free pool or acquire ownership.
The helper checks all three inherited COB words before any write.

The owner borrows its retained HVS/PV0/PV1 mappings. After actual native admission,
it freshly reads both retained MOP/MOPlet control words and refuses GO or BUSY.
It lazily maps only the six required read-only canonical windows. Callbacks check
the closed window enum, alignment, complete register bounds and active execution.
Read-only writes are refused. The attempt is published before all Giant recursion
is dropped. The exact recursion depth is restored after the synchronous helper.
Every error stops later I/O and retains maps, backing and reservations until reboot.

The terminal log reports the first profile mismatch, retained MOP words, list
capacity, RUN status and active list. After helper completion, a single 20 ms
diagnostic sample records STATUS and the six-bit frame counter. An unchanged
counter does not fail completion. A failed diagnostic read retains its error and
the separate completed helper result. No sample proves visible pixels or AXI drain.
A coherent GCC16 kernel and four-module kit links successfully. Actual target-TU
GCC, Cppcheck and Clang Static Analyzer checks pass their recorded classifications.
On one BCM2712 board, native MMU4 completes and the four GPU tests pass.
The source supports complete converted-RGB and retained CSC-bypass signatures.
One BCM2712 system displays the eight initial color bars in the expected order.
The retained-bypass path reaches COMPLETE with active list32 and a progressing
frame counter. Four native V3D regressions pass with HVS enabled.
This accepts one fixed test frame. It does not accept DRM/KMS, page flips,
buffer retirement, AXI drain or physical HDMI Plasma.

## Conservative inherited profile


The helper uses the actual fixed PV0 and HDMI0 encoders for 1920/2008/2052/2200,
1080/1084/1089/1125, progressive positive syncs. A GOP size, an HSM rate or an
inactive HVS is insufficient. The additional read-only signature is deliberately
strict. An equivalent firmware configuration outside it is refused. The last
window, offset, raw value, mask and expected value identify the first mismatch.
A future owner can then implement complete mode programming from actual facts.

| Window and offset | Checked mask | Required value |
|---|---:|---:|
| HVS global +20 | 807d30f0 | 802000f0 |
| HVS priority maps | ffffffff | ffffffff, ffffffff |
| HVS COB0/1/2 | ffffffff | 87004b10, 4b000f10, 0f000000 |
| PV0 +0 | 00e0003d | 0 |
| PV0 +4 / +c / +10, native PPC1 | ffffffff | 20000002 / 0094002c / 00580780 |
| PV0 +4 / +c / +10, inherited PPC2 | ffffffff | 2 / 004a0016 / 002c03c0 |
| PV0 +8 / +14 / +18 | ffffffff | 0 / 00240005 / 00040438 |
| PV0 +24 / +34 / +94 | 3ff / 3c / ff0 | 0 / 20 / 110 |
| HDMI0 +ec / +f0 | 1fffffff / 07ff07ff | 0058c780 / 0094002c |
| HDMI0 +f4 / +100 | 1f7f1fff | 05040438 |
| HDMI0 +f8 / +104 | 3fff01ff | 24 / 044c0024 |
| Shared HD +44 | 99040000 | 80000000 |
| HDMI0 +18c / +114 / +1e4 / +7c | 70f / f / 1 / 21 | 200 / 0 / 0 / 1 |
| DVP0 +bc / +f0 / +f4 | 2 / c / ffffff | 0 / 0 / 354021 |
| CSC0 +0 / +2c | 7f / c0 | 7 / 0 |
| PHY0 +1c / +28 / +2c / +44 / +54 | 23ff / 1f / 7ff / f / 3 | 2036 / 9 / 407 / 0 / 0 |
| PHY0 +0 / +4 / +190 / +194 | 7f / 1cf / 1 / 1 | 7f / 1cf / 1 / 1 |
| RM0 +18 | ffffffff | 98100000 |

The table describes the converted-RGB signature. A second complete firmware
signature requires all of these replacements together:

| Word | Bypass value |
|---|---:|
| PV VCONTROL / HORZA / HORZB | 2 / 004a0016 / 002c03c0 |
| PV PIPE_INIT_CTRL under maskff0 | 100 |
| HDMI VERTA0/1 / VERTB1 | 05000438 / 24 |
| HDMI deep color under mask70f | 4 |
| DVP XBAR | 543210 |
| CSC CTL under mask7f | 3b |
| PHY RESET_CTL under mask7f | 4f |

CSC CTL3b selects this signature. ENABLE bit2 is clear and 444-to-422 bit6 is
clear. The six inactive coefficients remain cached but do not gate admission.
Every other table check still applies. Mixed signatures fail before the first
write. This preserves a complete inherited path. It does not derive a new mode
from its unusual vertical fields or program the PHY. Active CSC still requires
the full or limited RGB matrix below. Visible colors need a display test.

All table numbers are hexadecimal. PV CONTROL retains the inherited FIFO levels,
CLR_AT_START, WAIT_HSTART and underflow policy. They are driver tuning, not a
format or routing proof. Only FORMAT24, PIXEL_REP0, CLK_SELECT0 and EN0 are
checked there. Exact VCONTROL selects one complete horizontal pair. VCONTROL
20000002 requires native PPC1 HORZA/HORZB 0094002c/00580780. VCONTROL 2 requires
inherited PPC2 004a0016/002c03c0. Crossed or mixed pairs are refused. Interlace
and every other VCONTROL encoding, including unknown bits 31 and 23, are refused.
The native BCM2712 encoder remains progressive PPC1. The helper does not write
VCONTROL bit 29 or treat it as an independent permission to halve a mode.

The pinned PV configuration divides all four horizontal components by the chosen
pixels-per-clock value. The progressive BCM2712 variant selects PPC1; its
interlaced variant selects PPC2. The retained firmware progressive packing is
accepted only as the exact additional tuple above. Vertical words, HDMI timing,
PHY, clocks, HVS width and frame span remain unchanged. The partial hardware
observation of HORZA alone does not establish the full inherited mode.

CSC coefficients must match the pinned full
RGB identity matrix or limited RGB matrix. The helper rejects YCbCr conversion,
channel remapping, pixel repetition, scrambling, negative syncs and blanking.
The PHY/RM signature corresponds to the pinned 54 MHz reference and 148.5 MHz
RGB8 programming. This is a configured digital signature, not a measured clock,
PLL-lock observation, fresh EDID decision or physical-link acceptance.

## Bounded order and errors

VERSION is the first register read. Low byte 53 selects C0, 54 selects D0.
C0 channel0 CTRL0/CTRL1/LPTR/COB/STATUS/active-DL offsets are 30/34/3c/40/44/48;
D0 offsets are 100/104/110/114/118/11c. Channel stride is 20 or 40. Priority maps
are b8/bc or 38/3c. The generation must match the supplied reservation variant.
All three HVS channels and both PVs must be inactive. RESET can remain set.

After actual native admission, generation, all inactive-pipeline checks and the
inherited HVS configuration checks, the helper captures twelve timing words. It
reads PV0 VCONTROL, VSYNCD_EVEN, HORZA/B and VERTA/B, then HDMI0 HORZA/B,
VERTA0/1 and VERTB0/1. It captures the complete group before evaluating a timing
member. The retained status records raw words and the successful prefix length.
A callback fault stops immediately and leaves later slots zero. Timing checks
use only the saved words. They retain the first mismatch window, offset, raw,
mask and expected value. The owner prints this cached snapshot after success or
refusal. Printing causes no additional register access. This bounded sequential
snapshot does not imply an atomic hardware sample or a confirmed physical mode.

After the twelve timing words, the helper captures the twenty link words in the
order from Shared HD +44 through RM0 +18 in the table. It then captures CSC0
+4, +8, +c, +10, +14 and +18. The retained `link_words` count covers only successful
reads. A read error stops the operation and leaves the rest of the array zero.
All link and CSC checks use these saved values. There is no repeated link read
for validation. Both complete RGB matrices and every profile mask stay unchanged.
The owner prints the saved twenty-word link group and six-word CSC group after
the helper returns. Neither log path performs MMIO. The snapshot can therefore
explain later link differences even when an earlier PV profile check refuses.
The snapshot does not establish a valid mode, PLL lock, display ownership or drain.

All native and inherited-profile checks finish before the first write. The helper
uses the actual plane encoder, writes ten SRAM words at +4080 through +40a4 and
checks every word while inactive. A barrier follows each successful write. The
first 128 SRAM bytes remain unchanged. LPTR receives word index 32. It receives
neither byte offset 80 nor a DMA address. The helper then writes RESET, reads
CTRL1 freshly, clears only INTLACE/BGENB, and writes CTRL0=877f0437. It restores
PV0 EN then VIDEN with fresh RMW. It never rewrites the active SRAM context.

After activation, at most 100 delays of 1 ms wait for active-DL word 32 and HVS
MODE exactly RUN. Persistent EOF does not complete this continuous profile. Completion records these observations. It does not prove visible
pixels or drain. A software timeout cannot bound a stalled native bus transaction.

NULL/invalid callbacks and invalid frame metadata return EINVAL before state
changes. Unknown frame variants return EOPNOTSUPP. After that validation the
attempt is one-shot. Missing native completion returns EBUSY; an existing native
operation result survives a later diagnostic fault. Unsupported generation/profile or an active pipeline returns
EOPNOTSUPP. Variant mismatch returns EAGAIN. A failed write readback returns EIO;
a latch timeout returns ETIMEDOUT. Callback errors remain unchanged. The first
post-attempt error is sticky, and every error stops later I/O. Registers can have
changed after a failed write. No cleanup write or release follows.

## Actual-source checks

```sh
CFLAGS=-DNDEBUG ruby ember/tools/bcmv3d-hvs-publish-contract.rb "$PWD"
HVS_PUBLISH_SKIP_MUTANTS=1 \
  CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  ruby ember/tools/bcmv3d-hvs-publish-contract.rb "$PWD"
```

The fixture reuses unchanged actual native/provider/cache/sequencer/backing
bodies and real fixed encoders. Kernel, allocation and bus services are controlled.
Independent literals check both layouts, word addresses, all ten list words,
PREWRITE status, fresh RMW, barriers, first 128 preservation, inherited profiles,
all read/write faults, partial writes, ignored writes, retained state and one-shot
refusal. Both exact PPC tuples, crossed and mixed pairs, all snapshot read faults,
pre-snapshot refusals, complete snapshots before a timing mismatch and cached
diagnostics are checked. Complete color profiles, eleven mixed-field refusals
and inactive coefficients are checked. Each causal mutant must compile, then fail its named CHECK with SIGABRT.
`HVS_PUBLISH_EVIDENCE`, `HVS_PUBLISH_SELECT`, `HVS_PUBLISH_MUTANTS`, `CC`, `CFLAGS`
and `TEST_RUNNER` preserve or select bounded host checks. These tests do not prove
native pool ownership or a working display.

The native composition check runs the actual cold/warm job, held serial route,
firmware protocol, clock engine, dual stop, native MMU4, encoders and helper:

```sh
SERIAL_HANDOFF_MUTANTS=hvs-owner-mmu4,hvs-read-busy,hvs-helper-native \
  ruby ember/tools/bootfb-serial-handoff-contract.rb "$PWD"
SKIP_MUTANTS=1 CFLAGS='-DNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer' \
  ruby ember/tools/bootfb-serial-handoff-contract.rb "$PWD"
```

Controlled services supply kernel, ACPI, renderer tuple, DMA and bus behavior.
Tests inject faults into the actual completed native object; they do not borrow
a fabricated positive MMU4 status. Both layouts, finalizer orders, both color profiles, eleven hybrids, owner and
native refusals, fresh writeback gates, every profile/read/write/map fault,
one-shot reentry, read-only windows, partial writes and retained errors are checked.
Host sanitizer execution does not establish target ABI, pool ownership or pixels.

Numeric facts use Raspberry Pi Linux

[a1a601d](https://github.com/raspberrypi/linux/tree/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4):
[HVS registers](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_regs.h#L550-L763),
[HVS install/start](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hvs.c#L1025-L1258),
[PV configure/enable](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_crtc.c#L346-L703),
[HDMI timing/CSC](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hdmi.c#L964-L1372)
and [VC6 PHY](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hdmi_phy.c#L960-L1005).
Capacity and partition facts use [HVS allocation](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hvs.c#L1637-L1722)
and [COB initialization](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hvs.c#L2087-L2110).
Only numeric facts inform this independent BSD implementation.

The explicit handoff profile can then start the separate
[native PV0 event owner](bcmv3d-vblank.md). It does not reuse the one-shot
publication flag or release the initial frame. DRM/KMS and BO retirement
remain separate integrations.
