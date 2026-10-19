# A733 DE3.52 native linear-plane probe

This is an original BSD-2-Clause driver with AI assistance. It uses hardware
facts from the pinned sources below. It imports no GPL implementation.
The probe is a native DE consumer. It is not a KMS device or a framebuffer
console. A successful software contract does not establish visible HDMI.

## Entry and ownership

Attachment validates FDT properties. It does not map, read or write display
MMIO. An administrator must request `hw.sun60ide0.stage=1` explicitly.
Use the actual attached device name. The request runs one bounded experiment.
No console output occurs inside the request handler.

The same softc address identifies the PCK, CCU, memory and output claims.
PCK domains DE_SYS 1 and VO1 10 are reserved first. The CCU DISPLAY claim is
reserved next. A separate fixed memory claim excludes native clock writers
for the whole retained lifetime. Two equal snapshots do not replace a claim.

The provider retains the PCK and CCU claims before the first attempted DE0
clock write. A provider error can still return `retained=true`. The consumer
keeps that state. It does not reset, stop or disable hardware after a fault.
Both provider fault APIs record sticky failure without hardware writes.

Before retention, release runs in reverse order. A failed CCU release keeps
both claims. A failed PCK release keeps its claim. `release_error` records
the release error separately from the initial `result`. A remaining claim
rejects another request. Release is not inferred from a failed return value.

The CCU provider selects the documented running PERIPH0-400M parent for DE0.
It does not enable PLL_DE. Its fixed operation sets mux 3/div 1 with the module
gate closed, releases DE0 reset 16, opens bus gate 0, then opens module gate 31.
Each owned field has a readback. DE_SYS and VO1 parent resets must already be
released. The provider must confirm the borrowed reference and parent rates.
This permits DE top access only. It does not prove internal clocks or DMA.

## Memory admission

The initial binding accepts `allwinner,display-engine-v352` at
`0x05000000`, size `0x400000`, with one DE0 clock and PCK domain 1.
`output` identifies the output provider.
The common A733 DTS keeps DE and both memory resource nodes disabled.
It does not supply an output phandle. A board integration must select its
native output provider before it enables DE. The zero-length
`netbsd,consumer-managed-power` property keeps generic FDT attachment from
requesting a power transition before the explicit driver operation.

The `iommus` cells must select ID 8 and bypass flag 1 from a disabled
`allwinner,iommu-v20` node at `0x03900000`, size `0x20000`, with two cells.
The `nsi` cells must select port 2 from a disabled `allwinner,sunxi-nsi-v2`
node at `0x02020000`, size `0x10000`, with one cell. Active memory providers,
nonempty ancestor `dma-ranges`, ancestor `iommus`, and `iommu-map` are rejected.
A missing IOMMU provider or an unchanged DMA tag is not mapping evidence.

The fixed memory claim covers these seven CCU words. Each is sampled twice.
The complete samples must match before any IOMMU or NSI access.

| Word | Required borrowed fields |
| --- | --- |
| IOMMU1_BGR,0x5b4 | reset 16 and gates 2:0 set |
| NSI_CFG_BGR,0x584 | reset 16 and gate 0 set |
| MBUS_CLK,0x588 | gate 31 set, mux 1 PERIPH0-600M, DFS/update clear |
| MBUS_MASTER,0x5e0 | IOMMU1 bit 1 and DE_SYS bit 11 set |
| PERIPH_GATE,0x1908 | PERIPH0-600M manual gate 9 and configuration 25 set |
| PERIPH_STATUS,0x1988 | PERIPH0-600M status 25 set |
| NSI_CLK,0x580 | gate 31/reset 30 set, mux 3 PERIPH0-600M, DFS/update clear |

Only then does the consumer map IOMMU1 at `0x03910000` and NSI port 2.
The two complete IOMMU1 samples must show version 0x16, master 2 reset bit 2
released, global enable 1, master 2 bypass 1, and no master 2 interrupt status.
The offsets are 0,0x10,0x14,0x804,0x838. NSI mode 0x410 bits 1:0 must be 0;
select 0x418 bit 0 must be 1. Priority 0x414 is recorded without an ownership claim.
The strict proof repeats before DMA publication and after RCQ completion plus
vblank. The already valid borrowed profile needs no IOMMU or NSI write.

If the clocks are valid but the local master profile differs, the coordinator
can prepare master 2 before DMA allocation. This requires version 0x16,
global enable exactly 1, no unknown bypass bits and no master 2 interrupt.
It first acquires the [inactive-peer lease](a733-iommu1-isolation.md), before
retention. After DE0 top access is available, every internal DE module gate
must remain closed and core 0 reset asserted. No DMA object may exist.

The finite sequence sets master 2 bypass, NSI port 2 mode 0, NSI port 2 select
1, then releases master 2 reset. It preserves all other bits. Before each
step, the coordinator checks the retained owner, peer lease, cold DE state
and complete expected memory snapshot. It checks the full field word again
before a write and verifies the full written word afterward. It repeats the
cold-state and strict memory proof before it opens the internal DE core.
Any failure after retention keeps all claims and records sticky failure.

This profile does not write global IOMMU enable/reset, table addresses,
interrupt controls, TLB invalidation, shared clocks or peer registers. A
global-off IOMMU1 still requires a separate cold-init contract and physical
acceptance. The inactive-peer lease alone does not authorize global writes.

The read-only `memory` sysctl returns native-endian 32-bit unsigned words:
version,valid,clocks[2][7],iommu[2][5],nsi[2][3]. Version is 1. Valid bit 0
means complete clock capture. Bit 1 means complete IOMMU and NSI capture.
These bits do not indicate acceptance. A partial access does not publish
partly initialized data. Each new proof clears the previous capture.

## Internal DE and first frame

Before internal writes, every DE module clock must be closed and core 0
reset must still be asserted. A closed gate does not prove that retained
RTMX/DMA registers are inactive. The consumer never asserts reset to replace
unknown state. All present channels must select core 0. Other output muxes
must not select TV0. The probe releases internal core 0 reset, opens its gate,
then releases the shared MBUS
reset and opens its gate. It preserves other fields. It does not open WB.
The older A733 top implementation supports this per-core gate operation;
the newer DRM implementation also opens WB. The isolated native choice
needs physical acceptance and is not inferred from a copied driver sequence.

The consumer checks that RTMX0, RCQ, offline mode, WB, blender pipes and every
present OVL layer are inactive. It rejects active AFBD/ATW/TFBD, UCH0 CSC,
scaling and backend FMT, deband, dither, gamma and SMBL processing.
It rejects unknown RTMX and layer bits that the fixed list would overwrite.
It does not clear an aperture or assume processing reset defaults.

The route is UCH0 layer 0, physical channel 6, routing ID 8, blender port 3,
pipe 0, DE0, vendor TCON 3 (TV0). The fixed output is progressive RGB888 with
one pixel per clock. The output provider selects and validates an exact
sink timing. The DE consumer does not select or round a pixel clock.
The DMA geometry limit is 1920 by 1080; it does not promise every such mode.

The [native DMA allocator](a733-de352-dma.md) prepares one XRGB8888 color-bar
plane and a two-entry RCQ list below 4 GiB. The consumer configures only known
owned fields and preserves unrelated bits. The output order is:

1. Obtain an accepted mode through the retained output owner.
2. Allocate the plane and queue through native `bus_dma`.
3. Prepare the output timing and DE route.
4. Enable DE RTMX0 and start the output provider.
5. Recheck memory admission and publish both complete DMA maps.
6. Write and verify the queue address and the literal list length 32.
7. Clear prior RCQ flags, then request an update during observed DE busy.
8. Observe a new RCQ finish, matching layer/address/pipe registers, and two
   distinct output vblank sequences. Recheck memory admission.

The busy sample and request have a short interrupt-disabled critical section.
The request bit can clear itself; the consumer does not require readback 1.
Waits have a 100 ms deadline. RCQ flags use W1C. Output TV0 vblank uses a separate
provider contract and W0C. A stale finish flag or one old vblank is insufficient.
The fixed list length 32 does not establish a full 32-bit writable length field.

DMA publication is irreversible before the first address write attempt.
An error or timeout after publication retains both buffers. The driver has
no stop, detach or unload operation. The repeated-frame interface below
can retire a replaced external frame. It cannot release the retained hardware
claims or establish a general stop/isolation operation.

## Repeated linear frames

`sun60i_a733_de352_scanout.h` exposes a cached state query, one opaque client,
and synchronous frame submission. The query performs no display I/O. It
requires successful explicit stage 1. The stable DMA tag belongs to the retained
bootstrap allocation. It is valid for the whole boot. It restricts all pixels
and queues to addresses below 4 GiB. A client must not substitute a parent tag.

The first profile accepts linear XRGB8888, offset zero, one loaded DMA segment,
and the exact admitted dimensions. The extent must cover the complete pitch
and height. Each frame has its own immutable two-entry, 32-byte RCQ list. The
layer high address byte remains zero. This profile does not expand the list
length or establish the unverified length-field width.

A sleepable frontend worker first waits for producers. Under the reservation
lock it rechecks writers and publishes a pending READ scanout fence. It then
unlocks the reservation and submits. A producer wait alone does not exclude
later writers. Submission must not hold DRM, reservation or provider locks.
The retain callback pins the loaded map, backing storage and callback code.
The DE provider synchronizes pixels and the queue with PREWRITE before its
first pointer attempt. It never unloads the caller's pixel map.

Before each replacement, the coordinator verifies the owned old queue and
route. It requires a new RCQ finish and all seven expected layer words plus
the blend pipe word. It consumes a possible prior TV0 event, then requires two
new distinct observations. The completion sequence and time describe software
observations. They are not an exact hardware frame counter or timestamp.

Only that proof retires the previous external frame. The provider performs
POSTWRITE and releases its queue, then calls the frontend release callback
outside the DE lock. That callback can signal the READ fence and drop the GEM
reference. A pre-DMA failure also releases a successfully retained new frame.
An error after the first pointer attempt retains both old and new frames,
queues and pending fences until reboot. No implicit stop or reset occurs.
Client release is permitted only before its first pointer attempt.

These source contracts do not establish physical page flips or a DRM frontend.
The bootstrap buffer and its DMA tag remain retained even after replacement.

## Source facts and checks

All BSP references use commit
`2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f` of
[linux-orangepi](https://github.com/orangepi-xunlong/linux-orangepi/tree/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f).
The A733 User Manual v1.00 has SHA256
`cdd77e5779d3c69d41b5f237f3db3db51c67fdf0f3aca9a2eedc77d9420f248d`.
It supplies the CCU fields, address map, IOMMU master assignment and TV0 facts.

Within that BSP, `bsp/configs/linux-5.15/sun60iw2p1.dtsi` supplies the DE,
IOMMU and NSI bindings and the 0x16 IOMMU version. The DTS is dual GPL-2.0+
or MIT. `bsp/drivers/iommu/sunxi-iommu-v2.c` supplies per-instance and
per-master address arithmetic and bypass meaning. Its SPDX identifier is
GPL-2.0-or-later; an older comment also claims dual BSD/GPL terms. This code
is used only for hardware facts. `bsp/drivers/bus/sunxi-nsi.c` and `.h` supply
the NSI port facts. The C file has GPL-2.0-or-later SPDX and an older GPLv2
notice. No implementation is imported under either interpretation.

Under `bsp/drivers/drm/sunxi_device/hardware/lowlevel_de/`, `de_top.c`,
`sunxi_de.c`, and the OVL/BLD/FMT/scaler/compression/backend platform and
register headers supply the DE topology and bit positions. They are
GPL-2.0-or-later. The older A733
`bsp/drivers/video/sunxi/disp2/disp/de/lowlevel_v35x/de35x/de_top.c` supplies
the per-core clock field description. It has GPL-2.0-or-later SPDX and an
older GPLv2 notice. This cross-check is within the same BSP, not independent
hardware evidence. They are hardware references, not copied
implementation or a permissive driver dependency. See the linked
[format contract](a733-de352-format.md) for the exact plane/list layout.

```sh
python3 ember/tools/a733-de352-native-contract.py
DE352_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    python3 ember/tools/a733-de352-native-contract.py
```

The fixture executes the actual consumer, DMA and formatter bodies. It uses
controlled FDT, MMIO, clock, PCK, output and DMA calls. It checks guarded
attachment, claim order, rejected memory states, incomplete capture,
release failures, inactive blocks, publication, stale flags, bounded waits,
self-clearing commands, RCQ register evidence and independent vblank.
These checks are not physical scanout acceptance. A board check must record
the exact kernel/DTB, mode, raw guards, visible pixels and failure boundaries.
HDMI PHY/DDC/EDID, KMS, PRIME and accelerated Plasma need their own checks.

## Native build integration

`files.sunxi` selects the DE352 formatter, DMA adapter, and FDT coordinator
through `sun60ide`. `GENERIC64` includes that attachment. The coordinator
uses the public CCU memory register enum and the retained DISPLAY APIs.
Its raw memory record stays at version 1 with seven CCU words per sample.
The host contract extracts that enum from the public CCU header.

This integration does not add or enable an FDT node. A board description
must provide the complete DE, disabled memory-provider, and output bindings
before attach can expose the explicit stage. The output provider must register
under the same phandle and validate the same owner cookie. Kernel build and
physical scanout acceptance remain separate requirements.
