# Opt-in CIX SPCR console correction

Status: loader source, host contracts and console boots on the observed CIX board.

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

## Automatic sample profile

The opt-in [CIX P1 profile](../../../../ember/boot/cix-p1-persistent.cfg) runs the command on each
boot. Copy it to `boot.cfg` at the root of the loader's EFI filesystem.
It selects `netbsd.wifi.gz` and `root=NAME=ember-cix-root`. Supply that checked
kernel and a uniquely named FFS wedge. The kernel must not use
`MEMORY_DISK_IS_ROOT`. The normal entry clears any previously selected initrd.
The recovery entry selects the known working `/netbsd` and `root.fs` pair.
Verify these file aliases before deploying the profile.

Both CIX entries queue `userconf disable acpibat` before kernel attachment.
This is a sample-local diagnostic policy for the observed battery wait.
It does not change `GENERIC64` or fix the underlying ACPI failure.
Do not add `-c`: the loader supplies the command through FDT without an
interactive userconf prompt. Commands remain queued until the loader resets.
The boot prompt and the five-second menu timeout remain available for recovery.

This loader has no shell conditionals. A failed SPCR correction prints its
reason and the menu continues. Keep UART recovery available. The profile is
not a firmware correction and does not qualify automatic physical startup.

The host profile contract executes the actual boot.cfg parser, ARM menu
dispatcher, selected command handlers and userconf queue. EFI correction
and kernel execution are boundary doubles. It checks command order, disk-root
arguments, stale initrd removal and RAM recovery. Run with GCC16:

```sh
CC=gcc-16 TEST_CFLAGS='-O2' \
    sh ember/tests/cix-p1-boot.sh /absolute/new-check-directory
```

The unchanged ARM dispatcher has an inherited uninitialized read for an empty
first command and no command-length bound. The profile contract rejects empty
or 80-byte command segments before execution. This profile contains only short,
nonempty segments. GCC path analysis still reports the inherited empty-command
finding; the sample does not repair that loader code.

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

## Manual kernel console profile

`EMBER64_CIX_UART2` is a separate console-only diagnostic kernel.
Build this configuration with the [GCC16 kernel procedure](../../../../ember/development/kernel.md).
It includes `EMBER64` and adds only `CIXP1_UART2_CONSOLE`.
It adds no early console, active VPU probe or SCMI service.
Generic `EMBER64` keeps its existing console selection.

When SPCR is absent, the manual profile can select DEBUG UART2 at
`0x040d0000`, 115200 8N1. It first validates bounded RSDP, XSDT and FADT
descriptions through trusted native firmware mappings. FADT must reference
the exact 81063-byte DSDT with SHA256
`9e7bf30cb29484910cceec208adf6fa9f1fab4ed54c93236d53bb4119b04260a`.
The check validates checksums, table limits and agreement between nonzero
legacy and extended DSDT references. The ordinary SPCR lookup must then
return `AE_NOT_FOUND`; other lookup errors do not select UART2.

When SPCR exists, this manual profile accepts only the checked 80-byte
revision-2 PL011 description for UART2, GSI330 and 115200 8N1.
It rejects the original UART3 description and descriptions for UART1.
The existing loader shadow matches the accepted UART2 fields.
Rejected descriptions cause no new UART register access.

Selection uses the existing polled PL011 console with frequency zero.
This preserves firmware divisors but still writes UART control/line registers.
Later ACPI attachment remains responsible for the native UART and interrupt.
The profile changes no firmware table, variable, pinmux or clock setting.
It does not repair NVRAM retention or restart by the power button.

On the observed board, GCC16 kernel `f84fcb5efef545db3f148035873a99ad23d7fe60`
passes a manual cold boot with SPCR absent and an automatic cold default boot.
An automatic warm default boot also passes. The cold default selects UART2,
hands the console to `plcom2` at 115200 8N1 and reaches a clean FFS root.
WPA2/CCMP, authenticated SSH/PTY and 2 MiB files each way recover with matching
SHA256. Association is initially pending and recovers without intervention;
instant availability is not established. This is one board and one cold default
cycle. Firmware settings retention, RTC time and S5 wake remain unresolved.

`EMBER64_CIXVPU_UART2_BOOT_PROBE` combines this console gate with the existing
manual VPU boot profile. It retains VPU7/SCMI128 and all exposure guards.
It is a separate manual configuration; the console-only and default profiles
keep their existing activation policy.

Run its actual-source host contract with GCC16:

```sh
CC=gcc-16 CFLAGS='-O2 -g -fanalyzer' CIX_UART2_ANALYZE=1 \
    sh ember/tools/cix-uart2-console-contract.sh
```

The contract models native mapping, SHA and PL011 services.
It exercises both option states. CI is configured to run it with sanitizers
and Memcheck.
These checks do not establish a physical cold boot or UART handoff.
Existing PSCI/MADT discovery still uses its native ACPI routines;
this profile does not harden the complete ACPI parser.

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
The observed CIX board keeps its DEBUG UART2 kernel console through five
unattended warm boots with the [persistent profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/cix-p1-orangepi6plus/boot.md#persistent-sd-root-and-recovery-applicability).
That acceptance is limited to the recorded stock firmware and table profile.
The command refuses, without changes, when the firmware places an ACPI
table in another memory type, reports `EFI_MEMORY_RP` for a table, or
reports `EFI_MEMORY_WP` or `EFI_MEMORY_RO` for the new page. The QEMU EDK2
map reported only write-back caching for ACPI reclaim memory; the CIX map
has not been recorded. Run `mem` at the prompt before the first board test.
The optional `CM5_UART_TRACE` loader trace probes SPCR before this command
and keeps using the firmware UART.
Cold power removal and other firmware/table profiles remain unqualified.
