# A733 read-only snapshot before kernel handoff

The [host builder](../../../../../ember/tools/a733-preboot-snapshot.sh) produces a separate
diagnostic boot command file for Orange Pi Zero 3W and Zero 4. It records
GPU_TOP and GPU_CORE PCK-600 registers in the in-memory FDT `/chosen` node,
immediately before the normal `booti`. No kernel change is required.
This diagnoses the [unresolved CORE transition](a733-power-domains.md);
it does not enable GPU acceleration or change power policy.

## Produce and recover a diagnostic boot

From a clean EmberBSD source export, choose an unused output path:

```sh
sh ember/tools/a733-preboot-snapshot-contract.sh
sh ember/tools/a733-preboot-snapshot.sh \
    ember/boot/orangepi-zero4-boot.cmd /absolute/output/snapshot-boot.cmd
```

The builder rejects an existing output, repeated insertion, multiple
`booti` commands, or a different FDT/boot entry sequence. It preserves
every original line. It neither installs the output nor replaces the
[normal script](../../../../../ember/boot/orangepi-zero4-boot.cmd).

Before a physical probe, retain verified copies and hashes of the board's
normal `boot.cmd` and `boot.scr`. Wrap the generated command file using
the same U-Boot script-image tool and header settings as the normal image.
Review the generated text and retain a recovery route to restore both
normal files. Use the normal DTB and kernel; no GPU experiment property
is required for this snapshot. Installation and reboot are separate actions.

After the diagnostic boot, read and save the FDT properties on EmberBSD:

```sh
ofctl -p /chosen
```

Restore both normal boot files from the verified copies, verify their
hashes, and reboot normally. The properties and U-Boot variables are
temporary RAM data; the insertion does not call `saveenv` or write storage.

## Exact read set and format

The first read is RCCU `S_PPU_BGR`, physical address `0x070101ac`.
Only bit 0 gates the PCK programming interface. If it is clear, the
insertion records `ppu-clock-gated` and skips every PCK read. It does not
enable that clock. A733 has no PPU reset in bit 16.

With bit 0 set, the insertion reads GPU_TOP at `0x07065000`, then GPU_CORE
at `0x07066000`. Each group contains these eight 32-bit registers:

| Cell after base | Register | Offset |
| --- | --- | --- |
| 1 | PWPR | `0x000` |
| 2 | PMER | `0x004` |
| 3 | PWSR | `0x008` |
| 4 | DISR | `0x010` |
| 5 | MISR | `0x014` |
| 6 | PWCR | `0x020` |
| 7 | IMR | `0x030` |
| 8 | ISR | `0x038` |

ISR reads preserve events; it requires a write of one to clear bits.
No PCK, CCU, reset, supply or GPU-aperture write is added. The normal
script's existing board-detection GPIO operations remain unchanged.

All property names begin with `ember,a733-preboot-`:

| Suffix | Value |
| --- | --- |
| `version` | One FDT cell, `1` |
| `status` | String: `started`, `complete`, or `ppu-clock-gated` |
| `gate` | Two cells: RCCU register address, observed value |
| `top` | Nine cells: TOP base, eight register values in the order above |
| `core` | Nine cells: CORE base, eight register values in the order above |

Accept TOP/CORE data only when `status` is `complete` and `version` is `1`.
The completion marker is written after all reads and both data properties.
An incomplete or missing marker means unavailable data; never substitute
zero for missing values. Command failure still reaches the original `booti`.
A CPU exception or bus access that never completes cannot be recovered by
this script. The gate check reduces this risk but is not a bus timeout.
The reads are sequential, not an atomic snapshot of hardware transitions.

## Firmware boundary and interpretation

The pinned vendor [U-Boot boot path](https://github.com/orangepi-xunlong/u-boot-orangepi/blob/b791be842935b27268ae3d00e943a9075495f30a/arch/arm/lib/bootm.c)
runs `ARM_SVC_ARISC_STARTUP` (`0x8000ff10`) inside `booti`, before
`ARM_SVC_RUNNSOS` (`0x8000ff04`) transfers to the 64-bit kernel.
The [A733 configuration](https://github.com/orangepi-xunlong/u-boot-orangepi/blob/b791be842935b27268ae3d00e943a9075495f30a/configs/sun60iw2p1_a733_defconfig)
enables `CONFIG_ARISC_DEASSERT_BEFORE_KERNEL`.
This snapshot is before those operations, not after ARISC startup.
Record the installed firmware identity when interpreting a board result.

- CORE already has PWPR `8`, PWSR `0`: the mismatch exists before the final
  ARISC-startup call. Investigate earlier firmware initialization or reset
  state; the snapshot does not identify the original writer.
- CORE has PWPR `0`, PWSR `0`, but kernel attachment observes `8`/`0`:
  the change lies after this snapshot. Inspect the final firmware handoff
  and early kernel path. This alone does not distinguish either SMC.
- PPU clock is gated: this probe provides no CORE observation. Do not
  interpret the missing cells as OFF or turn on the clock in this script.

PWPR `0` and PWSR `0` alone do not prove that a previously initiated
transition has completed. No result authorizes a PWCR override or a
power-off request to an active GPU.

## Parser provenance and validation limits

The vendor [setexpr implementation](https://github.com/orangepi-xunlong/u-boot-orangepi/blob/b791be842935b27268ae3d00e943a9075495f30a/cmd/setexpr.c)
dereferences `*address` for `.l` and stores the result as hexadecimal text.
This depends on the board's 32-bit ARM U-Boot: its `ulong` is 32 bits.
Do not reuse this script with an unverified 64-bit U-Boot implementation.
The [FDT command parser](https://github.com/orangepi-xunlong/u-boot-orangepi/blob/b791be842935b27268ae3d00e943a9075495f30a/cmd/fdt.c)
parses quoted `<...>` cells with base zero and converts them to big endian.
Each expanded hexadecimal value therefore has an explicit `0x` prefix.
This U-Boot's `setexpr` ignores the return from `env_set_hex`. Every result,
including the gate-bit calculation, is first initialized to `unavailable`
with checked `setenv`, then required to be nonempty and differ from that
sentinel. The underlying hash-table update can discard an old value before
an allocation fails. Both an unchanged sentinel and a missing value fail
the capture. The
[setenv command](https://github.com/orangepi-xunlong/u-boot-orangepi/blob/b791be842935b27268ae3d00e943a9075495f30a/cmd/nvedit.c)
propagates allocation and environment insertion failures. A failed result
store cannot reuse a previous register value or mark the capture complete.
The inserted command lines fit its 1024-byte console limit and its FDT
scratch buffer. The existing 8192-byte FDT expansion provides space.

The host contract executes the generated control flow with audited command
stubs. It checks exact read order, gate-only access, cell strings, every
read/record failure, all 18 silent result-store failures with retained or
missing values, all 18 sentinel-init
failures, unchanged normal boot flow, and builder rejection cases.
It does not execute firmware or validate a physical MMIO transaction.
Unknown `/chosen` properties survive the normal FDT copy into EmberBSD;
the board result must still verify their presence using `ofctl`.

## Physical Zero 3W result


Current hardware evidence and its applicability belong to the [DevBoards profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/a733-zero3w/interfaces.md).
The observed CORE ON-policy/OFF-status mismatch exists before the final ARISC-startup SMC and kernel entry. It does not identify a software writer or a completed OFF transition.
Arm DEN0051E permits a pending reset-to-ON transition; the configured DEF_PWR_POLICY is unknown. PWCR 0x101 is consistent with the one-Q-Channel reset default.
These observations do not identify the PCSM/QREQ phase or justify overriding DEVREQEN. See the [power-domain sources](a733-power-domains.md#provenance).
