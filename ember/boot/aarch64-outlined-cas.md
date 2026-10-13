# AArch64 outlined CAS argument regression

EmberBSD normalizes byte and halfword expected arguments in the NetBSD
LL/SC outlined CAS helpers. A signed narrow expected value can arrive
with nonzero upper register bits. Comparing those bits against a
zero-extended exclusive load incorrectly skips a matching store.

The owner is the OS source tree:
[`__aarch64_lse.S`](../../common/lib/libc/arch/aarch64/atomic/__aarch64_lse.S).
The change preserves NetBSD's implementation, licence and attribution;
it does not import GCC's assembly. AAPCS64 leaves unused argument bits
unspecified, and GCC's AArch64 outlined helper also normalizes narrow
expected arguments before its LL/SC comparison.

## Run the production contract

On little-endian NetBSD/EmberBSD AArch64, from a pinned source export:

```sh
CC=/usr/pkg/gcc16/bin/gcc sh ember/tools/aarch64-cas-contract.sh \
    "$PWD" /absolute/new-output-directory
```

Use an external timeout and appropriate resource guard on shared machines.
The script creates its output directory and retains generated wrappers,
objects, executable, link map, symbols, disassembly and execution status.
It exits nonzero on a failed check; do not interpret a completed diagnostic
controller as a passing regression.

The wrappers use the production Makefile's OP/SZ/AR definitions and compile
the actual source with native NetBSD headers. Only the test objects' symbol
names are changed. The one-instruction assembly call seam passes full
64-bit expected/desired values without C narrow-argument conversion.
The original named objects remain available for an isolated upstream test.

The 850 functional checks cover 1/2/4/8 bytes across relax/acq/rel/acq_rel/sync,
positive, negative and boundary bit patterns, independent upper-bit poison,
matching and mismatching expected values, truncated desired values, exact
old-value returns and adjacent-byte canaries. CAS4 must ignore high32;
CAS8 must compare them. The unchanged baseline must fail the ten narrow
variants; the repaired source must pass all twenty variants.

## Check a complete shared libc

The optional DSO mode uses the same matrix and raw argument seam without
compiling or linking renamed production objects:

```sh
sh ember/tools/aarch64-cas-contract.sh --dso /absolute/libc.so.12.224 \
    "$PWD" /absolute/new-output-directory
```

The provider must be an absolute regular file, not a symbolic link. The
fixture resolves all twenty original `__aarch64_cas*` names from that
explicit `RTLD_NOW | RTLD_LOCAL` handle. Before executing the matrix, it
checks each `dladdr` provider's device/inode against the selected file and
prints a binding receipt. Missing symbols, dependency-provided symbols,
wrong file identity and invalid provider paths fail with exit status 2;
functional failures retain status 1. There is no default-provider fallback.
The output includes the provider hash, ELF metadata, disassembly, fixture
symbols, link map, execution log and status.

A complete candidate libc must be selected at process startup. Prepare
private `libc.so.12`/`libc.so` links to that file and set `LD_LIBRARY_PATH`
and `LD_BIND_NOW=1` only for bounded test child processes. Verify their
loaded libc identity before testing. Do not load a second complete libc
into a process initialized with the installed one for allocator acceptance.
Build the fixture under the installed library first, then run its retained
`contract /absolute/provider` executable in each isolated environment. Use
the installed regular library file for the separate baseline run.

[`aarch64-libc-receipt.c`](../tools/aarch64-libc-receipt.c) can be built
with `cc -fPIC -shared` and passed as an absolute `LD_PRELOAD` path.
Set `EMBER_LIBC_EXPECT` to the regular provider file in the same bounded
child environment. Its constructor/destructor record loaded object identities
and reject a wrong or second libc mapping, including a wrong effective malloc
lookup. The runtime lookup avoids executable PLT addresses; it does not inspect
individual GOT slots.
Building it as an executable with `-DEMBER_LIBC_RECEIPT_MAIN` adds a bounded
allocator/string/stdio smoke. The receipt does not interpose calls or add fork
hooks; forked children inherit mappings. Snapshots can miss libraries opened
and closed between them and do not establish complete loader correctness.

## Evidence and limits

The original production source at `7f47b7c3f935ccba2207fe73b57742ae4b6b9f92`
failed 140 of 850 checks in an AArch64 UTM guest with GCC 16.2.0.
The repair at `a32a88d7b71a50f795df8a1b291b26968224e0b0` passed all 850 checks
on 2026-10-07. Comparing all twenty native object bodies found only the
first instruction changed in narrow helpers; all other bytes matched.

The unchanged GCC 16.2.0 `gcc.dg/atomic/c11-atomic-exec-2.c` was also linked
to the original named production objects in separate executables, using
`-O2 -flto -fuse-linker-plugin -fno-fat-lto-objects -std=c11 -pedantic-errors`
and the same build-tree libatomic/runtime paths as its earlier reproduction.
The original objects reproduced SIGABRT (134); fixed objects passed (0).
ELF symbols, link maps and direct call instructions confirmed that these
executables used the selected objects. Neither executable replaced libc.
This is an isolated software contract, not an installed libc update,
physical-board check or acceptance of the ongoing GCC test suite.

On 2026-10-07 a complete static/PIC/shared libc from `cffffd40` was built
with the normal libc Makefiles and native GCC 12.5 on Orange Pi Zero 3W.
Its shared artifact, SHA256
`5dff821f1a1b42845e642cedd16375b310b78d04fa624bb21472d33b0fc14130`,
passed all 850 checks through the original twenty DSO exports. The installed
libc baseline failed 140 checks through those same direct bindings. Startup
receipts confirmed one selected libc mapping and every helper's device/inode.
The same private candidate passed a bounded allocator/string/stdio smoke and
27 existing libc/libpthread ATF cases covering memory, streams, fork, signals,
TLS/dlopen, locale, time and threads before installation.
The matching static archive passed the same 850 CAS checks and allocator/stdio
smoke; link maps identified all twenty helpers in that archive.

The shared/static pair was then installed on that board with retained rollback
copies. Fresh processes using ordinary installed-library resolution passed the
smoke and all 850 CAS checks without `LD_LIBRARY_PATH`. Existing processes may
retain the old mapping until their next exec. This is not a VM libc update or
acceptance of the original full GCC suite.
The build used installed platform headers plus source-local headers; this
does not establish a complete fresh userland/header release or sustained use.

On 2026-10-09 the same accepted static archive was used in an EmberBSD cross
sysroot. This contract was freshly compiled with GCC 16.2 on macOS. On CM5,
the repaired archive passed 850 checks; the old archive failed 140 using the
same test objects. No libc rebuild or board installation was performed.
Ports records the [sysroot repair and provenance limits](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-build-tools/cross/sysroot.md).

All existing acquire/release instructions, retry branches and barriers
remain unchanged. Functional coverage of the five suffixes does not prove
the complete memory model. In particular, the existing `_sync` mismatch
exit skips its trailing barrier; that separate ordering issue is not fixed.
`_HAVE_LSE` is not enabled: its dispatch predicate requires a separate audit,
and this regression does not claim LSE execution or support.
