# Opt-in CIX SPCR console correction

Status: loader source and host contracts. Physical CIX acceptance is pending.

## Problem

On the observed CIX P1 firmware of an Orange Pi 6 Plus, SPCR selects the PL011
at MMIO `0x040e0000`, GSI 331. The DEBUG header and DBG2 describe the PL011
at `0x040d0000` (`\_SB.COM2`). The kernel selects its ACPI console from SPCR,
so console output goes to the wrong UART. A separate RAM-only correction to
`0x040d0000`, GSI 330, produced a working console on that board.

## Command

At the efiboot prompt, before `boot`, run:

```
acpi cix-spcr
```

The command never runs implicitly. `acpi`, `acpi on` and `acpi off` keep
their previous behavior. Results are printed with the prefix `CIX SPCR:`.
Success reports `shadow installed (MMIO 0x040d0000, GSI 330)`.
A second invocation reports `already correct` and allocates nothing.
Any other result leaves the ACPI root and all tables unchanged.
The correction is not persistent; repeat it after every reset.

## Applicability

The command accepts one table profile only: the exact 80-byte SPCR revision 2
and 92-byte DBG2 revision 0 with OEM `CIXTEK`, table `SKY1EDK2`,
OEM revision `0x01000101`, creator `CIX ` revision 1. Every byte must match,
including the checksums. An SPCR already equal to the corrected bytes is
reported as already correct.

This identifies the observed table defect, not a board or firmware build.
Another system with identical tables would also match. DBG2 has no interrupt
field: GSI 330 belongs to this observed profile and is not derived from DBG2.
SMBIOS strings are not used as a gate.

## Validation and ownership

Before any write the loader checks:

- the live EFI memory map: version, descriptor stride and count, page
  alignment, overflow and overlaps;
- every table read lies inside one `EfiACPIReclaimMemory` or
  `EfiACPIMemoryNVS` descriptor without `EFI_MEMORY_RP`;
- RSDP revision 2, length 36 and both checksums;
- XSDT length, revision, checksum and at most 256 entries; each child table
  is at most 64 KiB, checksummed, unique and non-overlapping;
- exactly one SPCR and one DBG2;
- an optional RSDT must reference the same unique child set as the XSDT.

Firmware ACPI tables are never written. The loader allocates one
`EfiACPIReclaimMemory` page, below 4 GiB when an RSDT exists, and refreshes
and repeats all checks after the allocation. It copies RSDP, XSDT, the
optional RSDT and SPCR into that page. Only SPCR base address, GSI and
checksum, the copied root references and their checksums change.
The loader publishes the new root only after every step succeeds;
otherwise it frees the page.

The kernel receives the root through `/chosen/netbsd,acpi-root-table`.
The loader does not report ACPI reclaim memory as usable RAM, so the page
survives `ExitBootServices` in the same way as the firmware tables.
EFI memory attributes describe capabilities, not current page permissions;
the design therefore relies on owned memory instead of in-place writes.

## Regression

The host contracts compile the production `cix_spcr.c`, the unchanged
`efiacpi.c` function bodies and `command_acpi` from `boot.c`. Firmware
memory-map and allocation services are fakes. Run from the source root:

```sh
TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tests/efi-cix-spcr.sh /absolute/new-check-directory
```

On macOS, the legacy RSDT cases need low 32-bit addresses. Use an x86_64
build under Rosetta with a reduced page-zero segment:

```sh
TEST_CFLAGS='-arch x86_64 -DCIX_LEGACY_TEST -fsanitize=address,undefined' \
TEST_LDFLAGS='-Wl,-pagezero_size,0x4000' \
    sh ember/tests/efi-cix-spcr.sh /absolute/new-legacy-directory
```

`ember/tests/efi-cix-spcr-mutations.rb` applies fourteen source mutations to
memory-map, range, checksum, profile, reference, shadow-alias and
4 GiB-bound checks.
Each mutant must fail a behavioral assertion. The RSDT fixtures are synthetic
standard-format tables; the board captures prove only the RSDP and XSDT.

## Limits

Host contracts and an EFI VM do not establish CIX firmware behavior.
The command refuses, without changes, when the firmware places an ACPI
table in another memory type, reports `EFI_MEMORY_RP` for a table, or
reports `EFI_MEMORY_WP` or `EFI_MEMORY_RO` for the new page. The QEMU EDK2
map reported only write-back caching for ACPI reclaim memory; the CIX map
has not been recorded. Run `mem` at the prompt before the first board test.
The optional `CM5_UART_TRACE` loader trace probes SPCR before this command
and keeps using the firmware UART.
Physical acceptance requires a CIX boot that shows the kernel console on
the DEBUG UART with this loader.
