# Native bootstrap uname

[bootstrap.json](bootstrap.json) selects the existing
[uname implementation](../../usr.bin/uname/uname.c) and [program recipe](../../usr.bin/uname/Makefile).
The implementation retains its original four-clause BSD license and notices.
On non-Apple hosts, its machine architecture buffer uses the actual `struct utsname.machine` size.
It does not require a host-specific `SYS_NMLN` macro.
On Darwin, `-p` selects the compiler's processor family, as in
[Apple uname](https://github.com/apple-oss-distributions/shell_cmds/blob/main/uname/uname.c).
The adapted branch retains its source copyright and license notices.
ARM, x86 and PowerPC produce `arm`, `i386` and `powerpc`, respectively.
GCC `__aarch64__` and Darwin `__arm64__` are both recognized.
An unknown Darwin family fails explicitly. Other hosts retain the original sysctl query.
The [host recipe](Makefile) uses `tools/Makefile.host` with `NOCOMPATLIB=yes`.
The [shared header](../compat/bootstrap_cdefs.h) retains source annotations.

The SDK `uname-bootstrap` profile needs the native `uname` API.
SDK owns native GCC16 and ABI selection, admission and process control.
Pin the source, header, manifest, README and makefiles to one OS revision.
The fifteen cases cover individual fields, combined and repeated flags,
option order, the option terminator, invalid options and operands.
Each success case declares ordered field names in `stdout_uname`.
The SDK reads the five standard fields through Python `os.uname()`.
For `machine_arch`, it maps the kernel-reported machine to a known processor family.
The native-host, GCC and Mach-O checks must agree before execution.
A failed API or unknown machine fails verification without fallback.
The oracle calls no external command or foreign-function interface.
It does not use the declared but unavailable Darwin `HW_MACHINE_ARCH` MIB.
No external command or child output supplies an expected value.
The expected bytes preserve spaces and embedded newlines in the version field.
As in the normal utility, `-a` excludes `-p`; `-ap` requests both.
The final runtime observation uses the default system-name invocation.

Source checks do not establish native compilation, C analysis or memory acceptance.
The `-p` cases need an actual accepted host ABI and runtime result.
