# A133 guarded factory USB reboot transitions

The env entry API prepares/restores storage but intentionally never reboots.
Add the next factory installer bridge: verify already prepared state, submit
one normal ADB reboot, wait for the explicitly selected USB device in the
opposite state and verify it again. Preserve the working reference and defer
unlock, image writes, accepted EmberBSD boot and hardware acceptance.

## Shared evidence and readback

Extract Entry's policy validation into immutable A133Recovery::Policy, copying
explicit ASCII serial/CID, trusted fresh in-process Capture hashes/flags and
the original16MiB env before any USB. Preserve Entry's existing error reasons,
original/armed hashes, exact consumed values and tail verification. Add the
captured boot hash; freeze all publicly accessible nested values. Policy is
private evidence, not an authenticated saved-JSON authorization token. Caller
retains/verifies evidence and owns exclusive device access throughout.

A133Recovery::Readback.new(adb:,policy:,state:,root_method:,timeout:600)
.verify(expected:) accepts original (device/recovery), armed (device only),
consumed (recovery only). It performs no storage write. Pin live source,
locked1/green, serial/CID, paired GPT/layout, root and target-usage guards;
hash whole bootloader/boot/recovery plus full env around checks. Consumed
prefix permits valid reordering/padding only with every exact expected value
and original tail. Other modes require exact whole env hashes. Return
status environment_state_verified, state/expected/env_sha256/hardware_bytes,
writes_performed0, installation_ready=false. Errors are redacted Invalid
with write_attempted=false. Entry's write behavior remains unchanged.

## Reboot interface

A133Recovery::Reboot.new(adb:,serial:,cid:,device_root_method:'vendor_su',
recovery_root_method:'adbd',timeout:600,wait_timeout:120) exposes
enter_recovery(original_env:,backup:) and return_android(original_env:,backup:).
The first requires already armed device env and observes consumed recovery;
the second requires already restored recovery env and observes original
Android. Neither prepares/restores env itself. New source verifiers delimit
the reboot epoch; hardware boot-area sizes must also match before/after.
Only locked1/green is supported. No root acquisition, unlock or alternate
reboot target is inferred. Native argv is exactly -s SERIAL reboot, as used
by the recorded factory procedure. All identity/options/policy bytes are
copied before USB; a caller mutation cannot change later checks.
Launcher paths must be valid, ASCII-compatible encoded strings without NUL;
UTF-8 filenames containing non-ASCII characters remain supported.

After preflight, mark reboot_attempted and submit once through the existing
bounded binary Channel. Any failed command/diagnostic/launch stops; no automatic
retry or rollback. Normal reboot can persist vendor env, so errors also set
write_attempted=true once submission begins, even though this library issues
no raw write. Success includes direct_writes_performed0, reboots_submitted1,
storage_side_effects_possible=true, installation_ready=false and destination.
Reboot::Invalid derives from A133Usb::Invalid and exposes reboot_attempted;
preflight errors have both flagsfalse, later errors bothtrue, cause:nil.

Wait only by bounded devices-l reads. Missing/offline or the original ready
state can persist until a monotonic wait deadline. Require one matching serial,
USB transport and destination state; reject duplicate, unauthorized or other
states. Ignore unrelated devices; never select them. Poll interval at most0.1s,
subprocess timeout bounded by min(configured timeout, ceil(remaining)), so
inventory waiting may exceed its deadline by at most one second plus channel
cleanup. The ready destination's full readback is outside that wait deadline
and uses the per-subprocess timeout1..7200, as do preflight/reboot. wait_timeout
also ranges1..7200; there is no whole-operation hard deadline or infinite wait.

## Proof and remaining gates

Use actual-dd production-geometry sparse fixtures and independent consumed
prefix encoding. A reboot boundary simulates vendor saveenv, offline/old-state
polls, command/late source/transport failures and physical-count markers. Verify
one submission, zero raw writes from the reboot API, whole critical/tail
preservation and before/after possible-side-effect flags. Exercise the complete
Entry.arm → enter → Entry.restore → return sequence on the host. Original
Entry65/regression2 and all relevant previous contracts must remain green on
Ruby4.0.5/system2.6.10. No new Python, credentials or private images in Git.

This cannot prove real reboot execution, vendor export, USB role/cable return,
root availability, no-swap/mount-inventory compatibility, MCU stability,
cold durability or Android filesystem consistency. A ready but incomplete
source fails; no guessed retry of invalid identity/root/profile/readback.
The API does not journal a crash marker or provide a global device lease:
a caller coordinating durable stages must mark possible reboot before calling
and freshly observe the device after interruption. Factory unlock and accepted
trial boot remain later work; all earlier BSP/main reconciliation stays draft.
