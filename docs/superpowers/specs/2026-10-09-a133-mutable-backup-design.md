# A133 recovery mutable-data backup

## Intent and scope

Make USB-only fleet installation retain complete per-device UDISK and metadata
copies before either partition is overwritten. The user authorized continuing
this plan autonomously, preserving the working reference and leaving audible
and interactive hardware tests for their return. This stage reads only; it
neither enters recovery nor unlocks, writes, reboots or restores a tablet.

The existing full acquisition can run in live Android and deliberately does
not claim filesystem consistency. Its schema2 captures full eMMC, four critical
partitions and boot0/boot1. Keep that contract unchanged. A separate recovery
acquisition adds mutable-data evidence; it does not rewrite the original
capture or compare mutable files against an older live-Android snapshot.

## Choice and evidence

Use a separate private directory and manifest, reusing the strict USB source
and exclusive host publication. Extending full capture would force another
31 GB read and conflate different capture times. An installer coordinator that
just trusts cached hashes would not establish the retained bytes.

Require explicit trusted serial/CID and root-readable USB recovery. Validate
both GPTs, all physical partition bounds, hardware boot sizes and the source
profile at each checkpoint. Observe all readable process/thread mount inventories,
refuse any mounted block device, nonempty swap inventory or eMMC holder.
Fail if the inventory cannot be read. Capture both entire partitions, then
read both again and require their complete hashes to match the captured hashes.
A late source-profile/mount/swap/holder change prevents successful publication.

These are sampled observations, not a kernel snapshot or cryptographic
attestation. A recovery can have unobserved raw writers or changes between
checks. A raw change after UDISK's second read, during metadata's second read,
can still yield a successful sampled-evidence receipt. Describe the result as
recovery_unmounted_two_matching_reads;
filesystem_consistency remains not_established_by_integrity_check and
installation_ready=false. Physical quiescence and Android restoration remain
acceptance work. Do not turn a manifest's existence into a success receipt.

## Host contract

A133Mutable.verify(path, serial:, cid:, timeout:3600) freshly hashes two private
files and returns a private string-keyed receipt. Exact schema1 fields are
schema, board, serial, cid, gpt_sha256, root_method, capture_state, observation,
partitions. board=ys-m33-a133, capture_state=recovery, root_method=adbd/vendor_su,
observation=recovery_unmounted_two_matching_reads. partitions contains exactly
UDISK and metadata objects with role/file/bytes/sha256. Sizes match the fixed
17-partition physical profile: UDISK27676098048, metadata16777216 bytes.

Files and manifest are owned by the caller, regular, single-linked, mode0600;
parent/destination mode0700. Hold distinct descriptors and directory identity
through verification/publication. Refuse duplicate JSON keys, path traversal,
case/inode aliases, unexpected fields, wrong lengths/hashes and late mutation.
Manifest size <=65536 bytes. Inputs remain private; CLI output/errors omit
identifiers, hashes, contents and paths. Deadline1..7200 seconds.

A133UsbBackup.collect_mutable(directory:, serial:, cid:, adb:'adb',
root_method:'adbd', timeout:3600) creates a new destination containing
udisk.raw, metadata.raw and mutable.json. No overwrite, automatic adoption or
retry. Interrupted evidence remains. Exact stream counts, remote exit/stderr,
fsync and no-replace publication must succeed before a receipt is returned.
Offline checking does not discover current device state or authenticate records.

## Validation and boundaries

Use independent small GPT/partition fixtures only inside test processes, real
dd/subprocess streams and real private host files. Exercise changed mutable
bytes, mount namespace aliases, swaps, holders, identity drift, truncated reads,
output collisions, timeout, unsafe inputs and publication lifetime. Recheck the
existing full collector and its lifetime regressions on both host Rubies.
No kernel/userland sources change; kernel build and hardware flashing are not
validation for these Ruby host-only tools. Native Ruby execution is the build
and contract check. Keep secrets/images outside Git and preserve existing WIP.
