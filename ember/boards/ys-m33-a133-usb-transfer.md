# YS-M33 guarded USB range transfers

[Cable installation](ys-m33-a133-cable-install.md) ·
[Backup/recovery preparation](ys-m33-a133-backup-recovery.md)

The [USB transfer API](../tools/a133-usb-transfer.rb) supplies the binary
write/readback stage for a future cable installer. Its public executable
only inspects a selected recovery device. The complete fleet flow and
physical acceptance of this new API are not implemented.

## Read-only recovery inspection

Keep the selected serial and eMMC CID in private operator state. An example
using operator variables, with no identity in source control:

```sh
ruby ember/tools/a133-usb-transfer.rb --serial "$tablet_serial" --cid "$tablet_cid"
```

Use `--adb /path/to/adb` for the trusted host executable and `--timeout` for
each ADB subprocess deadline (1–7200 seconds, default 600). The CLI has no
write/reboot/unlock/root-restart option and always reports zero writes and
`installation_ready=false`. A normal Android device or a tablet already in
EmberBSD is refused by this recovery-specific stage; do not reboot one merely
to try the inspection.

Inspection requires USB, an explicit serial/CID, shell_v2, root recovery,
unlocked/orange, the recorded factory Android model `a133`, A133 compatibility and the exact full eMMC
geometry, GPT copies and by-name mappings. Target device IDs are checked
against mountinfo so mount aliases cannot evade the gate. Mounted /data,
/metadata and every mounted block device found through /sys/dev/block are refused,
including device-mapper under an arbitrary source alias. This conservative stage
requires recovery with no mounted block-backed filesystems, even read-only ones.
These checks distinguish the inspected profile; they do not authenticate
vendor firmware or establish that another YS-M33 revision has identical hardware.
YS-M33 is the board label, not its recorded Android model property.

## API integration boundary

`A133Usb::Client.new(adb:, serial:, cid:, timeout:)` exposes `inspect!` and
`write_verified(role:, path:, bytes:, sha256:, backup:, protected_env:)`.
Only boot/resources/root are writable; the role selects a fixed partition.
Boot and resources require exactly 32 MiB; root is a positive multiple of 512
up to 27676098048 bytes. No env, GPT, recovery, early secure stage or hardware
boot-area write exists in this API. Images are raw regular files, not symlinks
or gzip/zstd streams.

The caller must first complete the trusted installer gates: device binding,
fresh full-backup verification, quiescent mutable-data copies and separate
boot0/boot1 retention, verified recovery round trip, release validation,
unlock and checked installation of the protected environment. This API cannot
make an unverified source into a fleet release and must not be exposed as
a standalone installation command.

`backup` is the successful output of the full checker with a private `cid`
field captured alongside that particular backup. The transport validates its
status, geometry, GPT flags and all four critical hashes, compares the CID,
and reads the entire retained recovery partition against the recorded hash.
The receipt is **trusted caller input**, not signed evidence: JSON can be
forged, this function does not re-read 31 GB, and attaching a CID does not
prove historical provenance. The caller must capture/bind/revalidate it.

`protected_env` is the original per-device env modified into the previously
tested protected profile and already written/read back by the orchestrator.
CRC, retained bootcmd/Android/recovery scripts, boot_normal running only
ember_recovery_once and its exact USB-device-role helper are required.
The complete live 128-KiB prefix must match. This profile keeps recovery
armed while image writes occur; it differs from the one-shot preparation
hook which clears itself before recovery.

Input size and complete SHA256 are verified through an opened descriptor
before writing. The actual bounded bytes sent are hashed again, descriptor
metadata is checked, and the entire written range is read back through the
binary channel and hashed on the host. Exit status includes remote `sync`;
no checksum shell pipeline can hide a failed read. Identity/layout/mount and
recovery/env gates run before writing and after readback.

Success reports `range_write_verified` and `installation_ready=false`.
`A133Usb::Invalid#write_attempted` distinguishes a rejected gate from a
possibly partial write. Do not retry automatically, release normal boot or
reboot after an error. The module never clears protection or reboots.
Local process-group cleanup cannot prove a disconnected remote writer stopped
and cannot guarantee rollback. Keep the tablet in recovery and re-establish
its identity/state before any repair. A dedicated physical USB cable is
required; adversarial transport substitution is outside this trust boundary.

## Reproducible host checks

```sh
ruby ember/tools/a133-usb-channel-test.rb
ruby ember/tools/a133-usb-transfer-test.rb
```

The channel contract executes actual child processes and binary pipe I/O,
including NUL/CR/LF preservation, simultaneous large diagnostics/output,
failed exit, output limits, short input, deadline and descendant termination.
Signal-refusal injection at the OS boundary checks primary-error preservation,
failure after unsuccessful cleanup and a bounded return with a live child.
The transfer contract substitutes only the unavailable ADB/device boundary:
a strict executable maps approved commands onto sparse files, and real dd
writes/reads actual bytes. It checks full-range hash, preserved tail, intact
protection and failures for identity, GPT/CRC, aliases, backups, input mutation,
partial writes, failed sync and corrupt/short readback. There is no production
test-device profile. These checks do not claim physical USB/recovery acceptance.

On 2026-10-08 installed host ADB was 37.0.1; its help confirms `shell -T`
disables PTY and keeps remote exit codes/stdout-stderr separation. No Android
ADB device was attached; the working EmberBSD reference was not rewritten.

Ruby 4.0.5 and compatibility Ruby 2.6.10 passed 13 channel cases and 41 transfer
cases. Independent review reproduced a device-mapper alias bypass and observed
an intermittent EPERM cleanup failure; both received failing regressions before
fixes. A stored factory-model mismatch was also reproduced and fixed using the
recorded `a133` metadata. Cleanup also rejects failure when called from an
outer rescue; body completion is tracked explicitly rather than inferred from $!.
Private images/IDs and physical acceptance are outside
these host receipts.
The final independent review of `86b2b56c7998` had no open actionable findings.
Its acceptance applies to this API primitive and host checks, with the caller
responsibilities and physical limitations above.
