# Native bootstrap uniq

[bootstrap.json](bootstrap.json) selects the existing
[uniq source](../../usr.bin/uniq/uniq.c) and
[program recipe](../../usr.bin/uniq/Makefile).
The C implementation, BSD license and original copyright notices are unchanged.
The [host recipe](Makefile) uses `tools/Makefile.host` with `NOCOMPATLIB=yes`.

Use the existing SDK direct-source `uniq-bootstrap` profile.
It requires native libc with `getprogname`, `setprogname`, `fgetln`, `asprintf` and standard stream APIs.
It needs no generated source, configure output or additional library.
The shared [annotation header](../compat/bootstrap_cdefs.h) supplies missing
source-ID, copyright and noreturn annotations, plus a guarded array-count macro.
Do not inject target headers.

The SDK owns input admission, native GCC16 and ABI selection, process control,
shared link resources and evidence. Pin all listed source members to one OS commit.
Retain this README, the manifest, both canonical makefiles and `tools/Makefile.host`.
Compile the one C file with the selected compiler tuple and native ABI arrays.
Resolve the manifest `-include` operand to its exact source-rooted absolute path.
Link the owned object with that same compiler and the native ABI link flags.
Use the shared runner; do not add a wrapper or separate executor.

## Finite runtime cases

The manifest owns 16 semantic cases and their expected bytes and exits.
The cases cover adjacent deduplication, counts, duplicate-only and unique-only
output, skipped fields/characters, nonadjacent repeats, empty and unterminated
input. Invalid skips, options, excess operands and missing input files fail.
The successful output-file operand is outside this initial runtime profile.
The implementation keeps that operation; no feature is removed.

Write the literal `files` only inside fresh owned work. Keep `missing.txt` absent.
Input fixtures must remain unchanged through completion. The selected shell's
builtin `printf` supplies stdin; no ambient utility computes the expected output.
Compare actual stdout and stderr separately. Error cases require exit 1 and
nonempty stderr, with the exact stdout retained even after partial success.

The first pinned case supplies the final DYLD runtime observation arguments.
It reads the owned `input.txt` fixture and needs no piped stdin.
Retain compiler children, native load commands and all actual result streams.
Source checks do not establish native compilation, source analysis or runtime acceptance.
