# A133 cable installer artifact manifest check

Origin: EmberBSD - design by Codex (GPT-6).

## Purpose

Provide the offline artifact-integrity stage of the future cable installer.
An operator supplies a directory containing a JSON manifest and three images.
The command reads only host files; it neither discovers nor writes devices.
The user requested autonomous fleet-installation preparation without hardware
acceptance questions. This stage does not constitute an installer release.

## Contract

`ruby ember/tools/a133-install-bundle.rb MANIFEST.json` emits one JSON result.
Exit zero means the manifest and complete file hashes match this contract.
Every result has `writes_performed: 0` and `installation_ready: false`.
Success has `status: artifact_manifest_verified`; failure has a bounded reason
and `status: inspection_stopped`. Do not print file paths or exception text.

The manifest has exactly `schema`, `board`, `source_commit`, and `artifacts`.
Schema is integer 1, board is `ys-m33-a133`, source commit is 40 lowercase hex.
The three artifacts have exactly `role`, `file`, `bytes`, and `sha256`.
Roles are unique: `boot`, `resources`, and `root`. File names are unique ASCII
basenames, 1 to 128 characters, starting with an alphanumeric character and
continuing with alphanumeric, dot, underscore or hyphen. Paths are never
interpreted as commands. SHA256 is 64 lowercase hex characters.

Boot and resources must be exactly 33554432 bytes. Root must be positive,
sector-aligned and at most 27676098048 bytes, the inspected UDISK size.
The fixed reference mapping is boot at sector 172032, resources at 73728,
and UDISK at 6565888. This maps roles in the report; it does not authorize
these destinations on any connected device. A later device inspector must
match that particular tablet and complete GPT independently.

Manifest input is a regular non-symlink file, at most 65536 bytes. Reject
duplicate JSON keys, unknown fields, bad types and missing/extra roles.
All payloads must be regular non-symlink files beside the manifest; read
them with NOFOLLOW, bounded chunks, and compare open-file metadata before
and after hashing. Refuse changed files, lengths and digests. No files are
created or modified, including on failure.

## Trust boundary

The manifest is untrusted and unsigned. Its matching hash does not establish
publisher authenticity, supported hardware, image filesystem validity,
absence of credentials, seed quality, recovery availability or acceptance.
Personalized images may match a manifest; they must never be called a fleet
release because of this result. These checks remain separate installer stages.
A future writer must revalidate the bytes it actually writes; this checker
does not prevent changes after it exits. Symlink checks protect accidental
selection, not a hostile concurrent process with write access to the bundle.

## Verification

Host-only tests use real temporary sparse files and the real command.
Exercise valid input, full-content corruption, wrong lengths, unknown roles,
path escape, symlinks, malformed/duplicate-key JSON, wrong types and excess
manifest size. Verify exit status, bounded JSON and unchanged fixture files.
Run the existing A133 preflight, environment and first-boot guard contracts.
No microphone, speaker, camera or storage-device access is part of these tests.
