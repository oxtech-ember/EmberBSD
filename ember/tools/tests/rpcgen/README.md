# RPC preprocessor lifecycle checks

These AI-assisted host tests run the actual `rpcgen` executable.
They cover a missing preprocessor, nonzero exit after valid input, signal
termination, and nonzero exit after the output pipe reaches EOF.
A failed second scan must remove earlier files from the default invocation.
A successful default invocation must reap each preprocessor before the next scan.
The ordinary header and all four default outputs must still contain generated data.

Build the native tool with `build.sh tools`, or rebuild only `rpcgen` against
an existing matching native compatibility library. For the latter, set `CC`
to GCC 16 and `tool_dir` to the native tool directory:

```sh
"$CC" -O2 -g -std=gnu17 \
    -I"$tool_dir/include/compat" -Itools/compat \
    -DHAVE_NBTOOL_CONFIG_H=1 -D_FILE_OFFSET_BITS=64 \
    usr.bin/rpcgen/*.c -L"$tool_dir/lib" -lnbcompat -lrt -lz \
    -o /absolute/output/rpcgen
python3 ember/tools/tests/rpcgen/run.py \
    --rpcgen /absolute/output/rpcgen --cpp /absolute/native/cpp \
    --work /absolute/new-test-directory
```

`--cpp` names one executable. Use a wrapper if the native compiler needs `-E -x c`.
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

Analyze `rpc_main.c` with GCC `-fanalyzer` and Clang `--analyze`, using the native
tool's actual include paths and defines. Cppcheck can consume the matching
`gcc -E` output to preserve the host compatibility configuration.
Compare findings against the same source before the repair.
These tests establish host process behavior, not target execution or a full OS build.
