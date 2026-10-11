# EmberBSD agent instructions

## Start

Read [README.ember.md](README.ember.md) and the relevant board or build guide.
Run `git status --short` and inspect the branch and remotes before editing.
Preserve unrelated changes. Use normal Git updates. Never force-push.
Talk to the user in Russian unless they choose another language.
Write all repository content in English. Use Conventional Commits.
Use short active sentences and exact CLI/API names, in ASD-STE100 style.
Keep task recipes in their owning documentation; link them from this file.

## Port to the current stack

Prioritize porting over compatibility workarounds and legacy dependencies.
Required libraries and tools must build with current standard EmberBSD tools.
Use the shared GCC16 toolchain and current supported dependencies from EmberBSD-Ports.
Select current stable releases, or supported LTS releases with a documented reason.
Never install older compilers or libraries to satisfy dependency builds.
Do not add parallel legacy versions for individual consumers.
Patch incompatibilities in the owning OS sources or Ports recipes.
Adapt affected consumers to the shared dependency version.
Use coding agents to produce focused patches. Review and test those patches.
Do not suppress diagnostics, disable checks, or add silent stubs to obtain a passing build.
Record missing modern capabilities and continue porting; workaround instructions do not complete the fix.

## Sources and ownership

Board support and provenance belong in `ember/`; kernel changes belong in `sys/`.
Follow NetBSD KNF for C. Preserve upstream licenses, authorship, and identifiers.
Start each project source diff with exactly one `Origin:` line.
Name the upstream revision for imported changes. Disclose AI assistance.
The target OS is EmberBSD. Retained NetBSD ABI names and triplets are compatibility interfaces.
Do not substitute upstream base sets for fork components.
Record source revisions and artifact hashes for kernels, libc, headers, and sysroots.
Do not accept a partial or mixed sysroot as a current EmberBSD base release.
Keep reusable third-party fixes in Ports and standalone applications in EmberBSD-Examples.
Keep application roles, models, credentials, and configuration outside OS sources.
Public instructions must work without private repositories. Exclude their names and addresses.
Keep network secrets, SSH keys, Bluetooth bonds, personal images, and build outputs out of Git.
New project helpers do not use Python. Keep existing upstream requirements explicit.
Read current NetBSD contribution rules before sending changes upstream.

## Build and verify

Export a clean commit for builds. Never leave a fix only in a build guest or cache.
Prefer cross-compilation on the development host. Execute target programs on a VM or board.
The kernel wrapper uses `build.sh` by default, including on NetBSD hosts.
Use `EMBER_BUILD_MODE=native` only for an identified native fallback.
Port missing cross support in OS or Ports; do not require a NetBSD development host.

```sh
CC=gcc sh ember/tools/kernel-contracts.sh "$PWD" portable
sh ember/tests/build-kernel.sh
NETBSD2_JOBS=4 sh ember/build-kernel.sh /absolute/output EMBER64
```

Select checks for the affected change. Check exit statuses, deliverables, and recorded hashes.
Keep kernels and modules from the same build. Failed builds must not expose stale artifacts.
Distinguish source, build, contract, VM, physical-board, and sustained-use evidence.
QEMU execution does not prove physical-board support.

## Public overview

Review the root README after a substantial feature, port, example, validation result, regression, or support withdrawal.
Review companion changes even when OS code is unchanged.
Keep the introduction, ecosystem links, support tables, and "What this fork adds to NetBSD 11" current.
Cover OS and optional AI, robotics, GPU, NPU, and GUI workflows.
State the user benefit, owning repository, validation level, and platform. Link public evidence and instructions.
Keep research and planned acceleration separate from available workflows.
Preserve upstream credit. Explain the EmberBSD adaptation or verified scenario.
Keep detailed recipes and logs with their implementation. Check links and correct stale claims.
Report any exact overview update that remains unpublished.
