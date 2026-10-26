# Native host chown and chgrp

This entry builds the complete canonical `sbin/chown` through `Makefile.host`.
It uses the ordinary configured `tools/compat` library, including user lookup.
It installs `chgrp` as an alias of the same executable.
Host invocation names can include the native tool prefix and `.exe` suffix.
Target invocation handling and the complete canonical CLI remain unchanged.

The program uses native ownership syscalls and FTS traversal.
It does not emulate permission changes or use an external ownership utility.
`--reference`, numeric and named identities, recursive link policies, and
normal error handling remain part of the canonical source.

Run `test-native.py` with selected absolute `--chown` and `--chgrp` paths,
and a fresh absolute `--work` directory. The checks use current-user files.
They verify permitted group changes, reference files, recursion, symlinks,
unchanged file data, verbose output, and invalid operands.
A second supplementary group makes the group-change assertions nontrivial.
The result states whether that group was available.
Privileged ownership changes and other hosts require separate checks.

`test-recipe.py` uses selected bmake, shell, and ln paths to check installation.
It keeps the owner recipes and omits platform/compiler rules in its test configuration.
It proves alias installation inside HOST_BINDIR and rejects leaked target filesystem aliases.
It does not compile the host program or prove the complete tools build.
