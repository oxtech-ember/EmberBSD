# YS-M33 persistent recovery protection

The guarded image writer requires a persistent recovery env. The
[protection primitive](../tools/a133-recovery-protection.rb) prepares it from
the retained original env partition, writes only its128KiB prefix and verifies
the entire16MiB partition. It is a host-tested library stage, not a complete
installer or a proven bootloader recovery path.

## Prerequisites and trust

Use only the fixed31,037,849,600-byte17-partition A133 profile. The selected
tablet must already be in root-readable signed USB recovery: adbd or the
existing vendor su wrapper, non-PTY shell_v2, locked1/green or unlocked0/orange.
No automatic root restart, recovery entry, unlock or reboot is implemented.

The caller must exclusively own all device operations during the invocation.
This library does not lease the device against another process or host. Run
[A133Capture.verify](ys-m33-a133-backup-recovery.md#bind-a-trusted-capture-record-and-critical-copies)
freshly for this trusted serial/CID and retain the original full snapshot,
critical copies and schema2 hardware boot0/1 copies. Freshly run
[A133Mutable.verify](ys-m33-a133-mutable-backup.md) for the same identity and
retain UDISK/metadata evidence. Supply those private in-process receipts;
reading a saved success JSON does not satisfy this precondition. A future
coordinator must enforce retained host evidence through use: this primitive
does not reopen/hash the31GB backup or maintain its descriptors.

The original_env argument is the full16MiB verified original env copy,
not just a128KiB prefix. Its size and complete SHA must match the capture
receipt. Caller-owned env and relevant hash strings are copied before USB I/O.
No public receipt, device identifiers or private env values should be logged.

## Library operation

```ruby
require_relative '../tools/a133-recovery-protection'

# These values belong to one exclusive coordinator invocation; obtain
# backup/mutable by the fresh verifiers above, not by parsing saved receipts.
protection = A133Recovery::Protection.new(
  adb: adb_executable, serial: trusted_serial, cid: trusted_cid,
  root_method: 'adbd', timeout: 600
)
result = protection.install(
  original_env: original_env_bytes, backup: fresh_capture_receipt,
  mutable: fresh_mutable_receipt
)
# Keep result[:protected_env] private for the existing guarded image writer.
```

The composed cable stage passes `require_unlocked: true` to the constructor.
That opt-in mode requires0/orange at every protection guard; the default
continues to support both locked1/green and unlocked0/orange recovery.
Non-boolean values are refused before transport observation.
An optional `additional_guard` adds the composed pinned policy to every
Protection guard, before/after the transition. It must return true; existing
identity, recovery and env checks remain mandatory. The composed flow uses
this to retain outer hardware/GPT/full-env and usage guards across handoff.

Each ADB subprocess has the configured timeout1..7200 seconds. Source
inspection pins USB serial, eMMC CID, root/state, hardware sizes, GPT and
layout. Every checkpoint inspects all readable process/task mount inventories,
active swaps and eMMC holders. The live GPT digest must match mutable evidence.
The full original recovery partition hash is checked before and after env
transition. Concurrent transient use between observations is not ruled out.

`A133Recovery.protect` accepts the known factory profile, rejects existing hook
keys even when empty, preserves bootcmd and unrelated/opaque values, changes
only boot_normal to `run ember_recovery_once` and adds the existing helper.
Reversing those two changes must restore the original bytes, including padding.

The live full env must equal either the original partition hash or the derived
protected partition hash. An original state receives exactly131072 binary
bytes through dd to mmcblk0p2 followed by sync. Full env readback must match
protected prefix plus original tail. An already-protected state receives no
write and still undergoes fresh env/recovery/source checks. An unrelated,
partly-written or foreign env is rejected, including an identical protected
prefix with a different tail. No other partition or hardware boot area is
written. There is no automatic retry, rollback or adoption of unknown state.
If a manual retry follows a failed sync but the complete protected env matches,
it still performs no write or extra sync. Readback alone does not establish
durability across a subsequent power loss.

Success returns recovery_protection_verified, env_sha256, private protected_env,
writes_performed0 or1 and installation_ready=false. A133Usb::Invalid carries
write_attempted=false before submission, true once sending the write has begun.
A failed transport/sync/readback or late drift may leave changed storage;
stop later stages and retain the original evidence for controlled recovery.
System I/O failures are normalized without private host paths and retain the
possible-write flag. Caller identifier/hash strings must be ASCII; a malformed
UTF-8 argument can still raise a generic ArgumentError before USB submission.
That minor diagnostics limitation is deferred.

## Validation and limits

```sh
ruby ember/tools/a133-recovery-protect-test.rb
ruby ember/tools/a133-recovery-protection-test.rb
ruby ember/tools/a133-recovery-protection-regression-test.rb
```

Tests encode factory env independently and run actual dd on sparse
fixed-layout GPT/partition files. They cover both permitted lock states and
root wrappers, exact prefix/tail preservation, idempotent rechecks, policy and
identity mismatch, thread mounts, swap/holders, short/noisy reads, partial
write, sync failure, readback corruption and post-write source drift.

No physical env transition was performed for this stage. Matching recovery
readback does not prove U-Boot consumes the env, cable return, cold boot,
restore durability, first-boot acceptance or power-loss atomicity. A damaged
CRC after interrupted prefix writing may revert to factory defaults. Physical
locked recovery round trip and protected reset must pass before unlocking an
unopened deployment device. The full installer still needs entry/unlock,
release/credential provisioning and accepted trial boot around the existing
guarded image session. Do not release this primitive as fleet readiness.
