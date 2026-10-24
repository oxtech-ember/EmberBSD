# ACPI passive cooling

A thermal zone with `_PSV`, `_TC1`, `_TC2` and a processor list `_PSL`
limits those processors when it reaches `_PSV` (ACPI 6.5, section 11.1.5.1).
Each sample changes the limit by

    dP[%] = _TC1 * (Tn - Tn-1) + _TC2 * (Tn - _PSV)

with temperatures in kelvin. One step changes it by at most 25 percent.
A small trend still moves the limit by one percent. Below `_PSV`, the limit
rises by at least one percent per sample until the zone releases its
processors. Without `_TC1`, `_TC2` or `_PSL` the zone does not limit them.
`_HOT` and `_CRT` are reported for passive-only zones as for other zones.

The zone samples every `_TSP` tenths of a second while it limits its
processors and when it is within 5 K of `_PSV`. Otherwise it keeps `_TZP`.
A missing or zero `_TSP`, or one not shorter than `_TZP`, keeps `_TZP`.

## Processor performance

`acpi_perflimit` keeps one request per zone and processor. The provider
applies the strictest request. CPPC (`acpicppc`) writes the desired
performance as the lower of the user's target and the limit. A limit is a
percentage of the range from the lowest to the highest performance; zero
selects the lowest. `machdep.cpufreq.cpuN.target` keeps the user's request;
`current` reports the desired-performance register. Without a provider the
zone logs the failure once and retries on later samples.

| Interface | Meaning |
| --- | --- |
| `hw.acpitzN.passive_limit` | The zone's request, percent; 100 means none |
| `acpitzN: passive cooling at T C` | The zone started to limit its processors |
| `acpitzN: passive cooling released at T C` | The zone withdrew its request |

## CIX P1

On the checked Orange Pi 6 Plus boot, 13 zones report passive 85 C and
critical 98 C. Four list processors: `TZB0` cpu10/11, `TZB1` cpu0/1,
`TZM0` cpu6/7 and `TZM1` cpu8/9. CPPC attaches to all twelve processors.
Cores cpu2 to cpu5 are not in any `_PSL`. Whether the firmware provides
`_TC1`, `_TC2` and `_TSP` is unverified until its tables are dumped; without
the constants the zones do not limit processors.

## Checks

`ember/tests/acpi-passive.sh` runs the extracted zone step, broker and CPPC
functions with controlled ACPI providers. `ember/tests/acpitz-fan.sh` keeps
the active cooling contract. Neither replaces a load test on the board:
temperatures, applied frequency and recovery must be read there.
