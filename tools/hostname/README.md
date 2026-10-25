# Native bootstrap hostname

[bootstrap.json](bootstrap.json) describes the canonical one C source from
[bin/hostname](../../bin/hostname/Makefile). The native libc supplies
`gethostname`, `sethostname`, `setprogname`, `getprogname` and the standard APIs.
The program implementation is unchanged. This bootstrap contract executes only
its read-only forms. It never invokes the hostname setter.

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

## Read-only runtime cases

The only approved argv tails are empty, `['-s']` and invalid option `['-z']`.
No hostname operand is allowed. The SDK fixed recognizer must reject any other
argv before it can dispatch a child. Do not test successful hostname changes.

The two read cases require exit 0, nonempty newline-terminated stdout and empty
stderr. Hostname data is dynamic; do not put a machine name in this source file.
The SDK's dedicated fixed check must compare the full result with its read-only
control-plane `gethostname` observation, and the short result with that name
before the first dot. The `stdout_nonempty` case data alone is not that equality
proof. The invalid option requires exit 1, empty stdout and a usage diagnostic.
The combined-log limitation must remain explicit when streams are combined.
