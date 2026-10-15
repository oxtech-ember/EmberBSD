# A133 Journaled Unlock Stage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans inline. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Guard factory unlock, persist possible effects and verify unlocked USB recovery before returning an env-original receipt.
**Architecture:** Independent private journal plus fixed-profile transport composed by one resumable stage; existing factory APIs stay unchanged.
**Tech Stack:** Ruby, bounded ADB, actual-dd sparse fixtures.
**Spec:** ../specs/2026-10-09-a133-unlock-stage-design.md

## Global Constraints

- Fixed31037849600-byte17-partition profile, env16777216/prefix131072; fresh caller-trusted retained Capture/Mutable and explicit physical round-trip gate.
- Immutable policy; full critical/env/GPT checks, same serial/CID and hardware sizes; exact original/armed/known consumed only.
- Private0700 directory/0600 single-link files, lock held through I/O;65536-byte checksummed exact schema and monotonic flags.
- Marker before every possible write/reboot; never replay a recorded reboot; no rollback/relock/image write/root restart/trial acceptance.
- Native subprocess tests on Ruby4.0.5/system2.6.10; no new Python/secrets/hardware or kernel rebuild; existing draft PR retained.

## Review Focus

- A crash after durable reboot intent but before the command must not cause replay on resume.
- Journal fsync/rename/cleanup errors must stop later device effects and hide private paths.
- Already-unlocked/unknown env on a fresh journal must not be adopted as a completed unlock.
- Caller mutation, identity/GPT/critical/tail/usage drift must not change trusted policy.
- A detached Store or different directory/owner must not claim the protected lock scope.

### Task 1: Private journal

**Files:** create ember/tools/a133-unlock-journal.rb and a133-unlock-journal-test.rb.
**Interfaces:** `Journal.open(directory,context){|store|...}`; Store snapshot, record(phase,write:,reboot:,hardware_bytes:,reason:), report; nil context is inspection-only.

- [x] Write real-file tests for durable monotonic intent, context/duplicate/checksum/mode/symlink/hardlink refusals, locking, escaped Store and injected publication failure.
- [x] Run new journal test on both Rubies. Expected: missing implementation FAIL.
- [x] Implement the schema/lock/publication contract in the spec.
- [x] Run journal tests on both Rubies. Expected: all pass.
- [x] Commit `feat(a133): persist private unlock stage intent`.

### Task 2: Guarded transition and instructions

**Files:** create a133-unlock-stage.rb, a133-unlock-stage-test.rb, a133-unlock-stage-cli-test.rb, a133-unlock-stage-admission-test.rb, a133-unlock-stage-fixture.rb and ember/boards/ys-m33-a133-unlock-stage.md; update README/cable/unlock-env instructions.
**Interfaces:** Stage.run signature from spec consumes Journal plus offline unlock encoder, Recovery Policy and Source/Channel; produces unlocked_recovery_verified or redacted Stage::Invalid report, installation_ready=false.

- [x] Write actual-I/O tests for happy/restored repeat, prefix/write/reboot order and marker inspection, command failure/resume without duplicate reboot, unknown fresh states, evidence/identity/critical/tail/usage drift and journal refusal before USB.
- [x] Run new stage test on both Rubies. Expected: missing stage FAIL.
- [x] Implement fixed transitions, live rechecks and inspection-only CLI.
- [x] Run all new suites and adjacent unlock44/cleanup3/codec/Recovery19/Protect14/Channel contracts on both; syntax/diff/local links. Expected: all pass.
- [x] Commit `feat(a133): guard journaled vendor unlock recovery`.

## Completion

One fresh independent GPT-6 Astra review; one regression-first fix pass, no re-review. Carry all rulings/minors into plan/final, update existing draft PR and both wikis, then remove only this plan scratch. No physical acceptance claim.

## Execution rulings

1. Keep the CLI late-close regression separate from transport tests. Inspection
   emits one final JSON only after lock release. Cost: host regressions do not
   establish filesystem power-loss durability.
2. Admission before journal load reports unknown/possible prior effects, rather
   than claiming a previous session had none. Cost: even a fresh invalid call
   can require journal inspection; this does not assert command submission.
3. Review the entire current plan range from `a86ef4d8555e`, including Journal
   and Stage. Cost: the whole historical BSP/main reconciliation and fleet
   release remain outside this review; the existing PR stays draft.
4. The task-done checkpoint uses syntax checks after the already completed
   ten-suite matrices on both runtimes. Cost: that checkpoint alone is weaker;
   completion depends on the separately read full outputs and exit codes. Do
   not repeat successful costly suites without a code change or new concern.
5. Actual vendor hook/USB/secure-storage execution remains a physical gate.
   Cost: native fixture success does not prove compatible physical unlock.
6. Data preservation and coherent Android restoration remain separate gates.
   Cost: env reversibility alone cannot establish recovery of user data.
7. Fresh retained trusted Capture/Mutable evidence belongs to the caller.
   Cost: receipt checks alone cannot reject a forged/stale trusted archive.
8. Post-transition boot0/boot1 checks bind sizes only. Cost: changed early boot
   contents are outside this readback's detection.
9. Real host power-loss/filesystem durability remains unaccepted. Cost: a cold
   interruption can lose an intent snapshot despite green injected faults.
10. Global tablet exclusion and malicious local-owner defense remain external.
    Cost: another host/directory can race device operations; inode checks stop
    accidental scope replacement but provide no authentication/global lease.
11. Keep command/inventory deadlines separate. Cost: complete readback and
    total runtime can exceed the inventory wait budget.
12. Persistent protection, images, release signing, accepted trial boot and
    fleet safety remain following gates. Cost: this stage is not a released
    complete installer and always reports installation_ready=false.
13. Historical BSP/main reconciliation and OS build remain outside this range.
    Cost: the current review cannot accept shared hardware effects or merge.

## Independent review and fix pass

One fresh GPT-6 Astra high read-only review covered `a86ef4d8555e..14ce4297980a`.
Two Important findings, no Critical/Minor: failure after rename but before
directory fsync completion could erase published reboot intent; moving a
directory/replacing its lock left the old Store able to overwrite a new journal.
The reviewer independently reproduced both, including the Stage failure/resume.
The first reboot had not been sent: the defect lost monotonic intent and
subsequently permitted submission, rather than proving two reboot commands.

The implementer's four real-file Journal regressions and two actual-I/O Stage
persistence/scope scenarios were RED on both runtimes before the fix. The Store
now retains visible intent after rename, holds directory/lock descriptors,
checks scope identity/owner/modes, and reports unknown effects on journal-scope
failure. The new tests and original Journal18 passed on both; syntax18 passed.
Tests simulate faults after actual rename/fsync and directory/lock replacement.
No second review or physical acceptance is claimed. All declined-to-judge
items are carried as rulings5–13 above; no Minor is deferred.

Regression files: [Journal scope/publication](../../../ember/tools/a133-unlock-journal-regression-test.rb)
and [Stage persistence/scope](../../../ember/tools/a133-unlock-stage-persistence-test.rb).

Final post-fix verification: all12 suites passed on both Ruby4.0.5 and system
2.6.10, with complete outputs read and exit0 for both commands. The matrix
includes the original ten plus Journal regressions4 and Persistence2. Syntax18,
diff whitespace,65 public local links and9 single Origin headers passed. The
fix pass is complete; no second review, hardware or whole-BSP claim is made.

## Verification before final review

On macOS, Ruby 4.0.5 and system Ruby 2.6.10 each passed the ten required suites:
Journal18, Stage23, CLI2, Admission2, Unlock44, Cleanup3, env codec, Recovery19,
Protect14 and Channel13. Both full commands exited0. Syntax14, diff whitespace
and63 changed-page local links passed. Initial Journal/Stage tests were RED
on both runtimes before implementation; CLI late-close and admission prior
effects were separately RED then GREEN. No tablet operation, OS rebuild or
private full-archive reread belongs to this stage.
