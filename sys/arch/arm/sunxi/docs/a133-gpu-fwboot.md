# A133 GE8300 explicit firmware bootstrap

<!-- Origin: EmberBSD A133 native PowerVR bootstrap, AI-assisted. -->

The optional `sun50ia133gpu` FDT consumer prepares the pinned GE8300 MIPS
firmware through the common `sys/dev/powervr` owner. Ordinary device attach
only creates `hw.<device>.firmware_boot`; it performs no GPU DMA allocation,
power transition or reset. Writing `1` explicitly requests the bounded boot.
On one office YS-M33 with preserved WLAN power, the explicit firmware boot
completed and reported BVNC 22.102.54.38 with the fixed 950000 µV setpoint and
504 MHz effective clock. This is not a DRM device or graphics acceptance.

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

The AXP803 DCDC4 rail must already be enabled with an initial programmed
setpoint of 920000, 940000 or 950000 microvolts. After preflight and before any GPU
clock or power transition, the explicit request asks the native regulator
provider to prepare exactly 950000 microvolts. The provider verifies the AXP803
chip ID and enabled DVM before writing, then requires DVM completion and the
exact selector readback with a bounded timeout. A failed or uncertain change
retains resources and prevents GPU activation. This is a fixed initial OPP,
not dynamic voltage scaling; a programmed selector is not an ADC measurement.
Regulator, clock and power-domain APIs do not supply an exclusive lease. This controlled trial requires one native GPU owner, an
FDT inventory excluding other active consumers of these resources and the GPU
rail, and no parallel direct MMIO or I2C agent. Fresh reads do not replace that
ownership condition. Before changing the GPU PLL the consumer asks the native
PPU provider for a read-only transition preflight and requires OFF. This is a
snapshot, not a lease; the single-owner condition above must remain true through
clock preparation. The A133 CCU permits only the fixed 432-to-504 MHz retune
with validated parent and factors, preserves PLL power and output, and requires
lock and factor readback. A100 retains its existing observation-only behavior.
The explicit adapter disables only the exact vendor
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
that observation was not made. Values are the last successful observations during
the request. `detail_error` preserves the error from a failed power or regulator
query when the public result is EBUSY. The stage identifies the operation
entered, not necessarily one that completed. Numeric stage values are defined
in `sun50i_a133_gpu_owner.h`; 3 is preflight regulator enabled, 4 is preflight
voltage, 36 is fixed-voltage preparation, 37 is its readback, 18 is PPU enable,
19 is its readback, 38 is the PPU OFF preflight, 39 is PLL set-rate,
40 is core set-rate, 41 is IRQ setup, and 35 is complete. Failed
preflight may be requested again; once activation is attempted the owner keeps
its one-shot state and resources. Reading diagnostics never retries the boot.

The separate, explicit `hw.sun50ia133gpu0.queue_probe` request is one-shot and
requires successful firmware boot. It submits LOGTYPE in KCCB slot 0, then
MMUCACHE with the interrupt flag in slot 1. Success requires both return words
and offsets to show consumption, a delivered MIPS IRQ in the armed interval,
and a fresh fault/HWR check. Queue stage, error, attempt, offsets, return words,
IRQ counters and fresh fault state are available as cached read-only sysctls.
The IRQ status has no command identifier: delivery during the MMUCACHE interval
is evidence of a real firmware IRQ, not proof of its unique cause. A timeout or
uncertain operation never retries or frees firmware DMA. On the same office
YS-M33 warm RAM trial, both commands were consumed with successful return
words, an IRQ arrived during the armed interval, and the fresh fault/HWR check
was clean. This confirms the bounded firmware queue check on that device.

A separate one-shot `hw.sun50ia133gpu0.compute_probe` requires the successful
queue probe and an exact pinned experimental compute image. It prepares the
common GPU heap and VM, submits a cache operation and compute kick, and checks
the completed command buffer, UFO and GPU-written scratch value after DMA
synchronization. Errors or uncertain device exposure retain the associated
resources and do not permit a retry. Cached compute diagnostics report the
stage, queue and completion observations without reading hardware.

On one office YS-M33 preserved-power warm RAM trial, the command buffer drained,
the UFO reached 1, and a GPU write produced `0x13579bdf` in the synchronized
scratch buffer; the fresh fault/HWR check was clean. This qualifies that bounded
compute operation on that device. It does not establish cold boot, rendering,
Vulkan or Qt acceleration.

The experimental DRM compute path is selected by `PVR_GE8300_DRM`. After a
successful explicit firmware boot, `hw.sun50ia133gpu0.drm_attach=1` performs
the bounded queue check if needed and adopts that queue for the DRM backend.
Do not run the diagnostic compute probe before adoption: its exposed allocations
are retained and the DRM path refuses to reuse them. Once adoption is attempted,
diagnostic compute cannot mutate the firmware queue. The owner retains resources
on uncertain failures; this path does not claim a GPU reset or firmware reboot.
The firmware's own recovery of one context is handled: after a validated
single-context `CONTEXT_RESET_NOTIFICATION`, only that context and its VM
fail with `EIO`, and `drm_failed` stays 0 for new contexts and processes.
`sys/dev/powervr/README.md` lists the admitted reports and the fail-stop cases.
The cached `drm_queue_read`, `drm_queue_write`, `drm_queue_error`,
`drm_queue_command` and `drm_failed` values do not read hardware. A successful
context-handle destroy alone does not prove firmware cleanup; the trial check
also verifies backend health and a later completed MMUCACHE command.

The source includes an experimental userspace check for the pinned compute
image: `ember/tools/pvr-ge8300-drm-compute-check.c`. It checks GE identity,
quirks, rejected foreign handles and malformed render objects, maps the
Mesa-generated resources, and submits two compute contexts with syncobj waits
and canary checks.
On one office YS-M33 preserved-power warm RAM trial, this check passed with two
userspace compute contexts, syncobj waits and GPU canary writes. The final
cached queue state was drained and healthy after context, VM and BO retirement.
The negative foreign-handle and render checks also passed. This qualifies only
that bounded DRM userspace compute operation on that device.

On NetBSD an ioctl error does not copy the modified output buffer to userspace:
the probe checks the original job count after the rejected submit. Linux
partial-error output semantics are not established here. Mesa and Vulkan
submission error handling and recovery have not been qualified.

GE graphics use parameter format 1: one macrotile, one 5-byte region header per
tile and no macrotile array. The driver admits render and transfer contexts,
free lists and HWRT datasets up to 4096x4096 with one sample and one layer.
On the same warm RAM trial setup, Mesa's Vulkan driver with the EmberBSD Ports
format 1 patches rendered offscreen 256x256, 1280x800, 256x64 and 64x256
triangles pixel-exact, with intact guards and no GPU failure. This does not
establish presentation, Qt acceleration, multisampling, layered rendering,
sustained use or cold-boot support.
