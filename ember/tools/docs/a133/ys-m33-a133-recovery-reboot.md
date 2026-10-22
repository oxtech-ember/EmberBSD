# YS-M33 guarded factory USB reboot

This host-side Ruby API bridges an already prepared one-shot factory env to
signed recovery, and an already restored env back to factory Android. It
submits one normal `adb -s SERIAL reboot`, observes the selected USB device
and performs a fresh whole-state readback. It never prepares/restores env,
unlocks, writes an OS image or accepts an EmberBSD trial boot.

## Evidence and use

Retain a fresh trusted in-process [capture result](ys-m33-a133-backup-recovery.md),
its full main-eMMC archive, independent critical/hardware copies and original
full16MiB env. Saved JSON is not an authorization token. The coordinator owns
exclusive device access and freshly verifies retained evidence before each
stage. Keep paths, identifiers, credentials and images private.
Launcher paths require valid ASCII-compatible encoding and no NUL; non-ASCII
UTF-8 filenames are supported.

```ruby
require_relative '../tools/a133-capture-check'
require_relative '../tools/a133-recovery-entry'
require_relative '../tools/a133-recovery-reboot'

backup = A133Capture.verify(private_capture_path, serial: serial, cid: cid)
original_env = File.binread(private_original_env_path)
entry = A133Recovery::Entry.new(adb: adb_path, serial: serial, cid: cid,
  state: 'device', root_method: 'vendor_su')
entry.arm(original_env: original_env, backup: backup)

backup = A133Capture.verify(private_capture_path, serial: serial, cid: cid)
reboot = A133Recovery::Reboot.new(adb: adb_path, serial: serial, cid: cid,
  device_root_method: 'vendor_su', recovery_root_method: 'adbd',
  timeout: 600, wait_timeout: 120)
# Durably mark a possible reboot BEFORE calling; this library is not a journal.
entered = reboot.enter_recovery(original_env: original_env, backup: backup)

backup = A133Capture.verify(private_capture_path, serial: serial, cid: cid)
recovery = A133Recovery::Entry.new(adb: adb_path, serial: serial, cid: cid,
  state: 'recovery', root_method: 'adbd')
recovery.restore(original_env: original_env, backup: backup)
backup = A133Capture.verify(private_capture_path, serial: serial, cid: cid)
# Mark the return reboot in the same durable journal before calling.
returned = reboot.return_android(original_env: original_env, backup: backup)
```

Both root methods mean already-root-readable access. No `adb root`, alternate
reboot target, automatic unmount, swap shutdown, unlock, retry or rollback is
performed. This is the locked=`1`, verified=`green` factory round trip before
unlock. After writing a different boot image, use a separately accepted trial
boot stage: `return_android` requires the original captured factory boot.

## Readback and transition boundaries

An immutable `A133Recovery::Policy` copies the explicit ASCII serial/CID,
trusted capture hashes/flags and original env before the first USB operation.
It is shared across the offline epoch. Every source must match the fixed
31,037,849,600-byte17-partition layout, paired GPT, board, root and lock state.
Whole bootloader/boot/recovery and full env are read before and after reboot;
hardware boot-area sizes must match across the transition.

`enter_recovery` requires the exact armed env in `device` state and verifies
the known consumed env in `recovery`. The consumed export may reorder entries
or valid padding only: every expected value and the original tail must match.
`return_android` requires and verifies the exact original full env, first in
`recovery`, then in `device`. Target usage is checked around reads. Android
permits other mounted partitions but rejects whole-eMMC/env mounts or holders,
unavailable inventories and any active swap. Recovery requires all eMMC block
devices unmounted and available holder directories for every expected partition.

`A133Recovery::Readback.new(adb:, policy:, state:, root_method:, timeout: 600)`
also exposes `.verify(expected:)`: `original` in either supported state,
`armed` only in device, `consumed` only in recovery.
The expected mode is copied and frozen before admission/USB; later caller
mutation cannot change the verified state or the receipt's expected mode.
Its receipt contains
`environment_state_verified`, state/expected/env_sha256/hardware_bytes,
`writes_performed: 0` and `installation_ready: false`. Readback errors are
redacted `A133Usb::Invalid` with `write_attempted: false`.

After one native reboot, inventory waiting selects only the explicit serial.
Unrelated devices are ignored. Missing/offline or the original ready state can
persist until the monotonic deadline. A destination row requires USB transport;
duplicates, TCP, unauthorized and other states stop immediately. A ready but
invalid destination fails readback; its checks are never retried speculatively.

`timeout` and `wait_timeout` are integer seconds1..7200. The former bounds each
ADB command. The latter bounds inventory observation, with polls at most0.1s
apart and a subprocess timeout at most ceil(remaining); it can overshoot by
one second plus bounded channel cleanup. Full destination readback lies outside
the inventory deadline. There is no whole-operation hard deadline.

## Receipts, failures and acceptance

Success returns `factory_recovery_verified` or `factory_android_verified`,
`direct_writes_performed: 0`, `reboots_submitted: 1`,
`storage_side_effects_possible: true`, `installation_ready: false` and the
destination readback. Normal reboot may cause vendor `saveenv` to persist
storage changes, even though this API issues no raw write.

Failures are `A133Recovery::Reboot::Invalid < A133Usb::Invalid`. Both
`reboot_attempted` and `write_attempted` are false before submission and true
once submission begins, including launcher failure or a command failing after
scheduling reboot. Causes/private diagnostics are discarded. Preserve a
possible reboot/write marker and freshly observe the actual device after
interruption. A local subprocess exit does not prove remote cancellation.
The API supplies no durable journal or global device lease.

Actual-process sparse fixtures independently encode vendor exports, simulate
one native reboot and exercise the complete Entry arm/restore round trip,
offline/source-state polling, changed critical bytes/identity/GPT/transport,
root/usage failures, timeout, caller mutation and private launcher errors.
Run `ruby ember/tools/a133-recovery-readback-test.rb` and
`ruby ember/tools/a133-recovery-reboot-test.rb` alongside the prior Entry suites.

This host contract does not accept real vendor env export, cable return, root
availability, no-swap/mount-inventory compatibility, MCU stability, cold-power
durability, coherent Android filesystems or physical restoration. Preserve the
working reference; another unopened-device round trip remains a hardware gate.
Factory unlock, image release/provisioning and accepted first EmberBSD boot
remain separate integration stages.
