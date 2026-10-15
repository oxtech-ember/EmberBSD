# YS-M33 cable installation and boot-loader boundaries

Installation must use a cable, a closed enclosure and no UART. The installed
tablet then boots autonomously from eMMC. One unopened factory tablet completed
USB-only backup, unlock, installation and a normal eMMC restart on 2026-10-08.
See the [physical receipt](validation/2026-10-08-a133-cable-install.md).
Cold power cycles, full Android restoration and fleet acceptance remain untested.

## Read-only first connection

The host needs Ruby and Android platform-tools. Connect the tablet's USB-C
device port and separate DC power. Cable/port negotiation must select the
USB device role. FEL detection alone does not establish ADB or fastboot access.

```sh
ruby ember/tools/a133-install-preflight.rb --adb /absolute/path/to/adb
# With multiple Android devices, explicitly select the intended USB device:
ruby ember/tools/a133-install-preflight.rb --adb /absolute/path/to/adb --serial DEVICE
# For a factory image with the inspected vendor su implementation:
ruby ember/tools/a133-install-preflight.rb --adb /absolute/path/to/adb --su
ruby ember/tools/a133-install-preflight-test.rb
```

The inspector issues only bounded ADB reads. It never restarts adbd as root,
unlocks, reboots, transfers files or writes storage. It requires one selected
ready USB ADB device, root read access, `allwinner,a133` in the device tree
and the inspected boot target/bounds. JSON does not print the device serial.
Missing access, multiple devices, foreign boards, wrong targets, timeouts
and command errors stop inspection. The fixture contract executes the real
tool against an ADB boundary rejecting all other commands.

`inspection_complete` describes a candidate only. The result always states
`installation_ready: false`. Exact board identification, complete partition
inventory, per-device backups, unlock/recovery and image validation precede
an installer's first write. Non-root factory adbd may require `adb root` or
vendor `su`; neither capability is inferred from an eng build label.
The explicit `--su` mode prefixes the same read commands with
`/system/xbin/su 0` and still verifies UID 0. It does not restart adbd.

On 2026-10-08, a second unopened factory sample passed this inspection with
`--su`: Android 10 build `a133-10.0-20231211.133302`, locked flash and green
verified-boot state. Its full 17-partition layout matches the reference
sample. Applying the production FDT adapter to a private copy of its exported
factory tree passed the audio, touch, eMMC, USB-A and opt-in SDIO guards and
created the framebuffer node. Subsequent physical installation booted EmberBSD,
Ethernet SSH and awesomeWM on that tablet. Copied FDT guards alone would not
establish those hardware results.

## Bootstrap through Android

A read-only snapshot on 2026-10-08 verified the inspected sample's first
128 KiB of `env`: CRC32, `bootcmd=run boot_normal`, `bootdelay=3` and stored
normal/recovery scripts. When factory Android provides root storage access,
a candidate bootstrap can preserve this per-device environment and generate
a temporary boot command to run the vendor unlock operation, select USB
device mode in the loader FDT and enter signed recovery. The recovery
installer would restore the intended normal environment before completion.

These primitives were observed separately: vendor U-Boot's
`pst write fastboot_status_flag unlocked`, volatile `usb_port_type <0>`,
recovery ADB root and verified partition writes. The second unopened sample
completed the bootstrap, with a locked recovery/Android round trip before
unlock and a recovery return after an unconfirmed EmberBSD boot. This single
factory-image result does not prove another image's root ADB, environment format or
secure-storage behavior. Userspace fastbootd enumerated in earlier tests
but did not implement the attempted OEM/flashing unlock operations.

The offline environment editor operates on a copied 128 KiB file only:

```sh
ruby ember/tools/a133-env-edit.rb env-copy.bin NEW-env.bin 'bootcmd=run boot_recovery'
ruby ember/tools/a133-env-edit-test.rb
```

It validates CRC32, termination, variable names, uniqueness and capacity;
preserves opaque values and the vendor's leading empty entry; and creates
a new private output without overwriting any existing path. It never writes
a device. Against the actual original factory `env-orig.bin`, changing
`bootcmd` and restoring its old value reproduced every original byte.
This proves offline encoding, not the proposed unlock/recovery bootstrap.
The second sample instead has `bootdelay=0`,
`bootcmd=run setargs_mmc boot_normal` and
`boot_normal=run ${hook};run boot_android`. Do not substitute the first sample's
environment or erase its hook. Recovery access should be established while
still locked before attempting an unlock. A temporary recovery command needs
a demonstrated return to the original Android boot when USB recovery fails;
a persistent recovery loop is not a fleet recovery mechanism. This factory
normal script provides an unused `hook`. A copied-file candidate keeps both
normal boot scripts intact, sets the hook to two `run` variables, first clears
the hook and saves the normal environment, then selects USB device mode and
enters recovery. Restore the exact original per-device environment after
recovery ADB is available. Do not execute it before checking the full backup.

On the reference sample, volatile command trials established that the vendor
`run` expands both hook variables before the first removes the hook, that a
failed first variable skips the second, and that the normal script's Android
fallback remains reachable. The save step was mocked: this validates the
vendor parser and ordering, not a physical environment save or the second
sample's recovery boot. The reference environment and installed boot hash
were unchanged after returning to EmberBSD.

The [guarded normal USB reboot API](ys-m33-a133-recovery-reboot.md) now checks
the prepared factory source, submits one native reboot and freshly verifies
the selected USB destination, complete critical images and env. Reboot may
persist vendor env; any failure after submission retains possible-side-effect
flags and is not automatically retried. Its actual-process host round trip is
separate from real vendor/cable acceptance, unlock and accepted EmberBSD trial boot.

The [copied-env unlock preparer](ys-m33-a133-unlock-env.md) now encodes the
observed vendor unlock/reset/recovery sequence without device access. It
preserves the per-device normal scripts and requires exact byte restoration
after removing its seven reserved variables. This prepares a private file,
not an unlock operation; guarded execution and physical acceptance remain open.
The [journaled unlock API](ys-m33-a133-unlock-stage.md) now composes guarded
prefix writes, one recorded native reboot, unlocked recovery readback and
original env restoration. Copied-file resume tests remain separate from the
required physical locked round trip and factory data-preservation acceptance.

## Android backup and QEMU

The [guarded recovery entry library](ys-m33-a133-recovery-entry.md) now exposes
Android-side one-shot arming and locked-recovery restoration with fresh capture
GPT/critical binding, target-usage guards and complete env readback. Host tests
accept only exact known states, including reordered consumed exports. A new
physical round trip, supported unlock interface and accepted first boot remain
integration gates; it performs no reboot or unlock itself.

The common [backup checker and recovery-copy preparer](ys-m33-a133-backup-recovery.md)
now validate complete saved main-eMMC images and produce a guarded one-shot
env candidate on the host. They neither write nor restart a tablet and never
claim installation readiness or live-filesystem consistency.

The second tablet has a verified full 31,037,849,600-byte live Android snapshot
and separate unmounted recovery snapshots of all UDISK and metadata bytes.
The latter preserve filesystem consistency for the overwritten data partition;
the initial live snapshot alone does not prove it. These private backups have
not yet undergone a physical full Android restore.

The private sample backup covers original boot/recovery, boot hardware areas,
environment, all eMMC bytes before UDISK, the overwritten first 4 GiB of UDISK
and the disk tail. It is not a complete standalone 31,037,849,600-byte eMMC
dump. Untouched UDISK data remains on the physical device. On 2026-10-08,
the boot/recovery partition copies matched their ranges in the original
3,361,734,656-byte head backup. Original snapshots are never VM write targets.

QEMU 11.1.1 lists no A133 board model. A bounded TCG `virt` trial with
factory ARM64 kernel/ramdisk and a read-only head snapshot exited without
Android console output or evidence of Android startup. Factory config
selects `ARCH_SUNXI`; this trial does not emulate the vendor boot chain,
secure storage, USB role hardware, radio or MCU. QEMU's
[Arm board requirements](https://www.qemu.org/docs/master/system/target-arm.html)
explain why a matching CPU cannot substitute for the board model.
Use copied-file and controlled ADB tests for installer behavior; full
hardware boot, unlock and recovery acceptance remains on the real tablet.

## Own boot loader

The sample's 2026-10-08 log reports `secure enable bit: 1` and loading
root-key, monitor, boot, vbmeta and recovery key/image hashes. Its unlock
permits an unsigned Android boot payload; it does not establish permission
to replace boot0, BL31 or the TOC1 U-Boot image without signing. Replacement
warning artwork does not change secure-boot state.

Upstream U-Boot `e1dcad5512b` has A133 support and DDR4/LPDDR4 options.
Liontron's defconfig uses LPDDR4 at 792 MHz; this sample logs DDR4 at 744 MHz.
That defconfig is not YS-M33 validation. The generic
[Allwinner installation recipe](https://docs.u-boot-project.org/en/latest/board/allwinner/sunxi.html)
does not prove a replacement of this secure vendor chain.

U-Boot v2026.10 reached its UART prompt as a RAM-loaded second stage on the
first sample after retained vendor boot0/BL31/OP-TEE. Position-independent
ARM64 entry was required because the vendor loader placed the Image at
0x40080000 despite its linked address. A SoC watchdog reset restored ordinary
EmberBSD boot; boot and environment readbacks remained unchanged. See the
[physical RAM-probe receipt](validation/2026-10-08-a133-uboot-ram.md).

A later RAM trial handed the installed kernel through to its FFS root and
started rc services. A prearmed 16-second watchdog restored ordinary boot.
The probe has no storage, USB recovery, display or MCU support of its own.
It is not permanently installed. Validate complete reserved memory,
framebuffer ownership, sustained MCU7502 service and direct return to signed
recovery in RAM. Then evaluate persistent second-stage installation. Full boot0/TOC1
replacement requires a separately demonstrated authenticated loading/recovery
path. A copied signature does not authenticate changed image contents.

## Fleet acceptance

### Offline artifact manifest

[a133-install-bundle.rb](../tools/a133-install-bundle.rb) reads a manifest and
checks complete SHA256 hashes of the three host files before device work:

```sh
ruby ember/tools/a133-install-bundle.rb /private/bundle/manifest.json
ruby ember/tools/a133-install-bundle-test.rb
```

The manifest has exactly `schema` (integer 1), `board` (`ys-m33-a133`),
`source_commit` (40 lowercase hexadecimal characters), and `artifacts`.
The array contains exactly these unique roles:

| Role | Reference start sector | File length |
| --- | --- | --- |
| boot | 172032 | 33554432 bytes |
| resources | 73728 | 33554432 bytes |
| root | 6565888 | Positive multiple of 512 bytes, at most 27676098048 |

Each entry has exactly `role`, `file`, `bytes` (integer), and `sha256`
(64 lowercase hexadecimal characters). `file` is a unique ASCII basename
beside the manifest, at most 128 characters, starting with an alphanumeric
character; the remaining characters may also be dot, underscore or hyphen.
The manifest is at most 65536 bytes. Symlinks, non-regular files, duplicate
JSON keys, unknown fields, incorrect lengths and digests are rejected.
Payloads are hashed in bounded chunks through non-following open descriptors.

Exit zero reports `artifact_manifest_verified`, `writes_performed: 0`, and
`installation_ready: false`. This is an **unsigned integrity receipt**:
it does not authenticate a publisher, inspect credentials/entropy inside FFS,
validate image filesystems, match a connected tablet, or authorize writes.
The role mapping is a reference, not discovery of a device's GPT.
A future writer must revalidate the bytes it actually writes and perform
independent device, backup, recovery and acceptance checks. A matching
personalized image must not become a fleet release because of this receipt.

The real-file contract passed on Ruby 4.0.5 and macOS system Ruby 2.6.10.
It checks complete-image corruption, bounds, schema/type errors, duplicate
keys, path escape, symlinks, FIFO refusal and unchanged bundle files.
This does not test physical cable writes or power-loss recovery.

### Physical release acceptance

The [guarded USB range-transfer API](ys-m33-a133-usb-transfer.md) now supplies
bounded binary writes and complete host readback hashes for a future installer.
Its executable is inspection-only; the API requires trusted backup provenance
and an already installed/read-back protected recovery environment.
File-backed tests do not establish physical USB or fleet acceptance.
The [session coordinator](ys-m33-a133-install-session.md) adds private locked
progress and fresh range verification on every repeat. It coordinates only
the prepared recovery write stage; factory backup/recovery setup and accepted
first boot remain integration work.
The [composed cable write stage](ys-m33-a133-cable-stage.md) now binds release
inputs before unlock and resumes through protection and image writes on host
fixtures. It does not release recovery or accept the first boot.
The [capture binding checker](ys-m33-a133-backup-recovery.md#bind-a-trusted-capture-record-and-critical-copies)
freshly verifies the full backup and four critical copies against a trusted
record and explicit serial/CID. Its offline binding does not replace live USB
identity, retained hardware/mutable copies or accepted first boot.
The [read-only USB backup collector](ys-m33-a133-usb-backup.md) acquires a raw
full snapshot, critical copies and boot0/boot1 from a root-readable source.
Its actual-dd host contract does not establish coherent Android or physical
restore acceptance; recovery setup and accepted first boot remain separate.

The first closed-enclosure installation is an experimental hardware result,
not a released fleet installer. Repeat the complete procedure without UART:
USB identification, per-device backup, unlock, recovery, boot/root writes
with full readback hashes, autonomous cold boot and cable-only recovery.
Include recovery from a failed installation. Device keys, tokens, identities
and personalized files must not be cloned from the working sample. Network
credentials are operator inputs outside Git. Preserve the working sample
as the independent reference system.
