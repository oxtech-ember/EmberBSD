# Compiler and system stddef-header interoperability

The libkvm source includes `machine/pcb.h`, which includes `sys/stddef.h`.
A later `sysctl.h` include selects the external GCC16 resource `stddef.h`.
The old system header declares a union `max_align_t` before GCC declares its struct type.
The real amd64 `kvm.pico` command fails with conflicting typedefs under `-Werror`.

Ordinary userspace now includes public `stddef.h` before the BSD fallback.
The fallback reads genuine `_GCC_MAX_ALIGN_T` or `__CLANG_MAX_ALIGN_T_DEFINED` flags.
It does not define those flags or copy compiler implementations.
Existing compiler definitions of `offsetof`, `unreachable` and the header version macro remain intact.
The existing system guard also permits an installed public-header symlink to this file.
Kernel, standalone and lint branches remain independent of compiler resource headers.
They retain the original BSD union and license.

## ABI boundary

The amd64 GCC16 public type has size 32 and alignment 16.
The old system-only union has size 16 and alignment 16.
These types are not ABI-equivalent for embedded objects or by-value arguments.
Rebuild the complete userspace base and its target packages from consistent headers.
Rebuild external consumers that embedded or passed the old system-only type by value.
This change does not assert compatibility with those old objects.

An amd64 `struct { char lead; max_align_t object; char tail; }` changes size 48 to 64.
Its object offset remains 16; its tail offset changes 32 to 48.
A source scan finds base TLS consumers that use only alignment, which remains 16.
It finds no base public structure or function interface embedding or passing `max_align_t`.
That scan does not cover third-party consumers.

Real-header PCB probes compare each compilation context before and after the change:

| Context | PCB size | PCB alignment | Saved-FPU offset | Save-FPU union size |
| --- | ---: | ---: | ---: | ---: |
| amd64 libkvm `_KMEMUSER` | 640 | 64 | 128 | 512 |
| amd64 GENERIC `_KERNEL` | 704 | 64 | 128 | 576 |

Every value in this table remains unchanged within its context.
The existing kernel and userspace FPU definitions account for the difference between rows.
Kernel `max_align_t` remains size 16 and alignment 16.
These measurements establish an amd64 boundary, not an ARM ABI claim.

## Run the regression

Use native C23 and C++20 compilers:

```sh
ruby ember/tests/stddef-interop.rb
```

Native mode models only prerequisite type and feature plumbing.
It compiles the production header and executes layout, offset, null and storage checks.
The recorded native run uses Clang ASan/UBSan for 21 cases.
Set `STDDEF_CC`, `STDDEF_CXX` and `STDDEF_FLAGS` to select compilers or analysis.
Set `STDDEF_RUN=0` for compile-only checks or `--analyze` in `STDDEF_FLAGS` for Clang Static Analyzer.
Valgrind is unavailable on this macOS host; sanitizer execution is not a Valgrind check.
Native Apple-target GCC has different BSD guard plumbing; use target mode for its NetBSD header contracts.

Use an external amd64 compiler and a verified EmberBSD sysroot for target mode:

```sh
STDDEF_CC=/absolute/toolchain/bin/x86_64--netbsd-gcc \
STDDEF_CXX=/absolute/toolchain/bin/x86_64--netbsd-g++ \
STDDEF_SYSROOT=/absolute/ember-sysroot \
ruby ember/tests/stddef-interop.rb
```

Target mode compiles 26 cases using real OS headers; it does not execute target code.
C11, C23 and C++20 cover system-first, compiler-first, partial-request-first, system-only and public-symlink inclusion.
Target-only C99 checks preserve the pre-C11 namespace.
Kernel, standalone and lint branches use `-nostdinc` with real source and machine headers.
The lint branch defines `__lint__`; it does not execute NetBSD lint.
The script removes its automatic temporary directories.

## Evidence boundary

The unchanged real libkvm compilation command fails before the fix and passes after it.
Only source/include roots and the output location change for the candidate replay.
Full-TU GCC16 `-fanalyzer` reports no candidate diagnostics.
The baseline analyzer stops at the conflicting typedef.
Clang Static Analyzer also covers all 21 native fixture cases.

Cppcheck cannot evaluate the implicit compiler `__has_builtin` in its direct-header model.
The comparison therefore uses actual GCC-preprocessed baseline and candidate TU inputs with real target flags.
Both produce the same multiset of 29 inherited reports, with no new report or suppression.
These include bitmask, constant-condition, cast, scope and const-pointer reports in libkvm and its headers.
They remain with the respective source owners; this bounded header fix does not change them.
Preprocessing selects the actual configuration; it does not analyze inactive branches.
Cppcheck also analyzes the preprocessed fixture in C11, C23 and C++20.
Its remaining reports identify deliberate constant test oracles and shared C/C++ representation casts.
The test owner retains those checks and casts without suppressions.

Ten negative controls remove the delegation, provider guard, resource-free guards, recursion guard or preserved-macro guards.
Each control fails with its expected diagnostic.

The checks establish header, isolated-TU and same-context PCB contracts.
A complete rebuilt base, package rebuild, target execution and hardware acceptance remain separate checks.
