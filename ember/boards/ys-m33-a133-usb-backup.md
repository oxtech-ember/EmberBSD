# YS-M33 read-only USB backup acquisition

[Cable installation](ys-m33-a133-cable-install.md) ·
[Capture checks](ys-m33-a133-backup-recovery.md)

The [backup collector](../tools/a133-usb-backup.rb) reads a root-accessible
Android/recovery source into a new private host directory. It saves the full
eMMC, four independent bootloader/env/boot/recovery copies and hardware
boot0/boot1. No device storage write, root restart, unlock or reboot command is
available. The complete cable installer and physical restoration remain open.

## Select the source and private destination

Choose an existing parent directory owned by your UID with mode0700. Each run
requires a **new** destination; existing files/directories/links are refused.
Outputs contain device identifiers, secure boot state and personal data. Keep
them out of Git and ordinary shared storage. All files are0600, directory0700.
Raw acquisition needs about31GB plus whole-partition/hardware copies; ensure
adequate host space. No compressed copy or automatic removal of backups occurs.

With exactly one authorized USB Android source, vendor su is the default:

```sh
ruby ember/tools/a133-usb-backup.rb "$a133_backup_parent/tablet-new"
```

Use `--serial "$a133_serial"` to select one inventory row explicitly.
The source must expose shell_v2 and root read access. `--root-method adbd`
uses an already root-readable daemon; it does not run adb root. Recovery
acquisition uses `--state recovery --root-method adbd` and requires no mounted
block device. The vendor_su method invokes the existing static
`/system/xbin/su 0 /system/bin/sh` read-command wrapper. If that method is not
available, acquisition stops; it does not install or enable root access.

`--adb` selects a trusted host executable. `--timeout` accepts1..7200 seconds,
default3600, bounding both the complete invocation and channel subprocesses.
There are no write/restore/unlock/reboot flags.

## What a successful result means

The source inspector selects a USB row in the requested state and identifies
model a133/compatible allwinner,a133. It checks the fixed31037849600-byte eMMC,
all17 GPT names/bounds/by-name targets and both GPT CRCs/layouts. It discovers
and pins CID, serial/state, GPT fingerprint, lock properties and equal nonzero
hardware boot sizes (at most32MiB each). Every payload is preceded and followed
by source inspection. This is the documented YS-M33 profile, not generic A133
board discovery or a proof of the enclosure's revision.

Binary reads have exact length limits, remote exit/diagnostic checks and bounded
process cleanup. Seven exclusive `.partial` host files are counted/hashed,
flushed/fsynced and published without replacement. A private schema2 candidate
records identity, raw hash and hardware evidence. The capture checker then
freshly rereads the full image and all copies, using retained distinct input
inodes and full hashes. Only after that and another source check is
`capture.json` published without replacement, followed by directory fsync.
Parent/destination directory descriptors remain pinned through the invocation.
All eight output descriptors stay open across verification, last source
inspection and publication. Paths/private metadata are rechecked before and
after publication; the manifest's bytes also match the original generated
record, accounting for the ctime change caused by link/unlink.

The redacted result is usb_backup_captured, host_files_created8,
saved_bytes, hardware_boot_copies_verified=true, writes_performed0 and
installation_ready=false. API and CLI errors discard private exception causes;
neither prints identities, hashes, paths, file data or decoder diagnostics.
Private identity is in the manifest, not the public receipt.

Errors stop acquisition and preserve completed files/partial evidence. They do
not report success, retry, overwrite or adopt that directory. Choose a fresh
directory after repairing the cause. A filename left around an interrupted
publication is not a readiness receipt: revalidate actual files and source,
never infer acceptance from its presence. Host/device power-loss durability
is not established by process/fsync fault tests.

## Consistency and the next installation stage

Device-mode reads can observe a running Android filesystem. The result always
says filesystem_consistency=not_established_by_integrity_check, even for
recovery mode. Matching hashes, no mounted block devices and a CID label do not
prove a coherent/pristine factory snapshot or full restoration.

Acquire the original baseline before modifying boot/unlock state. Independently
retain verified quiescent UDISK/metadata copies at the appropriate recovery
stage. The collector does not create that stage or replace those copies.
Release provenance, credential/entropy provisioning, protected recovery,
unlock, writing, restored Android and accepted first boot are separate gates.
Root/unlock/state are left as found. The first working sample remains a reference.

The library entry point is `A133UsbBackup.collect(directory:, adb: 'adb',
serial: nil, state: 'device', root_method: 'vendor_su', timeout: 3600)`.
Before a later guarded write, freshly verify the private capture with
`A133Capture.verify` and use live identity/recovery checks. The saved manifest
is trusted evidence, not a signature.

## Reproducible host checks

```sh
ruby ember/tools/a133-usb-backup-test.rb
ruby ember/tools/a133-usb-backup-lifetime-test.rb
ruby ember/tools/a133-capture-hardware-test.rb
ruby ember/tools/a133-capture-check-test.rb
ruby ember/tools/a133-capture-regression-test.rb
ruby ember/tools/a133-backup-library-test.rb
ruby ember/tools/a133-backup-check-test.rb
```

The independent small GPT/source fixtures execute actual dd against real source
files and compare captured bytes and unchanged inputs. They exercise identity,
root/state/layout/mount failures, exact stream bounds/exit/deadline, private
publication, fsync failure, output collision and redaction. Small geometry exists
only in the test process; production has no fixture mode. No physical tablet is
read or rewritten by these test commands.

On Ruby4.0.5 and system2.6.10, acquisition26, late-lifetime5, hardware18,
capture37, capture-review-regression6, library14 and backup29 passed.
Channel13/transfer43/session25/environment contracts passed on Ruby4.
Independent GPT-6 Astra review found one Important publication/lifetime gap;
five late boot/manifest/directory and postpublication mutations failed against
the pre-fix revision on both Rubies, then passed after the implementer's fix.
The reviewer examined the pre-fix range; no new physical acquisition was run.

## Separate mutable-data evidence

The [recovery mutable collector](ys-m33-a133-mutable-backup.md) supplements
this full capture with complete UDISK/metadata copies and two matching reads.
It requires an already-root-readable unmounted recovery and explicit trusted
serial/CID. It does not alter this manifest or establish filesystem snapshot
consistency; physical restoration and all-stages installation remain open.
