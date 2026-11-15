# ACPI AC adapter status

`acpiacad` keeps one sample of `_PSR` and reports a power switch event only
when the value changes. Three paths read `_PSR`:

- `Notify()` on the adapter device, and resume;
- the ACPI poll (`acpi_register_poll()`, `hw.acpi.poll_interval`, 5 seconds
  by default), for firmware that never sends `Notify()`;
- an envsys(4) refresh, when user space asks for the sensor.

sysmon refreshes every registered sensor on each dictionary request, under
one global lock, so `envstat -d` for any other device also refreshes this
one. `_PSR` can be an embedded controller transaction: on an Orange Pi 6
Plus (CIX P1, firmware BiosV1.5) one evaluation took about 0.08 s of CPU
time on a performance core and about 0.25 s on an efficiency core, and every
`envstat` call paid for it.

A refresh therefore reuses a sample that is at most `ACPIACAD_REFRESH_SECS`
(5) seconds old, counted in whole seconds of uptime. With the default poll
the sample is normally younger, and a refresh runs no control method; when a
poll runs late, one refresh evaluates `_PSR` and the next ones reuse that
sample. The age does not advance while the uptime clock is stopped, so a
sample taken before a suspend stays in use until the resume task reads
`_PSR` again. With the poll disabled a refresh still evaluates `_PSR`, at most once per bound, so
the sensor does not depend on `Notify()` alone. A failed read marks the
sensor invalid, keeps the last status and is not retried by a refresh inside
the bound; the next poll or `Notify()` retries it.

## Source contract

```sh
sh ember/tests/acpiacad-refresh.sh
```

The contract extracts `struct acpiacad_softc`, `acpiacad_get_status()` and
`acpiacad_refresh()` from the driver and runs them with a controlled `_PSR`
provider, clock and mutex model: first request, reuse inside the bound, the
bound itself, the notify and poll path, a 60-second run of the default poll
with 33 requests per second, a change seen only through a refresh, failed
and out-of-range reads. It then applies six deliberate faults to the
extracted functions and requires each to be caught;
`ACPIACAD_MUTATIONS=0` skips that part, for a run against a source without
the bound. A source path as the first argument selects another driver file:
the driver before this bound fails the same assertions.

The checks do not run a kernel or real AML. They do not measure the cost of
`_PSR` on a board; the figures above are one measurement of one board.
