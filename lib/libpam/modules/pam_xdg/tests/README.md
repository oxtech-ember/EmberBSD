<!-- SPDX-License-Identifier: BSD-2-Clause -->
# PAM XDG target checks

`lifecycle.c` exercises the actual module implementation and NetBSD filesystem
and `flock` calls with small PAM doubles. Build with the selected SDK cross
compiler and target sysroot, defining `XDG_CONTROL` and `XDG_RUNTIME` to two
unique absolute test-only paths. Run as root on the target only after verifying
both paths and the runtime path with suffix `-outside` do not exist. It tests
session lifetime, concurrent processes, stale lock recovery, ownership, mode,
symlinks and failure handling. Remove only artifacts created by that run.

`openpam-test.c` exercises the real target OpenPAM loader, environment and
session lifecycle. Build it against target libpam. The module must be built
with test-only runtime/control paths matching the source's expectations and
placed in a root-owned protected directory. Create the uniquely named test PAM
service only if it does not exist, with a `session required` entry naming that
module; remove only that service and the test artifacts after the run. Never
modify existing login services for this test.

For example, with `CC` set to the selected SDK cross Clang and `SYSROOT` to
its matching target sysroot, compile the filesystem fixture from this directory:

```sh
"$CC" --target=x86_64--netbsd --sysroot="$SYSROOT" -Wall -Wextra -Werror \
  -DXDG_CONTROL='"/tmp/ember-pam-xdg-check-control"' \
  -DXDG_RUNTIME='"/tmp/ember-pam-xdg-check-runtime"' lifecycle.c -o lifecycle
```

On the test target, check that `/tmp/ember-pam-xdg-check-control`,
`/tmp/ember-pam-xdg-check-runtime` and the same runtime path suffixed
`-outside` do not exist, then run `./lifecycle` as root. Remove only the test
control directory and empty parent created by this invocation.

For the OpenPAM check, build the module using `-fPIC -shared -lpam` and the
same target compiler with `XDG_CONTROL` set to
`/tmp/ember-pam-xdg-pam-control` and `XDG_RUNTIME` set to
`/tmp/ember-pam-xdg-pam-runtime`. Build `openpam-test.c` with `-lpam`. On the
target, after checking these paths and `/etc/pam.d/ember-pam-xdg-test` are
absent, place the test module in a protected root-owned directory and create
that service containing `session required /absolute/path/to/test/module`.
Run `./openpam-test` as root and remove only its temporary service, module and
created test directories afterwards.
