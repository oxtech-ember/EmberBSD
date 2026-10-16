# Compiler and system variadic-header interoperability

The libc assertion source includes `syslog.h`, which includes `sys/stdarg.h`.
Its later `extern.h` include selects the external GCC16 resource `stdarg.h`.
The resource header defines its public macros unconditionally.
The old BSD macro guards therefore handled only compiler-first include order.
System-first order failed under the real libc `-Werror -Wsystem-headers` flags.

Ordinary userspace now includes the public `stdarg.h` before the BSD fallback.
A compiler resource header can supply its own C11, C23 and C++ forms.
No compiler include guards or GNU typedef flags are fabricated.
No compiler header is patched or copied into the OS implementation.
If installed `stdarg.h` is a symlink to `sys/stdarg.h`, the existing OS guard breaks the cycle.
The existing BSD definitions then supply the fallback.
Kernel, standalone and lint branches do not include a compiler resource header.
The BSD license, builtin compatibility branch and ANSI fallback namespace stay intact.

## Run the regression

Use a C23-capable native compiler and a C++20-capable compiler:

```sh
ruby ember/tests/stdarg-interop.rb
```

The native run models only `sys/cdefs.h`, `sys/ansi.h` and feature-test prerequisites.
It compiles the production `sys/stdarg.h` and executes real variadic functions.
Set `STDARG_CC`, `STDARG_CXX` and `STDARG_FLAGS` to select compilers, sanitizers or analysis.
The recorded native checks use ASan/UBSan and GCC16 `-fanalyzer`.
They are not Valgrind checks; Valgrind is unavailable on the macOS host.

For a cross compiler and an existing, verified amd64 EmberBSD sysroot:

```sh
STDARG_CC=/absolute/toolchain/bin/x86_64--netbsd-gcc \
STDARG_CXX=/absolute/toolchain/bin/x86_64--netbsd-g++ \
STDARG_SYSROOT=/absolute/ember-sysroot \
ruby ember/tests/stdarg-interop.rb
```

Target mode uses real OS headers and compiles without executing target code.
Resource-free cases use `-nostdinc`, source headers and explicit amd64/x86 machine links.
They do not use the installed public header or compiler resource directories.
The script uses automatic temporary directories and removes its outputs.

The 21 cases cover C11, C23 and C++20 in both include orders, a partial compiler typedef request,
system-only inclusion and the installed public-symlink case.
C23 resource-header cases use one-argument `va_start`.
The resource-free BSD fallback keeps its existing two-argument form.
Kernel, standalone and lint branches also check the ANSI namespace.
Lint branches are compile-only checks with `__lint__` defined, not execution of NetBSD lint.
Other native cases execute integer, floating-point and pointer arguments plus independent copies.

## Evidence boundary

The actual libc `assert.c` command fails before the fix and passes after it with the same diagnostic flags.
The candidate also passes full-TU GCC16 `-fanalyzer` without diagnostics.
Baseline path analysis cannot proceed past the macro-redefinition errors.
Cppcheck retains the same one inherited const-pointer style report in `assert.c`.
That style report remains with the libc owner; no new report or suppression is added.
Five negative controls detect missing delegation, each resource-free guard and the symlink recursion guard.
These checks establish header and isolated-TU contracts, not a completed libc or base distribution.
