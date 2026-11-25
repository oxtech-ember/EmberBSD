# Automatic updates on a system with two boot slots

A provisioned image enables `ember_os_update=YES` in `/etc/rc.conf.d/ember_os_update`.
It supplies two independently bootable roots, a one-shot loader and an independent reset timer.
Each slot has its own kernel, modules, base, application prefix and package database.
Keep shared writable data in a format that both releases can read.

## Image configuration

Install the native SDK client with `fetch-os-update`, its Python interpreter and its HTTPS and release trust roots.
Use the [package update layer](../../image/README.md#package-updates-on-a-running-system) for these components.

Install a root-owned `/etc/ember-os-update.json` with these fields:

```json
{
  "profile": "example-stock",
  "backend": "/usr/libexec/ember-os-slot",
  "health": "/etc/ember-os-health"
}
```

Replace the example profile with the exact installed OS profile from the publisher.
The profile identifies a compatible kernel provider, machine, boot path and userland.
The backend and health programs must have absolute paths controlled by root.
The health program runs without arguments after multi-user startup. Return zero when the application can use its saved data.
Check local services that the image needs. A device must be able to confirm a valid installation while S3 is offline.

Create `/var/db/ember-os-update` with mode 0700 in each installed root.
Provision the initial `release.json` there with `profile`, integer `generation` and the exact 40-digit `os_commit`.
Use generation zero for an initial installation outside the published generation sequence.
The running kernel must report `EmberBSD:<os_commit>` through `sysctl -n kern.buildinfo`.
On each update the installer writes the complete authenticated `update.json` as the new root's `release.json`.

## One pass

The rc service starts after LOGIN, NETWORKING and syslogd. It checks at startup and once an hour after a successful pass.
A failed pass retries after 30 seconds. Later failures double the delay up to an hour.
The service passes the profile and generation floor to the [SDK client](https://github.com/oxtech-ember/EmberBSD-SDK/blob/main/docs/sdk-client.md).
The client authenticates one release snapshot and verifies every selected archive before exposing the staged directory.
It returns status 3 when the channel has no newer generation for this installation.

During preparation the controller holds the installed SDK client's package lock.
It writes the verified sets to the inactive slot. It compares the loader's prepared identity with the SHA-256 of the exact signed descriptor bytes.
It records the generation and digest in the old root before it requests a one-shot boot.
A normal shutdown stops package updates before the OS service releases the lock.
If the candidate starts, the service matches the running slot and its receipt digest to the durable prepared identity.
It requires a positive generation, the expected running kernel and a successful local health check before confirmation.
If the candidate hangs, the independent watchdog resets the machine. The consumed one-shot entry leaves the old slot selected.
That root retains the attempt in `attempt.json` and waits for a higher generation.
An interrupted download or incomplete preparation leaves the confirmed slot bootable.
After a power loss between the durable attempt record and the loader write, the previous root can conservatively skip that generation.

Inspect `/etc/rc.d/ember_os_update status` and search `/var/log/messages` for `ember_os_update`.
To run one pass in the foreground, stop the service and run `/usr/libexec/ember-os-update once` as root.
A pass can initiate a reboot. A process lock prevents concurrent OS update passes.
A stop request lets the current operation finish and prevents the next activation step.
If an attempt is already armed, the pending entry stays in the loader. A later ordinary reboot consumes it.
On service restart in the same boot, the updater checks the prepared digest against its saved attempt before requesting the reboot.
If the old root boots again with that entry still armed, the updater cancels it and keeps the running system available.
An error clearing the entry retries cancellation without requesting another reboot.

## Loader and installer interface

The configured executable accepts the operations below. Return zero only after the operation succeeds.
Return a nonzero status on any validation, storage or firmware error.

| Operation | Required behavior |
| --- | --- |
| `status` | Print a JSON object with `running`, `confirmed`, `next`, `trial` and `prepared`. Read the actual boot and loader state. Slots are `A` and `B`. Use JSON null for an empty one-shot entry. Set the boolean `trial` when the loader entered this boot from that entry. Return `prepared` as null or as an object with `slot` and `sha256` (the lowercase digest of the exact update.json bytes). |
| `prepare SLOT DIR` | Require an inactive slot. Install the verified release from DIR. Preserve local configuration and required application data. Copy the exact bytes of DIR/update.json to the new root's release.json. After all writes succeed, persist the slot and its descriptor digest for both roots to read. Exclude the old attempt.json and incoming directory. Leave the boot selection intact. |
| `arm SLOT` | Verify the selected boot files and root. Make one durable boot attempt that returns to the confirmed slot upon reset. Consume the attempt before candidate code runs. Arm the hardware watchdog before control passes to that code. |
| `cancel SLOT` | From the confirmed root, clear the pending entry for SLOT and verify its durable removal. Keep the confirmed slot selected. |
| `confirm SLOT` | Verify the running trial slot, its receipt digest against the durable prepared identity, and its active watchdog within its deadline. Finish the watchdog handoff and persist the working slot. Verify the confirmed selection. |

If a loader write reports an error, the controller reads the slot state before retrying.
A candidate with an unrecognized root, kernel or loader state cannot pass confirmation.
Validate actual reset, boot selection and both roots on the supported machine before enabling its image profile.
