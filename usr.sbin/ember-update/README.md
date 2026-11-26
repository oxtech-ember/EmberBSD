# Native system updates

An enrolled system runs `/usr/sbin/ember-update` from the base set. It uses
libfetch, libcrypto, libssl, zlib and libc. The client reads the signed SDK
release format directly. The base image installs the release trust anchor and
the HTTPS CA certificates before the first check. Update images set
`certctl_init=YES` so the first boot indexes the TLS anchors before the service runs.

## Service and commands

`/etc/rc.d/ember_update` starts the service after LOGIN and networking. Set
`ember_update=YES` in `/etc/rc.conf`. The service checks at startup and then
hourly. It retries failures after 30 seconds and increases the delay to 30
minutes. An armed trial checks the reboot window once per minute.
The defaults are `ember_update_channel=current`, `ember_update_packages=YES`,
`ember_update_os=YES`, and `ember_update_reboot=ask`.
Use `always` or a local interval such as `23:00-03:00` for automatic restarts.
The start script also checks manually staged trials when automatic updates
are disabled.

Run `ember-update check` to refresh available updates. Run `ember-update apply`
to prepare the selected packages and OS sets. On a two-slot device the
default policy leaves the new slot ready for the next restart.
`ember-update apply --reboot` allows an immediate restart.
`ember-update status` and `ember-update history` read local state without
network access. Both commands work for ordinary users.

## Store catalog and application installation

Run `ember-update catalog` as an ordinary user. It prints
`emberbsd.catalog-index.v1` JSON. Each row contains the repository, PKGPATH,
native package name, size, installation action, and confirmed desktop entries.
The signed package index supplies package identities. The installed package's
`+CONTENTS` must list each desktop file. Its executable must resolve within
the configured package prefix. A package without such an entry remains a
package or library in the catalog.

A privileged package check saves a small root-owned projection in
`/var/db/ember-update/catalog.json`. The catalog command reads it while its
signed release is valid and projects current installed state from the pkgdb.
With an empty or expired cache, it reads the signed channel, release, binary
catalog, and pkg_summary over HTTPS as the caller. It downloads no package
archive and makes no state changes.

Run `ember-update install PKGPATH` as root. A graphical store can pin the
selected native version with `install PKGPATH --pkgname FULLNAME`. The command
verifies a current release and resolves the selected root and its signed
runtime edges. It preserves a manually selected root on future updates.
On a single-root device it prints `idle` after registration. On an A/B device
it prints `ready` after arming the candidate. The public state then reports
`reboot_required=true` and lists the requested full package name in
`available.packages`. The Store command returns before the scheduled reboot.
A different pending transaction must finish first.

## Publication and installation

The client retrieves `CHANNEL.p7m` from the project package origin over
HTTPS. It checks the attached CMS signature with libcrypto and the installed
`/etc/openssl/emberbsd-release-root.pem`. It checks time bounds, channel
identity, release path, size and hash before verifying the release CMS.
Each channel stores the latest signed issue time and release hash.

An OS release must match all four target fields and the installed profile.
The descriptor must list exactly the set roles in that device's profile.
The client verifies the signed dependency edges, descriptor SHA-256, each
archive size and SHA-256, and the OS generation. A new issue of the same
generation must identify the same descriptor and set digests.

The package publisher resolves the runtime dependency graph. The device
reads the authenticated catalog, its gzipped pkg_summary, and
`/usr/pkg/pkgdb`. It chooses installed package names, follows their resolved
edges, and downloads changed dependencies before their consumers. It checks
pkg_summary membership, package file names and sizes against the signed
release. The installer runs `/usr/sbin/pkg_add` with local verified archives,
the release signing anchor and the target package database.

The device profile lives in `/etc/ember-update-profile.json`.
Kernel installation tools can run `ember-update kernel-mode` as an ordinary
user to read its installation mode, `single` or `slots`. A missing or unknown
profile returns an error. The query reads local profile data only.
For `storage=ffs`, the client stages OS and package changes on the inactive
root in one transaction. For `storage=single` and `adapter=none`, the client
updates packages on the live root. Each completed registration remains in
that package database. A retry selects changes from the current inventory.

## Installer interfaces

The controller writes a root-owned directory with these files:

* `plan.json` has schema `emberbsd.install-plan.v1`, `profile`, and `target`
  (`arch`, `triplet`, `abi`, `runtime`). It carries `os_generation`,
  `os_commit`, `os_update_sha256`, and dependency-ordered `packages`.
  Each package has its complete native `name`, archive `size` and `sha256`. An explicitly selected application also has
  `manual: true`. This promotes an existing automatic registration when needed.
* An OS update adds the exact descriptor bytes as `update.json` and one
  `ROLE.tar.xz` for each selected role. A package-only plan sets
  `os_update_sha256` to null and keeps the current OS identity. A single-root
  plan uses OS generation zero and a null commit.
* Package archives use `packages/NAME.tgz`, with NAME from the signed catalog.

`/usr/libexec/ember-install-slot prepare SLOT DIR` writes the inactive slot.
It copies the exact plan to `/var/db/ember-update/installed.json` and writes
the SHA-256 of that file as the boot receipt. The corresponding live operation
is `ember-install-slot packages DIR` for a single-root profile.
`ember-update apply-kernel DIR` requests `prepare-kernel SLOT DIR`. The local
kernel receipt in `local-kernel.json` names the SHA-256 of the installed ELF.
It holds automatic OS upgrades until the owner installs a stock kernel.

The boot adapter implements `ember-os-slot status`, `arm SLOT`, `cancel SLOT`,
and `confirm SLOT`. Status includes `running`, `confirmed`, `next`, `trial`, and
`prepared`. A preparation identifies a slot and SHA-256. The trial boot flag
can remain set after confirmation. The confirmed slot and the running slot
determine whether the running system needs confirmation.

The client records the verified preparation in the confirmed root before it
arms a one-shot boot. It confirms the trial after matching the installed
receipt, running kernel and local `/etc/ember-os-health` result. The health
check has 30 seconds. A failed trial stays unconfirmed and the watchdog
returns control to the last confirmed slot. A previous boot's unconsumed
request is cancelled before another restart. Failed OS generations stay
below the installation floor. Service stop permits the current storage
operation to finish. A stop after arming leaves the one-shot request for the
next restart.

## One-time transition from the former FFS updater

An enrolled legacy system fetches the transition with its existing signed
client. It prepares the other root and arms its normal one-shot boot. The
confirmed root keeps its original programs, policy, package database and
kernel. The transition requires a profile that the former U-Boot installer
supports.

The publisher assembles the final `etc.tar.xz` first. Include one file in the
transition's signed **base** set at
`/usr/share/ember-update/handoff/PROFILE.json`. Set `profile` to the complete
native hardware profile from the image description's `slots` object. Set
`generation` to the transition generation and `etc_sha256` to the SHA-256 of
the exact compressed etc set. Record the file in base's METALOG and mtree.
The signed boot, base, etc and optional comp sets must describe the same
target. The generic image and subsequent releases require no transition
file. The original controller also checks that its rc script and program are
executable in the prepared root and that the snapshot's installed Python can
import SSL. Publish an inactive executable at `/etc/rc.d/ember_os_update` in the transition
etc set. Publish an inactive executable at `/usr/libexec/ember-os-update` in
the transition base set. Both can be a two-line shell script that exits zero.
Record each regular file in its set list, METALOG and mtree. This satisfies
the old inactive-root preflight without starting either old service. The
former package rc script stays absent. The standard sets mark all three old
paths obsolete. Keep the defaults and programs for the installed former
release available from its immutable signed artifacts.
[Former FFS updater](https://github.com/oxtech-ember/EmberBSD/tree/3d0b206a05076152a7f8a50515b7c892a2a730db/libexec/ember-os-update)

On the first boot of the candidate, `/etc/rc.d/ember_update` invokes the
local handoff before starting its scheduler. The old services yield to their
native replacement. The handoff checks the legacy profile, target, set roles
and defaults archive against the signed base file. The boot adapter must
report a consumed trial whose prepared SHA-256 equals the exact legacy
release.json. The handoff copies those bytes to the native OS and installation
receipts and copies the original etc archive with mode 0600. The installed
generation becomes the native lower bound. The former controller only armed
a generation above both its installed and previously attempted generation.
A failure before confirmation keeps that controller and its attempt floor
on the previous root.

The handoff checks the stock running kernel and local health, then verifies
the archive of the former controller configuration. It imports the original
rc flags immediately after the system defaults are sourced. Later site
settings in rc.conf and rc.conf.d keep their priority. A durable journal
identifies the confirmed bytes if power fails during the firmware commit.
The private journal also keeps the verified transition record. Confirmation
consumes its public copy, including on a retry. Success retires the old configuration as `/etc/ember-os-update.migrated.json`.
Later transactions use native installation plans. Remove the transition
record and temporary programs in the next standard release after devices
have confirmed the transition. The etc merge preserves local settings. The
FFS installer checks the previous base mtree before removing the unchanged
program named in the successor's obsolete list. It retains a modified local
copy. The previous slot remains available until a
subsequent authorized update prepares it.

## Local state for applications

The controller atomically replaces the world-readable
`/var/db/ember-update/state.json`. Consumers can watch its directory and
reload the file after a rename. Initial absence means there is no saved
check. Other files in this directory contain private receipts and floors.

| Field | Type and values |
| --- | --- |
| schema | `emberbsd.update-state.v1` |
| status | `idle`, `checking`, `available`, `preparing`, `ready`, `trial`, `error` |
| channel | Release channel string |
| checked_at | Last successful check as Unix seconds or null |
| release_id | Last verified release name or null |
| running_slot, confirmed_slot | A, B, or null |
| available.os_generation | Selected generation or null |
| available.packages | Array of selected native package names |
| attempt | Object with slot and sha256, or null |
| reboot_required | Boolean |
| os_hold | `custom-kernel` or null |
| last_error | Operator-readable detail or null |
| history | Up to 32 events in chronological order |

Each history entry has integer `time`, string `detail` and `event`. Event
names are `prepared`, `confirmed`, `rolled-back`, and `error`.

## Tests

Run `tests/slots.sh` from this directory to exercise the boot state machine.
Run `tests/signed.sh ROOT.pem current.p7m release.p7m` with real published
envelopes and a separate local copy of the release anchor. The test checks
both signatures, the hash binding, time limits, and altered envelopes.
Run `tests/store.sh` for package selection, manual promotion and installed
desktop binding. Set `CC`, `CPPFLAGS`, and `LDFLAGS` to select the host SDK.

On NetBSD, run `tests/handoff.sh CLIENT INPUT PROFILE RC_SCRIPT` as root for
the offline transition test. Use the artifacts listed in the test header.
The test creates and removes a chroot with a boot-adapter model.
