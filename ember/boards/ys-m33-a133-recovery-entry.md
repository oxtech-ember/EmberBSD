# YS-M33 A133 guarded recovery entry

This host-side Ruby library guards the one-shot factory env prefix write from
root-readable Android and restores the original prefix in locked factory
recovery. It closes a transition gap before
[persistent recovery protection](ys-m33-a133-recovery-protection.md). It does
not reboot, unlock, install an OS or establish installation readiness.

## Evidence and usage

Use a fresh in-process [capture verifier](ys-m33-a133-backup-recovery.md) result
from the same explicit serial/CID, retaining the full archive, separate critical
copies and hardware boot-area copies. The verifier now returns `gpt_sha256` for
the first34 and last34 sectors of the full image. An old saved receipt is not
an authorization token: the coordinator must freshly verify evidence and own
exclusive device access for the whole operation.

```ruby
require_relative '../tools/a133-capture-check'
require_relative '../tools/a133-recovery-entry'

backup = A133Capture.verify(private_capture_path, serial: serial, cid: cid)
original_env = File.binread(private_original_env_path) # full16MiB captured env
entry = A133Recovery::Entry.new(adb: adb_path, serial: serial, cid: cid,
  state: 'device', root_method: 'vendor_su', timeout: 600)
armed = entry.arm(original_env: original_env, backup: backup)
# The coordinator separately arranges and observes a controlled recovery boot.
# Freshly verify the retained capture again before the next operation.
backup = A133Capture.verify(private_capture_path, serial: serial, cid: cid)
recovery = A133Recovery::Entry.new(adb: adb_path, serial: serial, cid: cid,
  state: 'recovery', root_method: 'adbd', timeout: 600)
restored = recovery.restore(original_env: original_env, backup: backup)
```

Both operations require locked=`1`, verified=`green`, the fixed31,037,849,600-byte
17-partition layout, supported factory env profile, paired GPT matching the
backup, matching whole bootloader/recovery hashes and full original16MiB env
matching its capture. `adbd` and `vendor_su` mean already-root-readable transport;
this library never restarts adbd or grants root. Deadline1..7200 seconds applies
to each ADB subprocess, not the whole workflow. Serial/CID must be explicit
ASCII identifiers. Keep paths, env bytes and credentials private.

## States and write boundary

`arm` requires `device` state and accepts only the complete original env or the
exact prepared one-shot env with the original tail. It sets the existing hook
that first clears/saves itself, then selects USB peripheral role and factory
recovery. `restore` requires `recovery` state and also accepts the known consumed
hook state: all original variables plus the two exact helper values, hook absent.
Valid CRC, termination and padding are mandatory; vendor export ordering,
leading-empty marker and zero/FF padding may differ. Extra or modified variables,
a changed tail, or an unrecognized export stop restoration without guessing.

Every operation checks usage and profile around streamed reads, verifies whole
bootloader/recovery partitions before and after, and checks the complete env
readback. Android may keep other partitions mounted. Whole-eMMC/env mounts in
any readable process/task namespace, whole-eMMC/env holders (including dm
aliases), incomplete usage evidence, and any active swap stop entry. This
conservative profile can refuse factory Android using zram. Recovery retains
the stricter guard against any mounted eMMC block partition, holder or swap.
It requires holder directories for the whole eMMC and every expected partition
to exist and be readable/traversable before enumeration.
No automatic unmount or swap shutdown is performed.

The only write is131072 bytes at the beginning of `/dev/block/mmcblk0p2`, using
binary non-PTY shell-v2 `dd ... conv=notrunc` followed by `sync`. Exact-state
repeats verify without rewriting. They do not repeat a failed sync and cannot
establish cold-power durability. There is no automatic retry, rollback,
reboot, unlock, GPT write, other-partition write or hardware boot-area write.

Successful receipts have `status: 'env_entry_armed'` or `'env_entry_restored'`,
`env_sha256`, `writes_performed: 0/1`, and `installation_ready: false`.
Failures are redacted `A133Usb::Invalid` with `write_attempted: false` before
submission or `true` once submission starts. A possible partial write remains
an explicit stop; retain evidence and diagnose the observed state separately.

## Validation and remaining gates

Host suites independently encode factory/consumed env and use actual `dd` on
sparse files with the production geometry. They exercise identity/GPT binding,
variable reordering, unexpected changes, usage aliases, partial writes, failed
sync, source drift, caller mutation and private transport/system errors.

They do not prove real vendor `saveenv` compatibility, hook execution, cable
return, filesystem consistency, cold-power durability or physical restoration.
A vendor boot changing another env variable is intentionally refused until its
behavior is measured. A new unopened-device locked recovery/Android round trip
remains required. A supported USB unlock interface and accepted trial boot are
separate later stages; this library supplies neither. Preserve the working
reference tablet until those gates are ready.
