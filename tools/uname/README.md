# Native bootstrap uname

[bootstrap.json](bootstrap.json) selects the existing
[uname implementation](../../usr.bin/uname/uname.c) and [program recipe](../../usr.bin/uname/Makefile).
The implementation retains its original four-clause BSD license and notices.
Its machine architecture buffer uses the actual `struct utsname.machine` size.
It does not require a host-specific `SYS_NMLN` macro.
The [host recipe](Makefile) uses `tools/Makefile.host` with `NOCOMPATLIB=yes`.
The [shared header](../compat/bootstrap_cdefs.h) retains source annotations.

The SDK `uname-bootstrap` profile needs native `uname` and read-only `sysctl` APIs.
SDK owns native GCC16 and ABI selection, admission and process control.
Pin the source, header, manifest, README and makefiles to one OS revision.
The fifteen cases cover individual fields, combined and repeated flags,
option order, the option terminator, invalid options and operands.
Each success case declares ordered field names in `stdout_uname`.
The SDK reads the five standard fields through Python `os.uname()`.
It reads `machine_arch` independently through Darwin `CTL_HW/HW_MACHINE_ARCH`.
The adapter calls the existing process library through Python `ctypes`.
A missing API, failed query or malformed string fails verification without fallback.
No external command or child output supplies an expected value.
The expected bytes preserve spaces and embedded newlines in the version field.
As in the normal utility, `-a` excludes `-p`; `-ap` requests both.
The final runtime observation uses the default system-name invocation.

Source checks do not establish native compilation, C analysis or memory acceptance.
The `-p` cases need an actual accepted host ABI and runtime result.
