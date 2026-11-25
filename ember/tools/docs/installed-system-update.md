# Update an installed EmberBSD system

Use this entry for physical machines and VMs. Select the operation first.

| Requested change | Procedure |
| --- | --- |
| Update one application or all installed packages | Use the native [SDK client](https://github.com/oxtech-ember/EmberBSD-SDK/blob/main/docs/sdk-client.md#installed-versions-and-updates). |
| Enable package updates at boot | Use the [package service](../../image/README.md#package-updates-on-a-running-system) on an image that includes it. |
| Enable unattended OS updates on a provisioned two-slot installation | Use the [automatic OS service](automatic-system-update.md). |
| Update the running OS manually | Use the signed release and a supported installation path for the installed profile, as described below. |

## Select and fetch the release

Record the running system with `uname -m` (machine), `uname -p` (CPU architecture), `sysctl -n kern.buildinfo`, `mount` and `df -k`.
Identify the boot loader, the kernel path it actually loads, the root volume and the installed distribution sets.
Choose a published release that covers this architecture, system profile, kernel configuration and boot path.
A shared architecture can use the same userland sets. Boot files depend on the loader and hardware.
For a custom kernel, obtain the kernel and compatible modules from its provider. Preserve its configuration and previous boot entry.

For a release delivered through the signed S3 channel, take each exact artifact ID from its release or profile instructions.
On a system with native SDK client 0.26 or later, an ordinary user can stage each archive on FFS.
Set `artifact_id` and `output` to the selected ID and a new output file in a private directory.

```sh
/usr/pkg/bin/python3.15 -I /usr/local/libexec/ember-sdk.pyz \
    --config /usr/local/etc/ember-sdk.json fetch-artifact \
    --artifact "$artifact_id" --output "$output"
```

The client verifies the channel and release signatures, size and SHA-256. Keep the returned record with the archive.
Each fetch reads the current channel. Before installation, compare all returned IDs, targets and hashes with the selected release and profile.
Collect every required component before writing the live system. A missing artifact, unsupported profile or failed verification stops this update.
[Signed artifact contract](https://github.com/oxtech-ember/EmberBSD-SDK/blob/main/docs/sdk-client.md#download-one-release-artifact)

## Prepare recovery and choose an installer

Preserve the working boot files, matching modules, configuration, application data and package inventory. Test the recovery path.
For a VM, preserve its powered-off disk and any writable firmware variables. For physical hardware, identify the recovery entry or medium.
The profile's installer determines how to write the selected sets and how to return to the previous installation.

- For a sysinst-based system, boot matching upgrade-capable media and select the installed root. Check the [installer's supported operations](../../../usr.sbin/sysinst/docs/text-installer.md) before modifying it. The [UEFI VM example](../../../usr.sbin/sysinst/docs/vm-installation.md#upgrade-and-configuration-merge) identifies which image profiles permit upgrade.
- For a set-based system, stage the matching kernel, modules and required userland sets. Follow the profile for boot files and use the [native userland steps](#native-set-installation) below when it supports them. The [AArch64 guide](aarch64-base-update.md#recovery-and-installation) explains the kernel and userland ABI order. Consult the hardware profile for its boot files, media layout and recovery entry. Preserve the active firmware and recovery tools.

## Native set installation

Use this route only for a profile that supports native set replacement. A sysinst profile uses its installer.
As root, prepare a mode 0700 staging directory on the installed root filesystem. Copy downloaded archives into that directory.
Recheck each copy against the digest from its authenticated release record. Set `new` to this root-owned directory.
Extract the selected sets into a separate candidate root. Test its shell, dynamic loader and required services before replacing files.
Keep a root-readable backup of every file selected for replacement and the list of paths the update adds.
Use a backup format readable with the system's retained rescue tools.

For example, a headless profile can replace the standard program and library directories from `base`, and merge `etc` separately.
This example uses NetBSD bsdtar and the member names in the native EmberBSD sets. Run it as root after backing up the selected paths.
Set `state` to a new private directory on FFS. Compare the selected roots with the installed profile before extraction.

```sh
/bin/tar -tf "$new/base.tar.xz" > "$state/base.list" || exit 1
awk '{ sub(/\/$/, "") }
    /^\.\/(bin|sbin|lib|libexec)$/ ||
    /^\.\/usr\/(bin|sbin|lib|libexec|libdata|include|mdec|share)$/ ||
    /^\.\/etc\/(release|mtree\/set\.base)$/ { print }' \
    "$state/base.list" > "$state/base.roots" || exit 1
test -s "$state/base.roots" || exit 1
cat "$state/base.roots"
```

Use one selection for the backup inventory and the install. Keep the selection in the root-owned staging directory so a later boot uses the same paths.
Set `extra` to the additional payload sets required by the installed profile.
For a minimal headless system, use the empty value below. A development system can select `comp`.
An existing X11 installation also needs the matching `xbase xcomp xfont xserver` payloads and the `xetc` defaults.

```sh
extra=''                         # or: extra='comp xbase xcomp xfont xserver'
printf '%s\n' "$extra" > "$state/extra.sets" || exit 1
cat > "$state/payload.sh" <<'SH' || exit 1
payload() {
    mode=$1
    archive=$2
    shift 2
    /bin/tar --safe-writes --exclude './rescue' --exclude './usr/pkg' \
        --exclude './libdata/firmware' --exclude './usr/libdata/firmware' \
        "$mode" -f "$new/$archive.tar.xz" "$@"
}
native_sets() {
    extra=$(cat "$state/extra.sets") || return 1
    payload "$1" base -T "$state/base.roots" || return 1
    for archive in $extra; do
        case "$archive" in comp|xbase|xcomp|xfont|xserver) ;; *) return 1 ;; esac
        payload "$1" "$archive" || return 1
    done
}
SH
. "$state/payload.sh" || exit 1
native_sets -t > "$state/set-paths.raw" || exit 1
LC_ALL=C sort -u "$state/set-paths.raw" > "$state/set-paths" || exit 1
mkdir "$state/defaults" || exit 1
/bin/tar -xpf "$new/etc.tar.xz" -C "$state/defaults" || exit 1
case " $extra " in
    *" xbase "*) /bin/tar -xpf "$new/xetc.tar.xz" -C "$state/defaults" || exit 1 ;;
esac
```

Review `$state/set-paths` against the installed profile. Back up the listed existing files, record the new paths and back up local configuration.
Keep the uncompressed backup readable by the static rescue tools. Confirm that it can be read before replacing files.
If the boot procedure requires an intermediate reboot, reopen a root shell. Set `new` and `state` to their original absolute paths.
Recheck the staged archive hashes against the authenticated release records. Load the saved selection and compare it with the backup inventory.
Then apply the payloads and merge the staged defaults once:

```sh
. "$state/payload.sh" || exit 1
native_sets -t > "$state/apply.raw" || exit 1
LC_ALL=C sort -u "$state/apply.raw" > "$state/apply.list" || exit 1
cmp "$state/set-paths" "$state/apply.list" || exit 1
(cd / && native_sets -xp) || exit 1
etcupdate -s "$state/defaults" || exit 1
postinstall -s "$state/defaults" check
```

This selection preserves the package prefix, `/rescue`, `/libdata/firmware`, `/usr/libdata/firmware` and local service configuration.
Review obsolete files and each proposed `postinstall` fix.
If extraction fails, restore the previous userland from the tested backup before booting. Keep a matching kernel and modules with it.

## Apply and verify

Follow the selected installer for set extraction. Preserve the existing package prefix, application data and trusted configuration.
Merge the new configuration defaults with the installed configuration. The installer may stage those defaults for a later `etcupdate` run.
Review `postinstall` findings before accepting the result. Keep local accounts, SSH keys, network settings and enabled services.
Observe the release's kernel and userland ABI order. A [station association ABI transition](../../../sys/net80211/association-lifecycle.md) requires matching WPA userland before the first new boot.

After a normal reboot, verify the running revision with `sysctl -n kern.buildinfo`. Compare installed base files and modules with the verified staged files. Check the mounted root, network and a new login.
Compare the package inventory and saved settings. Run a service or application that uses them.
Keep the previous installation available until these checks pass. OS activation and recovery currently use the selected installer and boot path.
