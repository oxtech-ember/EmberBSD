# A133 GPU power domains

The native PPU provider owns power requests independently of the GPU driver.
It does not implement Services, GPU virtual memory, firmware execution or EGL.
The experimental `EMBER64_A133_GPU` kernel and
`sun50i-a133-ys-m33-gpu.dts` select this provider explicitly.
The ordinary YS-M33 device tree keeps it disabled.

The experimental kernel also selects `SUNXI_A133_GPU_FDT`. The early adapter
can translate the measured vendor PPU binding in the copied FDT in RAM.
It preserves the complete board tree, existing domain phandle and GPU supply.
It adds the native R-CCU binding and reuses the existing SRAM-control register
range as a syscon provider. It marks GPU power as consumer-managed.
No DTB partition is written. Unknown resources are rejected before translation;
re-entry validates the converted providers before returning without changes.
The ordinary WLAN kernel does not select this adapter.

## Binding and lifetime

The local binding uses `allwinner,sun50i-a133-ppu`, one 64-byte register range,
one level-high interrupt, and zero power-domain cells. The clock and reset
names are `ppu`. `syscon` references the existing system-control provider;
the low three bits at offset `0x24` identify the silicon revision.
The A100 R-CCU already supplies the PPU gate and reset.

Attach maps the PPU registers with bounded reads and establishes the interrupt. It refuses
an inconsistent state, pending request or already enabled request source.
Attach does not enable the clock, release reset or change a power domain.
The first revision-6 enable acquires the PPU clock/reset and software control.
These infrastructure resources remain until shutdown. The FDT framework has
no power-controller unregister operation; this provider does not support detach.
An uncertain partial transition retains its mapping and infrastructure.

Consumers must declare `netbsd,consumer-managed-power`, explicitly call the
checked power API and inspect the state query before GPU MMIO. A missing
provider, pending request, unsupported operation or failed query is an error.
Before disable, the GPU consumer must quiesce firmware, interrupts and DMA.
The provider cannot infer GPU idleness from PPU registers.

## Power and request policy

The policy checks reset, clock gate, isolation and domain-state readbacks.
Full OFF-to-3D transitions are limited to revision 6. Full shutdown requires
power acquired by this driver. Firmware-owned domain A is preserved.
The experimental FDT provider refuses other revisions before mapping PPU MMIO.

The standalone policy recognizes domain-B requests on revisions 0, 3, 4, 5
and 6. It validates the transition and readback before ACK and completion.
Future revisions 1, 2 and 7 cannot acquire request control.
Unknown transitions are masked without ACK or a completion signal.
A failed mutation latches uncertainty and stops further mutations.
There is no implicit retry or power rollback.

The implementation uses independently written native code. Register and
sequence facts come from Allwinner's pinned
[PPU driver](https://github.com/CrealityTech/sonic_pad_os/blob/7f37a7fbf4ad59907034715e3d967b9177de1cd4/lichee/linux-4.9/drivers/soc/sunxi/pm_domains.c)
and [GPU platform](https://github.com/CrealityTech/sonic_pad_os/blob/7f37a7fbf4ad59907034715e3d967b9177de1cd4/lichee/linux-4.9/modules/gpu/img-rgx/linux/rogue_km/services/system/rgx_sunxi/sunxi_platform.c).
Their full callbacks exclude revisions 0/3/4/5 to preserve boot-owned domain A.
Their inclusion of revision 6 is source evidence, not physical acceptance.
The GPL implementation is not incorporated into this driver.

## GPU PLL lock detection

The A133 CCU callback can enable the PLL lock detector on a running PLL.
It preserves PLL power, output enable and frequency factors.
It requires valid firmware setup and refuses to start or retune the PLL.
It waits at most five milliseconds, then checks lock after a 20-microsecond settle.
Timeout restores only a detector bit acquired by the call.
An unrelated register change stops the operation without restoring old factors.
The A100 callback remains observation-only.
The vendor FDT adapter selects the A133 CCU binding first and retains its
A100 fallback. This selects the A133 GPU divider and detector callback.

Register and timing facts come from the pinned Allwinner
[clock descriptor](https://github.com/CrealityTech/sonic_pad_os/blob/7f37a7fbf4ad59907034715e3d967b9177de1cd4/lichee/linux-4.9/drivers/clk/sunxi/clk-sun50iw10.c)
and [factor clock](https://github.com/CrealityTech/sonic_pad_os/blob/7f37a7fbf4ad59907034715e3d967b9177de1cd4/lichee/linux-4.9/drivers/clk/sunxi/clk-factors.c).
This independently written callback does not incorporate their implementation.

## Validation boundary

`tests/a133-ppu-power.sh` uses the selected native `CC`. It checks domain
ownership, pending and unknown requests, protected revisions, readback failures
and faults at each ON/OFF/request write. Host fixtures do not establish
electrical timing, interrupt delivery, GPU identity or rendering on a tablet.
`tests/a133-gpu-pll.sh` checks detector ownership, bounded timeout, factor preservation
and the unchanged A100 policy with simulated register access.
`ember/tools/a133-gpu-fdt-test.sh` exercises the actual adapter with libfdt.
It checks resource rejection, provider ownership, unchanged vendor data and re-entry.
Clang 23.1.4 on Linux/amd64 built the experimental AArch64 kernel and its
selected DTB. Physical power/request tests and GPU rendering have not run.
