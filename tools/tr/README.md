# Native bootstrap tr

[bootstrap.json](bootstrap.json) selects the existing
[tr implementation](../../usr.bin/tr/tr.c), [string parser](../../usr.bin/tr/str.c)
and [program recipe](../../usr.bin/tr/Makefile).
The C implementations, BSD licenses and original notices are unchanged.
The [host recipe](Makefile) uses `tools/Makefile.host` with `NOCOMPATLIB=yes`.

The SDK `tr-bootstrap` profile needs native libc character classes, `err`,
memory allocation and standard stream APIs. No generated source or extra library is needed.
The [shared header](../compat/bootstrap_cdefs.h) supplies source annotations and
a guarded `__arraycount` expression. It preserves an existing host definition.
That expression matches the array-size division in [compat_defs.h](../compat/compat_defs.h).
It does not replace any libc function or alter the full compatibility library.

SDK owns native GCC16 and ABI selection, input admission and process control.
Pin both C files, `extern.h`, the shared header, manifest, README and makefiles
to one OS revision. Do not inject target headers.

The 18 C-locale cases cover translation, ranges, character classes, complements,
deletion, squeezing, padding, repetition, equivalence, escapes, empty input and errors.
Class cases exercise `genclass`, whose table length uses `__arraycount`.
The source remains a byte-oriented implementation; no new multibyte behavior is claimed.

`tr` has no file operands. Semantic cases supply literal stdin through the shared runner.
The final direct runtime observation uses valid translation operands and reads EOF.
It checks loaded images separately from the nonempty semantic cases.
Source checks do not establish native compilation, C analysis or memory acceptance.
