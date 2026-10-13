# A133 capture binding implementation plan

> **For agentic workers:** Use superpowers:executing-plans inline task-by-task.

**Goal:** freshly verify full backup and critical copies against a trusted capture identity.
**Architecture:** expose the current file checker; consume it in a private manifest verifier.
**Tech Stack:** Ruby standard library, existing GPT checker and decoder process group.
**Spec:** [capture binding design](../specs/2026-10-09-a133-capture-binding-design.md).

## Global Constraints

- Production size31037849600/17-partition inventory stays fixed; no test mode.
- Private directory0700/files0600; bounded65536-byte manifest and exact schema.
- Recorded serial/CID/SHA are trusted inputs; no inference for a missing CID.
- Fresh full-image read and four full critical-copy hashes per invocation.
- No hardware writes, reboot, audio/camera tests or installation_ready=true.
- Preserve existing linked worktree, private images and caller firstboot obligations.

## Review Focus

- Callable checker and CLI must share guards and decoder cleanup, including outer rescue.
- Success cannot come from a cached JSON receipt or a wrong-device capture record.
- Links, unsafe modes, duplicate keys or paths cannot bypass same-directory policy.
- Mutation/deadline after initial full-image verification must not return success.
- Private receipt never leaks through CLI/errors; no implied consistency/provenance claim.

### Task 1: callable full-backup file verification

**Files:** modify `ember/tools/a133-backup-check.rb`, extract fixture-only support
from `a133-backup-check-test.rb`; add `a133-backup-library-test.rb`.
**Interfaces:** produces `A133Backup.verify_file(path, sha256:, format: 'zstd',
zstd: 'zstd', timeout: 3600)` with the current symbol-keyed CLI receipt.

- [x] Write tests for silent import, real raw/decoded file verification, hash/size,
  source mutation, decoder failure/timeout and invocation from outer rescue.
- [x] Run `ruby ember/tools/a133-backup-library-test.rb`; Expected: missing API RED.
- [x] Extract current fixed-profile file check and normalized reasons; make CLI consume it.
- [x] Run library and backup29 on Ruby4/system2.6; Expected: all cases pass; commit.

### Task 2: trusted capture binding and critical copies

**Files:** add `ember/tools/a133-capture-check.rb`, `a133-capture-check-test.rb`;
update backup/cable instructions and affected overview claims.
**Interfaces:** consumes Task1 verifier. Produces `A133Capture.verify(path,
serial:, cid:, zstd: 'zstd', timeout: 3600)` returning string-keyed USB backup
receipt with recorded serial/CID; CLI redacts those identifiers.

- [x] Write real-file tests for a four-partition independent GPT/copy capture,
  identity mismatch, critical copy hash/size, changed backup after prior success,
  private modes/links, JSON schema/duplicates/path/size, and redacted CLI.
- [x] Run `ruby ember/tools/a133-capture-check-test.rb`; Expected: missing tool RED.
- [x] Implement exact record/file policy, fresh backup verification and four-copy hashes.
- [x] Run capture/library/backup29 on both Ruby versions and USB/session/environment
  contracts on Ruby4; Expected: all pass; commit.
- [x] Independent review, required RED/GREEN fixes and documentation links.

## Completion evidence and rulings

Initial commits `8377ea0e3467`, `776e63dfeab3`; no kernel/userland change or
native OS rebuild. Library14, capture37, review-regression6 and backup29 passed
on Ruby4.0.5/system2.6.10. USB43/session25/environment contracts passed after
the fix pass. Independent GPT-6 Astra review examined the pre-fix range,
independently running library14/capture37 and reproducing three Important
defects. All six regression cases failed on both Rubies before the fixes.
Critical/Minor findings: none. Fixes were verified by the implementer.

Recorded decisions and practical limits:

- Continue inline under explicit autonomous authorization; no repeated design
  approvals. Existing linked worktree preserved. Cost: scope review occurs in
  these saved artifacts instead of another human gate.
- Small geometry is test-process-only. Fixed production profile remains checked
  in source; no repeat of the prior private31GB/zstd acceptance. Cost: full-size
  integration of the additions remains an acceptance step.
- Keep six descriptors through return and compare actual current size before
  hashing. Cost of a detected change: stop the capture, never a device action.
- Provenance, live identity, Android consistency, quiescent/hardware copies,
  recovery/unlock/release and first boot are caller/hardware obligations.
  Cost: an offline result cannot release a tablet into normal boot.
- Trusted local owner and parent directories remain assumptions; changes after
  return cannot be excluded. Cost: no cached receipt as permanent write proof.
- Decoder/schema/CLI and existing GPT/USB behavior retain relevant host checks;
  hardware/native/full BSP integration remains draft. Cost: physical readiness
  is unclaimed. CLI strips private fields; callers must not log their own inputs.
- The supplied decoder is trusted caller configuration. Cost: it runs with
  the local user's file privileges.

Publication stays on the existing draft PR; no main merge or physical write.
