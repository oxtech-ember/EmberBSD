# FDT GPIO SPI controller

`spi-gpio` provides a low-speed SPI controller using ordinary GPIO pins. It is
useful when a peripheral must be tested without relying on a hardware SPI
controller. The FDT node requires exactly one `sck-gpios`, `mosi-gpios`, and
`cs-gpios` entry. `miso-gpios` is optional; without it the controller is
transmit-only. The child `reg` selects the controller's sole chip select and
must be zero.

The controller supports SPI modes 0 through 3, MSB-first bytes, and ordinary
single-line transfers. Receive phases require `miso-gpios`. Transfers that
request 3-wire, dual-line, or quad-line operation are rejected. The controller
validates every chunk before asserting CS and keeps CS asserted across all
chunks in one transfer.

Clients configure a speed from 1 through 100,000 Hz. The GPIO edge delay is
rounded up so the bit-banged clock does not exceed the requested rate; GPIO
call overhead makes the actual rate lower. This controller is intended for
diagnostics and low-speed devices, not as a replacement for a hardware SPI
controller on high-throughput paths.

Example controller node:

```dts
spi-gpio {
	compatible = "spi-gpio";
	#address-cells = <1>;
	#size-cells = <0>;
	sck-gpios = <&gpio 10 GPIO_ACTIVE_HIGH>;
	mosi-gpios = <&gpio 11 GPIO_ACTIVE_HIGH>;
	miso-gpios = <&gpio 12 GPIO_ACTIVE_HIGH>;
	cs-gpios = <&gpio 13 GPIO_ACTIVE_LOW>;

	peripheral@0 {
		reg = <0>;
		spi-max-frequency = <100000>;
	};
};
```

FDT GPIO writes use logical active/inactive values; the GPIO provider applies
the specifier's polarity. The controller asserts CS with logical one and
deasserts it with logical zero, including for active-low CS. The controller
owns the listed GPIOs for its lifetime.
