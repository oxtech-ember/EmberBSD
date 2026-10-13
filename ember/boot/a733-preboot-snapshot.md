# A733 read-only snapshot before kernel handoff

The [host builder](../tools/a733-preboot-snapshot.sh) produces a separate
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
[normal script](orangepi-zero4-boot.cmd).

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

On 2026-10-09, the builder from `4c8a1222a5e` produced a diagnostic script
for physical Orange Pi Zero 3W, A733, 4 GiB (PCB revision unknown).
The normal `f85ffd420f6` EMBER64 kernel and DTB were retained.
The kernel received `version=1`, `status=complete`, and RCCU gate value `1`.
The eight cells after each base address were:

```text
TOP  07065000: 8 0 8 0 100 101 2 0
CORE 07066000: 8 0 0 1   0 101 2 0
```

Values are hexadecimal, in the register order above. CORE already has
ON policy and OFF status before the final ARISC-startup SMC and kernel.
The same mismatch appears at kernel attachment. This locates the condition
earlier in boot; it does not identify a writer or establish completed OFF.
No experiment clock property or power-policy write was used.

The actual U-Boot and monitor binaries confirm the two-call handoff order.
Their SHA256 values are respectively
`2352631ca14bbd3de7d229cc750435776d0d554290429dfa2000db90efc50145`
and `8af16b86ae8e2eff634a3ebca76a8cdf3c10b3540387573d46c3d785545b2632`.
This is binary evidence for that boundary, not whole-source equivalence.
The source observation log SHA256 is
`624794c2fb50c786ab2c7a245372888303114e31e5ce0e20f737cf9ecb18cd0d`.
Normal boot files were restored with matching hashes before shutdown;
subsequent normal boot and SSH access passed. GPU identification and
acceleration remain unverified.

A second run used the same kernel and diagnostic script after the operator
removed power for ten seconds. The existing clock-only experimental DTB
(SHA256 `7a840baec0c4c76455b1ce8b2252e63bfea7a121e88c38c43c413ab1575dab7c`)
was used. Preboot cells were identical. Kernel preparation completed UPDATE
with GPU_CLK `0x83000000`, GPU_BGR `0x10001`, and open post-PLL gates, but
CORE again timed out with PWPR/PWSR/MISR `8`/`0`/`0`. GPU MMIO was not read.
The cold-start log SHA256 is
`12a29f72cf46776df34b171fdc106c68e8a9f86e168e02b6f5ecfdb1ee244151`.
This excludes a failure confined to warm reboot for this setup.

Arm DEN0051E section 5.2.3 explains that PWSR's reset state is independent
of the default policy. With default ON it updates only after the automatic
transition completes. Therefore `8`/`0` need not imply a software writer;
a pending reset-to-ON transition is another hypothesis. A733's configured
`DEF_PWR_POLICY` is unknown and is not exposed by the PPU ID registers.
PWCR `0x101` is also the one-Q-Channel reset default. These observations do
not identify the internal PCSM/QREQ phase or justify overriding DEVREQEN.
See the [power-domain sources](a733-power-domains.md#provenance).
