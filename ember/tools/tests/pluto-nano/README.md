# PlutoSDR Nano RAM profile contract

Run from the EmberBSD source root:

```sh
python3 ember/tools/tests/pluto-nano/minimal-config-contract.py
```

The contract follows the configuration includes and checks that the generic
source lists register ADI radio, Zynq SPI, ADC, and AXI-DMA for automatic
attachment. `PLUTO_NANO` must explicitly disable each one. It also checks the
Zynq platform, FDT UART attachment, and early-console address.

This source check does not run `config(1)`, build a kernel, inspect the live
FDT, or qualify the U-Boot handoff. Confirm the active console and memory
description before transferring any kernel to the device.
