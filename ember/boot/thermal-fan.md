# ACPI fan policies

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

The 2026-10-12 check used CM5 Rev 1.0, BCM2712 D0, 4 GiB, Waveshare
CM5-NANO-B and UEFI `rpi5-20260730.1`. The four-wire fan was reported as
MiiBestOD; its model and voltage rating remain unknown.
Twenty-second duty trials on kernel `0d87de738` showed tachometer activity
at 125, 150, 175, 200, 220 and 250/255. At 75/255, 90 of 100 settled
30 ms windows had no tachometer pulse despite a stable PWM signal.
Three starts from a stopped rotor at 125/255 succeeded without a maximum
boost. These were starts on a warm board, not cold power-on qualification.

Kernel `ab6f88f04ca73c6958adb39d0e1c717f629e7901`, built with GCC 16.2.0
as `EMBERV3DRENDER`, then ran with `fan_min_level=2`, `fan_max_only=0`
and `force_fan=0`. Cooling started at 50.1 °C with measured PWM duty
125/255. Four bounded SHA256 CPU workloads ran for 120 seconds.
During the 300-second sampled interval, all 272 active 30 ms windows
had tachometer pulses; the mean pulse frequency was 64.7 Hz.
No maximum-duty sample occurred. Peak ACPI temperature was 57.8 °C.
There were no undervoltage or throttle flags. The operator reported quieter,
steady rotation. This was a functional check on a shared board.
All three policy values persisted after a normal reboot; WPA3/PMF returned.
Ten post-boot windows again measured 125/255 PWM and tachometer pulses.

Cooling remained active at the end, at 50.1 °C. The run did not reach
its falling shutdown threshold or the rising 67.5/75 °C transitions.
The source fixture checks those transitions; direct duty trials check
rotation at the stronger steps. Neither is a physical higher-temperature
transition test. The probe only read PWM/GPIO during the automatic run.
It was unloaded afterward. See the [installed receipt](../boards/compute-module-5.md#cooling).

Tachometer pulses per revolution are unknown, so no absolute RPM is claimed.
Sampled windows do not establish continuous rotation between samples.
Measured noise, cold power-on, suspend/resume and long-duration operation
remain unvalidated.

### Off/maximum fallback check

The earlier [CM5 receipt](../boards/compute-module-5.md#station-and-thermal-policy-check-2026-10-11)
records the fallback on kernel `0d87de738`, GCC 16.2.0 and the same UEFI.
A 54-second heat/cool cycle entered maximum cooling at 50.1 °C, retained
it at 45.2 °C and stopped at 44.1 °C. Its settings survived reboot.
That result validates the separate fallback, not the minimum-level policy.

The first `envstat` fan reading can be the initial cached sample. Fan
refresh is asynchronous. For current ACPI state, also inspect the board's
`hw.acpi.power.acpifanN` sysctl nodes. Neither reading measures RPM.
