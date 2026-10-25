# Native bootstrap pwd

[bootstrap.json](bootstrap.json) selects the existing
[pwd implementation](../../bin/pwd/pwd.c) and [program recipe](../../bin/pwd/Makefile).
The C implementation, BSD license and original notices are unchanged.
The [host recipe](Makefile) uses `tools/Makefile.host` with `NOCOMPATLIB=yes`.

The SDK `pwd-bootstrap` profile needs native libc `getcwd`, `stat`,
`setprogname`, `getprogname` and standard stream APIs.
The [shared header](../compat/bootstrap_cdefs.h) retains source annotations.
No generated source, extra library or replacement libc function is needed.
SDK owns native GCC16 and ABI selection, input admission and process control.
Pin the C file, shared header, manifest, README and makefiles to one OS revision.

The ten C-locale cases cover the physical default, `-L`, `-P`, option order,
repeated options, the option terminator, invalid options and extra operands.
Each success case declares `stdout_work_root: true`.
SDK checks the real controller work directory followed by exactly one newline.
It obtains no expected path from child output or the caller's `PWD` environment.
The shared runner supplies no extra environment override.
The cases do not claim coverage of logical symlink aliases or invalid `PWD` values.
The final direct runtime observation uses the default invocation.
Source checks do not establish native compilation, C analysis or memory acceptance.
