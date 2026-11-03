# A133 GE8300 explicit firmware bootstrap

<!-- Origin: EmberBSD A133 native PowerVR bootstrap, AI-assisted. -->

The optional `sun50ia133gpu` FDT consumer prepares the pinned GE8300 MIPS
firmware through the common `sys/dev/powervr` owner. Ordinary device attach
only creates `hw.<device>.firmware_boot`; it performs no GPU DMA allocation,
power transition or reset. Writing `1` explicitly requests the bounded boot.
This is not a DRM device, a graphics stack, or evidence of hardware success.

The native FDT adapter must prove exclusive ownership before supplying its
`ember,a133-gpu-exclusive` marker. The consumer requires the native GPU and
four-byte DFS register resources, the PPU domain, `core` and `bus` clocks,
`bus` reset, the real `gpu-supply`, SPI 97, and the exact board OPP table.
It rejects an IOMMU or DMA range on the GPU or any ancestor. It only admits
reservation forms already handled by the FDT/UVM owner: valid nonzero, non-wrapping
memreserve entries and a single static `reg` for each active reserved child.
Other forms fail before allocation. `bus_dmamem` owns the admitted RAM; the
common owner verifies direct, non-bouncing 36-bit DMA and excludes both MMIO
resources and the MIPS wrapper window.

The existing rail must already be enabled at 950000 microvolts; this driver
never changes its voltage. Regulator, clock and power-domain APIs do not supply
an exclusive lease. This controlled trial requires one native GPU owner, an
FDT inventory excluding other active consumers of these resources and the GPU
rail, and no parallel direct MMIO or I2C agent. Fresh reads do not replace that
ownership condition. The explicit adapter disables only the exact vendor
`virtual-dcdc4` test consumer when it points to that GPU rail. The AXP803
provider and the rail remain unchanged. This follows the consumer role in
CrealityTech/sonic_pad_os commit `7f37a7fbf4ad59907034715e3d967b9177de1cd4`,
`drivers/mfd/axp2101.c` and `drivers/regulator/virtual.c`; no vendor driver
code is incorporated. An explicit request enables the existing PLL,
deasserts the bus reset, enables bus then core clocks, and asks the native PPU
for 3D. The consumer verifies the domain, supply, current clock and live BVNC
before reading the separate DFS register at `0x01880020`. It accepts only the
source-derived core/DFS pairs 504 MHz/1, 504 MHz/2, 504 MHz/4 and 252 MHz/1,
corresponding to 504, 472.5, 441 and 252 MHz effective rates. It does not
write the DFS register. Unknown values stop the attempt and retain resources.
The vendor source establishes kHz/µV units and the DFS relationship; it is not
an acceptance result for this native implementation.

The exact firmware digest is checked before memory is prepared. PPU, voltage,
clock, BVNC and DFS are rechecked immediately before firmware MMIO. The GE
path reports success only after a fresh `firmware_started` observation and
matching updated FWIF compatibility layouts and hardware/firmware BVNC. This
does not establish KCCB operation, compute, rendering, Vulkan or Qt.

Once activation is attempted, the owner refuses retries and retains platform
resources. Once device exposure is possible, the shared DMA owner also refuses
destruction. An error can mean the hardware changed state even when a write
reported failure; no reset/reclaim path is claimed here. Physical testing must
be coordinated separately with a known rollback path.

The read-only `hw.sun50ia133gpu0` diagnostic sysctls expose the last explicit
request without reading hardware: `stage`, `last_error`, `detail_error`,
`attempted`, `retained`, `regulator_enabled`, `microvolts`, `ppu_enabled`,
`core_hz`, `pll_hz`, `effective_hz`, `dfs`, and `bvnc`. A value of -1 means
that observation was not made; `pll_hz` is currently always -1 because the
boot path does not read it. Values are the last successful observations during
the request. `detail_error` preserves the error from a failed power or regulator
query when the public result is EBUSY. The stage identifies the operation
entered, not necessarily one that completed. Numeric stage values are defined
in `sun50i_a133_gpu_owner.h`; 3 is preflight regulator enabled, 4 is preflight
voltage, 18 is PPU enable, 19 is its readback, and 35 is complete. Failed
preflight may be requested again; once activation is attempted the owner keeps
its one-shot state and resources. Reading diagnostics never retries the boot.
