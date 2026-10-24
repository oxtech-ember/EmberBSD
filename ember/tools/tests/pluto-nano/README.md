# PlutoSDR Nano RAM profile contract

Run from the EmberBSD source root:

```sh
python3 ember/tools/tests/pluto-nano/minimal-config-contract.py \
  --config /absolute/tooldir/bin/nbconfig
```

The contract follows the configuration includes and rejects selected ADI radio,
Zynq SPI, ADC, or AXI-DMA attachments. Device declarations in `files.*` make
attachments available; they do not select instances. Removing an unselected
instance with a `no` directive is an error in `config(1)`.

The check also verifies the Zynq platform, FDT UART and early-console address.
With `--config`, it runs the real native configuration tool and inspects its
output. Without this option it performs source checks only. Neither mode
builds a kernel, inspects the live FDT, or qualifies the U-Boot handoff.
