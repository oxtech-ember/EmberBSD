# GTDT platform timers and the SBSA watchdog

`acpi_gtdt_walk` hands each GTDT platform timer to its drivers;
`sbsawdt` attaches to a non-secure watchdog GT entry. The walk starts at
`PlatformTimerOffset` and visits at most `PlatformTimerCount` entries.
Revision 3 tables place the EL2 virtual timer fields between the fixed
fields and the platform timers, so the entries do not start at
`sizeof(ACPI_TABLE_GTDT)`. An offset outside the table or before its fixed
fields yields no entries. An entry shorter than its header, or one that
extends past the table, stops the walk.

## CIX P1

The Orange Pi 6 Plus firmware (`CIXTEK SKY1EDK2`, OEM revision 0x1000101)
provides a revision 3 GTDT of 132 bytes with one watchdog GT at offset 104:
refresh frame `0x16008000`, control frame `0x16003000`, GSIV 408,
level-triggered, active-high and non-secure. Read with `acpidump -t` from
the board on 24 October 2026. Before this walk, the board attached only
`swwdog0`.

`sbsawdt` maps both frames and registers disarmed with sysmon; it writes
no register until `wdogctl` arms it. On the first timeout the hardware
raises WS0 (GSIV 408, not handled by the driver); on the second it raises
WS1, which the platform must turn into a reset. Whether WS1 resets this
board is unverified.

## Checks

`ember/tests/acpi-gtdt.sh` runs the production walk with byte-built
revision 2 and 3 tables, including the CIX P1 layout, count and bounds
cases. The previous walk returns no entry for the CIX P1 table.
Board acceptance: `wdogctl` lists `sbsawdt0`; an armed watchdog that is
not refreshed resets the board.
