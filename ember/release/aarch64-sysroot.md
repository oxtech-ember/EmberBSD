# Pinned AArch64 base and compiler-support sets

Status: implementation under test. Complete build, target qualification and
publication remain pending. A CI artifact is not an accepted binary release.
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

## Build and inspect

Use a supported foreign host with C/C++, Git, Ruby, tar, gzip, xz and enough space.
Select a full source commit and fresh absolute paths outside the source checkout:

```sh
sh ember/release/build-aarch64.sh FULL_SOURCE_COMMIT /absolute/new-build
ruby ember/release/sysroot.rb prepare /absolute/new-build /absolute/new-kit
ruby ember/release/sysroot.rb verify /absolute/new-kit
```

`EMBER_RELEASE_JOBS` selects concurrency; the default is four.
The wrapper exports `source.tar.gz`, creates fresh tools, objects and destination,
and runs standard `tools kernel=EMBER64`, then `distribution sets`.
It produces `release/evbarm-aarch64/binary/sets/{base,comp}.tar.xz`,
`netbsd-EMBER64.gz`, `build-profile.json`, `build.log` and `build-success`.
Inherited make controls cannot disable diagnostics or file-list checks.
A failed producer never exposes a successful build marker.

The compressed kernel contains an ELF, not an installation image.
For QEMU's ARM64 boot image, follow the source's `mk.generic64` rule:
target objcopy `-S -O binary`, then host mkubootimage `-f arm64 -u -a 0x200000`.
Keep the timestamp option from the recorded reproducible profile.

The **AArch64 development sets** workflow builds on Ubuntu 24.04.
It preserves candidate sets, METALOG, configuration, source hash and failure logs
before removing its own temporary tree. Its artifact has limited retention.
`build-run.json` can bind a downloaded candidate to its successful run URL and
exact head commit before kit preparation.

## Qualify the actual sets

Extract into a fresh sysroot on Linux and check every selected libc alias.
For GCC16 C++, add the matching Ports development/runtime payload under
`/usr/pkg/gcc16` and record it separately. It does not replace base OS files.
The existing Ports `profiles/development-toolchain/cross/compile-tests.sh`
builds C11 plain/LTO, atomics, TLS, threads, C++20 constant expressions
and exception unwinding across a DSO. Run on the matching QEMU kernel and base.
Inspect actual loaded libraries; link-time paths alone do not establish selection.
Also run the 850-case outlined CAS provider regression and the bounded
allocator/string/stdio and loader-binding check against the selected libc.
No compiler is needed inside the execution VM.

The qualification receipt binds these measured checks to the exact inputs:

```json
{
  "source_commit": "FULL_SOURCE_COMMIT",
  "base_sha256": "BASE_SET_SHA256",
  "comp_sha256": "COMP_SET_SHA256",
  "kernel_gzip_sha256": "KERNEL_GZIP_SHA256",
  "consumer_compiler": "actual measured GCC 16.2.0 banner",
  "execution": "EmberBSD aarch64 full-system QEMU TCG",
  "checks": {
    "libc_aliases": false,
    "cross_c11": false,
    "cross_cxx20": false,
    "target_c11": false,
    "target_cxx20": false,
    "base_loader_smoke": false,
    "fork_cas": false
  }
}
```

Record true only after the check succeeds. Preserve its sources, commands,
binaries, raw results and hashes. Put the raw VM serial log in the build
directory as `target-acceptance.log`. Add its SHA256 to the qualification's
`evidence_sha256` object under that exact filename. The qualified kit includes
that log and binds its bytes. The publisher checks bindings, not execution.
Source fixtures do not establish full-base or hardware acceptance.
Installation media, physical boards, all libc tests and long runs remain separate.

```sh
ruby ember/release/sysroot.rb prepare /absolute/new-build \
  /absolute/qualified-kit /absolute/target-qualification.json
ruby ember/release/sysroot.rb verify /absolute/qualified-kit
```

## Publish and consume

The separate immutable tag is `build-FULL_SOURCE_COMMIT-aarch64-sysroot`.
The source must already belong to accepted public `main`; `latest` stays unchanged.
The GitHub integration needs `contents:write` for releases and
`administration:read` to inspect the immutable-release setting.
The publisher does not change settings. Use a current GitHub CLI with
`release verify` and `release verify-asset`; the cloud uses verified 2.102.0.

```sh
ruby ember/release/sysroot.rb draft /absolute/qualified-kit
ruby ember/release/sysroot.rb publish /absolute/qualified-kit
ruby ember/release/sysroot.rb verify-remote /absolute/qualified-kit
```

Draft upload downloads and verifies every asset again.
Publication requires qualification and checks GitHub's immutable attestation.
Changed files, extra assets and foreign tags stop it; assets are never clobbered.
Existing amd64 releases and installer defaults remain unchanged.
GitHub attestation does not claim a project signing-key distribution channel.

External CI pins the full source commit and the accepted manifest's SHA256.
From a matching Ports checkout:

```sh
ruby profiles/common-build-tools/cross/fetch-sysroot.rb \
  FULL_SOURCE_COMMIT MANIFEST_SHA256 /absolute/new-work
```

The importer verifies HTTPS, hashes and provenance before preparing the sysroot.
It preserves regular bytes and normalizes absolute links inside the target root.
It rejects traversal and escaping links. The compiler and GCC16 package are
separate inputs. Do not extract development sets over a running system.

## Source regressions

```sh
ruby ember/tests/release-aarch64-build.rb
ruby ember/tests/release-aarch64-sets.rb /absolute/bmake
ruby ember/tests/release-sysroot.rb
ruby ember/tests/release-github.rb
```

The tests execute actual wrappers with structural archives and refusal controls.
They are separate from the complete CI build and actual target execution.
