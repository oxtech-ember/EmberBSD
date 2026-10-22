# ACPI fan policies

The canonical firmware series hands off at 125/255 until the OS establishes
its cooling policy. Source checks do not establish flashed-firmware or cold-start acceptance.

The tested CM5 fan almost stopped at 75/255 PWM. It started and ran at
125/255 without a maximum-speed boost. The thermal-zone driver offers a
configurable minimum active level and two optional fallback policies.
The boolean policies default to `0`; `fan_min_level` defaults to `-1`. The controls require
a maximum cooling list (`_AL0`).
Check the thermal-zone and fan names on the actual board.

| Setting | Behaviour |
| --- | --- |
| `force_fan=0`, `fan_max_only=0`, `fan_min_level=-1` | Use normal firmware cooling levels. |
| `fan_min_level=N` | Skip active cooling levels weaker than firmware index `N`. Stay off when cooling is not needed. |
| `fan_max_only=1` | Stay off below the first cooling threshold. Use maximum cooling when the temperature requires cooling. |
| `force_fan=1` | Keep maximum cooling enabled. This overrides `fan_max_only`. |

## Raspberry Pi OS reference

The official [CM5 device tree](https://github.com/raspberrypi/linux/blob/4103a989a46196239b830bd07693277e42b8f992/arch/arm64/boot/dts/broadcom/bcm2712-rpi-cm5.dtsi)
uses RP1 PWM1 channel 3 on GPIO45, an inverted output and a 41,566 ns period.
Its cooling curve has four active steps and 5 °C of hysteresis:

| Rising temperature | PWM value, out of 255 |
| --- | --- |
| 50 °C | 75 |
| 60 °C | 125 |
| 67.5 °C | 175 |
| 75 °C | 250 |

These are PWM duty values, not measured percentages of maximum RPM.
This is a stepped temperature curve, not a timed linear speed ramp.
The [RP1 PWM driver](https://github.com/raspberrypi/linux/blob/4103a989a46196239b830bd07693277e42b8f992/drivers/pwm/pwm-rp1.c)
updates an active channel without an intervening disable.
The [pwm-fan driver](https://github.com/raspberrypi/linux/blob/4103a989a46196239b830bd07693277e42b8f992/drivers/hwmon/pwm-fan.c)
also supports optional start duty and duration. The base CM5 device tree
does not set those options. Driver support alone does not enable a start boost.

The checked EmberBSD UEFI uses the same PWM channel, polarity and nominal
period. It exposes four ACPI cooling devices, without `_FIF` or `_FSL`.
`fan_max_only` replaces their normal selection with the highest step.
This fallback does not provide the Raspberry Pi temperature curve.
Matching register settings do not prove electrical compatibility or stable
rotation on an uncharacterized fan. Check the actual PWM and tachometer
before selecting a minimum speed or adding a start boost.

## Minimum active cooling level

Use `fan_min_level` when a measured low level stalls, but a stronger
intermediate level starts and runs reliably. ACPI indices run in reverse:
`0` selects maximum cooling; larger indices select weaker cooling.
`-1` restores the normal firmware selection. The setting accepts only
indices with a valid temperature threshold and cooling-device list.
It does not specify a PWM percentage or RPM.

For the checked CM5 firmware, index `2` selects 125/255 PWM. This example
skips its unstable 75/255 level while retaining stronger thermal steps:

```sh
sysctl -w hw.acpitz0.fan_min_level=2
sysctl -w hw.acpitz0.fan_max_only=0
sysctl -w hw.acpitz0.force_fan=0
```

The expected rising-temperature curve is off below 50 °C, 125/255 at
50 °C, 175/255 at 67.5 °C, and 250/255 at 75 °C. Each transition retains
5 °C of hysteresis. The floor cannot reduce a stronger cooling request.
A failed or invalid temperature sample selects maximum cooling.
`force_fan` and `fan_max_only` take precedence over the floor.

Add the same three assignments to `/etc/sysctl.conf` after validating
that the selected level starts and runs on the installed fan.
This is stepped temperature control. It does not add a timed PWM ramp.
A bounded physical CM5 check passed; see the validation limits below.

## Temperature-controlled maximum cooling

Use this policy when intermediate speeds do not work, but permanent maximum
cooling is too noisy:

```sh
sysctl -w hw.acpitz0.fan_max_only=1
sysctl -w hw.acpitz0.force_fan=0
envstat -d acpitz0
```

The firmware still supplies the temperature thresholds. The driver retains
5 °C of hysteresis. With the tested CM5 firmware, cooling starts at 50 °C
and stops below 45 °C. This policy does not wait for the firmware's 75 °C
maximum-speed threshold. It uses the maximum level for every active request.
A failed or invalid temperature sample selects maximum cooling.

To keep this policy after reboot, use these lines in `/etc/sysctl.conf`.
Replace any earlier `force_fan=1` entry:

```conf
hw.acpitz0.fan_max_only=1
hw.acpitz0.force_fan=0
```

This policy avoids intermediate PWM levels. Maximum cooling can still be
loud under load. It does not change the fan's electrical interface.
The temperature-controlled policy requires physical acceptance on each setup.

## Constant maximum cooling

```sh
sysctl -w hw.acpitz0.force_fan=1
```

Add `hw.acpitz0.force_fan=1` to `/etc/sysctl.conf` if this policy must persist.
To restore normal automatic operation, remove persistent overrides and use
`force_fan=0`, `fan_max_only=0`, and `fan_min_level=-1`.
The first two controls accept only `0` and `1`.

Temperature monitoring and hot/critical reporting remain enabled. Policy
changes run in the ACPI worker. A zone refresh retains active cooling when
the cooling devices are unchanged. An incomplete refresh retains the previous
working description. It does not pulse the fan merely to reload its settings.
When a policy step selects "no cooling", the driver clears every active
cooling list, not only the step it last engaged. This also clears devices
that a firmware failsafe or a handoff left engaged. A failed power transition
is reported in the kernel log. The remaining devices are still processed.

Check actual rotation, temperature and undervoltage indicators. An ACPI `D0`
state reports power state, not RPM. On the checked CM5 firmware, `_AL0`
drives `acpifan0` at 250/255 PWM. These policies do not change PWM frequency.
Other carriers, fans and firmware require their own check.

## Validation

The source regression executes the driver's thermal selection, sysctl and
zone-refresh functions with controlled ACPI providers:

```sh
sh ember/tests/acpitz-fan.sh
```

It checks normal thresholds, hysteresis, the minimum-level and maximum-only
policies, manual override, failed samples, queue rollback, and zone refreshes.
The minimum-level extension passes 136 production-function assertions under
ASan/UBSan on macOS and natively on CM5. GCC 16.2 path analysis found no
issues. Clang retained four inherited `bitops.h` dead-store findings.
Cppcheck reported 609 style findings and one information diagnostic, with
no errors or warnings. Eleven added style findings concern unnamed KNF
prototype parameters. Its narrow atomic model excludes alignment assertions;
GCC and Clang use the production atomics. Valgrind is unavailable on this
macOS arm64 host. The sanitizer run is not a Valgrind result.

### Minimum-level check


Current physical scope and source pins are in [DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/bcm2712-cm5/interfaces.md).

A duty trial, a source-fixture threshold transition and a physical thermal
transition establish different properties. Tachometer pulses per revolution are
unknown, so no absolute RPM follows. Sampled windows do not prove uninterrupted
rotation. Cold power-on, suspend/resume, noise and endurance need separate checks.

### Off/maximum fallback check


Current physical scope and source pins are in [DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/bcm2712-cm5/interfaces.md).

The off/maximum fallback and minimum-level policy have separate acceptance.
The first `envstat` fan value can be cached; refresh is asynchronous. Inspect
`hw.acpi.power.acpifanN` for current ACPI state. Neither value measures RPM.
