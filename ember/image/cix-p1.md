# CIX P1 persistent development root

This document specifies root assembly and startup contracts.
Hardware applicability and physical evidence belong to [DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/cix-p1-orangepi6plus/boot.md).
This profile targets the observed CIX P1 CD8180 board and RTL8822CE adapter.
It builds a reduced static C userland from EmberBSD sources with GCC16.
It does not produce a complete base release or an unattended disk installer.

## Inputs and build

Use a clean source commit, complete EmberBSD host tools, and the common
AArch64 GCC16 toolchain. The build uses separate object and destination trees.
It rebuilds the selected libraries before linking the selected commands.
It fences target headers against old compiler `sys-include` symlinks.
The receipt records source, compiler and runtime hashes.
Every selected target executable must be little-endian static AArch64 ELF.

```sh
sh ember/image/cix-p1-build-userland.sh \
    /absolute/source /absolute/new-userland /absolute/tools /absolute/gcc16
sh ember/image/cix-p1-root.sh \
    /absolute/new-userland /absolute/source /absolute/tools \
    /absolute/rtw8822c_fw.bin /absolute/new-root
```

The root assembler requires the exact source commit in the userland receipt.
Run it with the startup scripts from the reviewed profile revision.
The external Realtek firmware must have SHA256
`36b483e37640359a6c686be4661b5f2d94928f93e126a2de09e67c5e04c01b26`.
The image retains the Realtek firmware license. The
[firmware provenance](../../sys/dev/ic/rtw88-provenance.md) records its source.
The output is a new 2 GiB UFS2 image and its staging tree, metadata and hash.
The assembler never opens a block device. It does not include credentials.

## Target provisioning

Use the [board storage and recovery profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/cix-p1-orangepi6plus/boot.md#persistent-sd-root-and-recovery-applicability).
It defines where this filesystem image can be used. The assembler never selects a physical disk.

## Wireless configuration

After verifying the installed root, provision `/etc/wpa_supplicant.conf` separately as root with mode `0600`.
Use a WPA2-PSK network with CCMP and the intended SSID. Keep `update_config=0`.
Do not bind the normal profile to one BSSID or frequency when both bands share the SSID.
Keep credentials out of Git and shared images. The startup code checks the file before use.

## Optional SSH access

The reduced cohort includes the native listener, both OpenSSH reexec helpers,
`ssh-keygen`, `passwd`, `pwd_mkdb` and the PTY filesystem mount helper.
The image includes empty `/etc/ssh` and `/var/chroot/sshd` directories and native
PTY multiplexor nodes. It includes no SSH configuration, host key or password.
It does not add the `sshd` account or change the locked root account template.
Recreating an image does not preserve account changes made to an installed root.

Provision access separately from a trusted local console on the writable
persistent root. Create the native locked `sshd` privilege-separation account
and group with UID/GID 16. Keep `/var/chroot/sshd` root-owned and not writable by
other users. Set a nonempty root password with native `passwd -l root`.
Keep account databases, host keys and personal configuration outside Git.
Generate or install host keys only on the intended persistent root.
The profile never runs `ssh-keygen` automatically, including on first boot.

An administrator-created `/etc/ssh/sshd_config` enables startup. It must be a
root-owned regular file with mode `0600`. Symlinks are rejected. Its directory
must be root-owned, not a symlink, with mode `0700` or `0755`.
Select the intended host key explicitly. For password access, set
`PermitRootLogin yes`, `PasswordAuthentication yes`, `PermitEmptyPasswords no`
and `KbdInteractiveAuthentication no`. The build excludes PAM. Omit `UsePAM`:
this native build reports that directive as unsupported, even with value `no`.
Do not advertise an uninstalled external SFTP subsystem.

Absent configuration leaves SSH disabled without writes or mounts.
Startup validates native configuration, keys and privilege-separation setup with
`sshd -t`. It separately requires both executable reexec helpers. It mounts
`ptyfs` at `/dev/pts` when absent, reuses an existing `ptyfs`, and rejects a
conflicting mount. It launches the listener before network startup and after
persistent-root and entropy setup. Native diagnostics remain visible.
A PID file or an earlier daemon invocation blocks a duplicate launch in that
startup context. A failed launch requires operator inspection before retry.
SSH failure reports a warning and leaves local-console startup available.

Host script contracts check build selection, opt-in, metadata, ordering and
error handling with explicit build/daemon/filesystem boundary doubles.
They do not establish a complete cohort build or authenticated target login.
See DevBoards for bounded physical evidence.

```sh
ruby ember/image/test-cix-p1-userland.rb
ruby ember/image/test-cix-p1-ssh.rb
ruby ember/image/test-cix-p1-root-image.rb
```

## Startup and checks

See [startup, shutdown and source checks](cix-p1-startup.md).
