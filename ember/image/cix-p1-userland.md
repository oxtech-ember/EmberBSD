# Reduced C userland for CIX P1 development

This profile builds a bounded static C development cohort from one clean
EmberBSD commit. It does not build a complete base set, kernel or SD image.
The builder does not partition, mount or write storage devices.
Boot configuration, root filesystem assembly and physical acceptance are
separate steps.

Use the selected Clang target toolchain and installed native SDK tools. The
complete or imported AArch64 SDK sysroot supplies the common LLVM runtime;
no bootstrap stage is required. The selected libraries are rebuilt with the
current headers and SSP policy. An older static libc archive is not reused.

```sh
export EMBERBSD_SDK_SOURCE=/absolute/sdk-source
export EMBER_PORTS_SOURCE=/absolute/ports-source
export EMBER_HOST_PREFIX=/absolute/native-prefix
export EMBER_HOST_BFD_CONFIG=/absolute/host-bfd/host-bfd.mk
export EMBER_LIBEDIT_SOURCE=/absolute/prepared-libedit
export EMBER_LIBARCHIVE_SOURCE=/absolute/prepared-libarchive
export EMBER_LIBEVENT_SOURCE=/absolute/prepared-libevent
export EMBER_BYACC_SOURCE=/absolute/prepared-byacc
export EMBER_ISCSI_SOURCE=/absolute/prepared-iscsi
export EMBER_LIBPCAP_SOURCE=/absolute/prepared-libpcap
export EMBER_TCP_WRAPPERS_SOURCE=/absolute/prepared-tcp-wrappers
export EMBER_LIBCBOR_SOURCE=/absolute/prepared-libcbor
export EMBER_LIBFIDO2_SOURCE=/absolute/prepared-libfido2
export EMBER_LIBFETCH_SOURCE=/absolute/prepared-libfetch
export EMBER_MDNS_SOURCE=/absolute/prepared-mdns
export EMBER_DHCPCD_SOURCE=/absolute/prepared-dhcpcd
export EMBER_LLVM_SOURCE=/absolute/prepared-llvm
export EMBER_SDK_SYSROOT=/absolute/completed-aarch64-sysroot
"$EMBER_HOST_PREFIX/bin/python3.15" -B ember/image/cix-p1-native.py \
    /absolute/clean-source /absolute/new-output \
    /absolute/host-tools /absolute/target-clang-prefix
```

Build parallelism defaults to the CPU capacity reported by the selected SDK.
Set `BUILD_JOBS` to a positive integer to override it. If the SDK cannot
determine CPU capacity, the producer asks for an explicit `BUILD_JOBS` value.

`/absolute/host-tools` is the `TOOLDIR` of a native-mode OS build, such as the
SDK sysroot build. It does not need `nbmake`. Native-mode `build.sh` uses the
make from the SDK native-tool routing.

The SDK's `run-native-os` selects the native compiler and tools, independently
of the target family. `EMBER_HOST_COMPILER_FAMILY=gcc` explicitly selects the
native GCC alternative. `EMBER_TARGET_COMPILER_FAMILY=gcc` selects the GCC16
target alternative; that route does not require `EMBER_SDK_SYSROOT`. The
common LLVM source is still needed for libc and libm. No compiler fallback is automatic.

`EMBER_LIBEDIT_SOURCE` names the libedit source prepared by EmberBSD-SDK
`scripts/prepare-libedit.py` from the Ports recipe ([base libedit](../../lib/libedit/README.md)).
`EMBER_LLVM_SOURCE` names the matching LLVM source prepared by Ports. The
selected Ports checkout verifies the prepared libedit, libarchive, libevent, byacc, iSCSI, libpcap, TCP wrappers, libcbor, libfido2, libfetch and LLVM
sources against their recipes. Root `obj` and `includes` phases consume the
libarchive, libevent, byacc, iSCSI, libpcap, TCP wrappers, libcbor, libfido2 and libfetch source trees even in this reduced profile. The
host BFD configuration supplies the OS native tool contract. The output path
must not exist. Source and build paths must be absolute, without whitespace or
shell metacharacters. A dirty source tree is rejected and its commit must remain
unchanged until the build completes.

The builder installs current headers, CSU, libc, the selected C libraries,
OpenSSL and libpcap. It relinks the console and network tools, WPA programs,
DHCP client, filesystem checkers, mount helpers, GPT and wedge tools, stat,
shutdown, wall, native getty, stty, usbdevs and mknod. It generates the target
MAKEDEV script.
The sensor diagnostics `envstat`, `nvmectl`, `wdogctl` and `acpidump` use the
same static libraries; `envstat` also needs the netpgp `libmj` archive, built
before it. They read SoC thermal zones, NVMe health pages, watchdog state and
the firmware ACPI tables through `/dev/acpi` on the board.
Host contracts check their selection and order; a target link and readings on
the physical board are separate checks.
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
It sets `MKCOMPAT=no` and builds no 32-bit compatibility libraries. The
common LLVM runtime has no multilib stage, so the Clang route requires this.
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
EMBER_TEST_NATIVE_PREFIX="$EMBER_HOST_PREFIX" \
    "$EMBER_HOST_PREFIX/bin/python3.15" -B ember/image/test-cix-p1-userland.py
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

## Minimal static bring-up console

The same builder can select a small C-only console profile for `aarch64` or
`riscv32`. This bring-up profile is a single-user recovery console: `init` starts a root shell on
`/dev/console` without a login prompt. It is intended only for a controlled
local console. It does not enable network services or provide a multi-user
login. It produces a 4 MiB FFS2 image without writing a device. The profile
supports the LLVM C foundation with Clang; explicit GCC is rejected.
`EMBER_USERLAND_PROFILE=cix` (or an unset variable) selects the original CIX
profile; unknown profile names are rejected.

Set the common source, native prefix, host BFD and libedit inputs above, plus
`EMBER_BYACC_SOURCE`. Select a completed SDK `--c-foundation` stage and its
matching bootstrap runtime; a complete C++ SDK is not required:

```sh
export EMBER_USERLAND_PROFILE=console
export EMBER_CONSOLE_TARGET=riscv32
export EMBER_C_FOUNDATION=/absolute/sdk/stages/llvm-c-foundation-riscv32/base
export EMBER_LLVM_RUNTIME_STAGE=/absolute/sdk/obj/llvm-runtime/bootstrap-stage
# If the runtime was built from an equivalent earlier prepared LLVM tree:
export EMBER_LLVM_RUNTIME_ORIGINAL_SOURCE=/absolute/earlier-prepared-llvm
"$EMBER_HOST_PREFIX/bin/python3.15" -B ember/image/cix-p1-native.py \
    /absolute/source /absolute/network-output /absolute/os-host-tools /absolute/target-clang-prefix
```

The profile extends the verified foundation in a separate destination; it
installs the native `libterminfo`, `libedit` and `libutil` build inputs and
links `sh` and `init` with `SMALLPROG=1`. The root contains those programs and
`console`, `tty`, `null` and `zero` nodes from the target `MAKEDEV`. This is
not a full base set. It does not include terminal capability data, utilities,
networking, credentials or writable storage setup. Image generation and
static ELF checks do not establish root mounting, UART or target execution.
The receiving board owner must supply a compatible boot path and verify the
complete memory and storage layout.

A trial may use a modified OS checkout. The result records the source commit,
whether the checkout was modified, and a digest of the tracked changes and
untracked inputs; the builder checks that state again before accepting the
result. A modified-source trial is not a release built from published source.

Set `EMBER_USERLAND_PROFILE=console-tools` to select the opt-in compact
console. The native OS `nbcrunchgen` builds one static multicall executable
with `sh`, `init`, `hostname`, `sysctl`, `dmesg`, `ifconfig`, `cat`, `ls`,
`chmod`, `cp`, `mkdir`, `sleep`, and `reboot`; their normal paths are symbolic
links. The `-sh` crunch alias preserves the login-shell argument supplied by
`init`, and `halt` dispatches to the normal `reboot(8)` implementation with
its halt mode.
This profile adds `klog`, `iic0`, `ttyE0` and `wsmouse0` using the target's `MAKEDEV`, and
requires the selected host prefix's LLVM `nm`/`objcopy` and `nawk`. It is a
bring-up image, not a full base set.

## Static network userland staging

`EMBER_USERLAND_PROFILE=network` extends the same verified C foundation through
the native OS component runner. It builds full `init` (without `SMALLPROG`),
`getty`/`login`, the standard rc dispatcher, network tools and the selected
WPA, dhcpcd and mDNSResponder targets. The staged root uses normal `/etc/rc`,
`/etc/ttys` and `/etc/fstab`; it mounts `/tmp` and `/var/run` as tmpfs and
starts `wpa_supplicant`, `dhcpcd` and `mdnsd` through selected standard rc.d
scripts. Small base utilities share the existing `console-tools` crunch
binary; `init` remains a full standalone program. It does not build a
filesystem image. The result is a staged tree and `root.spec` for the existing
root-image producer.

This profile needs the existing common inputs above plus
`EMBER_CONSOLE_TARGET`, `EMBER_C_FOUNDATION`, `EMBER_LLVM_RUNTIME_STAGE`,
`EMBER_MDNS_SOURCE`, `EMBER_DHCPCD_SOURCE`, `EMBER_LIBPCAP_SOURCE`,
`EMBER_WPA_SOURCE`, `EMBER_PKGSRC_EXPORT`,
`EMBER_NETWORK_OVERLAY`, an explicit `EMBER_NETWORK_INTERFACE`, and explicit
`EMBER_NETWORK_ROOT_DEVICE` plus optional `EMBER_NETWORK_ROOT_PARTITION` (default `a`).
`EMBER_LIBPCAP_ARCHIVE` is an optional exact distfile input when the prepared
libpcap receipt predates the selected effective recipe; current matching
prepared sources need no archive. The selected root device group and partition are generated by target `MAKEDEV`,
including the block and raw nodes used by the standard root/fsck path. The mDNS,
DHCP, libpcap and WPA sources are checked against the selected Ports recipes
and shared export.
libpcap is built from its shared OS target for the WPA link. WPA uses the shared
personal profile selector; the common crypto backend remains at its normal default unless
`EMBER_WPA_CRYPTO_BACKEND` is explicitly selected. Interface names and local
identity are inputs, not board defaults. The existing `EMBER_CONSOLE_PROGRAM`
input may add a statically linked target application such as a GUI to `/bin`;
it is validated and recorded like the console profile input.

```sh
export EMBER_USERLAND_PROFILE=network
export EMBER_CONSOLE_TARGET=riscv32
export EMBER_C_FOUNDATION=/absolute/sdk/stages/llvm-c-foundation-riscv32/base
export EMBER_LLVM_RUNTIME_STAGE=/absolute/sdk/obj/llvm-runtime/bootstrap-stage
export EMBER_NETWORK_INTERFACE=wifi0
export EMBER_NETWORK_ROOT_DEVICE=md0
export EMBER_NETWORK_ROOT_PARTITION=a
export EMBER_NETWORK_OVERLAY=/absolute/private/network-overlay
# Optional: export EMBER_CONSOLE_PROGRAM=/absolute/target/lvgl-wscons
"$EMBER_HOST_PREFIX/bin/python3.15" -B ember/image/cix-p1-native.py \
    /absolute/source /absolute/network-output /absolute/os-host-tools /absolute/target-clang-prefix
```

The output contains `root/` and `root.spec` for the existing root-image
producer. It deliberately does not invoke `nbmakefs` or write a filesystem
image. `MAKEDEV` supplies the standard entropy devices and BPF node used by
the selected WPA/DHCP paths; the staged resolver, DHCP state and runtime
directories remain writable on the root filesystem or under `/var/run`.
Repeated runs reuse the same compatible `obj/` and `dest/` trees, including a
completed output. A small compatibility record protects target, ABI, runtime
and compile configuration; source and overlay edits flow through the normal
incremental build. The producer stages a fresh root and `root.spec` beside the
previous pair, validates the selected executables, then replaces both. A failed
build or check leaves the prior pair and its `BUILD-INPUTS.txt` intact.
`BUILD-INPUTS.txt` records the latest provenance but does not prevent reuse.

The selected overlay may add or replace regular configuration files in the
fresh staged root; symlinks remain rejected. It can supply `/etc/myname`, `/etc/wpa_supplicant.conf` and
`/etc/rc.conf.d/` service settings without replacing the OS account database.
Keep credentials and local identity in a private overlay. The standard OS
account database is retained and the `_mdnsd` service account is added from
the selected OS source. Do not boot this profile with `-s`: that explicitly
selects single-user init and skips the normal rc path.

The network profile is separate from the single-user `console` and compact
`console-tools` profiles. Those profiles remain bring-up tools and do not start
network services.

The network crunch binary includes `ping` for root-run diagnostics. It opens raw
ICMP sockets and is not installed setuid in this profile; run it as root.

Optional `EMBER_CONSOLE_PROGRAM` names a prebuilt static target ELF to add to
`/bin` using its basename in the `console-tools` or `network` profile.
Optional `EMBER_CONSOLE_OVERLAY` names a directory
whose regular files and directories are added to the image; it cannot replace
base paths or contain symbolic links. The image and its optional inputs may
contain private data and must not be published inadvertently. The builder
checks the target program's ELF ABI and static linkage and records its SHA256;
it checks both inputs for changes during the build. The caller supplies any
application-specific license files in the overlay. Neither this check nor image
creation proves that an application or its device drivers run on the target.

To check the generated login-shell dispatcher using the selected native tools:

```sh
"$HOST_PREFIX/bin/python3.15" -B ember/image/test-console-tools-dispatch.py \
    --host-prefix "$HOST_PREFIX" --crunchgen "$TOOLDIR/bin/nbcrunchgen"
```
