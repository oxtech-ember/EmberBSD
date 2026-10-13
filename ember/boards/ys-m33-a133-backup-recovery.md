# YS-M33 backup integrity and recovery preparation

[Cable installation](ys-m33-a133-cable-install.md) · [Board](ys-m33-a133.md)

These host tools implement two preparation stages of cable installation.
They do not discover a connected device, write storage, unlock or reboot.
Both always report `installation_ready=false`. A full fleet installer and
physical Android restoration remain separate acceptance work.

## Verify a saved full eMMC snapshot

Keep the original image and its separately recorded uncompressed SHA256 in
private storage. Use the recorded hash, not a fresh hash of a possibly damaged
file. The checker streams through the image without creating a raw copy:

```sh
ruby ember/tools/a133-backup-check.rb --sha256 "$a133_backup_sha256" /private/backup/emmc.img.zst
# A raw snapshot uses the same physical size and partition guards:
ruby ember/tools/a133-backup-check.rb --format raw --sha256 "$a133_backup_sha256" /private/backup/emmc.img
```

The default decoder is `zstd` from PATH; `--zstd /path/to/zstd` selects a trusted
host executable. The deadline defaults to 3600 seconds; `--timeout` accepts
1–7200. No decoder diagnostics, GUIDs, file contents or host paths enter JSON.
Zstd must finish without diagnostics or errors. Its process group is killed
and reaped on a read failure/deadline; stderr is drained with bounded memory.

Exit zero means all 31037849600 uncompressed bytes matched the expected hash,
both GPT header and table CRCs passed, reciprocal headers/geometry/GUIDs and
table copies agree, and all 17 partition names/bounds match this YS-M33 profile.
The protective MBR is checked; the vendor's 0xffffffff protective length is
accepted. It does not establish compatibility of another A133 board.
The same pass reports bootloader, env, boot and recovery range hashes so the
operator can compare independently saved per-device partition files.

`backup_integrity_verified` is an integrity receipt, not an authenticity or
restore receipt. A live Android snapshot can contain inconsistent mutable
filesystems. Before overwriting UDISK/metadata, retain their separately verified
quiescent recovery copies. eMMC hardware boot0/boot1 are outside the main
snapshot and must be saved separately. Device binding, retained secure state,
backup revalidation at use and physical restoration remain installer stages.

The same checker is callable as `A133Backup.verify_file(path, sha256:,
format: 'zstd', zstd: 'zstd', timeout: 3600)`. It returns the same integrity
receipt and normalizes read/decoder errors to `A133Backup::Invalid` symbolic
reasons. Importing it performs no CLI work. Physical geometry is fixed.

## Bind a trusted capture record and critical copies

The [capture checker](../tools/a133-capture-check.rb) freshly reads the full
snapshot and separate bootloader/env/boot/recovery copies. It compares recorded
serial/CID with explicit caller inputs and each full critical-copy hash with
its range in the freshly verified snapshot. Repeating it rereads the files;
a cached success receipt cannot replace those reads.

Keep a capture manifest in the same private directory as its five files.
Directory mode must be0700, every input file0600, owned by the current UID,
regular and single-linked. Leaf symlinks, hardlinks and unsafe modes are
refused. All input descriptors remain open across the checks, with distinct
device/inode identities. Names differing only by case cannot masquerade as
separate copies on a case-insensitive filesystem. Changed stat/inode/path or
a deadline prevents success; an early copy is checked again after later reads.

The JSON manifest has these exact fields, with no additional receipt object:

| Field | Value |
| --- | --- |
| schema / board | Integer1 / `ys-m33-a133` |
| serial / cid | Trusted capture serial and lowercase32hex eMMC CID |
| uncompressed_sha256 | Separately recorded lowercase64hex raw-image SHA256 |
| backup | Object with `file` (one filename) and `format` (`raw` or `zstd`) |
| partitions | Exactly four objects with `role` and `file`, for bootloader/env/boot/recovery |

Names are distinct single ASCII filenames, at most128 bytes; no directory
components or alias of the manifest. JSON is bounded to65536 bytes, with
duplicate keys refused. Copies cover the **whole** original GPT partition:
bootloader/boot/recovery32MiB each, env16MiB. A128KiB environment prefix used
by the preparer is not a substitute for the whole env backup.

```sh
ruby ember/tools/a133-capture-check.rb --serial "$a133_serial" --cid "$a133_cid" /private/backup/capture.json
```

This read-only command emits a redacted `capture_integrity_verified` result,
without identifiers, filenames or image hashes. The library
`A133Capture.verify(path, serial:, cid:, zstd: 'zstd', timeout: 3600)` returns
a **private** string-keyed `backup_integrity_verified` receipt with recorded
serial/CID and `critical_copies_verified=true`, for the guarded USB consumer.
Both report writes_performed0/installation_ready=false. No write/reboot flag.
Normalized library exceptions discard original and inherited causes so ordinary
full exception logging does not reveal input paths or malformed JSON contents.

The capture record is trusted input, not a signature. Offline matching labels
does not discover hardware: the caller must obtain live identity and use the
USB guards at write time. A backup without a recorded CID is refused; never
infer that value from a different tablet. Capture-time consistency, quiescent
mutable copies, hardware boot0/1, release authenticity and first boot remain
separate prerequisites. Do not persist this receipt as proof for a later write;
freshly verify the capture again in the consuming invocation.

Schema2 keeps those fields and adds `capture_state` (device/recovery),
`root_method` (vendor_su/adbd), and exactly two `hardware_boot` objects with
role/file/bytes/sha256 for boot0/boot1. Area sizes are equal, positive512-byte
multiples, at most32MiB each. Their files use the same private/inode/lifetime
guards and fresh hashes. Schema2 returns hardware_boot_copies_verified=true;
schema1 explicitly returns false. Recorded geometry and hashes remain trusted
input, not hardware discovery or authenticity. Acquisition from a root-readable
USB source is provided by the [backup collector](ys-m33-a133-usb-backup.md).

## Retain mutable recovery copies

The [mutable collector and fresh checker](ys-m33-a133-mutable-backup.md) retain
complete UDISK/metadata copies separately from the older full capture. They
require root-readable unmounted recovery, observe all readable mount inventories,
swaps/holders and two matching full reads. These are sampled observations, not
a filesystem snapshot or restore acceptance. Every receipt remains
installation_ready=false; fresh verification is required at use.

## Prepare a one-shot recovery environment copy

Read the exact original 128 KiB environment prefix from this particular tablet
into a protected host file. The preparer creates a new file; it cannot write
the tablet:

```sh
ruby ember/tools/a133-recovery-env.rb /private/backup/env-prefix.bin /private/staging/recovery-env.bin
```

Only the inspected factory profile is accepted: bootcmd runs setargs_mmc and
boot_normal, boot_normal runs the hook followed by boot_android, and the boot
and recovery scripts read their respective partitions at 0x45000000.
Existing hook or either helper variable is refused, including empty values.
Customized/reference-tablet scripts must not be coerced into this profile.

The candidate adds exactly hook, ember_restore_normal and ember_recovery_once.
The first helper clears the hook and saves normal boot; the second selects
USB device role in the vendor FDT and enters the retained recovery script.
Bootcmd, boot_normal, all other entries, padding and the original input remain
unchanged. Removing the three additions must reproduce every original byte.
The new 0600 file is published without replacing an existing output or symlink.
No environment contents are printed.

This is a prepared file, not an installed recovery path. Deployment must verify
the selected tablet, original env readback, full backup, locked recovery round
trip, complete write readback, rollback on failure and USB access. It must not
mistake a successful file-generation command for hardware acceptance.

## Install persistent recovery protection

The separate [persistent protection primitive](ys-m33-a133-recovery-protection.md)
consumes freshly verified capture/mutable receipts and the full original env.
It changes only128KiB, verifies the complete16MiB env including its retained
tail, rechecks the signed recovery and refuses unknown states. Its library
contract is tested with actual dd; locked cable-return/cold-boot/restoration
acceptance and the complete installer remain open.

## Evidence and reproducible checks

The host contracts use small independently constructed GPT images and real
decoder subprocesses, not a 31 GB allocation:

```sh
ruby ember/tools/a133-backup-check-test.rb
ruby ember/tools/a133-backup-library-test.rb
ruby ember/tools/a133-capture-check-test.rb
ruby ember/tools/a133-capture-regression-test.rb
ruby ember/tools/a133-capture-hardware-test.rb
ruby ember/tools/a133-recovery-env-test.rb
ruby ember/tools/a133-env-edit-test.rb
ruby ember/tools/a133-install-bundle-test.rb
ruby ember/tools/a133-install-preflight-test.rb
sh ember/tools/a133-firstboot-guard-test.sh
```

On 2026-10-08 the new backup checker read the complete previously saved factory
image. Its raw SHA256, both GPT copies and four independently saved partition
hashes matched. That image was captured from live Android: filesystem
consistency and a physical full restore were not claimed by this check.
The generated recovery candidate was byte-identical to the prior candidate
used in the closed-enclosure installation trial. That file comparison does
not constitute another device write or recovery boot.
Images, per-device environments, keys and private configuration are not in Git.

Both Ruby 4.0.5 and the host's compatibility Ruby 2.6.10 passed 29 backup cases
and 19 recovery-copy cases. The existing environment, bundle, ADB preflight
and first-boot guard contracts also passed. Independent Codex GPT-6 Astra
review found no blocking defect. Its regression-coverage finding was addressed
with successful-exit/noisy-stderr and exited-parent/stalled-descendant cases;
the latter checks that the descendant is actually terminated.
The reviewer did not independently inspect private images or accept hardware
restoration. Later library and capture-binding additions retain the streaming
GPT algorithms; that private31GB archive was not reread for these additions.

On 2026-10-09, library14, capture37, review-regression6 and backup29 passed on
Ruby4.0.5 and system2.6.10. USB43/session25/environment contracts also passed
after the fixes. Independent GPT-6 Astra review found three Important defects:
late mutation of an early critical copy, case-insensitive inode aliases and
private exception causes. Six regressions failed on both Ruby versions before
the fixes, then passed along with the relevant suites. The alias case ran on
this host's case-insensitive filesystem; it reports inapplicable elsewhere.
Fixes were verified by the implementer; the reviewer examined the pre-fix range.
No new full private-image, physical USB or restoration acceptance was performed.
