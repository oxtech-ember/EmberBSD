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

- EmberBSD source used for the current kernel build: `d67a6b5763f125ba13f06f5e92b3511628ca6b6f`.
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
starting at `0x02080000` with size `0x01e00000`, and the RAM boot command runs
`bootm ${fit_load_address}#${fit_config}`. The default `fit_config` is
`config@0`. The configuration undefines `CONFIG_BOOTM_NETBSD`, but this does not
block the Linux FIT path: U-Boot's ARM Linux handoff passes the FDT in `r2`, and
NetBSD's ARM startup reads the Linux boot protocol registers. A candidate FIT
can therefore identify the kernel as `os = "linux"` and use the standard Linux
handoff. This route remains unverified on hardware.

## Current offline evidence

- The Ports change adding the ARMv7 hard-float NetBSD linker emulation and
  correcting the ELF32 ABI probe is committed as
  [`EmberBSD-Ports` `f647dd60889985101bf70253976f935d0df1cdaa`](https://github.com/oxtech-ember/EmberBSD-Ports/commit/f647dd60889985101bf70253976f935d0df1cdaa).
- GCC 16.2.0 and Binutils 2.47 were built for
  `armv7--netbsdelf-eabihf`. The direct ABI probe passed after the probe fix.
  The first orchestration run exited at the SDK's ELF64-only parser; the full
  orchestration command was not rerun after the probe fix.
- A temporary `PLUTO_MIN_PROBE12` configuration compiled and linked the
  existing Zynq/ADI SPI, AD9361, AXI ADC and AXI-DMAC providers. It is a compile
  probe, not a Rev.B configuration; it has no board DTB or root image.
- The kernel build completed with exit status 0. Its ELF is little-endian
  ELF32 ARM EABI5, entry `0x80000040`, and has the NetBSD kernel's soft-float
  ABI flags. ELF SHA256:
  `9e31d21577a1e5b55ec2ce836e805abba10a16eb45e2af9f5367d913b870d3a4`.
  The flat image is 9,296,212 bytes; SHA256:
  `c2e84095865998c0385823575b7121a6d12a0026805522d1bf8cae8119b5eb30`.
- A possible load base is `0x08000000`, with entry `0x08000040`. The linked
  image has physical segment offsets through `0x00a34000`; this base keeps the
  image below `0x08a34000` and clear of U-Boot's default RAM-disk address
  `0x04000000`. The ARM startup relocation delta is 1 MiB aligned. These are
  layout calculations, not a boot test.
- The temporary config includes `arch/evbarm/conf/GENERIC`, which inherits
  `MEMORY_DISK_DYNAMIC` and `pseudo-device md` from `std.generic`. The generated
  kernel `opt_md.h` confirms the option is active. NetBSD's FDT startup can
  therefore accept a dynamic RAM-disk initrd; no initrd filesystem has been
  built or mounted yet.
- QEMU 8.2.2's `xilinx-zynq-a9` machine reported that it has no FDT when asked
  to dump one, so this machine cannot provide a QEMU boot check for the current
  path.
- The current Mac USB inventory found the Pluto runtime USB product and its
  31.5 MB `PlutoSDR` mass-storage volume. `dfu-util -l` found no DFU device.
  No reset, transfer, or storage write was performed.

The official [Rev.B Linux DTS](https://github.com/analogdevicesinc/linux/blob/main/arch/arm/boot/dts/xilinx/zynq-pluto-sdr-revb.dts)
and [shared Pluto DTSI](https://github.com/analogdevicesinc/linux/blob/main/arch/arm/boot/dts/xilinx/zynq-pluto-sdr.dtsi)
are GPL-2.0. Use them as external hardware references only; do not copy them
into the BSD source tree. A compatible Rev.B DTB and its provenance are still
required for the candidate FIT.

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

The hardware profile belongs in EmberBSD-DevBoards and must state the current
research-only support level. Existing AArch64 builds and other board
configurations remain in regression scope. No TX consumer is included in the
Pluto kernel profile.

The existing stock firmware recovery payload and calibration state must be
identified and checked before any physical transfer. The first hardware test
uses RAM DFU only; any inability to prove the selected RAM path stops the test.
Persistent QSPI installation requires a separate reviewed image/layout,
verified recovery procedure and explicit approval after RAM-boot and RX
acceptance.

The proposal and implementation are AI-assisted by Codex. The source and
physical results will be reported separately; this proposal does not claim
tested board support.
