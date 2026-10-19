# Zynq-7000 and ADI converter providers

The native providers expose kernel interfaces for Zynq PS SPI, AD9361,
ADI AXI ADC and finite ADI AXI DMAC transfers. They contain no application
video protocol, packet format or board image. A device tree and a kernel
consumer must supply the hardware configuration and ownership callbacks.

## Ownership and configuration

- `zynqspi` attaches to the Zynq PS SPI controller. It uses bounded polled
  transactions and the native SPI completion API. SPI configuration failures
  complete the transaction and release the bus for a later retry.
- `ad9361phy` adapts the BSD-licensed ADI no-OS implementation pinned in
  [its provenance](../../sys/external/bsd/ad9361/README). RX and TX are
  explicit, separate FDT opt-ins. Attachment does not start a radio session.
- [ADC leases](../../sys/dev/fdt/adi_axi_adcvar.h) serialize converter
  configuration and acquisition. Consumers must prove that outstanding AXI
  transactions have drained before changing clocks or releasing buffers.
  A failed drain retains ownership until a coordinated platform restart.
- [DMAC buffers](../../sys/dev/fdt/adi_axi_dmacvar.h) are kernel-owned.
  The provider supports finite transfers, completion interrupts and checked
  drains. Cyclic and 2D transfers are not supported. DMA completion does not
  establish when a downstream converter emitted a sample.
- [TX leases](../../sys/dev/spi/ad9361var.h) require an independent hardware
  reset deadline and zero DAC samples before bootstrap. The initial profile
  is 13 MSPS with 2 MHz bandwidth. Fixed FDT delay taps do not qualify a
  physical digital interface; `digital_tuning_valid` remains false.

Zynq GPIO numbering follows the contiguous FDT range of 118 pins across
banks of 32, 22, 32 and 32 pins. The clock provider exposes SPI and FPGA
clocks. GEM reference clocks follow the negotiated 10/100/1000 Mb/s link
rate. Failed clock programming reports the link down.

The common GEM driver retires a TX buffer only after the descriptor returns
ownership. TX recovery preserves RX buffers and retains mappings if DMA
halt cannot be confirmed. These changes preserve the common ACPI attachment.
The ARM pmap path recognizes normal non-cacheable DMA mappings.

## Checks and limits

The selected production objects compile for ARMv7 with GCC 16.2.0 and the
generic configuration below. This is an object build, not a linked kernel,
board image or fresh physical-radio qualification.
Synthetic register acknowledgements do not establish RF performance,
digital eye margins, bus timing or actual DMA quiescence.

Run the focused checks from the source root:

```sh
sh ember/tools/check-zynq-gpio.sh .
python3 ember/tools/tests/zynq-core/run.py
sh ember/tools/test-spi-configure-error.sh
CC=cc python3 ember/tools/tests/zynq-spi-attach/run.py
sh ember/tools/arm-dma-coherency-contract.sh .
python3 ember/tools/tests/zynq-gem-clock/run.py
sh ember/tests/cemac-stop.sh
sh ember/tools/tests/adi-axi-dmac/run.sh .
sh ember/tools/tests/ad9361/native-test.sh .
sh ember/tools/tests/ad9361/wrapper-test.sh .
```

The [AD9361](../tools/tests/ad9361/README.md) and
[DMAC](../tools/tests/adi-axi-dmac/README.md) test guides describe memory and
path analysis. The [generic compile configuration](../tools/tests/zynq-drivers/GENERIC_ZYNQ_DRIVERS)
adds these providers to ARM `GENERIC` without private consumers. Use
`build.sh -m evbarm -a earmv7hf` with an explicit GCC16 external toolchain
and the absolute path of that configuration. Follow the source export and
tool isolation requirements in [the cross-build guide](../boot/cross-build.md).
Compile integration does not select a physical device tree or authorize
loading the kernel onto a board.

Related independent repairs have their own contracts:
[cooperative vnode iteration](../tools/tests/vnode-iterator/README.md) and
[IPv4 fragmentation control](../tools/tests/ip-dontfrag/README.md).

The new adapters and contracts are AI-assisted EmberBSD work. Imported
source retains its original copyright, license and pinned provenance.
