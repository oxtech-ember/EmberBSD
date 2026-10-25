# Native bootstrap cmp

[bootstrap.json](bootstrap.json) describes the canonical four C sources and
`extern.h` from [usr.bin/cmp](../../usr.bin/cmp/Makefile). The host entry adds no
copy of the comparison implementation. Its native-libc requirements include
`mmap`, `fdopen`, BSD `err`/`warn`, `getprogname`, and the native file/type APIs.

The SDK owns input admission, GCC16 selection, native ABI arrays, process
control, shared link resources and output evidence. This entry does not qualify
an initial provider or an ordinary SDK. The existing program-manifest envelope
is used. Use an SDK build that recognizes this exact fixed manifest. The program
selection must preserve its source and native ABI checks.

The native libc must supply every called interface. No target headers, target
library, generated configure header or old library is selected. Pin every listed
C file, header and both canonical makefiles to one OS commit. Keep their BSD
licenses, copyright notices and upstream identifiers.

The shared [bootstrap annotation header](../compat/bootstrap_cdefs.h) includes
the native `sys/cdefs.h`. It defines missing source-ID, copyright, noreturn and
array-count macros. It retains source strings and a real noreturn attribute.
It supplies no function, changes no target header and suppresses no diagnostic.
The full tools compatibility layer needs generated `nbtool_config.h`; forcing
its config-dependent `compat_defs.h` into this direct build is not valid.

For each source, keep the accepted `selection.command('cc')` once, then the
native ABI `cppflags` and `cflags`. The one source-relative file operand of
manifest `-include` must become the exact absolute pinned header path:

```text
['-include', source_root / 'tools/compat/bootstrap_cdefs.h']
```

Add the listed source-rooted include directories. Compile with `-c source -o
owned_object`. Link the ordered owned objects with the same accepted compiler,
native C flags, native link flags and `-o owned_output`. There are no other
manifest flags or libraries. Use the SDK shared executor and fresh owned work.
The normal host entry uses `tools/Makefile.host` with `NOCOMPATLIB=yes` because
this bounded entry requires the native libc interfaces directly.

Source inspection is not compiler, loader or runtime acceptance. The first
actual build must record all GCC16 children and inspect native load commands.

## Finite runtime cases

Create the manifest files only in new owned runtime work. Keep `missing.txt`
absent and set `LC_ALL=C`. Cases cover equal and different files, `-s`, `-l`,
stdin, one and two skips, the `-c` stream route, EOF and error exits 0/1/2.
Keep exact stdout and empty stderr for successful and ordinary difference cases.
Where native libc diagnostics vary, require the stated nonempty stderr and exit.
Silent missing-file errors still require exit 2 and empty output.
The cases do not replace large-file, memory or complete upstream testing.
