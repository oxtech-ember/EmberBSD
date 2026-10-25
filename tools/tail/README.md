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
The unmodified source failed the unaligned case with `EINVAL` on the selected Darwin ABI.
Earlier ordinary and aligned cases passed. The later boundary cases were not reached.
The repair aligns each mapping base to the actual page size.
Each mapping ends at the next unscanned byte and spans at most 10 MiB plus one page minus one byte.
Only the physical EOF byte is skipped during newline counting.
Every byte at a preceding window boundary is inspected once.
The remaining mapped suffix is written before the caller resumes at that window's end.
The same 31 cases and exact expected bytes remain unchanged.

The existing [NetBSD scan](https://github.com/NetBSD/src/blob/trunk/usr.bin/tail/forward.c)
was compared with the [OpenBSD stream scan](https://github.com/openbsd/src/blob/master/usr.bin/tail/forward.c).
The [FreeBSD](https://github.com/freebsd/freebsd-src/blob/main/usr.bin/tail/misc.c) and
[DragonFly BSD](https://github.com/DragonFlyBSD/DragonFlyBSD/blob/master/usr.bin/tail/misc.c)
mapping helpers also align mapping offsets. No donor implementation was copied.
The local repair preserves the existing source notices and follow state machine.
Source and synthetic runner checks do not establish native correctness or memory acceptance.
