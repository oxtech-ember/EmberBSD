# A133 backup integrity and recovery candidate preparation

Origin: EmberBSD - design by Codex (GPT-6).

## Intended result

Move two already used private installation steps into reproducible OS tools:
verify a complete saved eMMC image, then prepare a one-shot recovery environment
copy without touching the tablet. The user requested autonomous continuation
of cable installation; no audio acceptance or operator questions are required.
Neither result permits flashing. Preserve the running reference tablet.

## Backup checker

`a133-backup-check.rb --sha256 RAW_SHA256 [--format zstd|raw] [--zstd PATH]
[--timeout SECONDS] IMAGE` reads a regular non-symlink file. Default format is
zstd and the default deadline is 3600 seconds; accept deadlines from 1 to 7200.
Use Ruby standard-library SHA256/CRC32 and a trusted host zstd executable.
Never decompress into a new raw image. Read bounded chunks and retain only
the first/last 34 sectors plus digest state.

The CLI requires exactly 31037849600 uncompressed bytes and the supplied
64-character lowercase SHA256. Check both GPT header and entry-array CRCs,
reciprocal header locations, matching disk GUID/geometry/arrays, valid usable
bounds and the exact inspected 17-partition YS-M33 inventory. Check the
protective MBR signature/type/start and reject a hybrid layout. Vendor PMBR
length 0xffffffff is accepted alongside the exact protective disk length.
GPT arrays contain 128-byte entries; unique/type GUIDs must be nonzero, unique
partition GUIDs must differ, and names/bounds must match the reference.
Return hashes of bootloader, env, boot and recovery ranges during the same pass.

For zstd, read the opened source descriptor through the child's stdin, drain
stderr concurrently with bounded memory, check decoder exit/diagnostics, and
kill/reap its process group on error or deadline. Reject truncated/excess
output, changed source metadata, non-regular input, decoder failure and timeout.
Emit one bounded JSON result without paths, GUIDs, archive contents or stderr.
Success is `backup_integrity_verified`; every result has writes_performed=0
and installation_ready=false. Integrity does not establish a consistent live
Android filesystem, device binding or successful physical restoration.

The module's stream/GPT verifier accepts an explicit inventory and disk size,
so small independently built disk fixtures can exercise the same implementation.
Only the CLI fixes the physical YS-M33 size/inventory. Test fixtures must not
require 31 GB host allocations. Run the CLI against the private real backup too.

## Recovery candidate

`a133-recovery-env.rb ORIGINAL_PREFIX NEW_OUTPUT` uses the existing A133Env
decoder/editor. Read exactly 128 KiB through a non-following descriptor.
Require the tested factory scripts:

- bootcmd: `run setargs_mmc boot_normal`
- boot_normal: `run ${hook};run boot_android`
- boot_android: `sunxi_flash read 45000000 boot;bootm 45000000`
- boot_recovery: `sunxi_flash read 45000000 recovery;bootm 45000000`
- hook, ember_restore_normal and ember_recovery_once must all be absent.

Create exactly three variables:

- hook: `ember_restore_normal ember_recovery_once`
- ember_restore_normal: `setenv hook; saveenv`
- ember_recovery_once: `fdt addr ${fdtcontroladdr}; fdt set /soc@03000000/usbc0@0 usb_port_type <0>; run boot_recovery`

Preserve all other entries and CRC/padding. Removing those three variables
must reconstruct the original bytes exactly. Publish a new 0600 regular file
without replacing an existing path, including a dangling symlink. Use an
exclusive private partial file, fsync, link without replacement, then remove
the partial; cleanup only the partial inode created by this operation.
Do not modify bootcmd, boot_normal, recovery, secure state, partitions or eFuse.
No ADB/UART, reboot, unlock or device write is performed. Output JSON includes
input/output SHA256, changed variable names and installation_ready=false,
not the environment contents. The candidate requires a separate verified
per-device backup/recovery/write-and-readback procedure before deployment.

## Tests and release boundaries

Use real small disk images, subprocess decoder fixtures and copied env files.
Corrupt CRCs, geometry, arrays, partition names/bounds, hashes and stream length;
exercise stalled/failed/noisy decoders, symlinks, invalid args and output collisions.
Run existing env, bundle, preflight and first-boot contracts. Physical recovery
acceptance remains separate. Record successful real-backup/candidate checks
without putting images, identities, credentials or host paths into Git.
