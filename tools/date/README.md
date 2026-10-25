# Native bootstrap date

[bootstrap.json](bootstrap.json) selects the complete [date source](../../bin/date/date.c)
and [declaration header](../../bin/date/extern.h).
The existing [host recipe](Makefile) and [target recipe](../../bin/date/Makefile) remain unchanged.
The host recipe selects `HOSTPROG`; the target recipe adds `netdate.c` and `-lutil` only for the target.
The finite profile has nine source members, including both recipes and the common host integration.

The profile defines `HAVE_NBTOOL_CONFIG_H=1` to select the existing native tool mode.
The shared [bootstrap mode header](../compat/bootstrap/nbtool_config.h) includes only the existing annotation header.
It defines no API capability values and contains no compatibility implementations.
Native headers, `tzfile.h`, time conversion and formatting come from the selected ABI.
The normal host build retains its full compatibility configuration and dependency route.

The existing tool mode excludes network time setting, clock setters and accounting writes.
It refuses a canonical date operand unless `-j` requests conversion without changing the clock.
The profile checks that refusal. It does not enable clock mutation through a bootstrap input.
Tool-mode `-d` accepts ISO dates; natural-language parsing and `-f` remain unavailable in this mode.
The target date implementation keeps its existing functionality.

The 41 owner cases use fixed epochs, fixed dates and literal expected output.
They cover pre-epoch values, integer bases, calendar fields, ISO forms, leap years,
canonical `-j` conversion, formatting growth and invalid inputs.
UTC cases use `-u`, which selects `UTC0`; the offset case uses a fixed POSIX timezone.
No expected value is copied from a child or depends on the current clock or a zoneinfo file.
The first fixed epoch also supplies the final direct runtime observation.

The ISO digit check validates every required byte before conversion.
It stops at a NUL or nondigit. Rejection cases exercise only the repaired parser.
No old malformed-input replay forms part of this profile's acceptance.
C analysis, memory validation and actual native execution remain separate gates.

The [NetBSD tool mode](https://github.com/NetBSD/src/blob/trunk/bin/date/date.c) remains the integration baseline.
[OpenBSD](https://github.com/openbsd/src/blob/master/bin/date/date.c),
[FreeBSD](https://github.com/freebsd/freebsd-src/blob/main/bin/date/date.c) and
[DragonFly BSD](https://github.com/DragonFlyBSD/DragonFlyBSD/blob/master/bin/date/date.c)
retain related date implementations and different parsing options.
No donor implementation was copied. The original copyright and license notices remain intact.
