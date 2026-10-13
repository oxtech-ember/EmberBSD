# A133 fresh backup and recorded capture binding

Continue autonomous cable-installer work for unopened ROOMY tablets. The next
missing boundary is freshly checked backup evidence, instead of a cached JSON
success receipt. Preserve the working reference and all private images. No
device write, reboot, microphone test or camera capture belongs to this stage.

## Selected approach

Export the existing full-image checker as `A133Backup.verify_file(path,
sha256:, format: 'zstd', zstd: 'zstd', timeout: 3600)`. Preserve the production
31037849600-byte/17-partition profile, decoder cleanup and CLI receipt. A
callable verifier avoids spawning/parsing another Ruby CLI or duplicating GPT
logic. A signed archive would add key management without establishing that a
live mutable filesystem was consistent when copied; that remains separate.

`A133Capture.verify(path, serial:, cid:, zstd: 'zstd', timeout: 3600)` reads a
private capture manifest and freshly verifies the whole image and four separate
critical partition files. It returns a private string-keyed backup receipt
usable by the existing guarded USB API. It never accepts a prior receipt in
place of reading the image. All input paths are single filenames in the same
private directory. No production small-image or test-mode option is exposed.

## Trusted record and private files

Exact manifest fields: schema1, board `ys-m33-a133`, serial, cid,
uncompressed_sha256, backup `{file, format}`, partitions containing exactly
bootloader/env/boot/recovery `{role, file}`. Identifiers must match caller-supplied
serial/CID; CID is lowercase32hex, serial1..128 ASCII alphanumeric/dot/hyphen/
underscore. Missing CID is refused, never inferred from another image or device.
SHA256 is a separately recorded lowercase64hex raw-image hash. Format raw/zstd.

Directory0700 and files0600 must belong to the current UID. Reject leaf
symlinks, non-regular files, hardlinks, unsafe modes, duplicate JSON keys and
fields, oversized (>65536) manifests, path traversal and repeated filenames.
Hold each source descriptor while checking it; compare dev/ino/size/mtime/ctime
before/after reading. Keep all six input descriptors open across verification,
refuse duplicate dev/inode identities, then recheck every inode and current path.
This detects accidental mutation,
not hostile changes by the local owner or parent-directory replacement.

Each critical copy must have its fixed partition size and its full SHA256 must
equal the corresponding range hash from the just-verified main snapshot.
The returned receipt retains `backup_integrity_verified`, adds the recorded
serial/CID and always has writes_performed0/installation_ready=false.
Errors use bounded symbolic reasons, with no contents, identifiers or paths;
discard original/inherited exception causes at each public library boundary.
An inspection-only CLI emits a redacted receipt, never the private bound one.

## Limits and next consumer

The record's provenance is trusted caller input: matching labels and SHA256 are
not signatures. The caller must obtain live identity and use guarded USB
inspection at write time; this offline tool does not discover hardware. It
does not prove capture-time filesystem consistency, retain hardware boot0/1
or quiescent mutable-data copies, prepare/unlock recovery, accept release
contents or accept first boot. Those prerequisites still gate the installer.
The saved sample without a recorded CID cannot be silently upgraded to a bound
capture. Do not rehash that31GB archive merely to claim another hardware result.

## Verification

Real private files and decoder subprocesses; successful raw/decoded library
reads, import silence, wrong hash/size, file mutation, decoder error/deadline,
trusted-record identity mismatch, critical-copy mismatch, permissions/links,
JSON/path/schema errors, fresh re-entry after changing an already-checked file,
and redacted CLI. Small independently built GPT geometry is installed only in
the test process, never selected through a production parameter or environment.
Retain backup29 and relevant USB/session/environment contracts on both Rubies.
