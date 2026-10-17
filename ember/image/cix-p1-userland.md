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
shutdown, wall and native getty. It generates the target MAKEDEV script.
This C-only profile does not provide C++, login/PAM or the complete rc system.
The receiving root profile owns startup, console access and shutdown scripts.
Use native getty to establish the console session before its trusted wrapper.
A direct shell wrapper in `ttys` does not establish the controlling terminal.

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

The cohort at source `dafccbdc09153ffc85be31fadfd3f7a6cdf536fe`, extended
with native getty at the same source and build configuration, passed a GCC16.2
cross-build on macOS. All 77 installed programs passed little-endian AArch64
static ELF checks and received SHA256 records. Current target headers and the
strong SSP flags were verified in actual compiler arguments. These are build
and artifact checks; target execution and persistent startup remain separate.

Keep generated binaries, local paths and personal configuration outside Git.
Network credentials belong to a separate protected configuration overlay.
Record the root-image hash and its kernel, loader and userland inputs before
deployment. Preserve a separately bootable recovery kernel and RAM root.
Use the board's [boot and recovery guidance](../boards/orange-pi-6-plus.md).
