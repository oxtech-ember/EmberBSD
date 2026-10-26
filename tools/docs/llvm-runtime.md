# Common LLVM sources and application runtimes

The [Ports LLVM runtime producer](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/pkgsrc/lang/compiler-rt)
owns the LLVM version, source archive, patches and runtime build. The SDK
selects its prepared source and target artifacts. Native OS rules retain the
EmberBSD libc, headers, CRT objects, target triplet and ABI integration.

## Source inputs and libc

Libc, libm and kernel builds require `EMBER_LLVM_SOURCE`, the absolute prepared LLVM monorepo root. The
runtime source subset must include `runtimes/CMakeLists.txt`, `compiler-rt`
and `libunwind`; a raw archive extraction does not supply the common patches.
The same source selection works with Clang and the explicit GCC alternative.
The make interface accepts paths containing `A-Z a-z 0-9 _ . / + @ % = -`.
Source provenance and patch verification belong to the Ports producer and
SDK, rather than another source acquisition path in the OS.

`lib/libc/compiler_rt/Makefile.inc`, `lib/libm/compiler_rt/Makefile.inc` and
`sys/lib/libkern/Makefile.compiler-rt` select builtins from that root. The
native architecture object lists and `abi.mk` policy remain in the OS,
including libm's complex arithmetic helpers. Missing common source is
an error for both Clang and GCC; there is no bundled-source fallback. Modern profiling
support uses the matching common profiling sources and include directory.

The AArch64 hard-float libc comparison object alone enables
`COMPILER_RT_NETBSD_AARCH64_FENV`. The common patch preserves binary128
comparison results and the existing INVALID policy: equality and unordered
operations signal for signaling NaNs; ordered operations signal for all NaNs.
Kernel, standalone and soft-float builds do not enable this policy.

The libc `GCDAProfiling.c` object alone enables
`COMPILER_RT_LIBC_LEGACY_GCOV`. This common compatibility mode preserves the
13 exported legacy GCDA entry points, their argument layouts, callback
behavior and file records. Ordinary LLVM profiling runtimes keep the modern
interface. Both modes share current compiler-rt file and list machinery.
The common producer owns the compatibility patches and their source tests.

When libc requires LLVM unwinding, set `EMBER_LIBUNWIND_ARCHIVE` to the exact
static archive selected by the target runtime manifest. Ports must build
this archive with PIC, its complete public unwind exports, and no libc++
dependency. The OS uses the selected target linker to combine the complete
archive into one relocatable object for libc.a and libc_pic.a. Unwind entry
points remain inside libc.so; this interface does not add a libunwind
DT_NEEDED entry to libc.

The ordinary archive is used with `MKPROFILE=no` and `MKDEBUGLIB=no`.
Instrumented profiling and debug-library variants require corresponding
producer artifacts; renaming a regular archive would not provide them.
`MKDEBUG` debug-symbol selection is a separate option.

The native `includes` and `obj` goals can run before the unwind archive is
available. They select the common `unwind.h` and its three companion headers.
An actual library build still requires the archive. Combining a header goal
with a build goal does not bypass this requirement.

## Application runtime inputs

For an external Clang application build, select these make variables:

| Variable | Input |
| --- | --- |
| `EMBER_COMPILER_FAMILY` | `clang` |
| `EXTERNAL_TOOLCHAIN` | Selected compiler prefix |
| `EMBER_EXTERNAL_LLVM_RUNTIME` | Manifest-selected staged `/usr/pkg/llvm` prefix |
| `EMBER_LLVM_RESOURCE_DIR` | Exact target compiler-rt resource directory from that manifest |
| `EMBER_LLVM_RUNTIME_PHASE` | `bootstrap` for initial C libraries; `full` (default) for the complete runtime |
| `MKLIBCXX` | `no`, to omit the old in-tree C++ runtime producer |
| `MKCXX` | `yes` for final C++ consumers |

The bootstrap phase requires `MKCXX=no`, the target compiler-rt resource
directory and `lib/libunwind.a`. It supplies the selected C runtime options
to ordinary program and shared-library links, including libm and libpthread,
without requiring libc++ artifacts that depend on those base libraries.
C++ consumers are rejected in this phase. The full stage supplies generated
`include/c++/v1` headers and the matching static and shared libc++, libc++abi
and libunwind libraries. The adapter rejects a
mixed GCC selection, missing artifacts and unsupported multilib stages.
Host generators and explicit freestanding links do not acquire these paths.

C++ compile and dependency commands select only the staged C++ headers.
Link commands use `-stdlib=libc++`, `--rtlib=compiler-rt`,
`--unwindlib=libunwind` and the exact resource directory. The common Clang
NetBSD driver supplies compiler-rt at the correct position among default
libraries, including static library groups. An archive placed arbitrarily
in `LDADD` is not an equivalent link interface.

`-resource-dir` is applied to link commands. Compilation retains the selected
native Clang's compiler-owned builtin headers, while target libc headers
come from the EmberBSD sysroot. A caller that adds `-resource-dir` to its
compiler command must also provide the matching native compiler's builtin
include directory. Host SDK headers must never replace target headers.

The staging path is used only for library search and `rpath-link`. The full
runtime phase adds target RPATH `/usr/pkg/llvm/lib`; the static bootstrap
phase adds no runtime search path. All startup objects remain the OS products from
`lib/csu`: crt0, crti, crtn, crtbegin and crtend variants. This path does not
build or copy compiler-rt CRT replacements.

`MKLLVMRT` controls the old target LLVM backend libraries, not compiler-rt
ABI support. It is not the selector for this runtime interface.
The explicit GCC application adapter remains documented in
[external-gcc-runtime.md](external-gcc-runtime.md).

## Checks and scope

Run the actual make-rule fixtures with the selected native Python and bmake:

```sh
python3 -B ember/tests/llvm-runtime-inputs.py \
    --make /absolute/native-prefix/bin/bmake \
    --prepared-llvm /absolute/prepared-llvm -v
```

These checks exercise libc/kernel source selection, header-before-runtime
staging, application flags, CRT paths, C++ dependency selection, and rejected
mixed configurations. The optional prepared-source inventory covers AArch64,
amd64, ARM EABI, PowerPC, SPARC64 and i386 source names. It does not compile
those targets. The small temporary path fixtures are metadata fixtures and
are never packaged as runtime libraries.

A complete target acceptance still builds and links the real artifacts,
compares libc exports and dependencies, and exercises C/C++ exceptions,
static links and the target FENV tests. A metadata or source-model check does
not establish that result or a booted Clang-built release.
