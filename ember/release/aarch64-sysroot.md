# Pinned AArch64 base and compiler-support sets

Status: proposed. Implementation and target acceptance are pending.
Origin: EmberBSD contributors, AI-assisted.

## Problem and result

The public binary channel currently has amd64 installation candidates.
External Linux CI builds also need an AArch64 EmberBSD sysroot.
An existing cloud bootstrap subset does not qualify complete release sets.
Publish `base.tar.xz` and `comp.tar.xz` from one clean full fork commit.
Include SHA256, exact build configuration, compiler identity and source provenance.

The OS owns the source build and binary-set publication.
Ports owns the GCC16 application consumer and sysroot provenance instructions.
SDK and Runtime application contracts remain separate work.

## Baseline and profile

The baseline is `e95f02d5b0727edf73aef748a1b4de1148d8d9f5`.
The build uses the fork's standard `build.sh` on a Linux development host.
The target is `evbarm`, little-endian `aarch64`, with the `EMBER64` kernel.
Source export, host tools, objects and destination start empty.
Strict release file-list checks remain enabled.
Keep base runtime, headers, startup objects and static libraries from that build.
Do not add upstream NetBSD binary sets or another build's missing files.

The in-tree GCC12.5 is a host bootstrap, as in the amd64 release.
It does not claim a GCC16 base userland or install native GCC12 commands.
GCC16 application cross-compilation and runtime selection are checked separately.
The non-desktop profile excludes X11, rump and optional debug/documentation sets.
The manifest must record every effective profile option and omitted component.
Preserve source notices, firmware licences and compatible NetBSD identifiers.

## Acceptance and integration

Build complete sets in CI, then verify their original checksums and inventory.
Bind both sets and the matching test kernel to the same full source commit.
Extract them into a fresh sysroot on Linux.
Check every libc alias and required startup object before consumer compilation.
Cross-build C and C++ consumers with the common GCC16 toolchain.
Run the consumers and relevant fork libc regressions in disposable AArch64 QEMU.
Record exact artifact hashes, numerical results and actual loaded runtime paths.
Reject changed sets, mixed source receipts and incomplete qualification.
Download the published assets again and verify their exact bytes.

The binary channel uses a separate immutable tag for this source and profile.
It must not replace either accepted amd64 release or its installer defaults.
The release remains a development sysroot, without installation-media acceptance.
Physical boards, graphics, cameras and long-duration operation are out of scope.

## Recovery

Use disposable output and guest paths and preserve the existing SDKs.
No device installation, base upgrade, reboot or media write is required.
A failed build remains unqualified and must not reuse an older success marker.
Retain the accepted source, receipts and binary hashes before temporary cleanup.
