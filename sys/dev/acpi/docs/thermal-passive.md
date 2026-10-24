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
The poll callout schedules the next poll before its sample runs; a sample
that enters the `_TSP` band shortens the pending poll at once, so the first
passive step is followed by the next within `_TSP`, not `_TZP`. Detach stops
the callout again after the last queued sample.

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
Cores cpu2 to cpu5 are not in any `_PSL`.

The DSDT read with `acpidump` on 24 October 2026 gives every zone `_TC1` 4,
`_TC2` 3, `_PSV` 358.2 K and `_CRT` 371.2 K; `_TSP` is 0.3 s (0.1 s in one
zone) and `_TZP` 5 s or 1 s. At 1 K above `_PSV`, rising 0.5 K per sample,
a step lowers the limit by 5 percent; 1 K below `_PSV` raises it by 3 percent.
These are firmware constants, not a measured thermal response.

## Checks

`ember/tests/acpi-passive.sh` runs the extracted zone step, broker and CPPC
functions with controlled ACPI providers. `ember/tests/acpitz-fan.sh` keeps
the active cooling contract. Neither replaces a load test on the board:
temperatures, applied frequency and recovery must be read there.
