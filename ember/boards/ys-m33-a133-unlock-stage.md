# YS-M33 journaled vendor unlock stage

[Cable installation](ys-m33-a133-cable-install.md) ·
[Offline unlock prefix](ys-m33-a133-unlock-env.md) ·
[Prepared image writer](ys-m33-a133-install-session.md)

The [stage API](../tools/a133-unlock-stage.rb) arms the fixed vendor unlock
hook, submits a normal reboot and verifies unlocked signed USB recovery.
It restores the original env prefix there. A private journal records possible
effects before their submission; an interrupted recorded reboot is never
automatically replayed. This host contract is not a released fleet installer.

## Caller evidence and scope

Use the fixed 31,037,849,600-byte/17-partition factory profile. Freshly verify
and retain the selected serial/CID's complete Capture evidence, critical and
boot0/boot1 copies and recovery UDISK/metadata receipts. Supply the full original
16MiB env, not a prefix. Keep evidence protected throughout the invocation and
exclusively own all operations on the tablet. Saved JSON alone does not satisfy
freshness. Stage checks receipt shape/identity but does not reread the31GB archive.

`locked_round_trip_verified:true` is the caller's explicit attestation that a
physical locked recovery/Android round trip and recoverable protected state
were accepted for this factory image. The API does not perform that experiment
or authenticate the attestation. Do not infer it from copied-file tests.

```ruby
require_relative '../tools/a133-unlock-stage'
receipt = A133Unlock::Stage.run(
  directory: private_session_directory, adb: adb_executable,
  serial: selected_serial, cid: selected_cid, original_env: original_env_bytes,
  backup: fresh_capture_receipt, mutable: fresh_mutable_receipt,
  locked_round_trip_verified: physical_round_trip_accepted
)
```

A fresh journal requires locked1/green Android and the exact original env.
The stage changes only131072 prefix bytes with sync, verifies all16MiB, and
then submits one normal ADB reboot. Returned recovery must be unlocked0/orange,
root-readable, unmounted and identified through the same selected USB serial,
CID and GPT. Bootloader/boot/recovery hashes and env tail remain bound to the
capture; hardware boot-area sizes remain bound across epochs, not their contents.
Unknown values, unavailable usage inventories, any swap/holders or image drift
stop the stage. Per-command and inventory limits are1..7200 seconds; complete
critical reads are outside the inventory wait and no overall deadline is supplied.

## Durable intent and resume

Use one operator-owned0700 directory per tablet/context. State and lock are
0600, regular, non-symlink and single-linked. A nonblocking exclusive lock
covers the invocation. Context pins identity and backup/GPT/critical/env/mutable
hashes; another context cannot reuse the journal. This is directory exclusion,
not a global tablet lease. Another host/directory must not operate concurrently.
Open directory/lock descriptors bind the scope to their inodes. Operations
recheck current path identity, owner and private modes; moving/replacing the
directory or lock stops the old Store before further device submission.

Snapshots have exact schema, duplicate-key checks and canonical checksum,
bounded to65536 bytes. Exclusive random temporary files are fsynced, renamed
atomically and followed by directory fsync. A checksum detects accidental
corruption, not a malicious local owner. Unknown crash leftovers are retained.
Host power-loss durability and physical vendor effects require separate tests.
After successful rename the visible snapshot becomes the Store's current
intent even if directory fsync fails. Failure reporting must not overwrite
that intent with earlier false flags; the operation still stops on the failure.

Possible-write intent precedes each prefix submission. Possible-reboot intent
precedes native reboot and remains true even when the command might not have
run. Resume then observes recovery without submitting another reboot. If the
tablet is still in Android, timeout stops for controlled operator recovery.
Interrupted prefix writes resume only from exact original/armed bytes; unknown
partial env is never repaired blindly. Known unlocked recovery after a previous
armed session can be finished without another native reboot. Repeating a
verified session rechecks live recovery/original bytes rather than trusting its
stored phase. Persistence failure stops later device operations.

Success returns `unlocked_recovery_verified`, the original env hash and
`installation_ready:false`. A `Stage::Invalid` report includes persistent
possible-write/reboot flags and redacted symbolic reasons. An uninspectable
journal may conservatively report unknown/possible prior effects.
Admission failures before loading it also report unknown/possible prior effects;
they do not mean this invocation submitted a command. Preserve
the journal and backups; local ADB failure does not prove remote cancellation.

Restoring env does not relock secure storage. This stage does not directly
write data partitions, images/early secure stages, provision a release or accept first boot.
Before image writes, install the separate persistent recovery protection.
Data preservation, full Android restore and permanent own loader remain open.

## Inspection-only CLI and host proof

```sh
ruby ember/tools/a133-unlock-stage.rb /private/existing-unlock-session
ruby ember/tools/a133-unlock-journal-test.rb
ruby ember/tools/a133-unlock-journal-regression-test.rb
ruby ember/tools/a133-unlock-stage-test.rb
ruby ember/tools/a133-unlock-stage-persistence-test.rb
ruby ember/tools/a133-unlock-stage-cli-test.rb
ruby ember/tools/a133-unlock-stage-admission-test.rb
```

The command only inspects an existing journal. It has no execution, initialization
or reboot flag. JSON is emitted after releasing the journal and hides identifiers
and host paths. Tests use native ADB-boundary processes, real dd on sparse fixed
geometry, independently encoded vendor-consumed bytes and real host files.
They simulate vendor flag/reset effects; they do not execute U-Boot or accept
physical USB return. The next gates are integration with the full installer,
physical locked/unlocked round trips, trial acceptance and Android restoration.
