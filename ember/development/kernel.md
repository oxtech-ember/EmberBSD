# Develop the EmberBSD kernel

Follow [CONTRIBUTING](../../CONTRIBUTING.md) for every shared change.
This guide supplies the kernel-specific decisions and evidence.
For a new physical target, also follow [adding a board](adding-a-board.md).

## Locate the change

| Area | Source owner |
| --- | --- |
| Architecture entry, MMU, interrupts and platform code | `sys/arch/` |
| Common drivers and bus attachments | `sys/dev/` |
| AArch64 kernel configuration | `sys/arch/evbarm/conf/` |
| Device trees | Architecture DTS and `sys/external/gpl2/dts/dist/`; inspect the selected build |
| Loadable modules | `sys/modules/` and the selected kernel headers |
| Loader and firmware integration | `sys/stand/`, architecture stand directories, and `ember/boot/` / `ember/firmware/` |
| Focused contracts | `ember/tests/` and `ember/tools/`; follow the owning subsystem guide |

Read the actual attachment, configuration, consumers, and existing tests before editing.
The patches in `ember/patches` are already applied to this tree.
Firmware series are separate inputs applied by their pinned builder.
Do not apply either series to arbitrary sources.

## Reproduce on pinned input

Record the symptom, source revision, configuration and smallest reproducer.
Capture a sanitized boot log for a boot failure.
Identify the artifact selected by the loader; a guest's `/netbsd` can differ from QEMU's `-kernel` input.

Keep the shared checkout intact and use the worktree procedure in CONTRIBUTING.
Run the relevant test before and after a fix.
A host contract can isolate ordering or memory ownership without access to the board.
It cannot prove controller timing, DMA, wiring, clocks or firmware behavior.

## Build the proposed commit

Commit the candidate before a reproducible build.
Use [the cross-build guide](../../tools/docs/cross-build.md) for clean exports, compiler selection, commands and outputs.
Use the common [Ports toolchain](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/development-toolchain)
through the SDK. LLVM/Clang is the default; a rare platform may explicitly
select GCC with `EMBER_COMPILER_FAMILY=gcc`. Kernel-only mode does not require
an application sysroot or supply a complete application runtime. Both families
consume the common Ports-prepared LLVM sources through `EMBER_LLVM_SOURCE`.
Host contracts use the shared SDK native-check runner and independent
`EMBER_HOST_PREFIX`/`EMBER_HOST_COMPILER_FAMILY` selection.
Keep historical GCC build results separate from Clang build and boot checks.

Inspect the selected builder before choosing a configuration.
`ember/build-kernel.sh` accepts AArch64 configurations but explicitly lists three DTBs and four board modules.
It is not an automatic image builder for a new board.
A new DTS needs build/install integration and an actual firmware or loader selection path.
An ACPI target needs the corresponding tables and attachment path instead of an invented DTS.

Use output directories outside the source export, specific to revision, configuration and toolchain.
Record full source IDs, compiler versions/triplets, firmware IDs, exit status and output hashes.
Do not mix modules or headers from different kernel builds.
Account for kernel/userland interfaces and each dependent consumer when an ABI changes.

## Validate the change

1. Check the reproducer against the old code and the fix.
2. Run the affected subsystem contracts and shared-path regressions.
3. Build the selected configuration and affected modules from the clean commit.
4. Run applicable native checks and the stated VM or physical scenario.
5. Review the entire diff and the evidence limits before integration.

The portable kernel suite has one owner: `ember/tools/kernel-contracts.sh`.
Native-only contracts remain separate when the host cannot execute them.
The source-encoding regression is `python3 ember/tests/contract-source-encoding.py`;
it exercises the owning PRIME and DMA-BUF runners with UTF-8 source comments
under an ASCII locale.
The build orchestration regression is `sh ember/tests/build-kernel.sh`.
Its controlled fixtures test routing, version refresh and failure cleanup.
The stale-`BUILDINFO` case keeps unrelated objects and another configuration.
These fixtures do not build a kernel or validate its embedded version string.
Follow the selected subsystem's commands rather than rerunning unrelated full builds.

For C/C++, run Cppcheck on the affected source and executable contracts.
Use target headers, macros and actual compile options, preferably a compilation database.
Add GCC `-fanalyzer` for C and Clang Static Analyzer for supported C/C++ paths.
Record analyzer versions, coverage, findings, and each justified suppression.
Compare inherited findings when adapting an upstream implementation.

Run actual-source user-space contracts with Valgrind Memcheck where supported.
Enable uninitialized-value and leak checks with a nonzero error status.
Valgrind does not inspect a running kernel or physical DMA.
On unsupported hosts, record that gap and use applicable ASan/UBSan or kernel diagnostics.
Do not report a sanitizer run as a Valgrind pass.
Static analysis and host execution supplement device acceptance.

The [native CMA import contract](../../sys/external/bsd/drm2/drm/docs/drm-cma-import.md) checks imported-page
ownership and segment bounds with the actual native source.

## A reproducible regression example

[USB control recovery](../../sys/dev/usb/docs/usb-control-recovery.md) provides an actual-source concurrency regression.
This exercise uses an already merged repair; it is not a new contribution.
Run from a fresh checkout containing this guide. Set `work` to a new absolute directory:

```sh
set -e
work=/absolute/new-usb-exercise
mkdir "$work"
git show 294614ed81a5^:sys/dev/usb/xhci.c > "$work/before.c"
git show 294614ed81a5:sys/dev/usb/xhci.c > "$work/after.c"
if XHCI_TEST_CASE=stall sh ember/tests/xhci-control-recovery.sh \
    "$work/before.c" "$work/before" > "$work/before.log" 2>&1; then
    echo 'Unexpected baseline success' >&2
    exit 1
fi
XHCI_TEST_CASE=stall sh ember/tests/xhci-control-recovery.sh \
    "$work/after.c" "$work/after" > "$work/after.log" 2>&1
```

Inspect both logs. The old program must compile and fail its terminal-STALL assertion.
A compiler error, missing revision, or timeout does not establish that failure.
The fixed program must print `xHCI control recovery: PASS`.
This contract tests recovery scheduling with kernel doubles, not Orange Pi USB hardware.
Do not reapply the historical patch to current `main`.

## Boot and recovery

Use the agreed external media or disposable VM first.
Define a known working kernel and recovery path before writing a device.
Record console output, memory, storage/network checks, workload and duration.
Identify the running kernel and matching module hashes.
Check cold boot, reboot and recovery when these are part of the support claim.

Coordinate changes to the loader entry, image contents, release build ID and set compatibility with their owner.
An amd64 installer result does not establish an AArch64 boot path.
A source-menu change or unpublished download server does not establish a network installation.
Keep pending boot work out of a kernel repair unless it is an explicit, tested dependency.

Update the DevBoards hardware profile or subsystem guide and the relevant README statement after validation.
Submit through CONTRIBUTING. Prepare any NetBSD upstream proposal separately.
