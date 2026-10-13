# Raspberry Pi 5 and CM5 power-button fallback

This optional module preserves the GPIO20 power-button path on existing
Pi 5 and CM5 installations whose UEFI does not deliver ACPI button events. It reads
the input, debounces it and sends a standard `sysmon_power` event. It does
not configure GPIO pins or claim their interrupts. The firmware must have
already configured GPIO20 as an input.

The source was recovered from a deployed EmberBSD Pi 5 on 2026-10-07.
The original source has SHA256
`23c35c01ce8b9bfb409971aeaa600d8b1fde5242cd65750a4bf976b508c7ed98`.
The original BSD-2-Clause identifier and implementation are preserved;
the import adds an origin comment and uses the common module build rules.
No upstream acceptance or new authorship is claimed.

`ember/build-kernel.sh` builds this module with the kernel and the other
board modules. Deploy only the module from the matching kernel build.
Loading remains opt-in: preserve an existing installation's `rpi5button`
rc service, or load it with `modload rpi5button` after starting `powerd`.
Do not enable it alongside a working ACPI power-button event source.

A successful build or module load does not prove that pressing the button
shuts down the board. A physical press and subsequent power-on must be
checked separately when changing firmware or the event path.

## Compute Module 5

The exact DMI product `Raspberry Pi Compute Module 5` is also accepted.
The Raspberry Pi [CM5 device tree](https://github.com/raspberrypi/linux/blob/rpi-6.18.y/arch/arm64/boot/dts/broadcom/bcm2712-rpi-cm5.dtsi)
identifies the power button as active-low BCM2712 GIO20, with a 50 ms debounce.
This is distinct from RP1 GPIO20 on the expansion header. On Waveshare
CM5-NANO-B use the **PSW** power button; KEY is a separate GPIO21 user button,
and BOOT selects USB provisioning. See the [carrier guide](https://www.waveshare.com/wiki/CM5-NANO-B).

For firmware without an ACPI button event source, install the matching module
and the optional [rc service](../../../ember/etc/rc.d/rpi5button), then enable:

```conf
powerd=YES
rpi5button=YES
```

Place the service in `/etc/rc.d/rpi5button` with mode 0555. Start `powerd`
first, then `rpi5button`. The service verifies powerd is running. The module
still refuses foreign product names and a GPIO pin not configured as input.
Keep the normal `/etc/powerd/scripts/power_button` handler for an orderly
`shutdown -p now`. A short press is enough; holding the physical button for
more than five seconds requests a firmware hard power-off.

```sh
sh ember/tests/rpi5button.sh
```

The platform-gate regression passed on macOS and an EmberBSD AArch64 VM;
physical CM5 acceptance is recorded on the [board page](../../../ember/boards/compute-module-5.md).
