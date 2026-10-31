# CO5300 SPI framebuffer

`cofb` attaches to the `chipone,co5300` SPI child of an FDT SPI bus. The
controller must support four-line transmit, retain chip select throughout a
multi-chunk transfer, and support the configured SPI mode 0 frequency. The
panel node supplies `width` and `height` (both 1–480, width divisible by 4),
`spi-max-frequency`, a `power-supply` regulator, and a nonempty
`chipone,init-sequence`. A `reset-gpios` GPIO is optional: its logical active
level is asserted for 10 ms and released for 150 ms; without it the driver
sends software reset and waits 80 ms.

`chipone,init-sequence` is a byte stream of records: command, data byte count,
16-bit big-endian delay in milliseconds, then that many data bytes. The whole
stream is checked before any command is sent. The panel-specific commands,
orientation, pixel format, and sleep/display transitions belong in the device
tree, not in this driver. The sequence is at most 1024 bytes, with cumulative delays at most 5000 ms,
and must configure native RGB565.

The driver creates a wired, zeroed native-endian RGB565 framebuffer exposed
through genfb/wsdisplay, including its normal mmap and framebuffer information
ioctls. Executable mappings are rejected. UART remains the console. A worker compares row snapshots to a separate shadow buffer at up to
20 scans per second. It converts changed rows to big-endian wire pixels,
sends the CO5300 address windows and four-line pixel data, and updates the
shadow only after successful transmission. The initial scan sends all rows.
There is no atomic frame presentation: concurrent writes can tear and are
observed again on a later scan. On a transfer error the scan stops, logs once, and retries next interval.
The framebuffer, shadow and row staging use at least 2 * width * height * 2
bytes plus two rows and bookkeeping; the wired framebuffer is page-rounded.

This driver has no detach or panel power-off operation; its worker and shared
regulator remain active for the device lifetime. A shared supply is never
disabled by an error cleanup. On genfb attachment failure its backing memory
is retained, because genfb has no unwind API and may retain references to it.
The implementation has host-function tests and
an RV32 compile; panel signalling, full graphics operation, memory use and
refresh rate remain to be checked on actual hardware.

The protocol opcodes follow Espressif's Apache-2.0
[CO5300 SPI implementation](https://github.com/espressif/esp-iot-solution/blob/8eee17c20da66048faa20c683ffeb80bfa85d723/components/display/lcd/esp_lcd_co5300/esp_lcd_co5300_spi.c).
