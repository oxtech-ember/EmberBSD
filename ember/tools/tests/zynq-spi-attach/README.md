# Zynq SPI attachment lifecycle

Run the actual-source host regression from the source root:

```sh
CC=gcc-16 python3 ember/tools/tests/zynq-spi-attach/run.py
CFLAGS='-fanalyzer' CC=gcc-16 python3 ember/tools/tests/zynq-spi-attach/run.py
```

The Python standard-library runner extracts the driver's actual softc,
attachment, register I/O and bounded FIFO drain functions. It uses the actual
register header and substitutes host clock, FDT and bus-space providers.
`SOURCE_ROOT` can select another source tree; `CC` and `CFLAGS` select a native
host compiler and optional analysis or sanitizer flags. On macOS, GNU GCC
may also need `CFLAGS="-isysroot $(xcrun --show-sdk-path)"` to locate the
installed SDK headers. This selects host headers, not a target sysroot.

The fixture injects missing clocks, each clock-enable failure, zero reference
rate, missing or short register ranges, failed MMIO mapping and a stuck RX
FIFO. Failed attachment must release only acquired resources, unmap before
disabling clocks, and disable ref_clk before pclk. No failed attachment may
register a controller or attach children. Successful attachment must retain
both clocks and the mapping, register once and attach the bus once.

These checks model provider failures and resource ownership. They do not
qualify physical SPI timing, peripheral transfers or board wiring.
