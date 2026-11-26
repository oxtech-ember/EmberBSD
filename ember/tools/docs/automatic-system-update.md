# Automatic updates on a system with two boot slots

The native [base service](../../../usr.sbin/ember-update/README.md) selects a signed release for the installed hardware and ABI profile.
It can prepare OS and package updates in the same inactive root.
A two-slot profile needs independent bootable roots, one-time selection and a reset provider that survives kernel entry.

## Build an image with automatic updates

Select the board with `-h` and pass its trusted update profile with `-U`.
Use a matching signed release for the kernel, modules, base, etc and optional comp sets.
Use the original etc archive and its authenticated SHA-256 to initialize the three-way merge.
Provide a site overlay with `-c` if required for networking, supported firmware or local services.

```sh
export OS_UPDATE_PYTHON="$HOST_PREFIX/bin/python3.15"
export OS_UPDATE_OS_COMMIT="$OS_COMMIT"
export OS_UPDATE_ETC="$SIGNED_ETC_ARCHIVE"
export OS_UPDATE_ETC_SHA256="$SIGNED_ETC_SHA256"
sh "$OS_SOURCE/distrib/utils/embedded/mkimage" -h "$BOARD" -U "$UPDATE_PROFILE" \
    -S "$OS_SOURCE" -D "$DESTDIR" -K "$KERNEL_DIRECTORY" "$IMAGE"
```

The host checks the profile, machine code in the release and target programs, module list and configuration baseline.
The image includes its hardware profile, local health check, release anchor and initial slot markers.
Set `OS_UPDATE_HEALTH` to install a site-specific executable check.
Use a probe that can pass locally after a successful trial, including when S3 is unavailable.

## Image configuration

Set `ember_update=YES` in `/etc/rc.conf`. Update images also enable TLS certificate initialization.
Choose the release channel, package and OS policy and reboot window in that file.
An explicit `ask` policy leaves a ready slot for the next authorized restart.
Use `ember-update status` before restart and `ember-update history` after it.
The installer holds OS upgrades for a locally installed custom kernel.
[Policy and public state](../../../usr.sbin/ember-update/README.md)

## Loader and installer interface

The controller verifies a plan and delegates FFS preparation to `ember-install-slot`.
This program takes a snapshot, prepares the inactive root, merges defaults, applies packages, validates the result and records a receipt.
A package-only selection follows the same slot transaction. Each slot has its own package database.
The adapter supplies `status`, `arm`, `cancel` and `confirm`. Its boot protocol reports the actual running root and trial identity.
U-Boot reads the one-shot markers on FAT. EFI reads the selected menu and consumes its marker on the ESP.
Only an image with a qualified reset provider can arm an unattended trial.
[Command and receipt contracts](../../../usr.sbin/ember-update/README.md) · [U-Boot adapter](uboot-ffs-slots.md)

## Earlier releases

Already installed G4 images use the service and receipts shipped with that generation.
Use the [G4 service procedure](https://github.com/oxtech-ember/EmberBSD/blob/c3bed3cee44/ember/tools/docs/automatic-system-update.md) for recovery of that installation.
