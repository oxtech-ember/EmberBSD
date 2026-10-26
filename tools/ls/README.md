# Native host ls

This recipe builds all five canonical `bin/ls` translation units. It uses
`tools/Makefile.host` and the normal `libnbcompat` configuration. It does not
select `SMALL` or omit the compatibility library. The target recipe keeps its
native libc, libutil, ACL interfaces, and command-line behavior.

Host builds use the shared listing and ACL declarations. ACL presence comes
from the shared no-follow adapter. An unsupported ACL operation remains an
error. Hosts without `struct stat.st_flags` reject requested flag output.
Host timestamp comparisons use the shared installed `compat_stat_time.h`
accessors. Normal configuration selects the real native fields. Target builds
keep their canonical stat aliases and do not include a host tools header.

## Initial SDK contract

`bootstrap.json` defines the initial `ls-bootstrap` source contract. It uses
the complete source roster and the built `listing` component.
Its archive and public headers remain separate inputs. The SDK does
not copy this C implementation or invent a replacement configuration header.

Before compiling ls, the SDK compiles, links, and runs `probes/control.c` with
the same selected compiler, ABI, and component inputs. This required control
checks native FTS traversal, no-follow descriptor operations, nanosecond stat
fields through the same shared accessor, `vis`, `strmode`, password/group lookup,
and wide-character operations.
It creates and removes only its owned work-directory fixture. A failed control
stops the build. The control checks native APIs; it is not an installed tool.
Builds do not require qualification receipts or an independent admission gate.

The 38 cases test exact streams and filesystem observations. They cover names,
hidden entries, recursion, symlinks, hardlinks, size ordering, and time ordering.
The time cases distinguish entries within one second and test reverse and tie
ordering. They support the high-resolution `ls -t` use in configure scripts.
The API control does not substitute for these canonical-program cases.

On macOS arm64 with APFS, GCC16 builds all five sources and the native control.
All 38 cases pass with the normal binary and with ASan/UBSan instrumentation.
The linked listing archive is not instrumented in the latter run.
Leak checking remains unverified because the host LeakSanitizer shutdown deadlocks.
Symbolization is disabled because the initial tools lack a symbolizer.

Darwin reports deprecated `strvis` calls in `util.c` and the native control.
`util.c` allocates the documented four-byte expansion bound before that call.
It also reports a signedness comparison between `size_t` and `MB_CUR_MAX`.
GCC path analysis adds no further diagnostics. None of these warnings is suppressed.
Cppcheck remains pending until the common CMake and analyzer dependencies are built.

This finite contract does not qualify all command-line combinations. Nonempty
long-format metadata, ACL-bearing long output, file-flag output, terminal layout,
non-ASCII locales, nonprintable names, signals, and injected I/O failures need
separate tests. Cppcheck, path analysis, memory checks, and ordinary host
builds remain separate checks. Successful source/data tests do
not prove compilation or native execution.

Run `test-source.py` with the selected SDK Python to check the source contract
and independent timestamp-ordering oracles. It starts no compiler or ls process.
