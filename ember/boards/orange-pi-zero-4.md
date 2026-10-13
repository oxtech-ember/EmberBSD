# Orange Pi Zero 4

[Board catalog](README.md) · [Build instructions](../../README.ember.md#building)

This page preserves the published hardware results; it is not a new test run.
Board revision: not recorded in the original support table.

## Capabilities

| Function | Result and limits |
| --- | --- |
| SoC | Allwinner A733 |
| Boot path | Vendor boot0 and U-Boot, device tree |
| Boot storage | microSD: Tested, 1.8 V signalling in SDR104 at 150 MHz |
| Serial console | Tested |
| All CPU cores | Tested (8 cores) |
| Ethernet | Tested (gigabit) |
| Wi-Fi | Tested: WPA2 on 2.4 and 5 GHz with 802.11n in channels up to 40 MHz (30-48 Mbit/s on 5 GHz through SSH); needs the vendor firmware file |
| Bluetooth | Classic: Tested (inquiry, service query, pairing with Secure Connections and reconnection to a MacBook); BLE: No; needs the vendor patch files |
| Temperature sensor | Tested (five sensors, factory calibration) |
| Fan control | No |
| Watchdog | Tested |
| Power button | No |
| I2C | Tested (power management chip) |
| Audio | No |
| Pin multiplexing and GPIO | Pin multiplexing and GPIO output: Tested; no pin interrupts |
| Real-time clock | Tested across a reboot |
| Processor frequency control | Tested: 408 MHz to 1.8 GHz (little cores) and 2.0 GHz (big cores) with the supply voltage following, by the chip's speed grade; drops to 408 MHz at 85 degC |
| Voltage regulators | Tested: all outputs of the AXP8191 read; the two core supplies are driven; the chip's own switch for the card pins is driven |
| USB | Type-C data port at USB 2.0 rates: a flash drive reads at 22 MB/s, the port is powered only while a device is plugged in (FUSB302, polled); the serdes starts and clocks the controller, SuperSpeed itself not tried (no USB 3 device, one plug orientation only); the two USB 2.0 hosts attach, no device tried |
| Hardware random numbers | Tested: the crypto engine's generator seeds the kernel at boot |
| GPU/NPU power domains | Native PCK600 provider passes software contracts and GCC16 cross object builds; physical transitions and acceleration unverified ([guide](../boot/a733-power-domains.md)) |
| GPU/NPU clocks and resets | Native main CCU providers pass 199 software assertions and GCC12/GCC16 object builds; firmware PLLs stay unchanged, physical sequencing unverified ([guide](../boot/a733-accelerator-clocks.md)) |

Long-run stability has not been established. Application results from an
AArch64 VM do not validate this board's camera, audio or GPU/NPU paths.

## Hardware notes

Boots from microSD to multi-user through the vendor boot loader: eight cores, clocks,
pin multiplexing, SD card, gigabit Ethernet with a Motorcomm YT8531 PHY, five
temperature sensors calibrated from the EFUSE, watchdog. Tested on physical hardware;
RGMII delays were measured on the board with random data.

The AXP8191 regulators are registered as the boot loader left them (`axp8191reg`);
the two core supplies are driven. `sun60icpupll` retunes each cluster's PLL, and the
operating points for the chip's speed grade (read from the EFUSE) take the little
cores from 408 MHz to 1.8 GHz and the big ones to 2.0 GHz, with the voltage raised
before the rate and lowered after it.

The thermal driver asks for the lowest rate at 85 degC and releases it at 75 degC.
Without a heat sink and with all cores busy the board spends most of the time at 408
MHz with short runs at full speed and peaks of up to 90 degC; held at 792 MHz it
still reaches 84 degC, so 408 MHz stays the lowest point.

Only speed grade 0x04 has been run. The chip switches the supply of the card's pins
itself; the pin controller offers that switch as a regulator, so the card runs at 1.8
V signalling in SDR104 at 150 MHz (59 MB/s read against 22 MB/s at 3.3 V; no tuning,
the sample delay stays at zero).

A reboot from that state works. Wi-Fi through the AIC8800D80 module with the `aicwf`
driver: WPA2 with CCMP on 2.4 and 5 GHz with 802.11n, one stream in channels up to 40
MHz (30-48 Mbit/s on 5 GHz and 24-35 on 2.4 GHz through SSH, against 10-20 before
802.11n; the firmware leaves the reordering of aggregated frames to the host, the
driver does it per traffic class; 802.11ac is not announced, because with it only
broadcast frames pass after the association; 802.11ax is not announced either).

The driver loads the vendor firmware `fmacfw_8800d80_u02.bin` from
`/libdata/firmware/if_aicwf`; the file is not part of this tree. The two USB 2.0 host
controllers attach (EHCI and OHCI), no device has been tried on them.

The always-on domain clocks and its I2C bus work; the AXP8191 power management chip
answers and its power key is reported to powerd (polled; not tried with a key, the
board has only the pads). Power-off goes through PSCI.

The real-time clock keeps the time across a reboot. Bluetooth of the same module:
`aicwf` uploads the controller's patches before the wireless firmware, the controller
then answers HCI on UART1 at 1.5 Mbaud with automatic RTS/CTS through `btuart`;
inquiry and a service query to a MacBook work alongside Wi-Fi on both bands, and so
do pairing with Secure Connections and reconnection with the stored key.

The four vendor patch files are read from `/libdata/firmware/if_aicwf` and are not
part of this tree. The true random number generator of the crypto engine
(`sun60icrypto`) seeds the kernel, so the first boot no longer waits for entropy.

The controller of the Type-C data port (DesignWare USB 3) attaches as xHCI through
its USB 2.0 PHY (`sun60iusb2phy`); the serdes for SuperSpeed (`sun60icombophy`, a
Cadence combo PHY) is programmed for USB 3, its PLL locks and the controller runs on
its pipe clock, but no USB 3 device has been tried and only the unflipped plug
orientation is set up, so SuperSpeed is unconfirmed. The Type-C controller of that
port (FUSB302, `fusbtc`) tells when a device is plugged in and only then switches the
port power on (pin PL8); its interrupt pin is not usable yet, so the driver polls
four times a second.

A flash drive attaches at high speed and reads at 22 MB/s. The controller's bus clock
must stay at 24 MHz: it counts its frames from it

## Evidence

Results were migrated from the [published support matrix](https://github.com/oxtech-ember/EmberBSD/blob/fe2727f6ef675868eba642efa73c5a0e33f92191/README.md#supported-boards)
and [hardware notes](https://github.com/oxtech-ember/EmberBSD/blob/fe2727f6ef675868eba642efa73c5a0e33f92191/README.ember.md#hardware-support-and-validation)
on 2026-10-07. These sources do not supply a complete per-check receipt
with tested OS/firmware revisions and dates. The source commit identifies
the documentation snapshot, not the kernel used in every original test.
Future validation should use the [evidence format](adding-a-board.md#record-the-result).
