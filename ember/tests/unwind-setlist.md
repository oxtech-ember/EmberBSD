# Unwind header set selection

The `comp` set requires `/usr/include/unwind.h` only when a selected base build
component installs that header. Two owners have different predicates:

| `MKGCC` | `HAVE_LIBGCC_EH` | Header installer |
| --- | --- | --- |
| no | yes | None in the base build. The external compiler supplies its own SDK header. |
| yes | yes | The selected GCC `libgcc` tree. |
| no | no | The libc `sys/lib/libunwind` include. |
| yes | no | The libc `sys/lib/libunwind` include. |

`lib/Makefile` selects the GCC tree only when `MKGCC` is enabled. Its common
libgcc makefile installs `unwind.h` when `HAVE_LIBGCC_EH=yes`. When that variable
is `no`, `lib/libc/Makefile` includes `sys/lib/libunwind/Makefile.inc`, which
installs its own header independently of `MKGCC`.

Setlist selectors combine comma-separated conditions with AND. Separate records
express the two installer alternatives: `gcc,libgcc_eh` and `!libgcc_eh`.
`MKGCCCMDS` controls compiler commands and must not control this runtime header.
An external compiler disables the base GCC libraries, even when the architecture
still reports `HAVE_LIBGCC_EH=yes`.

The previous unconditional record made `checkflist` require an uninstalled
header in that external-GCC configuration. The corrected record follows the
actual installers. It does not copy a header from an older compiler or relax
the missing-file check. The external compiler SDK and target runtime package
retain their existing owners and payload contracts.

```sh
ruby ember/tests/unwind-setlist.rb \
    /absolute/source-tree /absolute/native-tools /absolute/new-results
```

The regression uses real native `nbmake` to inspect the library traversal and
both owners' `INCS` values. Four flag combinations must match the table. It runs
the unchanged `makeflist`, `sets.subr`, `mkvars.mk` and `checkflist` against private
fixtures containing the actual affected records, directory records and a normal
libc header control. The old record fails with exactly one missing `unwind.h`.
The corrected records pass all four cases. Eight negative controls still reject
missing or extra unwind headers and missing ordinary headers. No allow-missing,
allow-extra or parser exceptions are used.

Fixture headers are labelled text, not installed SDK inputs. This is a host set
composition contract. It does not build an OS distribution, execute target
programs or qualify a VM image. The full distribution and image checks remain
separate acceptance gates.
