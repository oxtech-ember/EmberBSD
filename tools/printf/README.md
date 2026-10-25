# Native bootstrap printf

[bootstrap.json](bootstrap.json) selects the existing
[printf implementation](../../usr.bin/printf/printf.c) and
[program recipe](../../usr.bin/printf/Makefile).
The C implementation, BSD license and original notices are unchanged.
The [host recipe](Makefile) uses `tools/Makefile.host` with `NOCOMPATLIB=yes`.

The SDK `printf-bootstrap` profile needs native libc formatting, conversion,
locale, `asprintf`, `getprogname` and standard stream APIs.
The [shared header](../compat/bootstrap_cdefs.h) retains source annotations.
No generated source, private libc implementation or extra library is needed.
The standalone program does not select the shell builtin code paths.

SDK owns native GCC16 and ABI selection, input admission and process control.
Pin the C file, shared header, manifest, README and makefiles to one OS revision.
Do not inject target headers or replace the native libc.

The 24 C-locale cases cover literals, repeated formats, default arguments,
integer bases, floating point, width, precision, characters, escapes and errors.
They include the existing `-L`, `%B` and `\c` behavior.
The cases use literal arguments and exact output bytes from this OS contract.
They do not execute a system printf or depend on a shell printf builtin.
The final direct runtime observation repeats the first successful case.
Source checks do not establish native compilation, C analysis or memory acceptance.
