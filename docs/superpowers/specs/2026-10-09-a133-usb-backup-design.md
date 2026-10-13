# A133 read-only USB backup acquisition

Autonomous next step for the cable installer: acquire per-device backups instead
of asking a caller to provide already copied files. Preserve the working sample;
no device storage writes, root-daemon restart, unlock, reboot or audio/camera.
Android ADB is currently absent, so physical acquisition is not acceptance here.

## Choice and boundary

Use the existing bounded binary ADB channel to stream a raw whole eMMC file,
four independent critical partition files and both hardware boot areas into a
new private host directory. Raw output avoids a new compressor pipeline and its
failure modes; it requires about31GB plus copies. Compression can be added later.
The existing capture checker freshly verifies the resulting files before the
manifest is published. No saved success JSON replaces reading actual bytes.

Expose `A133UsbBackup.collect(directory:, adb: 'adb', serial: nil,
state: 'device', root_method: 'vendor_su', timeout: 3600)` and a backup-only CLI.
Auto-selection requires one inventory row; explicit serial selects exactly one.
Require a USB row in the requested device/recovery state and shell_v2. Root
methods are only vendor_su (static /system/xbin/su0 shell wrapper) and adbd;
both are read-only commands, not root restarts. Identify model a133, compatible
allwinner,a133, fixed full eMMC geometry,17 names/bounds/mappings and both GPTs.
Discover CID once, validate lowercase32hex, pin identity/state/geometry/GPT and
equal nonzero boot0/boot1 sizes (512-aligned, <=32MiB) on subsequent inspection.
Reinspect before and after every streamed file. Recovery acquisition additionally
requires no mounted block device; device-mode acquisition allows live Android
but never claims filesystem consistency. Shared silicon alone is not board proof.

## Private capture schema2

Keep schema1 compatibility. Schema2 adds exact `capture_state` (device/recovery),
`root_method` (vendor_su/adbd) and `hardware_boot` (exactly boot0/boot1 objects
with role/file/bytes/sha256) to the existing fields. Both area lengths must agree,
be positive512 multiples and <=33554432. All eight inputs including the manifest
must be distinct opened dev/inodes, private0600 in directory0700. Extend the
existing lifetime/stat/hash checks to boot areas; their bytes/hashes are trusted
capture metadata, not authenticated hardware geometry. Schema1 reports no
hardware boot verification; schema2 reports hardware_boot_copies_verified=true.
Keep four partition hashes unchanged for the existing guarded writer consumer.

## Acquisition and publication

Validate host arguments and the source before creating a destination. Never
overwrite or adopt an existing directory; retries choose a fresh directory.
Parent must be private0700 and owned by the current UID; leaf links are refused.
Files are exclusive0600 `.partial` outputs. Count and hash every streamed byte,
refuse over/short reads and remote failure, flush/fsync and publish each file
without replacement. Keep failed partials as evidence, do not delete unknown
files. Acquire all seven files before writing a private candidate manifest.
Freshly verify it with A133Capture, reinspect the source, then publish the final
manifest without replacement and fsync the directory. A failed invocation never
reports success; an interrupted publication must not be inferred ready merely
from filenames. No host/device power-loss durability is claimed by these tests.
Retain parent/destination directory descriptors through the invocation and all
eight output descriptors through fresh verification, final source inspection
and publication. Recheck paths/privacy/stat before and after publication.
Publishing the manifest deliberately changes ctime; compare its actual content
with the original generated record in addition to the retained inode/mtime.

Return a redacted usb_backup_captured receipt: counts/bytes, boot-area verification,
filesystem_consistency=not_established_by_integrity_check, writes_performed0 and
installation_ready=false. API errors and CLI must not leak identifiers, paths,
data or exception causes. Private identity exists only in the manifest.
Deadline is bounded1..7200 per channel and an overall acquisition deadline.
No write/restore/reboot flag. Retain source-side root/unlock state unchanged.

## Verification and limits

Use actual small independently built GPT/device files and strict subprocess ADB
fixtures executing real dd. Geometry replacement is test-process-only. Assert
actual captured bytes/hashes/private modes, original source bytes, copied boot
areas, no replacement, fresh verification, identity drift, short/oversized/error
streams, deadline/host fsync fault and redacted errors. Schema2 tests cover boot
size/hash/role/aliases/lifetime errors while schema1 tests remain passing.

This supplies acquisition from an already root-readable Android/recovery source.
It does not get root permission, enter/unlock recovery, prove original pristine
factory state or coherent live Android data, acquire quiescent mutable copies,
accept a fleet release/restore/first boot or replace the guarded write coordinator.
Those remain the next integration and physical acceptance steps.
