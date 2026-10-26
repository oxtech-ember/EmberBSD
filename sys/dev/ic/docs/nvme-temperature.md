# NVMe composite temperature

`nvme` registers one sysmon temperature sensor per controller, named after
the device (`envstat -d nvme0`). A refresh reads the controller-scope
SMART / Health Information log page (log identifier 0x02, 512 bytes,
namespace 0xffffffff) with the polled admin path of identify and reports
its Composite Temperature (kelvin, bytes 1-2). A refresh never waits for an
admin command slot: a busy queue, a failed command, an unreported value (0)
or one above 1000 K makes the reading invalid, and so does any refresh
while the controller is suspended. A timed-out command may still
complete, so its slot is retired with a no-op completion and the sensor
stops with one message instead of retrying every refresh. The controller's
Warning and Critical Composite Temperatures (WCTEMP, CCTEMP from Identify
Controller) are the lowest overheating values; sysmon alarms strictly
above a limit, so the limits are set one micro-kelvin below them and
`powerd(8)` sees the drive reach them. Detach unregisters the sensor before
the controller shuts down; the `nvme` module depends on `sysmon_envsys`.

## CIX P1

On the Orange Pi 6 Plus (`NE-256`, firmware `T0828A0`) on 26 October 2026:
`composite temperature` 50.85 C idle; WCTEMP 356 K (82.85 C), CCTEMP 358 K
(84.85 C).
During a sequential raw read of the drive under full CPU load the SMART
page reported up to 76.85 C. These are readings of one board on an open
bench, not a thermal qualification.

## Checks

`ember/tests/nvme-temperature.sh` runs the five production sensor functions
from `nvme.c` with a fake admin queue, DMA buffer and sysmon: command
encoding, decoding, invalid readings, limits, attach and detach failures.
including suspend and a timed-out command. It also applies thirteen
deliberate faults to the extracted functions and requires the contract to
catch each one. The fake admin queue cannot model a command completing
after its timeout; the polled path's own completion race predates the
sensor.
