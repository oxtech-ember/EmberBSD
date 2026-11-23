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

## Kernel configurations

The OS keeps general-purpose configurations, one for each functional task,
not one for each board. A driver of an absent device stays passive, so one
configuration serves all supported boards. Add a configuration only when a
task cannot use an existing one, and add its row here.

| Configuration | Task | Contents |
| --- | --- | --- |
| `EMBER64` | General AArch64 system; the default | `GENERIC64` with the passive drivers of the supported boards and the boot splash. `INSECURE` stays until the CEMAC driver is built in. |

## Extend the kernel

Select the narrowest mechanism that serves the functional task:

| Mechanism | Use | Rule |
| --- | --- | --- |
| Built-in driver | A device of a supported board | Add its attachment to the general-purpose configuration. Without its ACPI or FDT match it does nothing: no MMIO, power, clock or firmware action. |
| Default-off option | An experimental or bounded code path | Declare it with `defflag` in the owning `files.*`. The general-purpose configuration does not set it; a trial configuration does. |
| Attachment flags | A bounded diagnostic mode of one driver | The default flags keep the driver passive or minimal. A trial configuration sets other flags. |
| Loadable module | A driver loaded at run time | Add it under `sys/modules/`. `ember/build-kernel.sh` builds it against the objects of the same kernel; install them together. |
| Device tree | An FDT board description | Integrate the DTS in the builder output and in an actual loader selection path. |
| Prepared external source | Code that a Ports producer prepares | Declare it in [`ember/shared-sources.json`](../shared-sources.json) under `kernel.configs`. |
| Contract | A host check of actual kernel source | Add it to `ember/tools/kernel-contracts.sh` and its inputs to `kernel-contracts-inputs.txt`. |

## Modify a kernel

A trial or diagnostic configuration is a temporary local file. It includes a
general-purpose configuration and changes only the trial options. Build it by
its absolute path: `ember/build-kernel.sh OUTPUT /path/to/NAME`. Its name must
not be the name of an OS configuration: the objects and outputs are named by
it. When the trial is accepted, add its drivers or options to the
general-purpose configuration; then remove the trial. Board records in
[DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards) use the
general-purpose kernel.

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
The tools phase also needs Ports-prepared byacc and Heimdal sources through
`EMBER_BYACC_SOURCE` and `EMBER_HEIMDAL_SOURCE`; the latter supplies the host
ASN.1 compiler even for a kernel-only build. A kernel with libz also reads the
common Ports-prepared zlib source through the optional `EMBER_ZLIB_SOURCE`
(see the [base zlib contract](../../lib/libz/README.md)); the EMBER kernels do
not. The wrapper forwards the source paths explicitly to `build.sh`, including
on Linux where the SDK runner does not inherit arbitrary source variables from
the environment.

[`ember/shared-sources.json`](../shared-sources.json) names the Ports producer
and package of each prepared source in the `REPRO_EXTERNAL_ROOTS` table of
`share/mk/bsd.sys.mk`, and the sources that each kernel configuration reads.
The SDK build plan uses it to select and identify the sources of a kernel.
After the build, `build-kernel.sh` reads `REPRO_EXTERNAL_SOURCE_VARS` from the
kernel and module objects and stops if they differ from the declaration, with
`ember/tools/shared-sources.py`. When a configuration reads another source,
for example littlefs, add the configuration to `kernel.configs`.
Host contracts use the shared SDK native-check runner and independent
`EMBER_HOST_PREFIX`/`EMBER_HOST_COMPILER_FAMILY` selection. The wrapper passes
that prefix's include, library and runtime-library search paths to the runner's
compiler bindings, including for contracts that use only `CC`. These generated
bindings refresh under the output lock at each wrapper invocation; the SDK
validates their reuse between checks within that invocation.
Keep historical GCC build results separate from Clang build and boot checks.

Inspect the selected builder before choosing a configuration.
`ember/build-kernel.sh` explicitly selects DTBs and matched modules for each invocation.
It also publishes the native YS-M33 A133 DTB; its WLAN node remains disabled.
This adds a build artifact, not a firmware/loader selection or radio-enable path.
The wrapper is not an automatic image builder for new boards.
Each DTS needs build/install integration and an actual firmware or loader selection path.
An ACPI target needs the corresponding tables and attachment path instead of an invented DTS.

Use output directories outside the source export, specific to revision, configuration and toolchain.
Record full source IDs, compiler versions/triplets, firmware IDs, exit status and output hashes.
Use modules and headers from the same selected kernel build.
Account for kernel/userland interfaces and each dependent consumer when an ABI changes.

## Validate the change

1. For a logic or behavior repair, check the reproducer against the old code and the fix.
   A trivial fix needs no reproducer or regression test.
2. Run the affected subsystem contracts and shared-path regressions.
3. Build the selected configuration and affected modules from the clean commit.
4. Run applicable native checks and the stated VM or physical scenario.
5. Review the entire diff and the evidence limits before integration.

The portable kernel suite has one owner: `ember/tools/kernel-contracts.sh`.
`ember/check-kernel.sh OUTPUT [BASE_COMMIT]` runs it; `ember/build-kernel.sh` does not.
On native EmberBSD amd64, set `EMBER_NATIVE_ASAN_RESOURCE_DIR` to the
absolute `lib/clang/<major>` directory of the selected compiler-rt package
for ASan contracts. The SDK runner selects the resource directory and applies
the required PaX setting to the sanitized test executables.
See [SDK native source checks](https://github.com/oxtech-ember/EmberBSD-SDK/blob/main/docs/native-checks.md#netbsd-amd64-addresssanitizer).
Set `EMBER_WPA_SOURCE` to the absolute path to the prepared Ports WPA tree
when running the full suite or selecting either station contract.
Set `PVR_QUERY_FIRMWARE` to the pinned image to include the PowerVR query contract.
With `BASE_COMMIT`, it runs only the contracts that the changes can affect.
`ember/tools/kernel-contracts-inputs.txt` records the source inputs of each contract.
A contract without an entry runs on each change.
When a contract reads other files, run `ember/tools/kernel-contracts-trace.py` again.
Run all contracts before a release.
Native-only contracts remain separate when the host cannot execute them.
The source-encoding regression is `python3 ember/tests/contract-source-encoding.py`;
it exercises the owning PRIME and DMA-BUF runners with UTF-8 source comments
under an ASCII locale.
The build orchestration regression is `sh ember/tests/build-kernel.sh`.
Its controlled fixtures test routing, version refresh, failure cleanup and contract selection.
The stale-`BUILDINFO` case keeps unrelated objects and another configuration.
These fixtures do not build a kernel or validate its embedded version string.
Follow the selected subsystem's commands rather than rerunning unrelated full builds.

For C/C++, run Cppcheck on the affected source and executable contracts.
Use target headers, macros and actual compile options, preferably a compilation database.
Add Clang Static Analyzer for supported C/C++ paths.
Use GCC `-fanalyzer` only for code built with an explicit GCC platform profile.
Record analyzer versions, coverage, findings, and each justified suppression.
Compare inherited findings when adapting an upstream implementation.

Run actual-source user-space contracts with Valgrind Memcheck where supported.
Enable uninitialized-value and leak checks with a nonzero error status.
Valgrind does not inspect a running kernel or physical DMA.
On unsupported hosts, record that gap and use applicable ASan/UBSan or kernel diagnostics.
Name the sanitizer and its actual result; record Valgrind separately as executed or unavailable.
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
