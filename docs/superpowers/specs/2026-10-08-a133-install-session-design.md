# A133 resumable write-stage sessions

The user requests autonomous progress towards cable installation on unopened
ROOMY tablets. This scoped addition coordinates the existing guarded write API
with private, crash-aware host state. Preserve the working reference; no audio,
camera capture or physical reflash. Execute inline under that authorization.

## Boundary and selected approach

Use a single locked, atomically replaced JSON snapshot per installation session.
An append-only event log would need torn-record recovery and history compaction;
a database adds a dependency for three roles. The snapshot records progress hints,
never proof that device bytes remain correct. Recheck each range on every run.

Export the existing bundle verifier as A133Bundle.verify(path), returning its
unchanged public receipt plus internal role-to-file paths. CLI output remains
unchanged. Add Client.verify_installed with the same identity/backup/protected
recovery gates as write_verified and complete readback, but no write.

Session state is private: directory 0700, files 0600, owned by the current user.
Reject leaf symlinks, non-regular/hard-linked state/lock, unsafe permissions,
oversized/malformed/duplicate-key JSON and invalid schemas/checksums. Use a
nonblocking exclusive flock for the whole run; no ADB work on a busy session.
Temporary snapshots are exclusive, fsynced and renamed atomically, followed by
directory fsync. Fresh random temporary names avoid reusing crash leftovers;
never load or delete unknown leftovers. Cleanup removes only this call's inode.
These are filesystem/crash checks, not physical host power-loss acceptance.

## Immutable context and state

Context binds schema1, board ys-m33-a133, explicit serial/CID, source_commit,
backup raw SHA256, retained recovery SHA256, protected-env SHA256 and three
artifact roles/lengths/SHA256. Hash canonical JSON for accidental-corruption
detection. Context cannot change when reopening a session. There are no keys,
network credentials, recorded speech or env contents in state; identifiers stay
private. SHA/checksum and a CID label do not authenticate backup provenance.
The trusted caller must freshly verify/bind the actual backup and release before
invocation. This stage cannot create factory recovery, unlock, alter env or
accept first boot. No physical-write CLI is added.

State includes a random session_id, monotonic revision and root/boot/resources
rows with pending/checking/writing/verified/failed, symbolic reason or null.
Bound snapshots to 65536 bytes. Never treat persisted verified as sufficient.
Expose redacted status with no serial, CID, host paths or file/env contents.
The lock is scoped to that directory; all callers for one tablet must use the
same private session directory. Cross-directory coordination is not supplied.

## Coordinated run

A133Install::run(directory:, manifest:, adb:, serial:, cid:, backup:,
protected_env:, timeout:) hashes all bundle files first and locks private state.
Root, boot and resources run in that order. Persist checking before live readback.
If complete live readback matches, persist verified with zero writes. Only the
specific full-hash mismatch permits a write in this invocation: persist writing
and fsync it before calling Client.write_verified. Then persist verified after
the complete guarded write/readback succeeds.

Any other inspection/read failure or write failure stops the invocation. Preserve
a bounded symbolic failure and whether a write was attempted; never retry within
the run. A process crash leaves checking/writing and must trigger fresh live
validation on the next explicitly requested invocation. A persistence failure
before writing prevents the write; after writing it prevents success and later
roles. The next run uses live bytes, even if the saved snapshot is stale.
Do not promise remote cancellation/rollback after cable loss. Never clear recovery
or reboot. Success is write_stage_verified with installation_ready=false.

## Verification

Use actual files, restrictive modes, fsync/rename, forked lock contenders and
abrupt child exit. Test importable bundle verification through real files and
retain all CLI contracts. Extend the strict file-backed ADB boundary for an
already-written readback check, successful multi-role run, repeated run, failed
write/resume, mutated device bytes and changed context/identity/inputs. Assert
real bytes and persisted states, not mock call counts. A crash marker is not
hardware power-loss acceptance. Existing hardware is unchanged.
