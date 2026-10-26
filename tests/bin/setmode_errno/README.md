# setmode caller diagnostics

Origin: EmberBSD contributors (AI-assisted).

The canonical [chmod](../../../bin/chmod/chmod.c) and
[mkdir](../../../bin/mkdir/mkdir.c) callers clear errno immediately before setmode.
A NULL result without a new errno uses errx. A nonzero errno retains err.
This separates an unspecified parse failure from stale errno and real allocation errors.
The permission parser and all later filesystem operations remain unchanged.

Run the source/data regression with the selected SDK Python:

```sh
python3 -B tests/bin/setmode_errno/test-source.py
```

Use `--emit-fixture /absolute/new/diagnostic.c` to retain a standalone diagnostic source.
The emitter extracts both actual caller fragments. It does not copy their decision logic.
Typed diagnostic functions supply NULL without a new errno, fresh EINVAL, and success.
The ENOMEM case uses checked calloc size overflow, without host memory pressure.
It does not manually assign ENOMEM. The matrix covers stale EIO and stale ENOMEM too.
The fixture captures err/errx choice, error value, exit status and message for both callers.
It is a diagnostic input, not a production setmode replacement or a capability probe.

The Python check executes no C. Compilation and all twelve diagnostic cases require
a separately admitted SDK GCC16 route. Fresh chmod/mkdir native runs, applicable C analysis
and memory checks remain required. Earlier binary receipts are not changed by this fix.

Apple's [setmode source](https://github.com/apple-oss-distributions/Libc/blob/main/gen/FreeBSD/setmode.c)
can return NULL for an invalid operator without assigning errno.
The [canonical implementation](../../../lib/libc/gen/setmode.c) can report EINVAL or allocation errors.
The callers must retain either error contract.
