# A133 composed cable write stage

## Intent and choice

Continue the authorized cable-only fleet installation work without physical
device feedback. One API must bind a release before unlock, preserve recovery
through image writes, and resume after an interrupted host process. This is
host-tested preparation/write coordination, not an accepted fleet installer.

Independent calls leave the caller to reconstruct the phase after a crash.
Reusing image role states for preparation makes that boundary ambiguous.
Choose an explicit outer journal and reuse the existing guarded APIs. The
additional small journal duplicates proven private-file mechanics deliberately;
a general storage framework is outside this milestone.

## API and pinned policy

`A133Cable.run(directory:, manifest:, adb:, serial:, cid:, original_env:,
backup:, mutable:, locked_round_trip_verified:, timeout:600, wait_timeout:120,
device_root_method:'vendor_su', recovery_root_method:'adbd')`.

Validate/copy the complete factory policy and mutable evidence before USB.
Verify all release artifacts before unlock. Bind serial/CID, full backup,
GPT, original critical partitions, original/armed env, mutable-copy hashes,
protected full-env hash and a canonical bundle fingerprint. Bundle order and
filenames do not change semantic release identity. Writer admission must
compare its freshly verified bundle against this pinned fingerprint before I/O.
Caller receipts remain trusted; no new archive provenance or release signing.

Use one0700 outer directory and0600 single-link state/lock, exact bounded
checksummed schema, held directory/lock descriptors, exclusive flock through
every substage, PID/thread/inode/mode checks and atomic fsynced publication.
Admit no alternate context. `unlock/` and `images/` are subordinate journals;
all operations for that device must share the same outer directory.

## State and interruptions

Outer phases: checking, unlocking, unlocked, protecting, protected, writing,
verified, failed. `possible_write`, `possible_reboot`, `unlock_verified`,
`protection_started` are monotonic. Hardware boot sizes bind on successful
unlock. Intent is durable before entering a substage that can change the device.
After rename, retain the visible state even when directory fsync fails.

Before protection intent, invoke the existing unlock Stage on every resume;
it owns the at-most-once native reboot rule. Persist its verified outcome
before protection intent. After protection intent, never invoke unlock again:
env and boot may already be intentionally different. Instead verify selected
unlocked USB recovery, CID/GPT and hardware sizes, then rerun Protection's live
original/protected-full-env and retained-recovery guards. Unknown partial env
stops; exact already protected env proceeds without rewriting.

Carry the pinned policy inside every repeated Protection/Client guard, not
just a handoff observation: outer lock, identity, exact GPT/hardware, all-task
mutable usage and full env hash. Protection allows original/protected full env;
images allow protected only. Additional guards never replace mandatory core
checks. A non-nil expected digest is copied before verifier I/O; false rejects.

Then invoke the writer with the pinned expected bundle fingerprint. It checks
all three ranges even after previous verification, rewriting only a complete
hash mismatch. Verify image Store lock scope and retain published state on
fsync faults. Stop after journal/transport failure; do not proceed to another
substage or retry internally. Failure reports are redacted, symbolic and
conservative about possible prior effects. Inspection emits only after close.

Success: `cable_write_stage_verified`, `installation_ready=false`. No recovery
release, trial boot, relock, rollback, factory restore or early secure-area
write. Never substitute this result for physical power-cycle acceptance.

## Evidence and remaining gates

Real private files and native sparse-device ADB fixtures exercise the actual
unlock/protection/writer composition, interrupted protection, image resume,
changed context, publication failure and lock replacement on both Rubies.
Tests assert written bytes, preserved env tail/recovery and reboot count.
They do not execute vendor U-Boot, prove host power-loss durability, authenticate
receipts or establish global device exclusion. Existing draft PR remains draft.
