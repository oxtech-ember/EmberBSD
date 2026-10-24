# Draft proposal: ADALM-Pluto Rev.B

This proposal is under discussion. Implementation and physical hardware
acceptance are pending.

## Problem and expected result

EmberBSD has generic Zynq-7000 and ADI converter providers, but no ADALM-Pluto
Rev.B profile, ARMv7 build path, board device tree or user-facing finite IQ
capture path. The first expected result is an EmberBSD kernel that can boot from
RAM without writing QSPI and capture a requested number of RX IQ samples.

## Scope

- Add a Rev.B-specific `earmv7hf` kernel configuration, device tree and
  reproducible GCC 16 cross-build path.
- Use the existing Zynq SPI, AD9361, AXI ADC and finite AXI-DMAC providers.
- Add a bounded RX-only interface for finite IQ captures, with no userspace DMA
  addresses or arbitrary RF register access.
- Exercise RAM DFU on the physical Rev.B only after the accepted payload and
  U-Boot handoff are verified.
- Keep the spectrum-scanning application as a follow-on that depends on the
  accepted RX API.

The first stage excludes QSPI installation, TX, protocol decoding and support
for other Pluto revisions. The scanner is a later separate change.

## Baseline and target

- EmberBSD source baseline inspected for this proposal: `cffb12cd6ed5695582b7b9af10e69353bab70de8`.
- Device: ADALM-Pluto Rev.B, Xilinx Zynq-7000 Z7010 and AD9363A; 512 MiB RAM
  and 32 MiB QSPI per the vendor documentation.
- Current observed firmware: ADI `v0.39-dirty`, Linux
  `6.1.0-271883-gf3da30df6004-dirty`, U-Boot
  `v0.20-PlutoSDR-00066-g90401ce9ce`, FPGA
  `dev_prj_2018_r1-2241-g065c8-dirty`, and root filesystem
  `2022.02.3-adi-5718-ge783aa`. Exact firmware source revision and recoverable
  payload hash remain to be recorded.
- Target ABI: NetBSD `earmv7hf`; intended cross compiler: GCC 16.
- Board boot path: ADI RAM DFU is documented. The current Mac read-only check
  found the board in runtime USB mode and no DFU device; no reboot or transfer
  was attempted. The CDC interface reaches a login prompt, while U-Boot console
  visibility remains unverified.

The installed U-Boot revision is pinned to upstream
[`analogdevicesinc/u-boot-xlnx` commit `90401ce9ce`](https://github.com/analogdevicesinc/u-boot-xlnx/tree/90401ce9ce).
At that exact source revision, the Zynq environment defines a RAM DFU target
starting at `0x02080000` with size `0x01e00000`, and the RAM boot command expects
a Linux FIT image. The same configuration undefines `CONFIG_BOOTM_NETBSD`.
Even when enabled, the [NetBSD boot handler](https://raw.githubusercontent.com/analogdevicesinc/u-boot-xlnx/90401ce9ce/common/bootm_os.c)
rejects FIT and expects a NetBSD stage-2 loader in a legacy image. RAM DFU alone
therefore does not establish an EmberBSD boot path. A NetBSD-capable second
bootloader and stage-2 handoff, or another verified route, is still required;
no payload should be sent to the board until that handoff is proven off-device.

## Acceptance

1. Run existing AD9361, AXI-DMAC, Zynq SPI and relevant ARMv7 build checks.
2. Produce a pinned kernel, DTB and RAM-root payload with exact build identity,
   size and SHA256 manifest.
3. On the Rev.B, verify the RAM DFU selection, EmberBSD kernel handoff and
   console without selecting any flash altsetting.
4. Capture exactly the requested finite IQ sample count at low, middle and high
   frequencies, stop cleanly and capture again without DMA errors or timeout.
5. Compare repeatable peaks with the existing stock-firmware RX baseline at
   matching frequency, bandwidth, gain and FFT resolution. Report relative
   levels in dBFS; calibrated dBm is out of scope.
6. Reboot and verify that the prior stock firmware still starts.

## Integration and recovery

The proposed board profile belongs in EmberBSD-DevBoards after matching
evidence exists. Existing AArch64 builds and other board configurations remain
in regression scope. No TX consumer is included in the Pluto kernel profile.

The existing stock firmware recovery payload and calibration state must be
identified and checked before any physical transfer. The first hardware test
uses RAM DFU only; any inability to prove the selected RAM path stops the test.
Persistent QSPI installation requires a separate reviewed image/layout,
verified recovery procedure and explicit approval after RAM-boot and RX
acceptance.

The proposal and implementation are AI-assisted by Codex. The source and
physical results will be reported separately; this proposal does not claim
tested board support.
