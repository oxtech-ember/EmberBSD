# Native bootstrap wc

[bootstrap.json](bootstrap.json) selects the existing
[wc source](../../usr.bin/wc/wc.c) and
[program recipe](../../usr.bin/wc/Makefile).
The C implementation, BSD license and original notices are unchanged.
The [host recipe](Makefile) uses `tools/Makefile.host` with `NOCOMPATLIB=yes`.

The SDK `wc-bootstrap` profile requires native BSD libc.
The ABI must provide `u_quad_t`, `MAXBSIZE`, `mbrtowc`, `iswspace`,
`err` interfaces and standard file APIs. No generated source or extra library is needed.
The [shared header](../compat/bootstrap_cdefs.h) supplies missing source annotations.
Do not inject target headers or replace native libc functions.

SDK owns input admission, native GCC16 and ABI selection, process control,
link resources and evidence. Pin all source members to one OS commit.
Retain this README, the manifest, both makefiles and `tools/Makefile.host`.

The manifest owns 18 literal cases in the C locale.
They cover stdin, files, line, word, byte and character counts, longest lines,
totals, whitespace, empty input, unterminated input and errors.
The first case reads `input.txt` for the final runtime observation.
These fixtures do not establish multibyte-locale or arbitrary directory-size behavior.

The unchanged implementation sizes stack buffers from the native `MAXBSIZE`.
Darwin SDK 27 defines it as 1 MiB, so these buffers use about 5 MiB.
Runtime acceptance must retain the actual stack and memory result.
Source checks alone do not establish compilation, C analysis or runtime acceptance.
