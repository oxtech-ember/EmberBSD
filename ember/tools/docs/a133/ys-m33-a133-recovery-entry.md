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
exact prepared one-shot env with the original tail. The policy selects one
of two exact factory profiles. The hook profile retains model `a133` and its
existing clear/save hook. The direct profile requires `QUAD-CORE A133 c3`,
`bootcmd=run setargs_nand boot_normal`, the direct factory boot/recovery scripts,
no `boot_android`, and a retained nonempty `setargs_nand`.

For the direct profile, `entry_plan` changes only bootcmd and adds four helpers.
The proposed bootcmd is one `run` list:

```text
run ember_entry_restore ember_entry_save setargs_nand ember_entry_fdt ember_entry_usb boot_recovery
```

The first two single-command helpers restore the original bootcmd in RAM and
save it before the later steps select FDT, USB device role and factory recovery.
Normal bootcmd, boot_normal and setargs_nand retain their exact original values
in the predicted consumed export; only four unused helper values remain.
Reversing the host edits must restore the original prefix byte for byte.
Existing helper keys, including empty values, are refused.

`restore` requires `recovery` state and also accepts the exact profile-specific
consumed values. The hook profile expects its hook absent and two helpers.
The direct profile expects original bootcmd and four unused helpers.
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

`Entry.disarm(original_env:, backup:)` cancels entry from `device` state.
It accepts only original, armed, or the exact known consumed env and restores
the original prefix with the same full-env/tail, identity, usage and critical
partition checks. An already original env performs no write. Unknown or partial
states stop without adoption. The result is `env_entry_disarmed`, not a reboot
or recovery receipt. Recovery state still uses `restore`; no automatic cancellation
or rollback follows a failed operation.

The only write is131072 bytes at the beginning of `/dev/block/mmcblk0p2`, using
binary non-PTY shell-v2 `dd ... conv=notrunc` followed by `sync`. Exact-state
repeats verify without rewriting. They do not repeat a failed sync and cannot
establish cold-power durability. There is no automatic retry, rollback,
reboot, unlock, GPT write, other-partition write or hardware boot-area write.

Successful receipts have `status: 'env_entry_armed'`, `'env_entry_restored'`, or `'env_entry_disarmed'`,
`env_sha256`, `writes_performed: 0/1`, and `installation_ready: false`.
Failures are redacted `A133Usb::Invalid` with `write_attempted: false` before
submission or `true` once submission starts. A possible partial write remains
an explicit stop; retain evidence and diagnose the observed state separately.

## Validation and remaining gates

Run `ruby ember/tools/a133-recovery-direct-test.rb` and
`ruby ember/tools/a133-recovery-direct-usb-test.rb` with the existing entry,
readback and reboot suites. The direct plan uses the multi-variable `run`
failure contract in [U-Boot 2018.05](https://github.com/u-boot/u-boot/blob/v2018.05/common/cli.c#L113)
and the [command-line documentation](https://docs.u-boot.org/en/latest/usage/cmdline.html#general-rules).
These sources do not establish behavior of a vendor build.

Only Entry/Readback and their Reboot caller select the direct source.
The ordinary mutation source and unlock/protect/trial profiles are unchanged.
Calling `prepare`, `protect` or `trial` directly still refuses this direct env.

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
