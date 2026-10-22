# A133 installer preflight and artifact manifest

These host tools inspect a selected source and verify release bytes.
They do not implement an accepted end-to-end installer.
Read the [hardware and cable-installation profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/ys-m33-a133/boot.md) before device work.

## Read-only preflight

The host needs Ruby and Android platform-tools.

```sh
ruby ember/tools/a133-install-preflight.rb --adb /absolute/path/to/adb
# With multiple Android devices, explicitly select the intended USB device:
ruby ember/tools/a133-install-preflight.rb --adb /absolute/path/to/adb --serial DEVICE
# For a factory image with the inspected vendor su implementation:
ruby ember/tools/a133-install-preflight.rb --adb /absolute/path/to/adb --su
ruby ember/tools/a133-install-preflight-test.rb
```

The inspector issues only bounded ADB reads. It never restarts adbd as root,
unlocks, reboots, transfers files or writes storage. It requires one selected
ready USB ADB device, root read access, `allwinner,a133` in the device tree
and the inspected boot target/bounds. JSON does not print the device serial.
Missing access, multiple devices, foreign boards, wrong targets, timeouts
and command errors stop inspection. The fixture contract executes the real
tool against an ADB boundary rejecting all other commands.

`inspection_complete` describes a candidate only. The result always states
`installation_ready: false`. Exact board identification, complete partition
inventory, per-device backups, unlock/recovery and image validation precede
an installer's first write. Non-root factory adbd may require `adb root` or
vendor `su`; neither capability is inferred from an eng build label.
The explicit `--su` mode prefixes the same read commands with
`/system/xbin/su 0` and still verifies UID 0. It does not restart adbd.

## Artifact bundle

[a133-install-bundle.rb](../../a133-install-bundle.rb) reads a manifest and
checks complete SHA256 hashes of the three host files before device work:

```sh
ruby ember/tools/a133-install-bundle.rb /private/bundle/manifest.json
ruby ember/tools/a133-install-bundle-test.rb
```

The manifest has exactly `schema` (integer 1), `board` (`ys-m33-a133`),
`source_commit` (40 lowercase hexadecimal characters), and `artifacts`.
The array contains exactly these unique roles:

| Role | Reference start sector | File length |
| --- | --- | --- |
| boot | 172032 | 33554432 bytes |
| resources | 73728 | 33554432 bytes |
| root | 6565888 | Positive multiple of 512 bytes, at most 27676098048 |

Each entry has exactly `role`, `file`, `bytes` (integer), and `sha256`
(64 lowercase hexadecimal characters). `file` is a unique ASCII basename
beside the manifest, at most 128 characters, starting with an alphanumeric
character; the remaining characters may also be dot, underscore or hyphen.
The manifest is at most 65536 bytes. Symlinks, non-regular files, duplicate
JSON keys, unknown fields, incorrect lengths and digests are rejected.
Payloads are hashed in bounded chunks through non-following open descriptors.

Exit zero reports `artifact_manifest_verified`, `writes_performed: 0`, and
`installation_ready: false`. This is an **unsigned integrity receipt**:
it does not authenticate a publisher, inspect credentials/entropy inside FFS,
validate image filesystems, match a connected tablet, or authorize writes.
The role mapping is a reference, not discovery of a device's GPT.
A future writer must revalidate the bytes it actually writes and perform
independent device, backup, recovery and acceptance checks. A matching
personalized image must not become a fleet release because of this receipt.

The real-file contract checks complete-image corruption, bounds, schema/type errors, duplicate
keys, path escape, symlinks, FIFO refusal and unchanged bundle files.
This does not test physical cable writes or power-loss recovery.


## Integration

Use the [composed cable stage](ys-m33-a133-cable-stage.md) for the caller-owned state machine and the [trial preparer](ys-m33-a133-trial-env.md) for copied environment bytes.
Device identity, trusted full backups, recovery, release authentication and accepted first boot are separate requirements.
