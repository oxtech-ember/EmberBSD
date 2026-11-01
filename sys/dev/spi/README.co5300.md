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

For ordinary single-line writes, the four-byte command header and parameters
are coalesced into one chunk when the combined packet is at most 64 bytes;
larger writes and multi-line data remain separate chunks.

After the configured init sequence, the driver makes a best-effort status
read at 1 MHz when the SPI controller supports `SPI_CAP_3WIRE`. It sends
the CO5300 read header `03 00 <register> 00` and begins the response
immediately (without a dummy byte) while retaining chip select. It logs raw
values and per-register errors for ID `04` (three bytes), power `0a`, format
`0c` and brightness
`52`. Values such as all-zero or all-`ff` are not treated as validated
identity. A controller without 3-wire support or a failed read does not
prevent write-only panel initialization; the configured panel write speed
is restored after the diagnostic attempt. This does not establish successful
electrical reads on a physical panel.

The driver creates a wired, zeroed native-endian RGB565 framebuffer exposed
through genfb/wsdisplay, including its normal mmap and framebuffer information
ioctls. Mappings use the cacheable bus-space attribute to match the wired
kernel RAM mapping. Executable mappings are rejected. UART remains the console. A worker compares row snapshots to a separate shadow buffer at up to
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
Read framing follows section 4.3, page 28 of the [CO5300 datasheet](https://dl.espressif.com/AE/esp-iot-solution/CO5300_Datasheet_V0.00.pdf).

`sysctl -n hw.cofb0.status` reports a consistent snapshot of refresh activity.
`rows_changed` counts dirty-row attempts, including retries; `rows_sent` counts
successful sends and `errors` counts failed attempts. `last_error` is the last
nonzero transfer error and remains set after recovery. The 32-bit counters wrap.
One attempt may be in flight between the changed and sent/error increments, so
the snapshot does not always satisfy `rows_changed = rows_sent + errors`.
Successful transfers do not prove that the panel displayed the pixels.
