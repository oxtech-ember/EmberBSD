# Build AArch64 kernels on another host

EmberBSD does not require a NetBSD build host. The kernel wrapper uses the
fork's standard `build.sh` on macOS and other foreign hosts. That script
builds host executables which generate AArch64 NetBSD output. Use the common GCC16 external toolchain for target code.

## Build from pinned sources

Export a clean commit of the **full source tree**. A kernel-only export
without `tools`, `share/mk` and their source dependencies is insufficient.
Use a host C/C++ compiler and the host Python 3 for the existing source
contracts. On macOS, install the Xcode command-line tools first. Do not use
the target's `/usr/pkg/etc/mk.conf` as the host OS build configuration.

```sh
revision=FULL_COMMIT_ID
src=/absolute/source
out=/absolute/build-output
mkdir "$src"
git archive "$revision" | tar -xf - -C "$src"
EMBER_BUILD_MODE=cross EMBER_EXTERNAL_TOOLCHAIN=/absolute/gcc16 \
    BUILDINFO="EmberBSD:$revision" NETBSD2_JOBS=6 \
    sh "$src/ember/build-kernel.sh" "$out" EMBER64
```

The output must be outside the source tree. Keep one output directory per
source export, kernel configuration and toolchain. The default `cross` mode
uses `build.sh` on every host, including NetBSD.
Cross-compile on the development machine; VMs and boards execute acceptance checks.
The wrapper retains `native` and `auto` modes for compatibility. Those modes
do not establish the selected GCC16 cross-build workflow.

Cross builds keep host tools under `out/tools`, object files under `out/obj`
and the kernel configuration's generated headers under `out/obj/kernels`.
The four board modules use the same make wrapper, target settings and kernel
headers. DTBs use the target preprocessor and the source-built host `nbdtc`.
`build.sh -u` reuses tools and object files for repeated builds. Before each
cross build, the wrapper removes only the selected configuration's generated
`vers.c` and `vers.o`. The version make rule does not track `BUILDINFO` changes
as a dependency. Regeneration prevents reuse of an earlier embedded identity.
Other objects and other configurations remain intact.

Set `BUILDINFO` explicitly from the pinned revision, as above. The wrapper does
not derive or verify a source commit. Check the linked kernel's embedded
build information against the selected revision before installation; a new
build log alone does not prove that the kernel has new metadata. A failed build
returns nonzero and does not leave the previous candidate at the deliverable
paths. Build logs remain in the output directory.

`NETBSD2_JOBS` selects concurrency. `NETBSD2_PYTHON` can select an existing
host Python executable; there is no fixed Python 3.13 requirement. These
legacy Python source contracts have not yet been replaced.

Darwin host tools retain `-no-cpp-precomp` only for Clang. Modern GNU GCC
rejects that legacy Apple flag. The actual Makefile/compiler regression covers
six tool paths, GCC/Clang and both host-selector values. Its 24 cases pass on
macOS; the Linux selector is simulated, not a Linux execution claim.

```sh
sh ember/tests/darwin-host-flags.sh /absolute/source /absolute/nbmake-evbarm \
    /absolute/host-gcc /absolute/host-clang /absolute/SDK /absolute/new-work
```

## Compiler selection

The fork retains an older in-tree bootstrap compiler. Select the common
GCC16 cross toolchain explicitly for current development. Its recipe belongs to
[EmberBSD-Ports](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/development-toolchain).

To use a separately prepared GNU cross toolchain, provide its absolute
prefix. It must supply `bin/aarch64--netbsd-gcc`, `cpp`, `c++` and the matching
prefixed binutils commands expected by `EXTERNAL_TOOLCHAIN` in `share/mk`.
Use a fresh output directory when changing toolchains.
The wrapper sets `TOOLCHAIN_MISSING=yes` for this path to omit the in-tree
bootstrap compiler and its host math libraries. Other required host tools
are still built from the selected source revision. In particular, host
Binutils supplies BFD for `dbsym` and `mdsetimage`; an external compiler
prefix alone does not provide those libraries.

BFD source generators use explicit template paths for both GNU make and
BSD make. The former BSD-only automatic variable expanded to an empty input
under the GNU make used by host Binutils. The regression
`ember/tools/binutils-generators-contract.sh` executes all 18 recipes from
both source Makefiles with each make and three source-directory layouts;
216 generated outputs match. This is separate from a complete tools build.

```sh
EMBER_BUILD_MODE=cross EMBER_EXTERNAL_TOOLCHAIN=/absolute/cross-prefix \
    NETBSD2_JOBS=6 sh "$src/ember/build-kernel.sh" /absolute/new-output EMBER64
```

This parameter selects tools; it does not fetch or build a GCC16 cross
compiler. The [Ports cross recipe](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/development-toolchain/cross)
provides the host compiler and preserves its current source adaptations.
Its `--kernel` mode builds frontends without an application sysroot; the
application mode retains the target libc/header/runtime requirements.

### Reproducible debug paths with external GCC

Keep `MKREPRO=yes` when using the supported external GCC16 toolchain.
Set `NETBSDSRCDIR` and `NETBSDOBJDIR` to the actual absolute source and
object roots. With `build.sh -O /absolute/objects`, also pass
`-V NETBSDOBJDIR=/absolute/objects`. The arbitrary `MAKEOBJDIR` expression
cannot safely supply that root. A missing object root stops the build.
`MAKEOBJDIRPREFIX` retains its existing explicit root derivation.

The external path uses standard `-fdebug-prefix-map` and
`-fmacro-prefix-map` options with concrete path values. It maps source,
objects and X11 to `/usr/src`, `/usr/obj` and `/usr/xsrc`, and removes the
DESTDIR prefix. The object map takes precedence for generated sources
below the source tree. Debug maps normalize DWARF. The existing macro-map
policy separately normalizes `__FILE__` values.

Upstream GCC16 does not implement NetBSD's `-iremap`, environment-variable
prefix maps or `-fdebug-regex-map`. Those extensions remain selected only
for the in-tree compiler. No compiler feature probe runs during make parsing,
and no unknown option is added to the external compiler command.

Run the real amd64 compiler regression with an existing configured make
wrapper and cross prefix:

```sh
CPPCHECK=/absolute/cppcheck ruby ember/tests/external-repro.rb \
    /absolute/tools/bin/nbmake-amd64 /absolute/source /absolute/gcc16-prefix
```

The fixture compiles `-g` objects through the real make definitions and
GCC16 in two independent source/object layouts. One object tree is nested
under its source root. Object bytes must match. Target `readelf` checks
canonical DWARF paths, while object strings check source, generated-source,
DESTDIR and X11 `__FILE__` values. The fixture also rejects a missing object
root and checks that the in-tree GCC12 flag selection retains all four
regex replacements and its environment/preprocessor maps.
Cppcheck, when selected, analyzes only the small generated C fixtures for
NetBSD/x86_64. This is not a kernel source analysis or target execution.
The regression does not establish complete kernel reproducibility or boot.

### Darwin host compiler flags

With host GCC16 on macOS, pass the selected SDK through `HOST_CPPFLAGS`
and `HOST_LDFLAGS`. Keep host language and optimization flags in
`HOST_CFLAGS`. The host-mkdep and mandoc configure rules pass all three variables.
Mandoc preserves explicit build flags in its feature probes. It selects them
before adding definitions that require mandoc's generated configuration header.
The compatibility, MOP, CTF, ELF and DWARF host libraries select
`-no-cpp-precomp` only for Clang on Darwin. GCC does not support this flag.
This follows the compiler selection already used by GNU host tools.

Run the bounded regression with an existing configured make wrapper:

```sh
sh ember/tests/darwin-host-tools.sh /absolute/tools/bin/nbmake-amd64 \
    /absolute/source /absolute/host-gcc16
```

The check evaluates the real Makefiles with GCC16 and Clang. It also runs
host-mkdep and mandoc configure with the macOS SDK in private temporary directories.
Mandoc's generated configuration follows real host compile, link and execution
probes, including nanosleep. No target program is executed by this check.
These host checks pass on Apple Silicon macOS with GCC 16.2.0. They do not
establish a complete tools build, amd64 userland, boot or image acceptance.

Cross sysroots also need the [relative loader links](../../libexec/ld.elf_so/docs/ld-elf-so-link.md).

## Acceptance is separate from host selection

The wrapper runs the portable contracts listed in `ember/tools/kernel-contracts.sh`.
They include the AIC station and management-frame regression. On a foreign
host, four additional Bluetooth/audio contracts remain pending. Copy the
same pinned source export to an EmberBSD machine and run the complete suite
before deploying the artifacts:

```sh
NETBSD2_PYTHON=/path/to/python3 sh ember/tools/kernel-contracts.sh /absolute/source all
```

Those four additional tests compile against NetBSD Bluetooth/audio headers
and execute their probes. Their runtime requirement is not a build-host
restriction. Passing them does not prove that a new kernel boots, that a
module attaches, or that physical Bluetooth/audio works.

Run the shell orchestration regression on any host:

```sh
sh ember/tests/build-kernel.sh
```

After host tools have produced a make wrapper, check its actual dependency
selection with `sh ember/tests/external-toolchain-tools.sh
/absolute/output/tools/bin/nbmake-evbarm /absolute/source`.

It checks foreign/native command routing, target tool expansion, module
header selection, stale-version regeneration, object retention, CEMAC/Wi-Fi
contract dispatch and failure propagation. Its fixtures
are not a kernel build. For underlying build options see [BUILDING](../../BUILDING)
and the [NetBSD cross-build guide](https://www.netbsd.org/docs/guide/en/chap-build.html).

## ARM32 standalone checks

The ARM standalone contract checks the RISC OS module flags, the real libsa
single-device ioctl link, and the RISC OS and ARM EFI ELF layouts. It requires
GCC16, matching ARM binutils, NetBSD make, and explicitly selected EmberBSD ARM
headers. Use an empty output directory:

```sh
python3 ember/tools/tests/arm-standalone/contract.py \
  --cc "$ARM_GCC" --objcopy "$ARM_OBJCOPY" --bmake "$NBMAKE" \
  --sysroot "$ARM_SYSROOT" --output "$CHECK_OUTPUT"
```

The check rejects an uncovered ELF program-header segment and verifies that the
flat payload starts at address zero. It checks both module and non-module flags.
These object and link checks do not establish a complete loader build, firmware
execution, or compatibility with a different sysroot revision.

## Verified scope

On 2026-10-10, the same cross path on Apple Silicon macOS also built the
complete reduced base set (not only the kernel) following
[aarch64-base-update.md](../../ember/tools/docs/aarch64-base-update.md). The strict `checkflist`
validation passed after correcting a stale `libdwarf` entry in the set lists,
and the kernel, modules and base installed and passed their on-board
acceptance on a physical Pi 5. One transient host `cc1` SIGTRAP under `-j8`
did not reproduce; the build continued at `-j6`.

The wrapper does not derive a source identity. Supply `BUILDINFO` explicitly;
the current wrapper regenerates version metadata before an incremental cross
build. This behavior has an orchestration regression, not a new board acceptance.

On Apple Silicon macOS, the wrapper built the in-tree host tools, full
`EMBER64`, four board modules and all three DTBs. The two portable contracts
passed on that host; the full six-contract suite passed in AArch64 NetBSD
UTM. The kernel build used the in-tree GCC12.5 bootstrap. It has not been
booted on a board as part of this cross-build check.

Separately, Ports GCC16 built on macOS and cross-compiled C11 plain/LTO and
C++20 shared-library probes. They pass on physical Orange Pi Zero 3W with
its installed GCC16 runtime.

On 2026-10-08, the external GCC16.2 path built the full `EMBER64`, four
matched modules and three DTBs from `21cd2464c720159bec0a3ba352e4dee940a58b02`.
Kernel C objects retain GCC's default DWARF5 and the linked kernel contains
CTF. Strict compiler warnings remain enabled. The kernel SHA256 is
`d79c53a823855211c613245808c6f588d418c9e5bd83990ff3dc60cee643b54c`.
It cold-booted in AArch64 UTM with network and disk access. The installed
kernel passes fresh-exec FP defaults, fork/signal/thread state preservation
and exec reset from altered FP state. The font ioctl ownership regression
passes with GCC16 in that guest and with host ASan/UBSan; the original
handler fails its pointer-preservation assertion. Run it with:

```sh
sh ember/tests/wsdisplay-font.sh /absolute/source /absolute/new-test-output
```

This accepts the GCC16 kernel build and VM boot, not a complete GCC16-built
userland, a new physical-board deployment or board-module attachment.
All six native source contracts subsequently passed with the installed
Python 3.14 package. [Live DTrace](../../external/cddl/osnet/docs/dtrace-dwarf.md) also passes in this VM
with matching modules and typed FBT arguments from the GCC16 kernel's CTF.

### Linux GCC16

On 2026-10-11, Debian 13/x86_64 built the full `EMBER64` kernel, its image,
four matched modules, and three DTBs with four workers. Both host GCC/G++
and the Ports `--kernel` cross compiler were 16.2.0. The source export was
`d3cd85dbbcce4307f156e63f87e5ec29d58d7b99`. All ten portable contracts passed,
including the V3D causal mutants and production Wi-Fi link checks.
All nine artifact SHA256 checks passed. The kernel SHA256 is
`afdea5b38fb294960efb6e0d0e1c986fdfc66b0abf7912fe8bdf2ed9652c42d4`.
Strict compiler warnings remained enabled. Kernel mode uses no application
sysroot and selects GCC16 explicitly.

This accepts the final merged V3D changes and the Linux cross-build,
not boot, physical-board behavior, native contracts, or a complete GCC16 userland.

## DWARF5 and CTF

The CTF converter accepts ordinary DWARF5 C compilation units. The libdwarf
adaptation resolves indexed strings, including the two-byte index form,
loads and relocates the string-offset table, accepts implicit constants
and retains a relocation ending exactly at a section boundary. Invalid
string tables fail explicitly instead of producing unnamed CTF types.
GCC16 can keep its upstream DWARF5 default; no DWARF4 override is required.

Run the host conversion regression with the GCC16 cross driver and a host
Clang capable of emitting AArch64 NetBSD ELF:

```sh
sh ember/tests/ctf-dwarf.sh /absolute/cross/bin/aarch64--netbsd-gcc \
    /absolute/clang /absolute/output/tools /absolute/new-test-output
```

It compares actual GCC/Clang CTF types for DWARF4/5 and DWARF32/64, including
arrays, enums, bitfields and function pointers. It also checks more than
256 string indices, exact-end relocation, malformed tables and ctfmerge.
The separate [external DWARF and DTrace acceptance](../../external/cddl/osnet/docs/dtrace-dwarf.md) covers
native CTF execution and live tracing. The [external type-unit guide](../../external/cddl/osnet/dist/tools/ctf/cvt/docs/ctf-external-types.md)
extends conversion to GNU/standard split objects, DWP v2/v5, combined
supplements and multiple primary CUs, with explicit ownership limits. General
DWARF5 location-list evaluation is not established by type conversion.

For GNU host sub-makes on Darwin, an explicit `SDKROOT` is retained through
`env -i`. The owning wrapper still requires the selected GCC16 toolchain;
this does not select a fallback compiler. Exercise the actual build environment:

```sh
ruby ember/tests/darwin-gnuhost-sdk.rb SOURCE NBMAKE GCC SDK NEW_WORK
```

The imported flex host generator must terminate its variadic pointer list with
a pointer-typed NULL. An integer zero can leave an invalid extra argument on
Darwin ARM64 and make the generated m4 execution fail. The real host filter and
its generated scanner are tested without replacing m4:

```sh
sh ember/tests/lex-host-filter.sh /absolute/nblex HOST_CC SDK NEW_WORK
```

The flex caller repair has independent spec/quality/security approval. Original
and patched actual `main.c` compiler analysis each retain one GCC warning
(imprecise floating-point statistics) and one Clang warning (m4 allocation
ownership). Cppcheck could not parse this Darwin configuration: the compilation
database reached a NAME_MAX header guard; actual GCC preprocessing then exposed
an SDK errno typedef syntax limitation. Both inputs fail identically. These
failures are recorded, not a clean Cppcheck claim or a full flex audit.
