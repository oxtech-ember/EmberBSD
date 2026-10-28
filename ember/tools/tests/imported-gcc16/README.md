# Imported-source GCC16 regressions

These AI-assisted tests cover the imported IPFilter PCAP record reader and ICMP
lookups, nvi allocation-size checks, and both byacc parser skeletons. Production
sources retain their original copyrights and licenses. The fixtures are new
BSD-2-Clause code. Generated excerpts retain their source notices; do not
redistribute them without the source tree's corresponding licenses.

The IPFilter/nvi runner extracts the actual functions from the selected tree.
It does not copy their implementations into the fixture. PCAP reads are split
at every possible header fragment size. The fixture checks byte order, EOF,
read errors, invalid lengths, and that payload bytes remain unread. Global-header
open tests use real files and verify descriptor closure on every short-header
length and on rejected metadata, while a valid header leaves ownership with
the caller. ICMP tests
use sparse tables and missing, negative, and large type numbers. nvi tests use
both byte and wide characters, preserve cursor clamping, and check both sides
of the character-count limit. The `nvi-space` case compiles the real
`ADD_SPACE_RET` and `ADD_SPACE_GOTO` macros. A window temporary buffer grows
in place. A screen with no window grows the caller's block. Allocation failure
leaves that block allocated. nvi database and allocator hooks are models;
no test allocates an object near `SIZE_MAX`.

Use GCC 16. Set `CC` explicitly and keep outputs outside the source checkout.
On macOS, set `CFLAGS` to `-isysroot` followed by the SDK path printed by
`xcrun --show-sdk-path`. Python uses only its standard library.

```sh
CC=/absolute/gcc-16 python3 ember/tools/tests/imported-gcc16/run.py \
    /absolute/source-tree --nvi-source /absolute/prepared-nvi --work /absolute/new-work
```

The default mode uses ASan/UBSan. `--mode analyze` adds GCC `-fanalyzer` and
Cppcheck, retaining all diagnostics. Cppcheck error, warning, or performance
findings fail the runner; inherited style and missing-header information remain
visible in the logs. `--mode memcheck` uses Valgrind with leak and origin checks
on supported hosts. It is not a macOS ARM64 mode. `--mode plain` omits those
tools. All modes retain input hashes, commands, exits and logs in the work
directory. Existing work directories are rejected.

For nvi cases, select the common Ports prepared source with `--nvi-source`
or `EMBER_NVI_SOURCE`; no nvi source is needed for IPFilter-only cases.
For causal comparison with the historical embedded copy, use `--revision <commit>` to extract the same functions
from a Git commit. Run `--case pcap`, `--case pcap-open`, `--case icmp`,
`--case nvi`, and `--case nvi-space` separately
so one expected failure does not hide the others. The old PCAP fixture fails
its record/content assertions; the old short global-header path retains an open
descriptor and fails its ownership assertion; the old sparse ICMP lookup can fail under
ASan; the old nvi function reaches an allocation request that the boundary
contract rejects. The old `nvi-space` macro dereferences a missing window.
A baseline `btyacc-queue.py` run loses the previous block when realloc fails.
Such baseline failures are expected evidence, not a passing
command. No test requires a particular signal number.

## Parser generators

Build the prepared common Ports byacc source twice with GCC 16 and its `configure` script:
`--disable-btyacc` selects the classic skeleton; `--enable-btyacc` selects the
backtracking skeleton. For an out-of-source build, copy `yaccpar.skel` and
`btyaccpar.skel` into each build directory before running `make`; the upstream
regeneration rule reads those names relative to the build directory. Set
`CPP` explicitly to the same compiler with `-E` and the host SDK flags if the
host does not provide `/lib/cpp`. Keep these build directories outside Git.

```sh
CC=/absolute/gcc-16 python3 ember/tools/tests/imported-gcc16/byacc.py \
    --byacc /absolute/classic-build/yacc \
    --btyacc /absolute/backtracking-build/yacc \
    --work /absolute/new-parser-work
```

The runner generates actual reentrant parsers. Both valid input and recovery
from a syntax error must pass. One grammar omits `yynerrs`; a second reads it
from its error action and checks that the count is preserved. Removing only
the new explicit optional use is the negative control: GCC16 must reject the
first grammar with `unused-but-set-variable`, while the second remains valid.
The counter cannot be removed because user actions may inspect it. The
skeleton banner identifies the changed generator as its upstream comment
requires. Parser programs run with ASan/UBSan.

## Backtracking allocation failures

Build the backtracking generator after the skeleton repair. Then run:

```sh
CC=/absolute/gcc-16 python3 ember/tools/tests/imported-gcc16/byacc-oom.py \
    --btyacc /absolute/backtracking-build/yacc \
    --work /absolute/new-allocation-work
```

The runner generates parsers with and without location tracking. It injects a
failure into each `yyNewState` allocation, checks zero-sized and successful
states, and checks the first parser allocation failure. Each case runs in a
separate process. An allocation ledger checks for leaks and invalid frees;
ASan/UBSan and patterned automatic-variable initialization check the failure
paths. The generated parser implementation is used without edits.

For a causal comparison, pass a generator built before the repair. Its first
parser allocation failure and partial-state allocation failures must fail the
ownership checks. The baseline runner returns nonzero. These tests cover state
creation and initial parser cleanup. Lexical-queue growth is covered below.
Backtracking grammars are outside both contracts.

## Lexical queue growth

Compile both enlarge sites from `btyaccpar.skel` and from the embedded strings
in `btyaccpar.c`. Run each site with and without location tracking:

```sh
CC=/absolute/gcc-16 python3 ember/tools/tests/imported-gcc16/btyacc-queue.py \
    /absolute/prepared-byacc-source --work /absolute/new-queue-work
```

The queue already exists. The realloc hook moves each successful block. Failing
the first, second, or location reallocation must keep the earlier blocks and
retarget cursors of blocks that already moved. A successful growth preserves
the old elements. The runner does not execute a backtracking grammar. It does
not replace `byacc-oom.py`. `--mode analyze` adds GCC `-fanalyzer` and Cppcheck.
`--mode plain` omits ASan/UBSan. The default mode uses both sanitizers.

## Scope

These are host software contracts, not a full OS build or device qualification.
The nvi deletion test isolates the first partial-line allocation. The
`nvi-space` case covers the temporary-buffer macros, not a full editor.
The parser tests do not qualify all grammars.
The adjacent DTrace relocation-index cleanup preserves the existing `count`
sequence. It does not implement the existing ARM/AArch64 relocation TODOs.
Its target compile must be checked separately with matching headers and flags;
a successful object does not establish ARM DTrace runtime support.
