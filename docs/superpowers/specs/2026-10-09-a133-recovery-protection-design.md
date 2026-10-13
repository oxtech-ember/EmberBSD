# A133 recovery protection design

The guarded image writer requires a persistent factory recovery environment,
but the existing preparer only enters recovery once. Add the missing env
transition while preserving the signed recovery and all other storage.

## Scope and interface

`A133Recovery.protect(data)` takes the original128KiB factory env. It changes
only `boot_normal` to `run ember_recovery_once` and adds the existing exact
`ember_recovery_once` helper. Require the known closed-tablet PROFILE and reject
all reserved hook keys, including empty values. Restoring boot_normal and
removing the helper must reproduce the original bytes, including padding.

`A133Recovery::Protection.new(adb:, serial:, cid:, root_method:'adbd',
timeout:600).install(original_env:, backup:, mutable:)` takes the full original
16MiB env partition as bytes and trusted in-process receipts produced by fresh
`A133Capture.verify` (schema2, critical and hardware copies verified) and
`A133Mutable.verify` for the same explicit serial/CID. Receipts are caller
capabilities, not authenticated saved JSON: the future coordinator must run
those expensive checks freshly and retain their evidence. This primitive does
not accept receipt files, implement a CLI device writer, or prove retained
host evidence after the checks return. Capture policy bytes before USB I/O.

The caller owns exclusive device operation throughout the invocation. No
global device lease or protection against a second host/vendor process is
claimed. Require root-readable USB recovery via adbd or the existing vendor
su wrapper, locked1/green or unlocked0/orange, the fixed31,037,849,600-byte
17-partition geometry, valid paired GPT, and the mutable receipt's exact GPT
digest. Reuse Source's read-only namespace/swap/holder checks before/after reads
and writes. Do not add mutation to the Source API.

## Mutation and verification

Verify original_env's full size and hash against the capture env hash before
contacting USB. Derive the full expected partition from protected prefix plus
unchanged tail. Before any write, check device identity/state/GPT, absence of
block usage in process/thread mount inventories, and the whole original
recovery partition against its capture hash. Read the whole live env: accept
only exact original or exact expected protected partition hashes. Unknown,
partial and foreign states stop without guessing or rollback. If already
protected, freshly read and guard it and return zero writes.

For original state, repeat the source/recovery guard, send exactly131072 bytes
to mmcblk0p2 with binary non-PTY dd and sync, and check transferred digest,
whole16MiB env readback, recovery hash, identity/state/GPT and usage again.
Never write another partition, boot area or secure state; no unlock/reboot/root
restart. Partial transport, sync, readback and late drift failures propagate
redacted `A133Usb::Invalid` with `write_attempted=true` once submission began.
No automatic retry. Success returns protected prefix privately for the existing
image writer, a whole-env SHA and `installation_ready=false`.

This checks persisted bytes as reported by recovery. It does not prove that
the bootloader executes this env, power-loss atomicity, a cold boot, cable
return, restore durability or first-boot acceptance. Physical acceptance is
still required before fleet release. A damaged env during power loss may fall
back to factory defaults; no unconditional rescue promise is made.

## Validation

Host tests use independent factory env encoding and real dd against sparse
fixed-layout GPT/partition files. Catch wrong prefix/tail/target, omitted
guards, stale identifiers/receipts, unsupported state, thread mounts, swap,
holders, short/noisy/failed reads, partial writes, sync/readback failure and
drift after submission. Check factory and protected states, both root wrappers,
idempotent restart, errors with accurate possible-write status and no private
diagnostics. Preserve existing recovery-copy, capture, mutable, channel,
transfer and session contracts on supported Ruby runtimes. No hardware writes
are scheduled for the working reference in this stage.
