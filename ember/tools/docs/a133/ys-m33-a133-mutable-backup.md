# YS-M33 recovery mutable-data backups

[Full USB backup](ys-m33-a133-usb-backup.md) ·
[Backup/recovery](ys-m33-a133-backup-recovery.md) ·
[Write-stage sessions](ys-m33-a133-install-session.md)

Before an installer overwrites UDISK or metadata, retain their whole per-device
copies from a root-readable, unmounted recovery. This read-only host stage
creates separate evidence without altering the earlier full eMMC capture.
It does not enter recovery, restart root, unlock, write a device, restore
Android or accept a first boot. Every receipt has installation_ready=false.

## Acquire two matching reads

Use a trusted host ADB executable and the serial/CID obtained from this actual
tablet. Do not reuse the reference tablet's identity or infer a missing CID.
The factory recovery must already expose a USB shell_v2 transport and root
read access. The default root method is already-root adbd; vendor_su selects
the retained vendor su wrapper without restarting the daemon.

The destination must not exist. Its immediate parent must be owned by the
caller with mode0700. The collector creates the directory0700 and files0600,
exclusively, and never adopts or overwrites old evidence:

```sh
ruby ember/tools/a133-usb-mutable-backup.rb \
  --serial "$a133_serial" --cid "$a133_cid" \
  /private/backups/new-recovery-mutable
```

Optional --adb selects a trusted executable, --root-method accepts adbd or
vendor_su, and --timeout accepts1..7200 seconds (default3600). There is no
--state, --write, --unlock, --reboot or automatic recovery-entry option.

The fixed physical profile is31037849600-byte eMMC with17 named GPT partitions.
Both GPT headers/arrays, by-name mappings, geometry and boot-area sizes are
checked. Source serial/CID, GPT fingerprint, recovery state, root method and
lock properties remain pinned through each read and the final checkpoint.
All readable process/thread mount inventories are checked, not only the shell's own
namespace. Thread inventories use /proc/PID/task/TID/mountinfo; nonleader
thread directories are not listed directly under /proc ([Linux task documentation](https://man7.org/linux/man-pages/man5/proc_pid_task.5.html)).
Any mounted block device (including aliases), nonempty swap list,
eMMC holder, malformed/unreadable inventory or source change stops the stage.
The mount inventory is bounded to1MiB; an oversized inventory is refused.

UDISK and metadata are each read in full into private host files. Both are then
read again, with complete byte-count and SHA256 equality required. Remote exit,
stderr, short/extra bytes or deadline prevents success. Host outputs are fsynced
and published without replacement; a fresh offline verification and final source
inspection precede publishing mutable.json. Descriptors, path identity, private
attributes and manifest bytes remain guarded through publication and return.

The three files are:

| File | Content |
| --- | --- |
| udisk.raw | Complete UDISK,27676098048 bytes |
| metadata.raw | Complete metadata,16777216 bytes |
| mutable.json | Private schema1 identifiers, source observation, GPT fingerprint and copy hashes |

Allow at least27692875264 bytes of host space plus manifest/filesystem overhead,
in addition to the separate full capture. Two passes transfer at least
55385750528 mutable bytes over USB; host verification rereads the saved files.
No compressed output or resumable partial adoption is implemented.

On error, preserve partial files and prior evidence. Repair the source conditions
and explicitly start a new destination; there is no retry loop. After a crash,
a final manifest's presence does not establish a completed acquisition or readiness.

## Freshly verify retained files

```sh
ruby ember/tools/a133-mutable-check.rb \
  --serial "$a133_serial" --cid "$a133_cid" \
  /private/backups/new-recovery-mutable/mutable.json
```

The checker bounds JSON to65536 bytes and refuses duplicate keys, unexpected
fields/roles, unsafe filenames, symlinks/hardlinks, shared inodes, changed paths,
unsafe ownership/modes and wrong sizes/hashes. All descriptors remain open
across the complete check, and directory identity is retained too. Run it again
in the consuming invocation; cached JSON success cannot replace fresh reads.

Schema1 has exactly schema, board, serial, cid, gpt_sha256, root_method,
capture_state, observation and partitions. board=ys-m33-a133 and
capture_state=recovery. observation=recovery_unmounted_two_matching_reads.
partitions contains exactly UDISK and metadata objects with role/file/bytes/sha256;
physical sizes are fixed, hashes lowercase64hex. Names are distinct single ASCII
filenames, <=128 bytes, in the manifest's directory. gpt_sha256 is a recorded
fingerprint, not an offline validation of the physical GPT or a signature.

The library A133Mutable.verify(path, serial:, cid:, timeout:3600) returns a
private string-keyed mutable_integrity_verified receipt containing identifiers,
GPT fingerprint and partition_sha256. The acquisition API is
A133UsbBackup.collect_mutable(directory:, serial:, cid:, adb:'adb',
root_method:'adbd', timeout:3600). It returns a redacted usb_mutable_captured
receipt with copy count/bytes and the recorded observation. CLI errors and
receipts omit identifiers, image hashes, contents and paths. The exceptions
have bounded symbolic reasons and discard inherited private causes.

## Observation and acceptance limits

Two matching reads with unmounted inventories are sampled evidence, not a
kernel snapshot: transient mounts or an unobserved raw writer remain possible.
In particular, UDISK can change after its second read while metadata finishes;
the receipt describes the two matching reads, not a current common disk state.
The recorded source is trusted, not cryptographically attested. An offline
check cannot rediscover current source state or authenticate the manifest.
filesystem_consistency remains not_established_by_integrity_check.

This mutable capture intentionally need not match the mutable ranges in an
older live-Android full snapshot. It supplements that snapshot's retained
critical/boot-area evidence. It is not yet wired into an all-stages installer
that protects recovery/unlock and accepts first boot. Physical vendor command
compatibility, large transfers, quiescence, complete Android restoration and
power-loss/cold-start acceptance must be tested on the designated second sample.

Host checks use independent small GPT/partition fixtures, actual dd streams,
private files and source mutation at the strict ADB boundary:

```sh
ruby ember/tools/a133-mutable-check-test.rb
ruby ember/tools/a133-usb-mutable-backup-test.rb
ruby ember/tools/a133-usb-mutable-thread-test.rb
ruby ember/tools/a133-usb-backup-test.rb
ruby ember/tools/a133-usb-backup-lifetime-test.rb
```
