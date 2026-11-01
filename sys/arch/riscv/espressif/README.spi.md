# ESP32-S31 GPSPI controller

`esp_spi.c` implements the normal kernel SPI interface for GPSPI2/3.
Register definitions and sequencing derive from ESP-IDF 6.1 commit
`fff9895c82d744c7237be8847347bdd1b07c6643`, under Apache-2.0.
Board wiring and physical acceptance belong to DevBoards.

The FDT node requires:

- `compatible = "espressif,esp32s31-spi2"` or `"espressif,esp32s31-spi3"`;
- `reg` covering the controller registers;
- `espressif,syscon` referencing the HP clock/reset register bank;
- a fixed XTAL input in `clocks`, named `xtal` in `clock-names`;
- `pinctrl-0` routing the selected clock, data and chip-select signals;
- normal SPI child address cells and `spi-max-frequency` properties.

The driver selects XTAL with upstream divisors of one. Clock/reset
registers are changed under the shared syscon lock. It supports SPI
modes 0–3, single-line transmit/receive and dual/quad transmit. GPSPI2
has six hardware chip selects; GPSPI3 has three. Receive is limited to
10 MHz until GPIO input timing compensation is implemented and checked.
Multi-line receive, DMA and interrupts are not implemented.

Transfers use the 64-byte PIO FIFO. Chip select remains asserted across
FIFO blocks and chunks, including changes from a single-line command
to a quad data phase. UPDATE and completion waits are bounded. Timeout
resets only the affected controller, restores its configuration and
completes the transfer once with its unconsumed residuals preserved.

For diagnostics, read `hw.<gpio-instance>.registers` and
`hw.<spi-instance>.registers` with `sysctl`. For example, when the
instances are `esp32s31_gpio0` and `espspi0`:

```sh
sysctl hw.esp32s31_gpio0.registers
sysctl hw.espspi0.registers
```

The GPIO snapshot contains the output latch (`out`), output enable (`en`)
and input (`in`) register pairs, followed by each valid pin's IO_MUX
(`mux`), output matrix selection (`outsel`) and GPIO pin (`pin`) registers.
The SPI snapshot contains the shared clock/reset register (`clkrst`),
controller clock gate (`gate`), command, control, clock, user, user1,
user2, miscellaneous and raw DMA interrupt registers. Values are hexadecimal.
These are best-effort snapshots: hardware can change between reads, and
register values are not measurements of pad voltage.

Host regression (actual driver functions with modeled registers):

```sh
"$HOST_PREFIX/bin/python3.15" sys/arch/riscv/tests/t_espspi_pio.py \
    --source "$OS_SOURCE" --host-prefix "$HOST_PREFIX" --work "$TEST_OUTPUT"
```

The test covers divider bounds, FIFO tails, mixed widths, chip select,
receive, timeout/reset/retry and completion. It does not establish
physical pin timing, peripheral operation or kernel ABI compatibility;
those require a target kernel build and execution on hardware.
