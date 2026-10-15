# A133 Recovery Protection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prepare and verify the persistent recovery env required by the guarded A133 image writer.

**Architecture:** Extend the copied-byte env preparer; add a separate write primitive consuming trusted fresh backup receipts. Reuse read-only Source for pinned recovery inspection and full partition reads, with the bounded channel for exactly one env prefix write.

**Tech Stack:** Ruby, existing ADB channel, real-dd sparse device fixtures.

**Spec:** ../specs/2026-10-09-a133-recovery-protection-design.md

## Global Constraints

- English public code/docs, no Python or private identifiers/images in Git.
- Fixed31037849600-byte17-partition geometry; env16777216, prefix131072, recovery33554432 bytes.
- Explicit serial/CID; recovery locked1/green or unlocked0/orange; root_method adbd/vendor_su.
- Trusted fresh schema2 capture and mutable receipts supplied in-process by the caller; no saved-receipt CLI or global device lease.
- Only prefix of mmcblk0p2 may be written; no unlock/reboot/root restart/GPT/recovery/boot-area writes.
- Unknown state fails closed; no automatic retry/rollback. Return installation_ready=false.
- Capture caller policy bytes before USB; check all task mount inventories/swap/holders, full env and recovery hashes and pinned GPT/profile.
- Keep working reference untouched; physical cold boot/cable return/restore/first boot still pending.

## Review Focus

- Caller policy strings change during a blocking USB operation; pinned values still govern.
- Env tail changes even when prefix readback matches; reject completion.
- A nonleader recovery thread mounts an alias of eMMC between preflight and write; reject or report possible write after submission.
- Sync fails after successful dd; no success or unsafe automatic retry.
- Already-protected env belongs to a different original backup or GPT; no idempotent adoption.

### Task 1: Deterministic persistent protection bytes

**Files:**
- Modify: ember/tools/a133-recovery-env.rb
- Create: ember/tools/a133-recovery-protect-test.rb

**Interfaces:**
- Consumes: A133Env.decode/patch and existing PROFILE/UPDATES.
- Produces: A133Recovery.protect(data), returning131072 bytes or A133Recovery::Invalid.

- [x] **Step 1: Write independent-byte tests** for intended two-variable changes, exact reversible padding/opaque preservation, existing reserved keys, unsupported profiles, bad CRC/size and compatibility with the image writer's protection validation.
- [x] **Step 2: Run `ruby ember/tools/a133-recovery-protect-test.rb`.** Expected: FAIL because protect is missing.
- [x] **Step 3: Implement protect** with profile/collision checks and byte restoration.
- [x] **Step 4: Run new protect and existing recovery-env tests on `ruby` and `/usr/bin/ruby`.** Expected: all pass.
- [x] **Step 5: Commit** `feat(a133): prepare persistent recovery protection`.

### Task 2: Guarded env transition and instructions

**Files:**
- Create: ember/tools/a133-recovery-protection.rb
- Create: ember/tools/a133-recovery-protection-fixture.rb
- Create: ember/tools/a133-recovery-protection-test.rb
- Create: ember/boards/ys-m33-a133-recovery-protection.md
- Modify: README.md, ember/boards/ys-m33-a133-backup-recovery.md

**Interfaces:**
- Consumes: protect from Task1, Source.mutable_inspect!/read, Channel.run, trusted receipt shapes from Capture/Mutable.
- Produces: Protection.new(adb:,serial:,cid:,root_method:'adbd',timeout:600).install(original_env:,backup:,mutable:), returning symbol-keyed recovery_protection_verified receipt with protected_env, env_sha256, writes_performed and installation_ready:false. Failures are A133Usb::Invalid with accurate write_attempted boolean.

- [x] **Step 1: Write actual-dd tests** for exact prefix and tail preservation, idempotent fresh check, locked/unlocked/wrappers, receipt/identity/state/layout/GPT mismatch, foreign env/recovery, threaded mounts/swap/holders, policy mutation during USB, bounded read errors, partial write/sync/readback failure and post-write drift. Before-write failures preserve all device files; after-write failures retain attempted status.
- [x] **Step 2: Run `ruby ember/tools/a133-recovery-protection-test.rb`.** Expected: FAIL with missing protection primitive.
- [x] **Step 3: Implement pinned policy and separate guarded writer**, then document caller trust/exclusivity and physical limitations.
- [x] **Step 4: Run both new suites and existing recovery-env/channel/transfer/session/mutable/check/acquisition/capture contracts on both Rubies; syntax and `git diff --check`.** Expected: all pass. There is no single native test command covering all OS components; relevant host-tool contracts define this stage's suite.
- [x] **Step 5: Commit** `feat(a133): guard persistent recovery env writes`.

## Completion

One fresh independent review of this plan's entire change range; one regression-first fix pass. Update the existing draft PR and Ember/ROOMY wiki with source revisions and verification limits; preserve ROOMY review status. Publish under existing autonomous authorization. Copy all rulings/review evidence into this plan and final response before removing only this plan's scratch workspace.

## Execution evidence

On2026-10-09, protection-bytes14/protection-USB49/recovery-copy19/channel13/
transfer43/session25/mutable-check24/acquisition28/thread2/capture37 passed on
Ruby4.0.5 and system2.6.10. The fixture sends actual binary dd reads/writes to
sparse fixed-layout files. Mixed receipt keys reproduced ArgumentError before
string-key validation; the final API rejects them with a redacted Invalid.
The partial-write restart fixture places retained opaque data before the boot
variables: its512-byte cut is actually incomplete; an earlier small fixture
legitimately reached the exact expected env despite transport failure.
Syntax, diff and46 public local-link checks passed. No kernel/userland changes
or rebuilds; the primary dirty checkout was preserved. Fresh origin/main is
d8d473d58d9f; its new SVG documentation does not change these A133 host tools.
ADB inventory was empty. The working reference was reachable with GUI/voice/MCU
processes; no physical write, reboot, audio or camera test occurred.

## Independent review and one fix pass

GPT-6 Astra reviewed d1a6c92851cf..945a66200083, independently passed bytes14
on both Rubies, USB49 on Ruby4 and diff check. One Important finding: ENOTDIR
after an actual dd exposed the launcher path and lost write_attempted. Before
and after submission regressions failed on both runtimes, then passed after
SystemCallError/IOError normalization with cause:nil and preserved attempted.
The implementer reran the entire relevant suite: 256/256 on Ruby4.0.5 and
256/256 on system2.6.10 (11 scripts: bytes14/USB49/I-O2/recovery19/channel13/
transfer43/session25/mutable-check24/acquisition28/thread2/capture37).
Syntax/diff/public-link checks passed. No second reviewer or hardware acceptance.
One Minor remains deferred: malformed UTF-8 caller identifiers/hashes may raise
ArgumentError before USB instead of normalized diagnostics; callers require ASCII.

### Rulings carried from execution/review

- Ruling: Execute inline and publish the existing draft PR/wiki under repeated autonomous authorization — preserve the chosen plan without intermediate questions — cost: no per-step human design review.
- Ruling: Preserve the contribution branch without merging unrelated origin/main kernel work — host-only stage uses inspected sources — cost: BSP reconciliation remains before kernel changes/merge.
- Ruling: Accept trusted fresh in-process verifier receipts, not saved receipts — match existing writer boundary and keep this primitive focused — cost: future coordinator must verify and retain original evidence and exclusive device ownership.
- Ruling: Exercise file fixtures while preserving the working reference — no new identified recovery ADB target — cost: physical cable return, power-loss and restoration remain unaccepted.
- Final: Ruling: Backup/mutable evidence authenticity/freshness/lifetime is delegated to the trusted coordinator — existing receipt capability boundary stands — cost: stale/fabricated receipts are not detected by this primitive alone.
- Final: Ruling: Other hosts/processes and transient mounts remain outside sampled guards — require exclusive operation and retain explicit observation limits — cost: concurrent writers can still race observations.
- Final: Ruling: A manual retry with exact protected bytes does not issue an extra sync after a previous failed sync — preserve the idempotent no-write contract, without durability/acceptance claim — cost: persisted bytes and cold-power durability can differ until physical acceptance.
- Final: Ruling: Env execution/cold boot/cable return/power-loss/restoration stay physical acceptance stages — host readback cannot prove them — cost: a green host stage may still fail on hardware.
- Final: Ruling: Earlier BSP/main reconciliation is outside this plan review — keep existing PR draft — cost: the full historical branch is not yet merge-ready.
