# ACPI fan policies

The tested CM5 setup showed stops at intermediate PWM levels. The cause
was not established. The thermal-zone driver offers two optional fallback
policies for this case and a configurable minimum active level. The boolean
policies default to `0`; `fan_min_level` defaults to `-1`. The controls require
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
Physical acceptance of this new policy is pending.

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

On 2026-10-08, a CM5 on Waveshare CM5-NANO-B booted revision `72c00a3bbc19`
with `force_fan=1` from sysctl.conf. Its operator confirmed continuous rotation.
Nineteen samples over 18 minutes retained D0 at 37.5–41.3 °C. The setting
also returned after booting the later `b78f10c721f7` kernel. The exact fan model
was not established. These checks do not validate intermediate speeds.

The source regression executes the driver's thermal selection, sysctl and
zone-refresh functions with controlled ACPI providers:

```sh
sh ember/tests/acpitz-fan.sh
```

It checks normal thresholds, hysteresis, the minimum-level and maximum-only
policies, manual override, failed samples, queue rollback, and zone refreshes.
The minimum-level extension passes 136 production-function assertions under
ASan/UBSan on macOS. Physical acceptance of the extension is pending.
The 2026-10-11 UTC physical CM5 check used `EMBERV3DRENDER` from
`0d87de7380d863ff66a2dc8395929feb0b1b68ba`, GCC 16.2.0 and UEFI
`rpi5-20260730.1`. The board and installed hashes are recorded in the
[CM5 receipt](../boards/compute-module-5.md#station-and-thermal-policy-check-2026-10-11).
The native production-function fixture passed 57 assertions. Four bounded
CPU loads raised the temperature from 39.7 °C to 50.1 °C. Maximum cooling
then entered D0. After removing load, cooling stayed on at 45.2 °C and
entered D3 at 44.1 °C. The 54-second cycle used two-second samples.
No undervoltage or throttle flags appeared. Both policy settings survived
reboot; the normal WPA3 service returned with required PMF.

The first `envstat` fan reading can be the initial cached sample. Fan
refresh is asynchronous. For current ACPI state, also inspect the board's
`hw.acpi.power.acpifanN` sysctl nodes. Neither reading measures RPM.
The operator later confirmed that the fan stops and reaches full speed
quickly when it starts, without jerking. This observation applies to the
installed fan and the off/maximum policy. It does not validate intermediate
speeds, cold-start behavior or measured noise. RPM, suspend/resume and
long-duration operation remain separate checks.
