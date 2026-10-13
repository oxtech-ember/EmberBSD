# A133 bundle check implementation plan

> For agentic workers: use superpowers:executing-plans for inline execution.

**Goal:** provide a working host-only artifact-manifest check before cable installation.

**Architecture:** one Ruby CLI reads the manifest and hashes three regular files.
Fixed role bounds come from the inspected tablet; success never authorizes flashing.

**Tech stack:** Ruby standard library, JSON, SHA256, temporary sparse test files.

**Spec:** [artifact manifest contract](../specs/2026-10-08-a133-bundle-check-design.md).

## Global constraints

- Schema 1; board `ys-m33-a133`; source commit 40 lowercase hex.
- Exactly boot/resources/root; no duplicate JSON keys or unrecognized fields.
- Boot/resources 33554432 bytes; root positive, 512-byte aligned, at most 27676098048 bytes.
- Manifest regular, non-symlink, at most 65536 bytes; payloads ASCII basenames only.
- No device access, writes, secret/path output or installation authorization.
- New helpers use Ruby, not Python. Preserve all other work in this linked worktree.

## Review focus

- A corrupted byte far from the header must invalidate the complete hash.
- Absolute paths and symlinks must not select a different host file.
- Missing/duplicate roles must not yield a partial success.
- Huge, malformed or duplicate-key manifests must emit bounded failure JSON.
- A self-consistent personalized image must still return installation_ready=false.

### Task 1: checker and contract

**Files:** create `ember/tools/a133-install-bundle.rb` and
`ember/tools/a133-install-bundle-test.rb`; update the cable-install document.

**Interfaces:** consume schema 1 JSON and three host files; produce one JSON
receipt with status, role hashes/bounds, writes_performed=0, installation_ready=false.

- [x] Write real-file tests for success and each review input class, including
  size/type/schema/hash errors, FIFO and symlink refusal and unchanged files.
- [x] Run `ruby ember/tools/a133-install-bundle-test.rb`; expect a failure
  stating that the checker is missing before implementation.
- [x] Implement CLI validation and streaming SHA256 with NOFOLLOW and open-file
  metadata checks, retaining the spec's fixed limits and bounded error output.
- [x] Run the new contract and existing `a133-install-preflight-test.rb`,
  `a133-env-edit-test.rb` and `a133-firstboot-guard-test.sh`; expect exit zero.
- [x] Document the exact command and manifest format, and distinguish integrity
  from authenticity, credential scrubbing, device matching and physical acceptance.
- [x] Review the diff, verify links/whitespace, commit and push the working branch.

## Completion evidence

Task completed by Codex (GPT-6). The real-file contract passed 35 cases on
Ruby 4.0.5 and system Ruby 2.6.10. Existing ADB preflight, environment and
first-boot guard contracts passed. One simultaneous inspector run exceeded
its one-second fixture limit; isolated reruns passed without changing it.
Independent Codex GPT-6 Astra review found the empty-file failure; its
regression failed before the correction and passed afterwards. No other
Critical, Important or Minor findings remained. No physical writes or audio
acceptance were part of this stage. Code is published on the working branch,
not integrated into main; the complete tablet change remains a draft.
