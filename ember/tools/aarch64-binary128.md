<!-- Origin: EmberBSD; AI-assisted bounded AArch64 libc binary128 FENV guide. -->
# AArch64 libc binary128 comparison exceptions

EmberBSD adapts the existing LLVM compiler-rt `comparetf2.c` in libc.
The owner is the OS repository. Its original MIT/UI notices remain in the
source; this is a local adaptation, not an upstream LLVM import.

`lib/libc/compiler_rt/Makefile.inc` sets the source-local
`COMPILER_RT_NETBSD_AARCH64_FENV` macro only for AArch64 with
`MKSOFTFLOAT=no` (including the default). The opt-in requires NetBSD,
hardware double precision and binary128 `long double`. Kernel, standalone,
soft-float and other targets reject an erroneous opt-in at compilation.
Consumers without the macro retain the previous quiet exception policy.
The lint-only stub checks syntax and is not executable-object evidence.

All helpers use a private integer classifier and comparison core. The
numeric return conventions and the ELF `__cmptf2` alias remain unchanged.
Equality (`__eqtf2`, `__netf2`) and unordered (`__unordtf2`) comparisons
raise INVALID for signaling NaNs. Ordered (`__letf2`, `__lttf2`, `__getf2`,
`__gttf2`, `__cmptf2`) comparisons raise it for any NaN. Both operands are
classified independently, including a quiet NaN paired with a signaling NaN.

The private hook moves a fixed double quiet NaN into a compiler-allocated
SIMD scratch register and executes volatile `FCMPE`, with a `cc` clobber.
It adds IOC to FPSR, or raises a hardware exception when IOE is enabled.
It does not write FPCR, clear other sticky flags, call public FP helpers,
or add a libc dependency on libm. See the [Arm FCMPE reference](https://documentation-service.arm.com/static/67e40f3398aa3c3b6eea6a85).

## Host production-source contract

From the source root, run:

```sh
sh ember/tools/aarch64-binary128-contract.sh
sh ember/tools/aarch64-binary128-boundaries.sh
BINARY128_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/aarch64-binary128-contract.sh
```

The C/shell fixture compiles the complete production source. It replaces
only `fp_lib.h` with an integer bit container and the delimited hardware
hook with a counted state model. Extraction checks fail closed for missing
or repeated boundaries. Expected predicates use a separate IEEE bit table
with ordered ranks and quiet/signaling classes, not a copied comparator.
The 15-by-15 operand matrix covers all eight entry points and both fresh
and preexisting IOC, for 3,600 rows. It includes negative NaNs, minimum
and maximum payloads, mixed NaN pairs, both zero signs and infinities.
The host alias call is modeled; real ELF alias identity needs object/native checks.

For a known source baseline, set `BINARY128_SOURCE=/absolute/comparetf2.c`.
Its real comparator must compile and fail the INVALID assertions. A compile
failure is not evidence that the numerical/exception regression reproduced.
The boundary script checks old ungated policy, forbidden opt-ins and
extraction failures. Host model PASS does not prove native SIMD ABI, FPSR,
trap delivery or an installed libc. No native test runs through these scripts.

## Native ATF/raw ABI matrix

`tests/lib/libc/gen/t_comparetf2.c` and `t_comparetf2_abi.S` are included
only for hard-float AArch64. Build them with the normal NetBSD test tools
from a clean pinned checkout. libm is a test dependency for `fetestexcept`;
the production object has no new undefined symbol.

```sh
cd tests/lib/libc/gen
make t_comparetf2
./t_comparetf2 masked_matrix
./t_comparetf2 controlled_modes
./t_comparetf2 invalid_traps
```

The shim loads raw `q0/q1` operands, calls the selected function pointer,
and captures integer `w0`, FPSR and FPCR immediately. A raw roundtrip
first checks that NaN payloads reached the SIMD arguments intact.
The default provider is `libc.so.12`. `dlsym`/`dladdr` receipts identify
each direct binding, and `__cmptf2` must share `__letf2`'s address.
No C binary128 arithmetic or comparison is used to construct inputs or
derive expected predicates. The raw fixture currently skips big endian.

`masked_matrix` retains the fresh process's FPCR and refuses inherited
trap enables. Every case seeds DZC/QC, tests clear and preexisting IOC,
checks raw IOC and `FE_INVALID`, and restores FPCR/FPSR before reporting.
`controlled_modes` runs each rounding/FZ/DN combination in a separate
child, restores its state, and skips if the requested controls are not
writable. Neither test treats normalized FP state as the inherited baseline.

`invalid_traps` first probes writable IOE, restoring the original state
before a hardware prerequisite skip. Separate children expect SIGFPE for
signaling-NaN equality and quiet-NaN ordering, and expect quiet-NaN equality
to return. They terminate after reporting delivery, leaving the parent's
state unchanged. The recorded `si_code` is diagnostic: AArch64 `trap.c`
currently reports `FPE_FLTUND` for FP traps. This libc change does not fix
INVALID-specific signal classification. Unsupported IOE is a skip, not PASS.

To inspect a private candidate DSO without installing it:

```sh
./t_comparetf2 -v binary128_dso=/absolute/libcandidate.so masked_matrix
```

An optional `binary128_prefix` selects namespaced entry points. Rename
all eight symbols in the object with `objcopy --redefine-syms` before
linking; this preserves the real alias and any baseline internal relocations.
Defining names with `-D` alone is insufficient because the existing
`FNALIAS` macro stringifies its target. Check exports, undefined symbols,
relocations and disassembly on the unrenamed candidate too. Do not use
`-Bsymbolic` to conceal public-helper calls or installed-library bindings.

Source host contracts and cross-object checks are separate validation
levels. Native candidate execution, native compiler coverage, the full
libc link, installed-library bindings and affected compiler regressions
require their own results. These tests do not establish arithmetic,
conversion, complete LTO or full compiler-suite correctness.

## Release-set coverage

The native test and its optional debug companion are included in the
AArch64 release lists. Their selectors match the test Makefile: ATF enabled,
hard-float AArch64, and debug symbols enabled for the companion only.
They are not duplicated into AArch32 compatibility test directories.
Run `sh ember/tools/aarch64-binary128-sets-contract.sh "$PWD" /absolute/new-output`
on NetBSD to exercise the real set-list generator across seven configurations.
This check covers packaging selection; it does not execute the floating-point test.

## Native candidate validation

On 2026-10-07, GCC 12.5 and GCC 16.2 on NetBSD 11/AArch64 in UTM built the
actual source as static/PIC objects and private namespaced DSOs. The actual
ATF Makefile built both test executables. Each candidate passed the 3,600-row
masked matrix, all 16 controlled modes and the trap cases. IOE was writable;
the expected children received SIGFPE with the existing `FPE_FLTUND` code.
INVALID-specific signal classification remains unimplemented.

All eight helper exports and the real cmp/le alias were preserved. The
unrenamed objects/DSOs had no undefined symbols, public-helper calls, FPCR
writes, dynamic relocations or new library dependencies. A controlled
installed-libc baseline failed 1,255 of the expanded 3,600 rows; live GOT
inspection confirmed its internal calls resolved to that same libc. A test
executable linked to GCC16 can interpose those old calls, so direct `dladdr`
identity alone is insufficient for a baseline comparison.

On physical Orange Pi Zero 3W, native GCC 12.5 and the normal libc Makefiles
built the complete static/PIC/shared library from `cffffd40`. The final shared
library, SHA256 `5dff821f1a1b42845e642cedd16375b310b78d04fa624bb21472d33b0fc14130`,
preserves all 4,587 defined dynamic exports and the comparison aliases.
It passes the masked matrix and all 16 FP modes through the original exports;
the installed baseline failed 1,255 rows. IOE is unavailable on this board,
so physical trap cases skip. The complete PIC comparison text is present in
the final DSO without public-helper calls or new undefined symbols.

That shared library and its matching static archive are now installed on the
board. A fresh masked-matrix run passes all 3,600 rows through normal installed
library resolution, without `LD_LIBRARY_PATH`. Startup/final receipts confirm
one selected libc mapping. The build used installed platform headers plus
source-local headers; this is not a complete fresh userland/header release.
LLVM23 compilation, original GCC LTO regressions against the installed pair
and full compiler-suite acceptance remain separate. For a native test build
that overrides `CFLAGS`, retain `-fPIE` when linking the default PIE.
