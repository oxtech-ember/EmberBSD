# YS-M33 resumable guarded write-stage sessions

[Cable installation](ys-m33-a133-cable-install.md) ·
[Guarded USB transfers](ys-m33-a133-usb-transfer.md)

The [session coordinator](../tools/a133-install-session.rb) combines bundle
integrity, live guarded readback, raw writes and private host progress for an
already prepared factory recovery. It is a write-stage API; the complete
factory-to-accepted-first-boot installer remains separate.

## Private progress and resume

Use one private session directory per tablet and installation context. The
directory must be owned by the current user with mode0700; state/lock are0600,
regular, non-symlink and single-linked. Every invocation holds a nonblocking
exclusive lock for its entire execution and failure recording. All callers for
that tablet must use the same directory: no cross-directory/global lease exists.
Preserve the directory while a run is active.
Held directory/lock descriptors are compared to current inode/owner/mode
before publication. A detached lock cannot publish into a replacement scope.

Context pins serial/CID, source commit, full-backup/recovery/protected-env hashes
and the three image roles/lengths/hashes. A new bundle or context is refused
when reopening an existing session. Do not discard old recovery/backup evidence
to bypass that refusal. Host snapshots are bounded to65536 bytes, checked for
schema/duplicate keys/checksum, written to an exclusive random temporary file,
fsynced, renamed atomically and followed by directory fsync. Unknown crash
leftovers are preserved and never trusted. Checksum is corruption detection,
not authentication against a malicious local owner.
After rename, the in-memory report retains the already visible state even
if directory fsync fails; later failure recording cannot erase that intent.

Rows record pending/checking/writing/verified/failed. `writing` is persisted
before device writes; `verified` follows successful guarded readback. These
rows are progress hints. Every run performs fresh guarded readback for each
root → boot → resources range, including previously verified rows.
Matching bytes are left untouched. Only a complete hash mismatch starts one
checked rewrite in that invocation; short read, identity/recovery mismatch,
mounted storage, failed write or failed journal persistence stops the stage.
There is no retry loop. After a repaired connection or stopped process, a new
explicit invocation checks live state before deciding whether to rewrite.

Failure before the durable writing marker prevents that write. Failure saving
the result after a write prevents success and later roles. A later run rechecks
the actual bytes even when the journal is stale. Callback-owned Store objects
cannot record changes after their lock scope ends or from another process/thread.

## Caller contract

`A133Install.run(directory:, manifest:, adb:, serial:, cid:, backup:,
protected_env:, timeout: 600)` calls the existing complete bundle verifier,
builds the pinned context and coordinates the three guarded ranges.
Success is `write_stage_verified` with `installation_ready=false` and the count
of ranges actually rewritten in that invocation. `A133Install::Invalid` carries
a bounded symbolic reason and a redacted `report`, including possible-write
status. Do not treat all persisted verified rows as a first-boot receipt.
An optional `expected_bundle_sha256` checks the semantic fingerprint of the
freshly verified release before USB admission. `A133Install.bundle_digest`
produces that digest from a verified receipt, independent of artifact order.
The [composed cable stage](ys-m33-a133-cable-stage.md) supplies this pin.
Only `nil` disables expected-release admission. A non-nil pin is validated and
copied before the bundle verifier performs blocking I/O.
Its optional `additional_guard` is passed to every Client core guard, including
before/after readback and writes. It must return true or raise a bounded error;
it adds caller policy and cannot replace the existing mandatory core checks.

The caller must first identify and bind the tablet, freshly verify its full
backup and quiescent mutable-data copies, retain boot0/boot1, accept release
contents/entropy, establish recovery/unlock, and install/read back its protected
env. Backup JSON remains trusted caller input. The coordinator does not prove
its provenance or re-read31GB; CID/hash pinning prevents accidental context
changes. Signing and personalized credential provisioning remain separate.
The working reference must not be cloned as a fleet release.

This stage never writes env/GPT/recovery/early secure areas, releases recovery,
reboots, unlocks or accepts first boot. Killing local ADB cannot guarantee
remote cancellation or rollback after disconnection. Restore identity and
recovery access before repair; retain the host session and original backup.

## Inspection-only command

```sh
ruby ember/tools/a133-install-session.rb /private/tablet-session
```

The command reads an existing locked snapshot and emits redacted progress.
There is no write/init/reboot flag; identifiers, paths and env contents are not
printed. The same directory locking rule applies to inspection.

## Reproducible host checks

```sh
ruby ember/tools/a133-bundle-library-test.rb
ruby ember/tools/a133-install-bundle-test.rb
ruby ember/tools/a133-usb-transfer-test.rb
ruby ember/tools/a133-install-session-test.rb
```

The tests use real private files, restrictive modes, fsync/rename, lock-contender
processes, abrupt child exit and real dd at the strict file-backed ADB boundary.
They inspect actual bytes/tail and persisted failure/verification hints. OS
fsync faults are injected at that boundary while writes remain real.
These checks do not establish physical host power-loss durability, USB cable
acceptance or boot acceptance on a tablet. No physical device is rewritten by
these test commands.

The implementation passed library4, bundle35, transfer43 and session25 on
Ruby4.0.5 and system Ruby2.6.10. Independent Codex GPT-6 Astra review of
`9e75d7a..d5b8b96` found no open findings for this API. It reran all four Ruby4
sets and eight exploratory filesystem edge cases; Ruby2.6 was checked by the
implementer. Hardware acceptance and the caller obligations above remain open.
