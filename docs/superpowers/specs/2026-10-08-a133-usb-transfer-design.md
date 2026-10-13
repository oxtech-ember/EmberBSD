# A133 guarded USB range transfer

The user requests autonomous progress towards cable installation on unopened
ROOMY tablets. Preserve the working tablet and perform no audio tests. This
subsystem replaces the private single-device transfer script with a reusable
transport/verified-write API, not a fleet installation command.
The existing linked worktree is retained. The user explicitly requested
autonomous execution, so the scoped design/plan are executed inline.

## Chosen boundary

Use installed host ADB and Ruby standard libraries. A USB binary subprocess
channel supports concurrent stdin/stdout/stderr, bounded memory, deadlines,
process-group cleanup and remote exit status via shell_v2 and `shell -T`.
The alternative adb push/staging path requires remote free storage and two
additional copies; the alternative remote checksum pipeline can conceal short
reads. Stream raw input and full raw readback through host SHA256 instead.
Compressed images and automatic recovery/unlock/firstboot orchestration follow
separately. Public CLI is inspection-only: there is no executable write flag.

## Device and write gates

`A133Usb::Client.new(adb:, serial:, cid:, timeout:)` requires an explicit ASCII
USB serial and lowercase 32-hex eMMC CID. It must reselect the same USB transport
and read CID on every inspection. Require recovery, root, unlocked/orange,
shell_v2, the recorded factory Android model `a133`, allwinner,a133 compatibility,
disk size 31037849600 and
the full 17-entry reference inventory. Check both live GPT copies/CRCs with
the existing A133Backup validator and the by-name mapping of every partition.
Check target major/minor IDs against mountinfo so aliases cannot evade the gate.
Refuse every mounted block device found through /sys/dev/block, including
device-mapper aliases, as well as /data or /metadata. No root restart, reboot,
unlock, wireless ADB or fallback.

`write_verified(role:, path:, bytes:, sha256:, backup:, protected_env:)` allows
only boot, resources and root. Boot/resources are exactly 33554432 bytes; root
is a positive multiple of 512 at most 27676098048. It cannot write env, GPT,
recovery, hardware boot areas or early secure stages. Input must be a stable
non-symlink regular file and match complete SHA256 before device write.
Hash the bounded bytes actually streamed and compare descriptor metadata.

The trusted orchestrator supplies a freshly verified backup integrity receipt
plus the CID captured with that backup. Receipt must have the exact disk size,
successful GPT/layout flags, status backup_integrity_verified and valid raw and
critical-range hashes; backup CID must equal the selected expected CID.
This is an accident-prevention interface, not authentication of JSON. The
transport does not reverify 31 GB, establish snapshot consistency or establish
receipt provenance. The orchestrator must do those before calling it.

Read the entire installed recovery partition and match the backup's recovery
hash. Read the complete env prefix and require byte equality with the supplied
protected environment. Decode CRC and require the retained factory bootcmd,
boot_android/recovery scripts, boot_normal=run ember_recovery_once, and the
helper which selects USB device role and runs recovery, without clearing the
protection. The already tested private protected profile defines this guard.
Reinspect identity/GPT/mounts and guard immediately before writing and after
complete readback. A detectable switch or mismatch fails without normal boot.
An adversarial USB switch during an open stream is outside this transport's
trust model; a dedicated physical cable remains required.

## Failure and output

Reaping after termination is bounded to one second; signal/cleanup failure
must not replace a primary error or turn a successful body into accepted success.
Non-block Open3 avoids its automatic unbounded join. No retries after a write starts.
Exit/error status must distinguish a rejected
write from a possibly partial write. Errors contain bounded symbolic reasons,
never stderr, serial, CID, paths or env contents. Killing local ADB does not
prove a disconnected device stopped its remote process; never promise rollback
or safe reboot. Do not clear the protection or reboot on any path.
Success reports one verified written range, its full hash and
installation_ready=false. It does not accept first boot or the fleet.
Read-only CLI reports compatible recovery inspection with zero writes and
installation_ready=false, or bounded failure. Receipt/captured IDs are private.

## Verification

Use a file-backed ADB boundary executable with strict command matching,
sparse reference-size disk/GPT and real shell/dd byte streams. Assertions
check actual destination bytes and preserved tail, not fake call counts.
No special production fixture mode. Test binary NUL/CR/LF, failed child,
large simultaneous stderr/stdout, deadline and descendant termination.
Exercise wrong identity/transport/state/GPT/mapping/mounts/backup/guard/source,
partial transfer and full readback corruption; none may clear recovery.
Host fixtures demonstrate policy and transport behavior, not physical USB
acceptance. No connected Android ADB device is currently enumerated; the
working EmberBSD reference stays untouched.
