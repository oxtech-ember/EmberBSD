# YS-M33 copied-environment unlock preparation

[Cable installation](ys-m33-a133-cable-install.md) ·
[Locked recovery entry](ys-m33-a133-recovery-entry.md) ·
[Factory USB reboot](ys-m33-a133-recovery-reboot.md)

The [offline preparer](../tools/a133-unlock-env.rb) produces the vendor unlock
hook for the inspected factory profile. It reads a private copied 128KiB env
prefix and creates one new private file. It performs no USB operation, device
write, reboot or unlock, and always reports `installation_ready=false`.

## Exact profile and preservation

The original must have these values and none of the seven reserved hook keys,
including a key with an empty value:

```text
bootcmd=run setargs_mmc boot_normal
boot_normal=run ${hook};run boot_android
boot_android=sunxi_flash read 45000000 boot;bootm 45000000
boot_recovery=sunxi_flash read 45000000 recovery;bootm 45000000
```

The new hook schedules the vendor `pst write fastboot_status_flag unlocked`,
then saves the ordinary one-shot recovery hook and resets. That reset lets the
factory early stages reload the unlock flag. On the next loader pass, the
one-shot hook clears itself and saves normal boot before selecting USB device
mode and loading signed recovery. The normal boot scripts stay unchanged.

The preparer validates CRC32, unique entries, termination and capacity. It
preserves unknown/opaque values, empty variables, leading-empty encoding,
padding and entry order. Removing exactly the added seven variables must
reproduce every original byte. A foreign profile, occupied hook, malformed
image or insufficient capacity is refused without publishing a candidate.

This reversibility applies to the env file. Restoring env does not undo a
vendor secure-storage unlock. Neither the copied hook nor Android unlock
changes the early secure-boot root of trust or authorizes replacing boot0/TOC1.

## Copied-file command and API

Use an operator-owned private directory. Obtain the prefix from the retained,
verified original full env copy for the selected tablet; do not copy the
reference tablet's personal settings into another device.

```sh
ruby ember/tools/a133-unlock-env.rb /private/original-env-prefix.bin /private/unlock-prefix.bin
```

The input must be a non-symlink regular file of exactly 131072 bytes. The
command checks its descriptor metadata around the read. Existing outputs,
dangling output links and occupied `.partial` paths are refused. Publication
uses a new 0600 temporary file, file fsync and a no-overwrite hard link; cleanup
removes only the owned temporary inode. JSON contains symbolic errors and
hashes, without host paths or private env values. It is not a durable install
journal, release signature or proof against a malicious local directory owner.

The final JSON is emitted after temporary cleanup. A cleanup failure returns
`preparation_stopped`, a nonzero exit and `temporary_cleanup_failed=true`, with
no raw filesystem error. `host_files_created` still reports one if publication
already succeeded; that output and the owned temporary file can remain.
Preserve and inspect them before a new invocation, which will refuse occupied
paths. A previous operation error is retained when cleanup also fails.

```ruby
require_relative '../tools/a133-unlock-env'
candidate = A133Unlock.prepare(original_prefix_bytes)
# Keep the frozen binary candidate private. No device is touched by this call.
```

## Integration and acceptance boundary

Before any future device-side use, the coordinator must freshly bind the
serial/CID and full backup/critical/boot0/boot1 evidence, retain quiescent
UDISK/metadata copies, and physically accept the locked recovery/Android
round trip and recoverable protected state. Unknown live env must not be
overwritten. Preparation does not supply those receipts or test vendor command
availability, secure-storage behavior, factory resets or data preservation.

The separate [journaled stage](ys-m33-a133-unlock-stage.md) now provides
guarded prefix writing, durable intent before possible effects, USB return
with unlocked-state verification and a known-state env restore on host fixtures.
Physical integration and the caller's locked round-trip acceptance remain
prerequisites. The locked-only Readback/Reboot API
does not accept this candidate or the resulting orange/unlocked state. Do not
substitute it into those APIs or count this file as unlock acceptance.
Accepted EmberBSD trial boot and complete Android restoration remain separate.

## Host checks

```sh
ruby ember/tools/a133-unlock-env-test.rb
ruby ember/tools/a133-unlock-env-cleanup-test.rb
ruby ember/tools/a133-env-edit-test.rb
ruby ember/tools/a133-recovery-env-test.rb
ruby ember/tools/a133-recovery-protect-test.rb
```

The copied-file contract exercises exact independent bytes, both padding
forms, opaque values, reserved-key collisions, foreign profiles, CRC/encoding
damage, capacity overflow, real CLI files, symlinks/FIFO and no-overwrite
publication. These tests never execute the vendor scripts or access a tablet.
The previously observed physical procedure is recorded in the
[cable-installation receipt](validation/2026-10-08-a133-cable-install.md);
this common preparer requires its own future physical integration acceptance.
The cleanup regression uses real permission failures before/after publication
and at lstat; run it as a non-root user so directory access restrictions apply.
