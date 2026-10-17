# Pinned AArch64 base and compiler-support sets

Status: full source build and finite AArch64 VM qualification passed.
Binary publication remains pending. A CI artifact is not a public release.
Origin: EmberBSD contributors, AI-assisted.

## Qualified snapshot

External Linux CI needs matching fork runtime, headers and startup objects.
The OS owns the complete source build and binary publication. Ports owns
the [GCC16 consumer and import contract](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-build-tools/cross/sysroot.md).

The qualified source is `6dd57144183678938164b4c2dcfc370cdbde8473`.
The [successful producer](https://github.com/oxtech-ember/EmberBSD/actions/runs/38843939746)
uses standard `build.sh` on Ubuntu 24.04, with fresh source, tools, objects
and destination. Strict `checkflist` remains enabled. No upstream NetBSD
binary sets or earlier cloud bootstrap files are substituted.

| Artifact | SHA256 |
| --- | --- |
| `base.tar.xz` | `688dc6701cd83283d1287fc18dee8b9af35f067096cdd4a92de7e0593fdacddb` |
| `comp.tar.xz` | `8c813303f0500ae1a4ba6381eee6a1189ffab86036963fb26f36ec313e02daa6` |
| `netbsd-EMBER64.gz` | `fbc6fa0afe2a91e7798734f58a326d18ccb20bb46e20b1f3579359264e68ea90` |

The qualified kit's manifest SHA256 is
`efc1cd827598451b00b7d743b35a4901a3fd25cf24191b3b3bb679d53f068f5b`.
It identifies a prepared kit, not a published download channel.

The profile is `evbarm/aarch64`, little-endian, with `EMBER64`.
The in-tree GCC12.5 builds the base runtime; native GCC12 commands are omitted.
Application consumers use the separate common GCC16.2 headers and runtime.
X11, rump, compat, optional modules, manuals, debug, profile libraries and
test sets are omitted. `build-profile.json` records every selected option.

The finite full-system QEMU TCG run uses Cortex-A72, two CPUs and 1 GiB RAM:

- Both sets pass producer SHA256/SHA512 checks. The snapshot has 8188 entries;
  5880 regular files match METALOG. The two generated set inventories are
  covered by archive hashes. All four libc aliases select one fork provider.
- GCC16 C11 plain/LTO, atomics, TLS and threads pass. C++20 constant expressions
  compile; threads and exception unwinding across a DSO pass on the target.
  Loaded `libstdc++` and `libgcc_s` paths and hashes match the selected GCC16.
- Shared and static libc each pass 850 CAS checks across 20 variants.
  The static link map binds every variant to this build's `libc.a`.
- GNU M4 1.4.21 passes 236 of 243 upstream invocations, with seven optional
  `changeword` skips. Stack-overflow handling passes. The guest shuts down normally.

The kit binds the raw serial log to its qualification receipt. The execution
image contains no compiler. This is finite ABI/consumer acceptance, not all
base commands, all libc tests, a memory-checker run, installation media or
physical-board acceptance. Bit-for-bit reproduction across hosts is unverified.

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

The separate **Sysroot release contracts** workflow checks the proposed helper
revision. A synchronized PR can reuse an earlier successful OS producer only
when its exact-source artifact remains available and OS producer inputs have
not changed. The job names that source and run explicitly. It does not claim
new binaries for the helper revision or target qualification. Manual dispatch
always builds the selected source again. Preserve the actual producer's
commit and profile when preparing a kit.

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
