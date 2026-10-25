# Initial efun compatibility subset

[bootstrap-efun.json](bootstrap-efun.json) selects four complete OS C sources.
The normal compatibility library uses the same [source fragment](efun.mk).
The [shared header](compat_efun.h) contains the original common declarations and macros.
Both `compat_defs.h` and the finite bootstrap configuration include that header.
No function implementation is copied, shortened or replaced.

The source closure contains `efun`, `reallocarr`, `strtoi` and `strtou`.
The integer conversions retain the shared `_strtoi.h` template and its BSD notices.
The control program checks native declarations, linking and execution first.
It requires the native APIs used by this initial subset, including `fgetln`.
The five subsequent probes check declarations and linkable native functions.
Exit zero means available. Exit one means unavailable for that exact compiler and ABI.
Any other exit stops the producer. Every command and raw diagnostic remains evidence.
A successful negative observation does not establish why a declaration or link failed.
Review those diagnostics before accepting the resulting program.

SDK writes `nbtool_config.h` only after all five observations drain.
The header contains their actual boolean results and the common declarations.
It is an efun subset header, not a claim that full OS configure completed.
The five observations cannot substitute a configure cache or a host preset.
A failed control, incomplete probe roster or changed input refuses the build.
No make, installer, shell-based configure or previous compatibility archive is needed.
The existing SDK program controller owns execution, locks and output checks.

The initial consumers are [cut](../cut/README.md), [sort](../sort/README.md)
and [hexdump/od](../hexdump/README.md).
Their finite native profiles use the same probes, source closure and configuration bytes.
They currently accept Darwin GCC16 and an explicit native ABI.
The normal OS compatibility recipe keeps its complete source list and configure route.
This subset is not the Ports `pkgtools/libnbcompat` interface or an ordinary SDK provider.
Actual compilation, Cppcheck, path analysis and memory validation remain separate gates.

The design retains the established [NetBSD tools/compat source closure](https://github.com/NetBSD/src/blob/trunk/tools/compat/Makefile).
[OpenBSD](https://github.com/openbsd/src/blob/master/usr.bin/cut/cut.c),
[FreeBSD](https://github.com/freebsd/freebsd-src/blob/main/usr.bin/cut/cut.c) and
[DragonFly](https://github.com/DragonFlyBSD/DragonFlyBSD/blob/master/usr.bin/cut/cut.c)
retain related BSD cut algorithms with different native integrations.
Their implementations are not substituted for the current EmberBSD sources.
