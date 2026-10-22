# Reduced C userland for CIX P1 development

This profile builds a bounded static C development cohort from one clean
EmberBSD commit. It does not build a complete base set, kernel or SD image.
The builder does not partition, mount or write storage devices.
Boot configuration, root filesystem assembly and physical acceptance are
separate steps.

Use the same GCC16 target compiler and complete host tools as the kernel
build. The selected libraries are rebuilt with the current headers and SSP
policy. An older static libc archive is not reused. The compiler's existing
libgcc runtime is recorded by hash.

```sh
BUILD_JOBS=2 HOST_CC=gcc-16 HOST_CXX=g++-16 \
    sh ember/image/cix-p1-build-userland.sh \
    /absolute/clean-source /absolute/new-output \
    /absolute/host-tools /absolute/gcc16-prefix
```

The output path must not exist. Source and build paths must be absolute,
without whitespace or shell metacharacters. The compiler must report GCC16
and an AArch64 NetBSD ABI target. A dirty tracked source tree is rejected.
The source commit must remain unchanged until the build completes.

The builder installs current headers, CSU, libc, the selected C libraries,
OpenSSL and libpcap. It relinks the console and network tools, WPA programs,
DHCP client, filesystem checkers, mount helpers, GPT and wedge tools, stat,
shutdown, wall, native getty, stty, usbdevs and mknod. It generates the target
MAKEDEV script.
Native `mknod` lets the installed target run its own architecture-specific
MAKEDEV definitions. The builder requires and records `sbin/mknod` with the
other static executables. It does not run mknod or create host device nodes.
The builder also selects native OpenSSH `sshd`, `sshd-session`, `sshd-auth`,
`ssh-keygen`, `passwd`, `pwd_mkdb` and `mount_ptyfs`. It builds libblocklist,
libwrap and private libssh before the server programs. All selected programs
use the same static target libraries and receipt checks. PAM, Kerberos, LDAP,
YP, S/Key and Hesiod are disabled explicitly before headers and libraries build.
The private libssh archive stays in the object tree; it is not an installed ABI.

The DHCP client uses `SMALLPROG=1 SMALLPROG_INET6=1`. The [native dhcpcd Makefile](../../external/bsd/dhcpcd/sbin/dhcpcd/Makefile)
retains IPv4 and adds IPv6 router discovery, SLAAC and DHCPv6.
The reduced profile still omits privilege separation, DHCP authentication and
prefix delegation. It runs as root; it does not require a new `_dhcpcd` account.
This retains the existing reduced-client privilege policy. It is not the full
base-system dhcpcd profile. Host contracts check the flags at object, build and
install stages; they do not establish a target link or live IPv6 configuration.

This C-only profile does not provide C++, PAM or the complete rc system.
The receiving root profile owns startup, console access and shutdown scripts.
Use native getty to establish the console session before its trusted wrapper.
A direct shell wrapper in `ttys` does not establish the controlling terminal.
The wrapper retains the noninteractive session leader while its interactive
child owns normal job control. Native leader teardown closes the terminal on
TERM. An interactive TERM trap can be deferred until the next input command.

The host also needs Ruby and the native `nbmkesdb` and `nbmkcsmapper` tools.
The profile installs native iconv indices, ISO646-US and UTF-8 ESDB data, and
ISO646-US mapper data using their normal build recipes. It preserves canonical
indices without filtering their entries. The selected static libc modules are
a separate input: installed DATA alone does not add a converter or establish
that every encoding named by a canonical index is available.

`I18N-INVENTORY.json` records the source revision, exact installed DATA file
hashes, and explicit native METALOG entries for those files and their parent
directories. Seven runtime paths are required: `iconv/iconv.dir`, `esdb/esdb.dir`,
`esdb/esdb.alias`, `esdb/ISO646/ISO646-US.esdb`, `esdb/UTF/UTF-8.esdb`,
`csmapper/mapper.dir` and `csmapper/ISO646/ISO646-US%UCS.646`, all below
`usr/share/i18n`. Native index databases and charset pivot files may accompany
these paths; unreviewed DATA files are rejected. The receipt pins the inventory,
profile, inventory helper and native generator hashes.

The receiving root stager checks this separate inventory and its source pin,
rejects missing, changed, symlinked, unrecorded or malformed DATA, and appends
native ownership and modes to its image specification. It checks staged DATA
again after the image-tool boundary and copies the inventory alongside the
userland receipt. A failed final check publishes no accepted image receipt.
Ruby handles inventory validation only; native tools generate conversion data.
These host contracts use synthetic DATA and build/image service doubles. They
do not establish a target relink, conversion result or physical acceptance.

Each component has separate build and install logs under `logs/`.

Failures stop the build. `BUILD-INPUTS.pending` records an incomplete run.
Only a completed run publishes `BUILD-INPUTS.txt`. That receipt includes the
source revision, compiler, compiler runtime, profile and generated wrapper
hashes. `BINARIES-SHA256.txt` records installed program hashes.
The builder inspects each installed executable as an ELF64 AArch64 executable
and rejects interpreter or dynamic segments. It does not run target programs.

The host orchestration contract uses fake build and compiler boundaries.
It tests input rejection, component ordering, missing programs and failure
propagation. It does not establish a successful target build or boot.

```sh
ruby ember/image/test-cix-p1-userland.rb
```

Keep generated binaries, local paths and personal configuration outside Git.
Network credentials belong to a separate protected configuration overlay.
Record the root-image hash and its kernel, loader and userland inputs before
deployment. Preserve a separately bootable recovery kernel and RAM root.
Use the board's [boot and recovery guidance](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/cix-p1-orangepi6plus/README.md).

The selected native shutdown and halt programs support the explicit `-i` timing
mode. It removes administrative shutdown pauses and ends process waits early
when no permitted broadcast targets remain. Live processes retain the normal
five-second TERM grace and thirty-second KILL retry limit. The mode preserves
sync, shutdown hooks and the kernel filesystem shutdown path. It rejects
options that skip synchronization or process termination. Rebuild both native
programs before enabling a root profile that requests this mode. Source contracts
model process and time services; they do not run host shutdown or establish a
physical shutdown duration.
