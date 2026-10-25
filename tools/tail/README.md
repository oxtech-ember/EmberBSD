# Native bootstrap tail

[bootstrap.json](bootstrap.json) selects the complete existing
[tail recipe](../../usr.bin/tail/Makefile) and its five C files.
The [host recipe](Makefile) uses `tools/Makefile.host` with `NOCOMPATLIB=yes`.
It includes the source `extern.h` and the shared
[annotation header](../compat/bootstrap_cdefs.h).
The source copyright and BSD license notices remain intact.

The profile needs native libc, mmap, kqueue and kevent from the selected host ABI.
It does not remove follow mode, file rotation handling or the existing sleep fallback.
The normal OS recipe already provides `tac` as an invocation of the same implementation.
The bootstrap runner creates that alias and checks both invocation names.
SDK owns compiler and ABI selection, process control and input admission.
Pin the full twelve-file source roster to one OS revision.

The 31 cases cover files, stdin, byte and line offsets, zero counts,
reverse output, headers, errors and the existing tac invocation.
The pipe-follow case checks the existing POSIX behavior for a pipe.
It does not exercise live regular-file append, rotation or truncation.
Those follow cases require separate runtime acceptance.

Large fixtures use a nonrecursive list of UTF-8 strings and `{text, repeat}` segments.
SDK validates and expands these data values without a shell or generator command.
The same data form defines exact expected stdout.
Each value is limited to 16 segments and 32 MiB of decoded bytes.
All fixture files together are limited to 64 MiB.
The decoder preserves literal string values used by earlier profiles.

The fixtures bracket the existing 10 MiB mmap window and cross a window boundary.
An unaligned file checks the mapping offset requirement.
A two-window file places a newline immediately before the second window.
Its one-line and two-line cases check that the scan neither skips nor duplicates that boundary.
These are success expectations for the canonical implementation, not accepted failures.
This initial profile keeps C unchanged so the selected compiler can establish the regression first.
Source and synthetic runner checks do not establish native correctness or memory acceptance.
