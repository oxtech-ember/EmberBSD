# Native touch

The normal `tools/touch` target builds the complete `usr.bin/touch/touch.c`.
It uses the common host compatibility library through `Makefile.host`.
Target builds retain libutil, the native time interfaces, and the full date grammar.

The `touch-bootstrap` profile uses two separately accepted initial components:
`filetime` and `parsedate`. Each receipt retains its own actual probes,
configuration, archive, source proof, and independent review.
The SDK includes the parsedate feature header before both configurations.
It links both exact archives and the host math library.
It does not merge configurations or copy compatibility implementations.
Darwin builds use native `struct stat` timestamp fields. Normal host builds
use the existing `HAVE_STRUCT_STAT_ST_ATIM` configure observation when needed.
The actual compiler must still accept the selected ABI headers and declarations.

The 36 cases use fixed timestamps and isolated filesystem fixtures.
They cover ISO and general parsedate input, numeric and legacy dates,
reference files and links, no-follow updates, separate access and modification
times, hard links, missing paths, continued processing, and syntax errors.
The SDK observes timestamps before it reads file data or directory contents.
The native plan contains one compilation, one link, the cases, and one
separate runtime observation: 39 commands in total.

Epoch second `-1` is valid. Each parsedate or mktime conversion clears stale
errno first. It rejects `-1` only when the conversion reports an error.
The numeric and legacy conversions also retain their calendar normalization
checks. Legacy input sets seconds to zero; its source regression checks the
error rule without claiming a native epoch-minus-one case for that syntax.

Run `test-source.py` with the selected SDK Python. These source and causal
mutation checks do not execute touch or prove the native C behavior.
Current-time accuracy, concurrent filesystem changes, locale and signal
behavior, C path analysis, memory checks, and normal host integration remain
separate checks. Finite bootstrap acceptance is not ordinary SDK qualification.
