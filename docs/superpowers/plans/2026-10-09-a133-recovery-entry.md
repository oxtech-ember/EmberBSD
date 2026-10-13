# A133 Recovery Entry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Guard the Android one-shot recovery env write and exact factory restoration from known recovery states.

**Architecture:** Expose the full-backup GPT digest, add read-only target-env usage inspection, and implement a separate two-phase env transition library. Reuse the existing env codec, one-shot preparer, Source reads and binary channel.

**Tech Stack:** Ruby, ADB, actual-dd sparse GPT/partition fixtures.

**Spec:** ../specs/2026-10-09-a133-recovery-entry-design.md

## Global Constraints

- English code/public docs; no Python, secrets or private images in Git.
- Production31037849600-byte17-partition geometry; env16777216/prefix131072 bytes.
- Fresh in-process capture receipts with serial/CID, GPT digest and critical/hardware evidence; caller retains evidence/exclusive operation.
- Only locked1/green, explicit device/recovery state and adbd/vendor_su root wrapper.
- Android permits other partition mounts but rejects whole-eMMC/env mounts/holders and all active swap; recovery retains stricter mutable guard.
- No automatic reboot/unlock/root restart/retry/rollback. Only env prefix dd+sync; installation_ready=false.
- Consumed env matches every expected variable and original tail, while valid export ordering/padding may differ.
- Preserve old protection/session/capture schema contracts and working reference; no kernel/userland changes.

## Review Focus

- Android uses a dm alias of env while userdata is also mounted; refuse the target use and permit ordinary unrelated mounts.
- Vendor saveenv reorders variables or changes another variable; accept only exact known consumed values, never an extra or modified value.
- A caller replays an old backup on matching CID with different valid GPT; refuse before writing.
- After env submission, launcher/transport fails or another namespace uses env; preserve possible-write status and privacy.
- Manual repeat after partial/sync failure; fresh exact-state checks must not adopt an unknown env or claim durability.

### Task 1: Backup GPT binding and target-env usage inspection

**Files:**
- Modify: ember/tools/a133-backup-check.rb (receipt field only)
- Modify: ember/tools/a133-usb-backup-source.rb (read-only environment_inspect!)
- Create: ember/tools/a133-backup-gpt-test.rb
- Create: ember/tools/a133-env-usage-test.rb
- Modify: ember/tools/a133-recovery-protection-fixture.rb (test-only queries)

**Interfaces:**
- Consumes: verify_stream retained GPT bytes, Source.inspect!/mutable_inspect! and channel.
- Produces: gpt_sha256 in stream/file/capture receipts; Source.environment_inspect! returning pinned profile or Invalid.

- [x] **Step 1: Write tests** for fresh raw/capture GPT digest, changed valid GPT identity, other Android mounts permitted, whole/env/task alias mount, holders, swap, unreadable/malformed inventories and unchanged source.
- [x] **Step 2: Run new GPT/usage tests.** Expected: FAIL with missing receipt field or inspection method.
- [x] **Step 3: Implement receipt propagation and target guard** while keeping Source write-free and old schemas unchanged.
- [x] **Step 4: Run both new suites and old backup-library/capture-check/mutable-thread tests on both Rubies.** Expected: all pass. Whole relevant suite is completed with Task2 because its fixture exercises this interface during actual writes.
- [x] **Step 5: Commit** `feat(a133): bind env transitions to backup GPT and usage`.

### Task 2: Guarded arm/restore library and instructions

**Files:**
- Create: ember/tools/a133-recovery-entry.rb
- Create: ember/tools/a133-recovery-entry-test.rb
- Modify: ember/tools/a133-recovery-protection-fixture.rb (actual entry fault modes)
- Create: ember/boards/ys-m33-a133-recovery-entry.md
- Modify: README.md, ember/boards/ys-m33-a133-cable-install.md

**Interfaces:**
- Consumes: capture receipt gpt_sha256 and Source.environment_inspect! from Task1; prepare/decode/patch, Channel.run.
- Produces: Entry.new(adb:,serial:,cid:,state:,root_method:'vendor_su',timeout:600).arm/restore(original_env:,backup:), symbol-keyed env_entry_armed/restored receipt with accurate writes_performed, env_sha256 and installation_ready:false; errors Invalid with write_attempted.

- [x] **Step 1: Write actual-dd tests** for device arm/repeat, recovery restore of armed/consumed/reordered states, original repeat, full tail and critical images unchanged; wrong state/backup/GPT/profile/usage, altered consumed values, caller mutation, read/partial/sync/readback failures and late transport/source drift.
- [x] **Step 2: Run `ruby ember/tools/a133-recovery-entry-test.rb`.** Expected: FAIL with missing entry library.
- [x] **Step 3: Implement policy pinning and strict state selection**, with only prefix writes and normalized errors. Document vendor/hardware/no-swap limits and remaining unlock/trial boot stages.
- [x] **Step 4: Run all new tests and old recovery-env/protect/protection/regression/channel/transfer/session/mutable/capture/backup/acquisition contracts on both Rubies.** Expected: all pass; no single whole-OS host command exists. Run syntax/diff/local-link checks; expected success.
- [x] **Step 5: Commit** `feat(a133): guard one-shot recovery entry and restoration`.

## Completion

One fresh independent review of this plan's whole range; one regression-first fix pass. Publish to the existing draft PR and update related project evidence without changing product acceptance. Carry all rulings/minors into the plan and final answer, then remove only this plan's scratch workspace. Factory unlock, real round trips and accepted trial boot remain required stages.

## Validation and independent review record

Task1 RED: missing GPT field/inspection method and unreadable holder guard;
GREEN GPT4/usage19/library14/capture37/thread2 on Ruby4.0.5/system2.6.10.
Task2 RED: missing Entry; GREEN Entry65 on Ruby4 and system62 plus3 newly added
constructor checks selected from the same suite. All23 adjacent host suites
passed on both versions; syntax/diff and49 local public links passed. No OS
rebuild, physical write/reboot or new full31GB archive verification was performed.

One read-only GPT-6 Astra review covered ef33106c09a8..532ad4699db9. It independently
passed GPT4/usage19 on Ruby4, ran a narrow actual-dd restore and two real shell
experiments. One Important safety finding: recovery's old holder glob accepted
missing/unreadable directories as empty. Two before/after actual-write regressions
failed on both versions before the fix and then passed. The single fix pass
requires whole-eMMC and every expected partition's holder directory to exist
and be readable/traversable. All25 host scripts passed after the fix on both Ruby4.0.5/system2.6.10, including
complete Entry65 and the two regression cases. The exact production holder query
also refused missing and mode0000 nonempty directories in two native host-shell
experiments. Syntax/diff and49 public local links passed.
There were no new Minor findings. No second review is planned; the implementer
verifies the fix. The review does not accept the earlier BSP or a fleet release.

## Carried decisions

- Ruling: vendor saveenv/hook/USB return/physical restore require hardware acceptance; host tests stand as host evidence only. Cost if wrong: an unsupported factory image could be accepted.
- Ruling: cold-power durability and Android filesystem consistency remain unaccepted. Cost if wrong: damaged or incoherent restored state.
- Ruling: no-swap and bounded mount-inventory compatibility require physical measurement; conservative refusal stands. Cost if wrong: a valid factory image may stop safely.
- Ruling: USB unlock and accepted trial boot remain later integration. Cost if wrong: the installer remains incomplete.
- Ruling: trusted receipt provenance/freshness/retention and exclusive access remain caller duties. Cost if wrong: stale or forged evidence may authorize an unsafe operation.
- Ruling: foreign raw writes between observations are not atomically excluded; there is no global device lease. Cost if wrong: concurrent changes may evade sampled guards.
- Ruling: OS/BSP validation and reconciliation with current main remain outside this host range; keep the whole branch draft. Cost if wrong: host checks may be mistaken for kernel/fleet acceptance.
- Ruling: draft publication is performed by the author after the fix matrix; release gates remain open. Cost if wrong: an unaccepted installer could be shipped.
