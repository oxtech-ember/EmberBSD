# A133 Guarded USB Reboot Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Guard one normal factory reboot and verify recovery/Android return through explicit USB identity and captured images.

**Architecture:** Share immutable evidence policy with Entry, add a read-only whole-state verifier and compose two fixed reboot transitions. Keep env preparation/restoration and unlock outside this API.

**Tech Stack:** Ruby, bounded ADB channel, actual-dd sparse fixtures.

**Spec:** ../specs/2026-10-09-a133-recovery-reboot-design.md

## Global Constraints

- English public artifacts; no new Python, secrets or private images.
- Fixed31037849600-byte17-partition geometry, env16777216/prefix131072 bytes.
- Fresh retained trusted Capture evidence, explicit ASCII serial/CID, exclusive caller operation, locked1/green.
- Reuse usage guards; zero raw writes by Readback/Reboot; no unlock/root restart/automatic retry/rollback.
- Exact original/armed hashes; consumed exact values/valid export/original tail.
- Per-command timeout and monotonic inventory wait1..7200; at most0.1s poll interval; one reboot submission.
- Both possible-side-effect flagsfalse before submission andtrue after; installation_ready=false.
- No physical writes/reboots to reference, no kernel/userland changes, existing draft PR retained.

## Review Focus

- Same serial returns via TCP or on a different CID/GPT after reboot: never accept or issue another reboot.
- Command fails after scheduling a reboot or launcher disappears: preserve possible persistent-side-effect flags and private-path redaction.
- Captured boot changes although recovery/env are valid: refuse before normal Android return and after reboot.
- Offline/source-state polling and a hanging ADB inventory: bounded wait, no infinite loop or retry of reboot.
- Caller mutates identifiers/evidence during the offline epoch: later state checks must use immutable original policy.

### Task 1: Immutable shared policy and read-only state verification

**Files:**
- Create: ember/tools/a133-recovery-policy.rb
- Modify: ember/tools/a133-recovery-entry.rb (policy delegation only)
- Create: ember/tools/a133-recovery-readback.rb
- Create: ember/tools/a133-recovery-readback-test.rb
- Create: ember/tools/a133-recovery-reboot-test-support.rb
- Modify: ember/tools/a133-recovery-protection-fixture.rb (boot reads)

**Interfaces:**
- Consumes: current Entry policy/codec/Source/channel.
- Produces: frozen Policy snapshot and Readback.verify(expected: original/armed/consumed) with whole boot/bootloader/recovery/env checks, zero writes and hardware_bytes.

- [x] **Step 1: Write actual-I/O tests** for frozen policy/input mutation, exact modes/states, consumed reordering/extra changes, boot/critical/tail drift, usage and invalid capture/options/privacy.
- [x] **Step 2: Run new readback test.** Expected: FAIL with missing Policy/Readback.
- [x] **Step 3: Implement shared policy and verifier**, preserve Entry write behavior and old reasons.
- [x] **Step 4: Run new tests and Entry65/regression2 on both Rubies.** Expected: all pass.
- [x] **Step 5: Commit** `feat(a133): verify immutable factory recovery states`.

### Task 2: Guarded reboot transitions and public instructions

**Files:**
- Create: ember/tools/a133-recovery-reboot.rb
- Create: ember/tools/a133-recovery-reboot-test.rb
- Create: ember/tools/a133-recovery-reboot-fixture.rb
- Modify: ember/tools/a133-recovery-readback.rb (launcher argument admission)
- Create: ember/tools/a133-recovery-readback-argument-test.rb
- Modify: ember/tools/a133-recovery-protection-test.rb (fault-scenario subprocess budget)
- Create: ember/boards/ys-m33-a133-recovery-reboot.md
- Modify: README.md, ember/boards/ys-m33-a133-cable-install.md

**Interfaces:**
- Consumes: Policy and Readback from Task1, bounded Channel.
- Produces: enter_recovery/return_android verifying prepared source and destination with one normal ADB reboot; possible-side-effect errors and installation_ready=false.

- [x] **Step 1: Write actual subprocess tests** for full Entry round trip, offline/source-state polling, unchanged critical/tail bytes, no raw writes, identity/GPT/transport/root/boot/env changes, timeouts, failed reboot/sync-like diagnostics, launcher before/after submission and caller mutation.
- [x] **Step 2: Run new reboot test.** Expected: FAIL with missing reboot API.
- [x] **Step 3: Implement fixed transitions and bounded wait**, document operational flags, caller journal/exclusive obligations and hardware gates.
- [x] **Step 4: Run all host A133 Ruby suites on both versions**, syntax/diff/local-link checks. Expected: all pass; no OS rebuild needed for host-only source.
- [x] **Step 5: Commit** `feat(a133): guard factory USB reboot transitions`.

## Completion

One fresh independent GPT-6 Astra review of this whole range, one regression-first fix pass, no re-review. Carry all rulings/minors into plan/final. Update existing draft PR and Ember/ROOMY evidence, preserve review/product acceptance and remove only this plan's scratch workspace.

## Execution evidence

Task1 readback21/Entry65/holder regressions2 passed on both Ruby4.0.5 and
system2.6.10. Task2 exercised26 adjacent host suites, launcher arguments7
and45 native reboot cases on both. System ran all45 at1s fault budgets;
Ruby4 completed the31-prefix cases, then its intended timed fault was blocked
by a legitimate preflight timeout. All14 tail cases passed on both with3s
subprocess budgets; no single Ruby4 all45 invocation at3s is claimed.
Protection49 was repeated successfully on both at3s after the same test issue.
Inventory waits retain1s: a focused probe measured wait1.001s versus
operation26.613s. Syntax/diff and100 local public links passed. No hardware.

Implementation rulings (costs):
- Measure inventory bounds from the native reboot marker, excluding preflight;
  no whole-operation deadline is supplied or proved.
- Admit valid ASCII-compatible NUL-free launcher strings in the new APIs;
  UTF16 paths require conversion, while non-ASCII UTF8 is tested.
- Fault scenarios use3s command budgets, avoiding unrelated1s preflight refusal;
  progress under1s/resource contention is not accepted, and Channel covers1s.
- Resume unchanged successful31-prefix reboot cases with all14 tail cases
  rather than repeat heavy successful reads; Ruby4 has no single all45-at3s
  invocation or associated whole-run resource-pressure proof.

## Final review and fix evidence

One fresh GPT-6 Astra read-only review of4e368c1b0e88..d53a20511a09 found
one Important/P2: mutable expected could change the public verification mode
during actual USB reads and alter the issued receipt afterward. Both live and
post-return regressions failed on both runtimes, then passed after the
implementer copied/froze expected before admission/USB. The one fix pass
completed all29 suites on BOTH runtimes, including full defaultReboot45 at3s,
expectation2 and argument7. Both matrix processes exited0; all29 result lines
were read and checked. Syntax/diff/public100 links passed. No new Minor or
re-review. The earlier split-run limitation is now superseded by full-run
evidence; no general performance or sustained-load acceptance is implied.

Reviewer scope rulings (costs):
- Vendor saveenv/USB/root/MCU/cold boot/Android FS remain physical gates;
  unsupported or incoherent factory behavior remains possible.
- Boot0/boot1 contents stay outside this readback; only sizes are bound,
  so content changes can remain undetected.
- Other owners/atomic whole-eMMC snapshots remain caller exclusion duties;
  transient raw changes can evade sampled guards.
- Durable journal/global device lock remain coordinator duties;
  omitted ownership/progress protection can make interruptions unsafe.
- There is no overall operation deadline beyond command/inventory bounds;
  total duration can exceed the inventory budget.
- Initial31+14 evidence was accepted without whole-run stress proof;
  full all45-at3s now passed both after the fix, closing that specific gap.
- Historical BSP/main reconciliation/unlock/trial/release remain draft;
  this host proof cannot authorize fleet release or merging the whole branch.

Existing draft PR and both wikis will publish this host evidence without
hardware acceptance. No physical writes/reboots, audio/camera or private
full-archive rehash occurred.
