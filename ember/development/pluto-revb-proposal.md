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

- EmberBSD source baseline: `16c3eddc0d73750360f81620b1d36c61ca2dc38c`.
- Device: ADALM-Pluto Rev.B, Xilinx Zynq-7000 Z7010 and AD9363A; 512 MiB RAM
  and 32 MiB QSPI per the vendor documentation.
- Current observed firmware: ADI `v0.39-dirty`; exact firmware source revision
  and recoverable payload hash remain to be recorded.
- Target ABI: NetBSD `earmv7hf`; intended cross compiler: GCC 16.
- Board boot path: ADI RAM DFU is documented, but payload format, U-Boot command,
  console and EmberBSD handoff on this device remain unverified.

The [current official Pluto U-Boot defconfig](https://github.com/analogdevicesinc/u-boot/blob/adi-xlnx-u-boot-2025.1-y/configs/zynq_pluto_defconfig)
explicitly disables `CONFIG_BOOTM_NETBSD` and enables a silent U-Boot console. The installed
`v0.39-dirty` bootloader revision has not been pinned, so direct native NetBSD
handoff cannot be assumed. A RAM-loaded U-Boot with NetBSD support or another
verified handoff may be required.

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

The proposal and implementation are AI-assisted. The source and physical
results will be reported separately; this proposal does not claim tested board
support.
