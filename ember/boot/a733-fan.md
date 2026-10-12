# Orange Pi Zero 3W fan control

The stock fan connector uses A733 PWM0 channel 5 on PD5.
EmberBSD provides a native PWM controller and temperature-based `pwmfan` control.
The controller and policy have executable software checks.
Physical speed control and the fan's lowest reliable starting duty remain unverified.

## Hardware interface and provenance

The wiring, 25,000 ns period and duty table come from the
[Orange Pi board tree](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/arch/arm64/boot/dts/allwinner/sun60i-a733-orangepi-zero3w.dts).
The [vendor PWM v203 driver](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/pwm/pwm-sunxi.c)
provides register facts for the native BSD implementation.
The [pin controller](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/pinctrl/pinctrl-sun60iw2.c)
selects PWM0 channel 5 with PD5 function 7.
No Linux implementation is copied into the BSD driver.
The device trees retain their original dual license.

PWM0 takes the raw DCXO source.
The CCU provider reads its frequency through the existing RTC observation interface.
It does not change an oscillator or PLL.
One Zero 3W reports 26 MHz, while its older fixed `osc24M` node reports 24 MHz.
The PWM provider uses the observed rate instead of that fixed node.
The resulting electrical frequency still needs an instrument measurement.

The vendor `fan-supply` is DC1SW2, which also powers microSD.
The fan driver never disables that shared regulator.
It changes PWM duty only.
A fan cable alone does not establish a usable tachometer input.\nThis driver does not read a tachometer.
Duty readback alone does not prove rotation or RPM.

## Control policy

The board DTS selects these cooling states:

| State | Duty / 255 | Rising temperature | Release temperature |
|---|---|---|---|
| 0 | 0 | Below every active trip | After the lowest trip releases |
| 1 | 50 | 55 °C | 50 °C |
| 2 | 102 | 65 °C | 60 °C |
| 3 | 170 | 75 °C | 70 °C |
| 4 | 255 | 85 °C | 80 °C |

These thresholds are an EmberBSD policy, not vendor hardware limits.
The vendor table supplies the duty values and its first fan threshold is 55 °C.
The existing independent CPU throttle at 85 °C remains enabled.

The native `allwinner,sun60i-a733-ths` binding exports physical indices 0 through 4.
Local index 5 returns the hottest of all five sensors.
It fails if any conversion is missing or calibration is incomplete.
The standard `thermal-sensors` specifier connects that provider to the thermal zone.
The fan samples once per second.
It combines all assigned active trips and their hysteresis.
An invalid temperature or unsupported thermal configuration requests full cooling.

The generic fan consumes `thermal-zones`, active `trips`, and `cooling-maps`.
Each supported map specifies one fixed state: equal minimum and maximum values.
The hottest mapped trip in each zone must select the final full-duty state.
Ranged states, passive governors and `THERMAL_NO_LIMIT` maps are not implemented.
Without an assigned zone, the driver retains full-speed behavior.
Other boards need their own wiring, provider and physical acceptance.
The fan cannot detach while the FDT PWM API lacks a channel-release operation.

The PWM provider implements continuous output on ten channels.
It preserves unrelated channel registers and shared pair clocks.
Capture, pulse groups and dead-zone modes are not implemented.
Conflicting firmware ownership returns an error.
Duty changes preserve a running waveform when its clock configuration is unchanged.
Hardware errors propagate to the fan policy and trigger a full-duty retry.

## Inspect and set a minimum state

Read the attached driver name from `dmesg`.
For an instance named `pwmfan0`, run:

```sh
sysctl hw.pwmfan0
envstat
```

`level` is the last applied state, not measured RPM.
`temperature` uses millidegrees Celsius; `INT_MIN` means unknown.
`error` contains the last temperature or PWM error.

A minimum state can avoid a fan's unreliable low-speed range:

```sh
sysctl -w hw.pwmfan0.min_level=2
```

This sets a floor; hotter trips can still request states 3 and 4.
State 0 restores the DTS policy without a floor.
Do not copy a tested minimum from a different fan or board.
After physical acceptance, an administrator can save the chosen value in `/etc/sysctl.conf`.
No private stand setting is part of the source tree.

## Build and validate

Build a coherent `EMBER64` kernel, module set and Zero 3W DTB with the
[cross-build procedure](cross-build.md).
Install the Zero 3W DTB selected by the actual boot script.
Updating only the kernel leaves the fan node absent from an older DTB.
Retain the previous kernel, modules and DTB together for recovery.

Run the production-source regressions:

```sh
sh ember/tests/a733-pwm.sh
sh ember/tests/pwm-fan.sh
sh ember/tests/fdt-thermal.sh
sh ember/tools/a733-accelerator-clock-contract.sh
```

Host ASan/UBSan checks exercise fake registers and temperature failures.
They do not prove physical PWM, rotation, cooling capacity or noise.
Valgrind does not support the Apple Silicon host; these checks are not a Valgrind run.
Physical acceptance must cover stopped starts, each useful duty, sustained load,
hysteresis, reboot persistence and storage/network regression.
The separate [power-off result](../boards/orange-pi-zero-3w.md#hardware-notes)
does not establish speed control.
