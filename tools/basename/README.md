# Native bootstrap basename

[bootstrap.json](bootstrap.json) describes the existing
[basename source](../../usr.bin/basename/basename.c) and its
[program recipe](../../usr.bin/basename/Makefile).
The [host recipe](Makefile) uses `tools/Makefile.host` with `NOCOMPATLIB=yes`.
The implementation, BSD license and original copyright notices remain unchanged.

This entry requires native libc, including `basename`, `getopt`, `err` and locale APIs.
It needs no generated source, configure output or extra library.
Use the shared [bootstrap annotation header](../compat/bootstrap_cdefs.h).
It supplies missing source-ID, copyright and noreturn annotations only.
Do not inject target headers or the generated full-tools compatibility layer.

Use the existing SDK direct-source program profile, `basename-bootstrap`.
The SDK owns input admission, native GCC16 and ABI selection, process control,
shared link resources and output evidence. It must recognize this exact manifest.
Pin the listed source, shared header, manifest, README and both canonical makefiles
with `tools/Makefile.host` to one OS commit. Refuse extra files and source links.

Compile the source with the accepted compiler tuple and native ABI arrays.
Resolve the manifest `-include` operand to the exact pinned absolute header path.
Link the one owned object with that compiler and the native ABI link flags.
Do not add compiler flags, libraries, alternate sources or a separate executor.

The finite cases cover normal, empty, root, repeated-slash and trailing-slash
paths, a name without a directory and an explicit option terminator.
Suffix cases check matching, absent, complete-name and empty suffixes.
Missing operands, excess operands and an invalid option must fail with exit 1,
empty stdout and a nonempty diagnostic. Successful cases require exact stdout
and empty stderr. The selected shell supplies stdin with its builtin `printf`.
No system path utility computes the expected outputs.

The runtime observation uses the fixed operand `/usr/file`.
It is string input; the program does not open that path.
Keep actual compiler children, native load commands and separate stdout/stderr.
Source checks do not establish compilation, native runtime or provider acceptance.
