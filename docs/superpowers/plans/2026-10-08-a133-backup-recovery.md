# A133 backup and recovery preparation implementation plan

> For agentic workers: use superpowers:executing-plans for inline execution.

**Goal:** supply verified backup inspection and a safe copied recovery candidate.
**Architecture:** two independent Ruby tools; backup streams read-only, candidate
uses A133Env and creates one private file. Neither can flash or restart a device.
**Tech stack:** Ruby standard library, host zstd, existing A133Env.
**Spec:** [backup and recovery contract](../specs/2026-10-08-a133-backup-recovery-design.md).

## Global constraints

- Backup CLI requires 31037849600 bytes and the exact 17-partition reference layout.
- Streaming memory stays bounded; no raw decompressed file is created.
- Deadline 1–7200 seconds, default 3600. No decoder diagnostics or identities in JSON.
- Candidate changes exactly three named variables and reconstructs the original on removal.
- New output is 0600, exclusive and published without replacement.
- All tools always installation_ready=false; no device writes, reboots or unlock.
- Preserve current linked worktree and unrelated work; no new Python helpers.

## Review focus

- A matching hash must not conceal incoherent primary/secondary GPT copies.
- Decoder corruption, noisy stderr or stalled descendants must not hang or succeed.
- Host symlinks, empty files and an output collision must preserve existing files.
- A live Android snapshot must not be called a consistent rollback image.
- A foreign/custom environment must not be silently changed into the tested factory profile.

### Task 1: full-backup integrity

**Files:** create `ember/tools/a133-backup-check.rb`, `a133-backup-check-test.rb`.
**Interfaces:** `A133Backup.verify_stream(io, bytes, sha256, inventory)` returns
disk/critical-range digests after GPT validation; CLI supplies fixed YS-M33 profile.

- [x] Build independent small GPT fixtures; test hashes, truncation/excess,
  CRCs, copies, names/bounds, GUIDs and geometry through the actual verifier.
- [x] Run the test; expect missing verifier before implementation.
- [x] Implement bounded stream verification and CLI raw/zstd descriptor reading.
- [x] Test real decoder subprocess failures, timeout, stderr drainage and malformed inputs.
- [x] Run on the saved private factory image and compare four critical hashes
  with independently saved partitions. Do not print/archive private identifiers.
- [x] Document command, provenance and the filesystem-consistency boundary; commit.

### Task 2: one-shot recovery candidate

**Files:** create `ember/tools/a133-recovery-env.rb`, `a133-recovery-env-test.rb`.
**Interfaces:** `A133Recovery.prepare(data)` returns copied bytes; CLI publishes
new private output. Imports `A133Env.decode/patch` from the existing editor.

- [x] Write copied-file tests for exact three changes, byte restoration,
  unsupported scripts, hook collisions, CRC errors and output/input protection.
- [x] Run the test; expect missing recovery preparer before implementation.
- [x] Implement factory-profile guard and exclusive publication; no device transport.
- [x] Compare generated private candidate with the previously tested candidate
  and verify the original's digest is unchanged.
- [x] Run both new contracts plus env, bundle, preflight and firstboot guards.
- [x] Review, document actual limits, update the draft PR/wiki and push. Do not merge main.

## Completion evidence — 2026-10-08

- Backup checker: `423b8aab1333`; recovery preparer: `70a61e8a1bf3`.
- Ruby 4.0.5 and 2.6.10: 29 backup cases and 19 recovery cases passed.
- Complete private 31037849600-byte factory backup and four independent
  partition hashes matched; filesystem consistency remains unestablished.
- Prepared candidate equals the prior physically tested candidate byte-for-byte;
  original env unchanged. No device write, recovery boot or audio test now.
- Existing env, bundle, ADB preflight and firstboot contracts passed.
- Independent Codex GPT-6 Astra review: no blocking defect; subprocess regression
  gap addressed, descendant reaping assertion uses bounded settling time.
- Existing draft PR #1 and Ember/ROOMY wiki carry these limits. Main is not merged.
- The scoped preparation plan is complete. USB transport/writes, per-device
  binding, guarded first boot and physical restoration remain fleet work.
