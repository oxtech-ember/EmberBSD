# A133 journaled vendor unlock stage

Continue the cable-only factory installer, preserving the working reference.
Build host APIs and copied-file tests; this work performs no physical write,
unlock, reboot or full private-archive read. Caller acceptance of a physical
locked recovery/Android round trip remains an explicit precondition.

## Contract

`A133Unlock::Stage.run(directory:, adb:, serial:, cid:, original_env:, backup:,
mutable:, locked_round_trip_verified:, timeout:600, wait_timeout:120,
device_root_method:'vendor_su', recovery_root_method:'adbd')` owns one private
journal lock and verifies fixed 31037849600-byte/17-partition hardware. Fresh,
retained trusted Capture and Mutable receipts belong to the caller; accepting
saved JSON is not a new physical acceptance. The explicit gate must be true.

Copy policy/input strings before USB. Bind serial/CID, full backup/GPT,
bootloader/boot/recovery, original full16MiB env and quiescent UDISK/metadata
hashes. Derive the exact unlock prefix using the existing offline preparer.
Only the original full env, exact armed full env and known consumed export
(all original values plus six fixed helpers, no hook, original tail) are known.
Boot hardware sizes are bound at first observation and on every later epoch;
their contents remain outside readback. Whole critical images are verified.

A fresh session requires locked1/green Android with original env. Persist
possible-write intent before writing only131072 bytes of env plus sync. Verify
the whole env. Persist possible-reboot before submitting one normal ADB reboot.
Wait only for the selected USB recovery, then require unlocked0/orange,
unchanged images/GPT and known consumed env. Restore the original prefix in
that recovery with another durable possible-write marker and full readback.
Successful repeated invocation freshly checks recovery/original without writes.
The next image stage must install its persistent protection separately.

Never replay a recorded possible reboot. Resume an interrupted write only from
an exact original/armed state; unknown bytes stop. A previously armed session
which already reached known unlocked recovery can finish without native reboot.
No automatic relock, rollback, root restart, image write or accepted trial boot.
Unlock can have persistent effects independent of env restoration.

## Journal and errors

Use one operator-owned0700 directory per tablet/context,0600 single-linked
regular lock/state, nonblocking lock held throughout. Bound snapshots to65536
bytes; exact schema, duplicate-key rejection and canonical checksum. Publish
via exclusive random temporary file, fsync, atomic rename and directory fsync.
Unknown crash leftovers are retained. Context changes and corrupted snapshots
stop before USB. This is no global lease or authentication against its owner.

Errors carry redacted reports with persistent possible-write/reboot flags.
Markers are monotonic; failed result persistence cannot permit later steps.
Uninspectable journal effects are conservatively unknown/possible. Per-command
timeout and inventory wait are1..7200 seconds; no total-operation deadline.
CLI only inspects an existing locked journal. Installation_ready is always false.

## Proof boundary

Independent fixtures use real dd, production-size sparse files and native
processes. Exercise order, restart ambiguity, failures around markers, identity,
boot/env/tail/usage drift, locks, malformed journal and private-path redaction.
Physical vendor behavior/data preservation, cold durability, coherent Android
restore, release signing and the full historical BSP remain separate gates.
