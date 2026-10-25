# Native bootstrap env

[bootstrap.json](bootstrap.json) selects the existing
[env implementation](../../usr.bin/env/env.c) and [program recipe](../../usr.bin/env/Makefile).
The implementation retains its BSD copyright notices.
It includes the standard `stddef.h` declaration for `ptrdiff_t`.
The [host recipe](Makefile) uses `tools/Makefile.host` with `NOCOMPATLIB=yes`.
The [shared header](../compat/bootstrap_cdefs.h) retains source annotations.

The SDK `env-bootstrap` profile uses native libc, GCC16 and the selected host ABI.
Pin the source, header, manifest, README and makefiles to one OS revision.
The nineteen cases cover assignment, replacement, empty values, clearing,
NUL separators, option terminators, removal, directory changes and failures.
Child cases execute `./env`, the same owned output in the work directory.
They check inherited values, removal, `PWD` removal and child exit propagation.
The missing command and directory use the preflight-absent `missing.txt` member.
The nonexecutable command is the work directory itself.
No test executes a bare command or searches the host default `PATH`.
Normal `env` command search behavior remains available in the implementation.
The final runtime observation uses `-i` without a child.

These cases do not qualify arbitrary external child programs or `PATH` providers.
Source checks do not establish native compilation, C analysis or memory acceptance.
