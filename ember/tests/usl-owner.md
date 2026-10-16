# USL VT lease ownership and process exit

This is a source and function-test gate for the native wscons VT ABI.
It does not enable the ConsoleKit native device backend.

The original `VT_RELDISP` and automatic `VT_SETMODE` paths accepted any
process with a file descriptor for the exact screen. A descriptor holder
could acknowledge another process's release request or remove its mode.
The stored PID and `struct proc` address also lacked a death marker.
Both values can be reused after the owner exits and its process is reaped.

The bounded policy is:

| Operation | Allowed caller |
| --- | --- |
| Release ACK/deny or acquire ACK for a lease | Live owner process or `KAUTH_GENERIC_ISSUSER` authorization |
| Remove a lease through automatic mode | Live owner process or the same administrative authorization |
| Read `VT_GETMODE` | Existing descriptor holder, as before |
| Register `VT_PROCESS` on an occupied live screen | `EBUSY`, including the owner and administrator, as before |
| Register `VT_PROCESS` on an unclaimed screen | Existing FD-based ABI, as before |

The owner check follows process identity. It does not compare saved UID
values. Another process with the same credentials, including a fork child
that inherited the FD, does not inherit this lease. The current owner can
still acknowledge after a credential change. Administrative authority uses
kauth, not a hard-coded UID check. A failed authorization returns `EPERM`
without changing the pending request. A caller must explicitly reset the
mode before it can replace a live owner.

The ioctl pins its cookie before an authorization operation can drop the
BKL. It checks DYING again after administrative authorization. A reset
that raced with teardown cannot remove a replacement cookie.

## Process lifetime

Each published cookie registers a standard process exit hook. Hook
allocation uses the existing `M_NOWAIT` API. Failure returns `ENOMEM` and
destroys unpublished resources without changing the screen's lease.

`exit1` calls `doexithooks` before `proc_free_pid` and `proc_free_mem` can
reuse the owner identity. The hook sets sticky `SF_OWNERDEAD`, invalidates
the notification generation and schedules the existing notify callout.
The hook takes only the cookie spin lock. It does not invoke callbacks,
take process locks, allocate memory, or depend on the BKL.

The tombstone rejects the old identity even if a later process has the
same PID and the same `struct proc` address. The deferred callout removes
an idle dead owner's cookie too. A process-lock wait rechecks that marker
before signal commit. A new lease can be published while the dead cookie
retires; old cleanup must not remove the new lease.

An administrator can still acknowledge a pending dead-owner request.
That ACK must retain the queued death cleanup. It must not cancel the
notify callout and leave the dead cookie published indefinitely.

Retirement calls `exithook_disestablish` without the cookie spin lock or
process locks held. In `kern_hook.c`, dispatch holds the hook-list reader
lock and removal takes its writer lock. Removal therefore drains callbacks
before their cookie argument is freed. A DYING cookie cannot queue new
work from its exit hook. The existing operation pins and callout drain
remain necessary as described in [USL sync tests](usl-sync.md).

Holding `p_reflock` for the lease lifetime would block process exit before
its FDs close. A start timestamp is not a kernel-unique incarnation token.
Neither mechanism is used here.

## Run the regression

Use the source root and a new private receipt directory:

```sh
HOST_CC=gcc USL_WORK=/absolute/receipt/owner \
  ruby ember/tests/usl-sync.rb "$PWD" owner
HOST_CC=clang \
  HOST_CFLAGS='-O1 -g -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer' \
  USL_WORK=/absolute/receipt/owner-sanitizer \
  ruby ember/tests/usl-sync.rb "$PWD" owner
```

The runner executes the real USL sync functions and full `ioctl2` dispatch.
It also extracts the real hook registration, removal and dispatch functions
from `kern_hook.c`. It reads the actual native VT command definitions.
Process lookup, kauth results, locks, allocation, publication and callout
scheduling remain controlled host boundaries. The hook lock model checks
ordering and reader/writer exclusion; it is not a parallel SMP kernel test.
Unrelated bell and graphics ioctl conversion boundaries are stand-ins.
Both source licenses and input hashes are retained.

The original source must compile and fail at the foreign-ACK assertion.
Set `USL_SOURCE` to that snapshot to reproduce the RED result. The owner
suite has 25 cases. The existing 40 sync cases must still pass without
the `owner` argument. Run Cppcheck and GCC `-fanalyzer` on both fixtures
and the complete target TU, as described in the sync test document.
Use ASan/UBSan on macOS; this is not a Valgrind pass.

Owner-specific `USL_MUTATION` controls remove the owner check, death
tombstone, hook drain, paired ioctl pin, post-authorization DYING check,
post-process-lock death check, or preservation of cleanup after an admin
ACK. Each must compile and fail at runtime.
The pin control removes both hold and release, so it tests the lifetime
window rather than a deliberately unbalanced reference count.

## Acceptance boundary

The first claim on an unclaimed screen still follows the old open-FD ABI.
This patch does not prove controlling-TTY/session ancestry, authenticated
login ownership, device entitlement, coordinated DRM/wscons revocation,
or KWin/libinput suspend and resume. It preserves the inherited successful
detach result when the owner dies. That result is not proof of device-FD
revocation. Signal routing between sessions in one controller process is
also a separate gate.

Host function tests and an isolated target compile cannot qualify a new
kernel's real exit/IRQ/SMP behavior. Build target test executables on the
cross host and run prepared outputs in the VM acceptance step. The native
device backend remains `none` until all coordinated gates pass.
