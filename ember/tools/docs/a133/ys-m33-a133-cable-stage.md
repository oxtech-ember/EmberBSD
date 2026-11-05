# YS-M33 composed cable write stage

[Cable installation](ys-m33-a133-cable-install.md) ·
[Unlock stage](ys-m33-a133-unlock-stage.md) ·
[Image sessions](ys-m33-a133-install-session.md)

The [composed API](../../a133-cable-install.rb) binds a release before unlock,
verifies unlocked recovery, installs persistent recovery protection and writes
the three guarded image ranges. An outer private journal remembers when
protection began so an interrupted run cannot accidentally re-enter unlock
after env or boot have changed. It leaves the device in protected recovery;
the trial arm/reboot and complete factory installer remain separate.
The [copied-env trial preparer](ys-m33-a133-trial-env.md) prepares a proposed
next step without device access; this coordinator does not adopt its trial
or rearmed states and continues to leave recovery protected.

## Caller contract

```ruby
A133Cable.run(directory: session, manifest: manifest, adb: adb,
  serial: serial, cid: cid, original_env: original_env,
  backup: trusted_capture, mutable: trusted_mutable,
  locked_round_trip_verified: true, timeout: 600, wait_timeout: 120,
  device_root_method: 'vendor_su', recovery_root_method: 'adbd')
```

Inputs are copied before I/O. The caller must freshly verify and retain full
backup, critical copies, boot0/boot1 and quiescent mutable copies. The physical
locked Android/recovery round trip is a caller attestation, not measured by
this API. Trusted receipt fields do not authenticate an archive. The fixed
31,037,849,600-byte/17-partition YS-M33 profile is mandatory.

Verify release contents, signing, entropy and personal-data removal separately;
never clone the working reference as a fleet release. Bundle source commit,
role sizes/hashes, factory evidence, env variants and identity are pinned in
the outer context. Artifact order and filenames do not change release identity.
The image writer re-verifies the release against the pinned semantic digest
before it admits USB I/O. Changing the release or evidence refuses resume.

All calls for one tablet must use one outer directory, with no competing host
or standalone API calls. Directory exclusion is not a global device lease.
Preserve its subordinate `unlock/` and `images/` journals. Moving/removing an
active directory or replacing its lock stops later journal publication.
Inode/permission checks do not authenticate a malicious same-user owner.

## Resume behavior

The outer journal uses0700 owned directories,0600 owned regular single-link
files, descriptor-bound flock, bounded65536-byte exact checksummed state,
exclusive temporary files, fsync and atomic rename. Possible write/reboot,
verified unlock and started protection are monotonic flags. After rename,
the visible state survives subsequent directory-fsync failure reporting.
Unknown leftover temporary files are retained and never trusted.

Before the protection marker, the existing unlock journal owns resumption and
the at-most-once native reboot rule. A recorded reboot is never replayed;
operator recovery can be necessary if interruption preceded submission.
Verified unlock and hardware boot sizes are persisted before protection intent.
After that intent, every invocation skips unlock, checks selected unlocked
USB recovery/CID/GPT/hardware sizes and repeats live protection verification.
Protection requires unlocked recovery at its own repeated guards in this flow.
The pinned outer policy is also checked inside Protection and every image
read/write guard: outer lock scope, unlocked USB recovery, CID/GPT/hardware,
all-task mount inventories/swaps/holders and the full env hash. Protection
accepts the pinned original/protected env pair; images require exact protected
env. Standalone core guards remain mandatory alongside these additional checks.
The shared policy retains the selected factory profile across recovery guards:
hook-profile operations require model `a133`, and direct-profile operations
require `QUAD-CORE A133 c3`. A resume with a different profile/model is refused.
Exact original or exact protected full env is accepted; unknown partial env
stops without a blind repair. The env tail and retained recovery are checked.

Then the image session rechecks root → boot → resources, including previously
verified ranges. Matching bytes are not rewritten. Only a full hash mismatch
can begin one guarded rewrite in an invocation. Any persistence or transport
failure stops later stages; no automatic retry, rollback, relock or reboot.
An interrupted remote command may still complete; local cancellation does
not establish remote rollback. Resume checks actual bytes, not just receipts.

Success is `cable_write_stage_verified`, always `installation_ready=false`.
The report includes current image/protection write counts and verified roles.
Errors carry bounded symbolic reasons and redacted conservative prior-effect
hints. Invalid admission or unreadable prior state reports effects unknown.
No identifiers, private paths, credentials or binary env are printed.

## Native confirmation and normal release

The [native postboot API](../../a133-native-postboot.rb) extends this same
journal after a caller-owned trial has reached native EmberBSD.
It does not arm the loader, start a trial or request a reboot.

```ruby
require_relative '../../a133-native-postboot'
require_relative '../../a133-native-source'

source = A133Native::Source.new(ssh: ssh_path, host: native_host,
  host_alias: unique_alias, known_hosts: private_known_hosts,
  identity: client_identity, host_key: provisioned_public_key,
  kernel_version: selected_kernel_version)
A133Cable.native_postboot(directory: session, policy: captured_policy,
  source: source, nonce: image_nonce, acceptance: application_acceptance,
  release: false)
# After confirmation, the same call with release: true restores normal boot.
```

Reuse the initialized `A133Recovery::Policy` from the verified capture.
The nonce is a unique48-hex value installed in this image's root-owned0600
`/etc/ember/firstboot-pending` file. The source pins this tablet's preprovisioned
Ed25519 public key; a network key scan does not establish that identity.
The private known_hosts file has exactly one selected alias/key entry.
The required `kernel_version` is the exact `uname -v` identity from the selected
kernel producer, without trailing newlines. Do not discover it from an unknown
running target. The source checks that identity as well as the installed boot hash.
The producer/caller must bind the version to the selected image; this is not
cryptographic runtime attestation. The caller retains the trial, watchdog and
image-personalization responsibilities.

The API holds the outer lock, then the existing image-store lock.
It requires verified image roles and matches their semantic bundle digest
against the outer context. It does not rehash a mutable installed root.
Native observation checks root/model, MCU service, current boot epoch,
partition mapping, GPT, installed boot hash, retained recovery and marker.
`acceptance` is a caller-supplied callable. It must return exactly `true`
after checking the intended application. OS checks do not certify that UI.

Before marker removal, the existing journal records `confirming` and pins
key/kernel-version/nonce/boot/epoch hashes plus before/normal env hashes. It also retains
`native-env-before.bin` privately. Normal release records `releasing` before
writing only the prepared128KiB prefix, then reads the full16MiB partition.
Each mutating SSH shell checks its running kernel version and boot epoch before changing data.
Successful confirmation and release advance to `confirmed` and `released`.
Old cable journals remain readable; cable writes cannot resume after this boundary.

A resumed marker removal accepts the same nonce or an already absent marker.
It rejects a foreign nonce. An ambiguous env write is never replayed:
matching final bytes can finish observation; any other state stops for recovery.
A different boot epoch stops this operation. Inspect the journal for later boots.
No automatic rollback, reboot or per-boot recovery is claimed.
All results retain `installation_ready=false`; these APIs do not complete
trial arming, full restoration or a factory-to-accepted fleet installer.

## Inspection and host checks

```sh
ruby ember/tools/a133-cable-install.rb /private/tablet-session
ruby ember/tools/a133-cable-journal-test.rb
ruby ember/tools/a133-native-journal-test.rb
ruby ember/tools/a133-native-postboot-test.rb
ruby ember/tools/a133-native-source-test.rb
ruby ember/tools/a133-native-epoch-test.rb
ruby ember/tools/a133-cable-install-test.rb
ruby ember/tools/a133-cable-install-cli-test.rb
ruby ember/tools/a133-install-session-regression-test.rb
ruby ember/tools/a133-recovery-protection-unlocked-test.rb
ruby ember/tools/a133-cable-stage-guard-test.rb
ruby ember/tools/a133-bundle-pin-regression-test.rb
```

The CLI only inspects an existing locked journal and emits one JSON after
the lock closes. It has no initialization/write/reboot flag. Native fixtures
run actual dd against sparse files, inspect persisted intent before effects,
verify actual image bytes and preserved env/recovery/tail, and exercise resume
before/after protection publication, range drift, release mismatch and errors.
Late close and filesystem faults are injected at real I/O boundaries.

Native postboot tests use a transport fixture and real private journal files.
SSH tests check pinned options, binary bounds and independent GPT/marker responses.
A shell fixture injects a reset between inspection and mutation; removing the
in-shell epoch or kernel-version guard lets the same fixture reach the forbidden mutation.
These contracts do not establish physical postboot acceptance.

These tests do not execute vendor hooks on a physical device, prove cold
power-loss durability, verify early-boot contents after transition, accept data
preservation/full Android restore, or establish signing/first boot/fleet readiness.
No physical tablet is modified by these test commands. The historical BSP,
main reconciliation and native OS build remain separate integration gates.
