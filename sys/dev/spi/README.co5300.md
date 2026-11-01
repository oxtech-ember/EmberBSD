# CO5300 SPI framebuffer

`cofb` attaches to the `chipone,co5300` SPI child of an FDT SPI bus. The
controller must support the selected transmit width, retain chip select
throughout a multi-chunk transfer, and support the configured SPI mode 0
frequency. The panel node supplies `width` and `height` (width 1–480 divisible
by 4; height 2–480 and even),
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

The optional `spi-tx-bus-width` property selects pixel writes: `1` uses the
CO5300 single-line command/pixel-write opcode `0x02`; `4` uses the existing
four-line pixel-write opcode `0x32` and is the default when the property is
absent. Other values are rejected. Initialization commands always use the
single-line opcode. The single-line pixel path is intended for low-speed
controllers such as GPIO SPI, capped at 100 kHz; a full 480 × 480 RGB565 image
requires at least 37 seconds on the wire at that rate, before command and GPIO
overhead. The opcode assignments follow the CO5300 datasheet, page 20. This
mode is selected by configuration and does not fall back after transfer errors.

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
kernel RAM mapping. Executable mappings are rejected. UART remains the console.
A worker compares adjacent two-row snapshots to a separate shadow buffer at up
to 20 scans per second. It converts changed row pairs to big-endian wire pixels,
sends a two-row CO5300 address window and four-line pixel data in one RAMWR,
and updates both shadow rows only after successful transmission. If either row
is dirty or differs from the shadow, both rows are sent; a failed pair remains
dirty for retry. Even panel heights are required so no unpaired final row is
sent. This follows the CO5300 address-window requirement that the selected
column and row extents each contain an even number of pixels (datasheet pp. 159,
161). The initial scan sends all rows.
There is no atomic frame presentation: concurrent writes can tear and are
observed again on a later scan. On a transfer error the scan stops, logs once, and retries next interval.
The framebuffer and shadow use 2 * width * height * 2 bytes. Pair snapshot and
wire staging add four rows plus bookkeeping; the wired framebuffer is
page-rounded. Diagnostic row records report the pair's starting `y`, row count,
combined nonzero count and hashes; the first nonzero sample includes its actual
row and column.

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
