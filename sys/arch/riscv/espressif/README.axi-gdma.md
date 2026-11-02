# ESP32-S31 AXI GDMA receive controller

The `espressif,esp32s31-axi-gdma` FDT controller has one register range,
`espressif,syscon` referencing the S31 HP clock/reset syscon, three RX
interrupts in channel order, `dma-controller`, and `#dma-cells = <1>`. Its
single DMA specifier cell is the hardware peripheral trigger selector, not a
fixed channel. A free RX channel is reserved for each acquisition; CAM0 uses
selector 0. The provider owns the AXI GDMA clock and address-window registers.

This first implementation accepts one-shot device-to-memory transfers only.
It supports at most 1024 descriptors per request, requires each supplied bus
segment address and size to be 64-byte aligned, and rejects unsupported
transfer options. The caller owns the bus-dma map for the destination and must
perform the appropriate preread/postread synchronization around a transfer.
After the callback, query `fdtbus_dma_result` from deferred thread context;
the result remains available until the next transfer or halt. A completed
transfer must be queried or halted before reusing its channel. Error results
may report zero bytes where the actual partial count cannot be established.
Callers must stop the peripheral from requesting more DMA before halting, and
serialize transfer, result, halt and release for a given channel. The
completion callback should only notify a worker; it must not query, halt, or
release its own channel. A worker uses `fdtbus_dma_halt_checked` before
releasing a channel or destination mapping.
The controller disables interrupts, requests STOP, waits for PARK, drains
callback/result work, resets the channel, and performs post-transfer DMA sync.
Descriptors after a short successful EOF may remain DMA-owned; only the
completed prefix through EOF contributes to the result. On a PARK timeout,
retain the destination mapping and channel; the channel is quarantined from
reuse. This register-level contract follows the IDF DVP stop/restart sequence
and does not claim measurement of external bus transactions beyond the
controller's PARK indication. The legacy void halt cannot report a timeout and
panics if checked halt fails.

Descriptor layout and register sequencing are derived from the ESP-IDF v6.1
sources identified in the implementation. Host tests model MMIO and descriptor
ownership; they do not establish real interrupt timing, bus reachability,
PSRAM coherency, sensor operation, or a captured frame. No board node is
provided by this change.
