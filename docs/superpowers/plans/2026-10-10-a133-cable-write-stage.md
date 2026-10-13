# A133 composed cable write stage implementation plan

> For agentic workers: use superpowers:executing-plans inline and TDD.

**Goal:** Resume unlock → protected recovery → image writes in a single pinned
host context without returning to unlock after protection starts.
**Spec:** ../specs/2026-10-10-a133-cable-write-stage-design.md
**Base:** 7c394ab2f00d009ab46d6b8b033b60701b585d50

## Global constraints

Existing worktree, fixed factory profile and trusted caller evidence; private
host state; no physical writes or feedback, secrets, new Python, OS rebuild,
trial boot or main merge. Preserve other checkouts and wiki work. Fresh scoped
Git before each stage. Keep draft PR and both wikis current.

## Review Focus

- A resume after protection intent must never re-enter unlock or submit reboot.
- Release/evidence context must be pinned before unlock and rechecked before image I/O.
- Fsync/rename/scope failures must preserve visible intent and stop later effects.
- Unknown partial env and live identity/recovery/hardware/usage drift must stop.
- Inspection and error paths must redact private inputs and conservatively report prior effects.

### Task 1: Image Store scope and bundle admission

**Produces:** unchanged Store schema/API with descriptor-bound lock, visible
post-rename state; optional `expected_bundle_sha256` writer admission and
`A133Install.bundle_digest(receipt)` semantic fingerprint.
**Files:** a133-install-session.rb, a133-install-session-regression-test.rb.

- [x] Add real-file fsync/scope regressions and changed expected-bundle rejection.
- [x] Run both Rubies; Expected: regressions FAIL for missing guards.
- [x] Implement minimal guards and canonical fingerprint admission before USB.
- [x] Run regressions and original Session25 both; Expected: all pass.
- [x] Commit fix and record completion.

### Task 2: Outer journal and composed API

**Consumes:** Task1 fingerprint/admission, unlock Stage, Protection and image writer.
**Produces:** A133Cable.run and Journal, inspection-only command, public instructions.
**Files:** a133-cable-journal.rb, a133-cable-install.rb, corresponding tests and
native combined fixture; README.md and ember/boards/ys-m33-a133-cable-stage.md.

- [x] Write journal and native composition/resume/refusal tests first.
- [x] Run both; Expected: missing implementation FAIL.
- [x] Implement explicit monotonic preparation journal and composed guards.
- [x] Run new suites and affected Journal/Stage/Protection/Session/Transfer/Bundle
  suites on both Rubies; Expected: all pass, actual bytes/reboot evidence checked.
- [x] Check syntax, diff and public links; commit implementation/docs.
- [x] One fresh GPT-6 Astra high review of full current plan range; regression-first
  Important/Critical fixes once, record every ruling/minor and final test evidence.
- [x] Update existing draft PR and both wikis; cleanup only this plan scratch.

## Current verification

Both final matrices completed with exit0 and their entire outputs were read:
26 suites on Ruby4.0.5 and26 on system Ruby2.6.10. They include all production
fixes and ran without overlapping native regression jobs. Syntax/Origin15,
diff whitespace and66 public local links also passed. No native OS build.

The26 suites, each run as `ruby ember/tools/<name>`, were:

```text
a133-usb-cleanup-permission-test.rb
a133-bundle-pin-regression-test.rb
a133-cable-stage-guard-test.rb
a133-cable-journal-test.rb
a133-cable-install-cli-test.rb
a133-recovery-protection-unlocked-test.rb
a133-install-session-regression-test.rb
a133-install-session-test.rb
a133-bundle-library-test.rb
a133-install-bundle-test.rb
a133-usb-transfer-test.rb
a133-usb-channel-test.rb
a133-unlock-journal-test.rb
a133-unlock-journal-regression-test.rb
a133-unlock-stage-persistence-test.rb
a133-unlock-stage-cli-test.rb
a133-unlock-stage-admission-test.rb
a133-unlock-env-test.rb
a133-unlock-env-cleanup-test.rb
a133-env-edit-test.rb
a133-recovery-env-test.rb
a133-recovery-protect-test.rb
a133-recovery-protection-test.rb
a133-recovery-protection-regression-test.rb
a133-cable-install-test.rb
a133-unlock-stage-test.rb
```

Host milestone completed. Code fix1c0d362f5904 is published in the existing
draft PR; both knowledge bases are synchronized. The task-done checkpoint
repeated Journal15 and bundle-pin2 on both runtimes after reading the complete
26-suite outputs; the cheap checkpoint does not substitute for those matrices.
Only this plan's completed scratch was removed. The worktree is retained.
Next: accepted first boot/recovery release, then fresh capture integration and
physical cable installation/restoration acceptance. No merge or physical write.

## Execution rulings

1. Continue inline under the user's repeated autonomous-work authorization;
   no additional design/publication question. Cost: design decisions receive
   independent final review but no new manual approval before implementation.
2. Retain an independent outer journal rather than generalizing existing
   storage classes. Cost: duplicated mechanics require keeping regression
   coverage aligned; a later shared primitive remains separate work.
3. Hardware feedback and accepted first boot remain future gates. Cost: a
   green host composition does not prove physical install or fleet readiness.

4. The task-done checkpoint repeats inexpensive regressions after the read
   Session25 outputs. Cost: that checkpoint alone does not replace the original
   suite evidence.
5. Add require_unlocked:true to Protection's repeated guards in this flow;
   preserve its default API. Cost: one additional opt-in constructor contract.
6. Start the single fresh review while the final regression matrices run, after
   initial native/new tests pass. Cost: its verdict alone cannot establish final
   test success; publication waits for both matrices and any required fix pass.

## Independent review

One fresh Codex GPT-6 Astra high read-only review covered the whole current
range7c394ab2f00d..ae09293fd1c8. No Critical; two Important: live full-env/GPT/
hardware/mutable-usage guards are lost at substage handoffs; the standalone
expected bundle String can change while verifying, and false disables it.
Native probes demonstrated all three image writes and success after holders,
hardware or env-tail drift. The SHA probe admitted image I/O/journal creation.
One Minor about public numeric spacing is deferred; no second review.

## Additional rulings and review boundaries

7. Carry pinned safety policy through additive repeated Protection/Client
   guards, preserving standalone defaults. Cost: extra live inspection/full-env
   reads; observations still cannot make the external device atomic.
8. Physical vendor hooks, secure flag and USB return remain a separate gate.
   Cost: fixture execution cannot accept a physical transition.
9. Real host/device power-loss durability remains separate. Cost: cold loss
   can violate assumptions not demonstrated by injected filesystem faults.
10. First boot/recovery release, relock/rollback, full Android restore and
    data preservation remain separate. Cost: write success is not an accepted
    installation or a demonstrated path back to factory user data.
11. Fresh trusted archives, release signing/entropy and personal-data removal
    remain caller/release obligations. Cost: checksum/receipt pins cannot detect
    a forged archive or certify a distributable release.
12. Post-transition boot0/boot1 and early secure contents remain unverified.
    Cost: sizes detect geometry drift, not altered early-boot bytes.
13. Global device exclusion across hosts/directories/standalone callers remains
    external. Cost: callers violating the single outer-directory rule can race.
14. Malicious same-UID journal modification remains unauthenticated. Cost:
    inode/mode/checksum guards do not protect against an authorized hostile owner.
15. External change between observations/submission and remote cancellation
    remain unproved. Cost: sampled guards do not imply atomicity or rollback;
    this does not excuse dropping guards at handoff.
16. The inherited standalone image-session inspection late-close behavior is
    deferred outside the composed CLI. Cost: that older inspection can emit
    progress before a close error; use the composed inspection for this flow.
17. Historical BSP/native OS build/main reconciliation remain separate.
    Cost: this review and host tests cannot authorize merging the entire PR.
18. Verify both final matrices directly; the reviewer did not establish their
    completion. Cost: an unobserved/stale test result is not success evidence.

Deferred Minor: numeric spacing in the new public cable/protection instructions;
keep as a later documentation correction rather than expand this fix pass.

19. Avoid overlapping full matrices with native regression runs. Cost: verification
    takes longer; reduced contention did not fully explain usb_cleanup_failed,
    so do not claim those failed matrices passed or that concurrency is the cause.
    Diagnose exact kill/close/join failure before changing the transport.

20. Retry only local group cleanup EPERM up to3 calls with two10ms pauses.
    Why: native diagnostics isolated EPERM; Darwin killpg filters zombies and
    can return EPERM for an existing group without eligible members (XNU source).
    Cost: up to20ms extra cleanup and an inference about the observed race;
    persistent denial still stops. No ADB/device command is replayed.
    Native transient/persistent fault regressions were RED then GREEN on both.
    Source: https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/kern_sig.c

## Regression-first fix pass

Important1: six handoff cases demonstrated unsafe root writes on both Rubies.
Separate native after_root and valid GPT-change cases demonstrated additional
unsafe image writes on both; the first Ruby4 boundary run was inconclusive due
cleanup failure and was not counted as RED. Pinned additional guards now run
inside Protection and each Client read/write guard, retaining outer scope,
unlocked identity/GPT/hardware, all-task mounts/swaps/holders and full env SHA.
All8 drift regressions passed on both after this fix.

Important2: both mutable expected String and false option admitted a journal
before the fix, on both runtimes. A non-nil SHA is validated/copied/frozen before
bundle I/O; only nil disables admission. Both regressions and existing image
journal regressions5 passed on both after the fix.

The initial23-suite matrices stopped at CableInstall with usb_cleanup_failed
on both; Stage23 was not reached. These are recorded failures, not a full pass.
Instrumentation isolated Process.kill EPERM; no join/timeout cause was shown.
The transient/persistent kernel-boundary regressions were RED then GREEN with
bounded local cleanup retries; Channel13 passed both. Final26-suite matrices
passed with all fixes on both runtimes, without overlapping native regression
jobs. No second review.

21. Restore read-only tablet access by binding SSH's per-call ProxyCommand to
    the Mac sharing bridge, without changing routes/router or restarting the
    tablet. Cost: this local workaround does not repair subnet overlap or accept
    physical installer transitions; private addresses/settings stay out of Git.
