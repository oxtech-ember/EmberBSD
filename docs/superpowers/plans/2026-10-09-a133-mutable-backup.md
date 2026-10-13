# A133 Mutable Backup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Retain verified per-device UDISK/metadata files through a read-only recovery acquisition.

**Architecture:** A separate strict manifest checker feeds a second acquisition API in the existing backup module. Extend Source only for guarded mutable reads; preserve full-capture behavior and sampled-observation limits.

**Tech Stack:** Ruby, existing bounded ADB channel, actual dd fixtures, private files.

**Spec:** ../specs/2026-10-09-a133-mutable-backup-design.md

## Global Constraints

- English public code/docs; no new Python, secrets or images in Git.
- Fixed production17-partition31037849600-byte profile; UDISK27676098048/metadata16777216 bytes.
- Exact schema1 mutable manifest, <=65536 bytes; files0600/private directory0700; distinct inodes.
- Only root-readable USB recovery; explicit trusted serial/CID; no device writes/reboot/unlock/root restart.
- observation=recovery_unmounted_two_matching_reads; filesystem_consistency=not_established_by_integrity_check; installation_ready=false.
- New outputs only, no retries/adoption; preserve failed evidence; deadline1..7200 seconds.
- Preserve existing schema1/schema2 full-capture contracts and working reference.

## Review Focus

- A second namespace mounts the block device through an alias; reject the acquisition.
- UDISK changes during the first pass and differs in its next full read; no final success manifest. Raw changes after its second read remain a sampled-evidence limit.
- Host evidence changes during the last source check or publication; fail despite matching earlier hashes.
- The source has no mounts but active swap/holders or unreadable process inventory; reject.
- An old valid manifest is replayed for another CID or an aliased host file; refuse fresh verification.

### Task 1: Fresh private mutable-manifest verification

**Files:**
- Create: ember/tools/a133-mutable-check.rb
- Create: ember/tools/a133-mutable-test-support.rb
- Test: ember/tools/a133-mutable-check-test.rb

**Interfaces:**
- Consumes: A133Backup::INVENTORY physical partition sizes.
- Produces: A133Mutable.verify(path, serial:, cid:, timeout:3600), returning a private string-keyed receipt with mutable_copies_verified, serial, cid, partition_sha256 and observation; filesystem_consistency remains unestablished.

- [x] **Step 1: Write real-file tests** for fresh whole-byte hash verification, trusted identity mismatch, wrong schema/fields/roles/bytes/hash, duplicate keys, unsafe mode/path, inode aliases, late mutation and redacted CLI/no write flag.
- [x] **Step 2: Run `ruby ember/tools/a133-mutable-check-test.rb`.** Expected: FAIL with missing mutable checker.
- [x] **Step 3: Implement the strict verifier** with retained descriptors, privacy/identity/lifetime guards and bounded errors.
- [x] **Step 4: Run `ruby ember/tools/a133-mutable-check-test.rb` and `/usr/bin/ruby ember/tools/a133-mutable-check-test.rb`.** Expected: all cases pass, no private CLI output.
- [x] **Step 5: Commit** `feat(a133): verify retained mutable partition copies`.

### Task 2: Read-only recovery acquisition and public instructions

**Files:**
- Create: ember/tools/a133-usb-mutable-backup.rb
- Modify: ember/tools/a133-usb-backup.rb (parameterize retained manifest name only)
- Modify: ember/tools/a133-usb-backup-source.rb (guard mutable reads/checkpoints)
- Create: ember/tools/a133-usb-mutable-fixture.rb
- Test: ember/tools/a133-usb-mutable-backup-test.rb
- Create: ember/boards/ys-m33-a133-mutable-backup.md
- Modify: ember/boards/ys-m33-a133-backup-recovery.md, ys-m33-a133-usb-backup.md and README.md

**Interfaces:**
- Consumes: A133Mutable.verify from Task1; Source.inspect!/read; existing exclusive private host output helpers.
- Produces: A133UsbBackup.collect_mutable(directory:, serial:, cid:, adb:'adb', root_method:'adbd', timeout:3600), redacted usb_mutable_captured receipt, exactly3 private outputs; Source.mutable_inspect! and recovery-only UDISK/metadata read roles.

- [x] **Step 1: Write actual-dd tests** for exact two-pass copies, namespace mount aliases, swap/holders/unreadable inventory, source change across copies, changed second-read bytes, late host mutation/publication, stream failure, destination collision, fsync failure, timeout and redacted CLI.
- [x] **Step 2: Run `ruby ember/tools/a133-usb-mutable-backup-test.rb`.** Expected: FAIL with missing mutable acquisition.
- [x] **Step 3: Implement source guards and collector** without device-write or automatic recovery commands; retain evidence through publication; document observation limits and storage/time cost.
- [x] **Step 4: Run on both Rubies** mutable checker/acquisition and existing USB backup26/lifetime5/capture37/hardware18/review-regression6 suites. Expected: all pass; old full-capture behavior unchanged. Run existing channel13/transfer43/session25 on defaultRuby. Expected: all pass. Check Ruby syntax and `git diff --check`; expected: success.
- [x] **Step 5: Commit** `feat(a133): retain recovery mutable backups over USB`.

## Completion

Fresh independent review of this plan's change range; findings get regression-first fixes, one fix pass. Publish the existing draft PR and separately update Ember/ROOMY evidence, preserving review status. Physical USB/restoration/power-cycle acceptance remains open. Record tested revisions, source observations, test counts and every ruling before removing this plan's scratch workspace.

## Execution evidence

On2026-10-09, mutable checker24, acquisition28, old USBbackup26/lifetime5,
capture37/hardware18/review-regression6 passed on Ruby4.0.5 and2.6.10.
Channel13/transfer43/session25 passed on Ruby4.0.5; syntax/diff/local-link checks passed.
The working reference remained reachable with GUI/voice/MCU processes running;
ADB inventory was empty. No physical acquisition, storage write, reboot, sound
or camera test occurred. Kernel/userland sources were not changed or rebuilt.

## Review and execution decisions

Independent GPT-6 Astra reviewed7167770feb63..82119597c164 and independently
passed checker24/acquisition28/diff check. It identified one Important gap:
nonleader recovery threads were missing from mount inventory. Two regressions
reproduced accepted hidden/unreadable thread inventories on both Rubies before
the fix. The source now reads each process's task/TID mountinfo, failing closed.
After the fix, checker24/acquisition28/thread2/fullbackup26/lifetime5/
capture37/hardware18/regression6 passed again on both Rubies;
channel13/transfer43/session25 passed on Ruby4, with syntax/diff/link checks.
Fix verification belongs to the implementer; no second review or physical Linux
namespace reproduction is claimed. No Critical/Minor findings were returned.

- Execute inline under autonomous authorization; written artifacts and final review replace per-step human review. Cost: no per-step human design feedback.
- Preserve the contribution branch without unrelated main kernel integration. Cost: BSP reconciliation still required before kernel work/merge.
- Keep unmounted/two-read observations separate from filesystem snapshots. Cost: physical quiescence/restoration acceptance remains required.
- Test fixtures while physical ADB is absent. Cost: real vendor/large-transfer acceptance remains open.
- A raw UDISK change after its second read can accompany a success receipt; narrow the overbroad Review Focus to changes visible in its next full read. Cost: copies need not share an instantaneous filesystem state.
- The review sets physical compatibility/quiescence/restoration aside as hardware stages. Cost: real recovery durability can still fail despite green host contracts.
