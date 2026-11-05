# A133 power and volume keys

The A133 adapter preserves the vendor NMI phandle and the PMIC's two-cell
IRQ. It adds the A80-compatible NMI binding only to the matched controller
at 0x07010320 feeding GIC SPI 103, with the AXP803 at 0x34 using IRQ 0
active low. The same register block and parent interrupt are described in
the [A100 DTS](../../../../external/gpl2/dts/dist/arch/arm64/boot/dts/allwinner/sun50i-a100.dtsi).

An appliance image can add the empty boolean property
`ember,power-key-long-hotkey;` to its selected AXP803 node. That image
receives the existing long-press IRQ (status bank 5, bit 3) as a sysmon
hotkey named `power-long`. The stock binding retains the kernel's
`PSWITCH_TYPE_POWER` handling, which can power down the machine without
powerd. Select the hotkey property explicitly when packing an appliance
that owns its power policy. The adapter never adds it. A short press has a
separate IRQ (bank 5, bit 4); a future opt-in can expose it as a separate
`power-short` hotkey.

For the vendor 1350 mV keyboard at 0x05070800, the adapter supplies the
A100 LRADC clock and reset and preserves SPI 22. The common driver selects
this path from the `allwinner,keyboard_1350mv` hardware-compatible string.
It requires no board marker. The legacy `allwinner,sun4i-a10-lradc-keys`
binding keeps its regulator-based path. The driver reads the two
configured voltage/code pairs from `key0` and `key1`. It accepts volume
codes 115 and 114 and ignores unused entries beyond `key_cnt`.
A down interrupt enables sample-ready interrupts until two equal samples
identify a key. Release reports the same switch. For the 210/410 mV ladder,
samples 0–14 select `volume-up`, 15–41 select `volume-down`, and 42–63
select neither. The thresholds follow the
[vendor input driver](https://github.com/orangepi-xunlong/linux-orangepi/blob/orange-pi-4.9-sun50iw9/drivers/input/keyboard/sunxi-keyboard.c).
The hotkeys carry sysmon pressed/released events; application actions belong
to the selected userspace policy. Host FDT and ladder checks and cross-compiled
objects cover the source path. Physical key events require a booted candidate.
