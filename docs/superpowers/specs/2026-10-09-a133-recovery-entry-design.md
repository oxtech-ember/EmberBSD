# A133 guarded recovery entry and restoration

The copied one-shot preparer and persistent protection stage lack a guarded
Android-side env write and a known-state recovery handoff. Add that transition
without inventing a generic USB unlock protocol or mutating the working sample.

## Interfaces and trust

Extend backup verification receipts with gpt_sha256 = SHA256(first34 sectors +
last34 sectors), using the bytes already retained by the full stream verifier.
Capture verification carries that field without changing schema1/schema2 files.
This lets the env transition compare live paired GPT to fresh backup evidence.

Source.environment_inspect! stays read-only. In recovery it delegates to the
existing all-block mutable inspection. In Android device state it checks all
readable process/task mount inventories against whole-eMMC/env device IDs,
rejects whole-eMMC/env holders (including unmounted dm aliases), and requires
no active swap. Other Android partition mounts/holders are permitted. Missing,
malformed or unreadable target-usage evidence fails closed. This conservative
no-swap profile may refuse Android images using zram; physical compatibility
and any later narrowly verified exception remain separate work.

A133Recovery::Entry.new(adb:,serial:,cid:,state:,root_method:'vendor_su',
timeout:600) exposes arm(original_env:,backup:) for Android device state and
restore(original_env:,backup:) for recovery state. Both require explicit ASCII
identifiers, fresh trusted in-process Capture receipts with critical/hardware
evidence, full original16MiB env matching its capture hash, and locked1/green.
The caller freshly verifies and retains evidence and exclusively owns operations;
receipts are not authenticated saved JSON. Deadline1..7200 is per ADB subprocess.

## Allowed states and writes

arm derives the existing reversible one-shot prepare prefix from the original
factory env. Live whole env must equal original or exact armed bytes plus the
original tail. Write only131072 bytes of mmcblk0p2 with binary dd and sync;
already-armed state is freshly checked without rewriting. Hash the entire env
before/after, and whole bootloader/recovery partitions plus GPT/profile/usage
at each guard. No unverified persistent loop is armed directly from Android.

restore accepts original, armed, or a known consumed one-shot state. The
consumed state has all and only original variables plus the two helper values,
with hook absent. The prefix must have valid CRC/termination/padding; order,
leading-empty marker and permitted pad byte may differ after vendor saveenv.
Every variable value and all original tail bytes must match. Extra/modified
variables or tail changes fail; unknown saveenv changes are not guessed.
Restore only the original prefix and check the complete original partition.
This enables the locked recovery/Android round trip and a later call to the
existing protection stage with exact original env. It does not execute reboot
or certify the round trip itself.

Pin policy bytes before USB. Refuse wrong identity/state/root/profile/GPT,
changed critical images, usage, unknown env and failed stream/sync/readback.
Errors are redacted A133Usb::Invalid with accurate write_attempted once write
submission begins. No automatic retry/rollback, root restart, reboot, unlock,
GPT/other-partition or boot-area writes. Return env_entry_armed or
env_entry_restored, writes_performed0/1, env_sha256 and installation_ready=false.
Manual exact-state repeats do not add sync or establish cold-power durability.

Host fixtures use the physical fixed31037849600-byte17-partition layout and
actual dd on sparse files, independently encoded original/consumed env. They
cannot prove vendor saveenv encoding, boot execution, cable return, unlock,
filesystem consistency, durability or restoration on a real tablet. Some vendor
boots may change other variables and are intentionally rejected until profiled.
The unopened-device round trip remains a release prerequisite. Unlock and
accepted trial boot are the next stages, not capabilities supplied here.
