# ACPI fan policies

Some four-wire fans fitted to a CM5 carrier stall or restart at intermediate
PWM levels. The thermal-zone driver offers two optional policies for this
case. Both default to `0` and require a maximum cooling list (`_AL0`).
Check the thermal-zone and fan names on the actual board.

| Setting | Behaviour |
| --- | --- |
| Both settings `0` | Use the firmware's normal temperature and cooling levels. |
| `fan_max_only=1` | Stay off below the first cooling threshold. Use maximum cooling when the temperature requires cooling. |
| `force_fan=1` | Keep maximum cooling enabled. This overrides `fan_max_only`. |

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

This policy avoids intermediate PWM levels. It does not repair a fan that
jerks or clicks while starting. Maximum cooling can still be loud under load.
The temperature-controlled policy requires physical acceptance on each setup.

## Constant maximum cooling

```sh
sysctl -w hw.acpitz0.force_fan=1
```

Add `hw.acpitz0.force_fan=1` to `/etc/sysctl.conf` if this policy must persist.
To restore normal automatic operation, remove both persistent entries and
set both controls to `0`. Each control rejects values other than `0` and `1`.

Temperature monitoring and hot/critical reporting remain enabled. Policy
changes run in the ACPI worker. A zone refresh retains active cooling when
the cooling devices are unchanged. An incomplete refresh retains the previous
working description. It does not pulse the fan merely to reload its settings.

Check actual rotation, temperature and undervoltage indicators. An ACPI `D0`
state reports power state, not RPM. On the checked CM5 firmware, `_AL0`
drives `acpifan0` at 250/255 PWM. Neither policy changes PWM frequency.
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

It checks normal thresholds, hysteresis, the maximum-only policy, manual
override, failed samples, incomplete refreshes and unchanged cooling devices.
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
Start-up noise, RPM, suspend/resume and long-duration operation remain
separate checks. This short cycle does not establish acoustic performance.
