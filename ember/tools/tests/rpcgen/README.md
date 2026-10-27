# RPC preprocessor lifecycle checks

These AI-assisted host tests run the actual `rpcgen` executable.
They cover a missing preprocessor, nonzero exit after valid input, signal
termination, and nonzero exit after the output pipe reaches EOF.
A failed second scan must remove earlier files from the default invocation.
A successful default invocation must reap each preprocessor before the next scan.
The ordinary header and all four default outputs must still contain generated data.

Build the native tool with the selected native Ports toolchain through
`build.sh tools`. To rebuild just this tool in a matching existing tool tree,
use that tree's `nbmake-<target>` wrapper in `tools/rpcgen`. Clang is the default;
an explicit GCC platform profile uses the same producer and tests.

```sh
python3 ember/tools/tests/rpcgen/run.py \
    --rpcgen /absolute/tools/bin/nbrpcgen --cpp /absolute/native/bin/clang \
    --cpp-argument=--driver-mode=cpp \
    --cpp-argument=--target=x86_64--netbsd \
    --work /absolute/new-test-directory
python3 ember/tools/tests/rpcgen/arguments.py \
    --rpcgen /absolute/tools/bin/nbrpcgen --cpp /absolute/native/bin/clang \
    --make /absolute/native/bin/bmake --source /absolute/EmberBSD \
    --work /absolute/new-argument-test-directory
```

`RPCGEN_CPP` and `--cpp` name one executable, including paths containing spaces.
The repeated `-X` option forwards one separate argument without shell parsing.
The OS make caller separates the executable from the selected CPP arguments;
a standalone GCC `cpp` therefore receives no invented driver flags. External
Clang uses `--driver-mode=cpp`, matching the in-tree `clang-cpp` role: `clang -E`
alone treats an `.x` file as unused linker input. Explicit caller language
options remain available.

The argument check invokes actual Clang on an `.x` input requiring the NetBSD
amd64 predefined macros, preserves a macro value and paths containing spaces,
executes the common make-generated command and checks its single-executable
case. Missing/invalid arguments and the bounded argv list remain failures.
It also checks an explicit assembler-language override.

The runner retains commands, statuses, diagnostics, binary hashes and output hashes.
It refuses an existing work directory. A compiler failure must return status 1.
The test does not accept another status from an analysis tool as a passing failure.

For a causal comparison, run the same tests against a pre-repair native tool.
It accepts failed preprocessors and leaves previous children unreaped.
Its ordinary header case passes, while the other six cases fail.
Use a separate work directory for each binary.

For memory checks, add `--runner` with a Valgrind command:

```sh
python3 ember/tools/tests/rpcgen/run.py \
    --rpcgen /absolute/output/rpcgen --cpp /absolute/native/cpp \
    --work /absolute/new-memcheck-directory \
    --runner 'valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --track-fds=yes --error-exitcode=99'
```

Keep baseline and repaired diagnostics. Existing parser allocation leaks and
output streams retained until process exit remain outside this lifecycle repair.
They prevent a clean whole-program Memcheck result. Do not suppress them or
describe a nonzero memory-check run as passing.

Analyze `rpc_main.c` with Clang `--analyze`, or GCC `-fanalyzer` for the explicit
GCC profile, using the native tool's actual include paths and defines. Cppcheck
can consume matching preprocessed output to preserve the host compatibility
configuration.
Compare findings against the same source before the repair.
These tests establish host process behavior, not target execution or a full OS build.

For the separate-argument change, the native Clang checks and ASan/UBSan
argument/lifecycle cases pass. This is not a Valgrind or leak-check result.
Paired Clang Static Analyzer runs report the same six existing `outfilename`
leaks; the changed argument parser has no new finding. Cppcheck 2.22.0 stops
at an Apple `_errno_t.h` syntax error in both preprocessed versions. That
analysis remains incomplete. Paths containing spaces are checked for direct
`RPCGEN_CPP` and separate `-X` arguments; make retains its CPP word-list contract.
