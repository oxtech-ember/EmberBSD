# USL VT request notification and cookie lifetime

This test covers one kernel prerequisite for a native VT controller. It does
not enable a ConsoleKit device backend or establish device entitlement.

`wskbd.c` calls `wsdisplay_switch` with `waitok=0` for a console switch key.
The old detach path did not set its pending flag or notify the controller in
that case. Its timeout then returned without completing the switch. The old
attach path could call `psignal` from the same interrupt continuation.

The sync code now records the pending request in interrupt context. A
non-MPSAFE callout performs the notification after one tick. The normal
five-second reply timeout starts when the notification commits. This is a
scheduling interval, not a wall-clock deadline guarantee. A process-context
request retains immediate notification and the existing dead-owner result.
Signal zero remains valid and starts the reply timeout without a signal.
The unused `frsig` field is unchanged.

The signal commit occurs after `proc_lock`, the owner's `p_lock`, and the
last generation/state check. No adaptive lock is acquired while `s_lock`
is held. The code then calls `kpsignal2` with the same empty signal info as
`psignal`. Cancellation before commit prevents the signal. Cancellation
after commit does not retract that signal. In particular, this does not
solve routing between successive sessions in the same controller process.
The regular kernel signal implementation can take further locks after
commit, for example through `child_psignal` for `SIGCONT`.

The initializer rejects `acqsig` or `relsig` outside `[0, NSIG)` before
allocation or publication. `vt_mode` carries signed shorts. Without this
check, a nonzero invalid value reaches the `kpsignal2` assertions and its
`sigprop[signo]` access in `kern_sig.c`.

Destroy marks the cookie DYING, invalidates the generation, clears the
callback, and unpublishes it before invoking that callback. Each operation
pins the cookie across an adaptive lock wait or a reentrant callback.
The retirement callout waits for those pins, halts the other callouts, and
destroys their storage before freeing the cookie. Init failure destroys
unpublished callouts immediately. A stale timeout checks the current
notification state and deadline before it can complete a newer request.

This uses the existing wsdisplay BKL contract. An entry point pins its
cookie before its first BKL-dropping operation. The character device is
not `D_MPSAFE`. On x86 the non-MPSAFE interrupt wrapper holds the BKL as
well. A keyboard interrupt on the BKL owner CPU is serialized by the
`IPL_TTY` spin lock. This change does not make wsdisplay MPSAFE.

`kern_timeout.c` supplies the remaining lifetime contract:

- `callout_init` without `CALLOUT_MPSAFE` binds these callouts to CPU 0.
- `callout_softclock` runs them on the same softclock LWP with the BKL.
- Another such callback cannot be suspended before its first pin while
  the retirement callback runs on that LWP.
- `callout_stop` does not drain an active invocation. Retirement uses
  `callout_halt` without `s_lock` or a process lock held.
- The callout implementation permits a callback to free its own storage.

## Run the host boundary tests

Run from the source root. Use an absolute path for `USL_WORK` and `ROOT`.
Use the current host GNU compiler for the first run.

```sh
ROOT="$PWD"
HOST_CC=gcc USL_WORK=/absolute/private-receipt/gnu \
  ruby ember/tests/usl-sync.rb "$ROOT"
HOST_CC=clang \
  HOST_CFLAGS='-O1 -g -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer' \
  USL_WORK=/absolute/private-receipt/sanitizer \
  ruby ember/tests/usl-sync.rb "$ROOT"
```

The runner extracts the real struct, syncops and all sync functions from
`wsdisplay_compat_usl.c`. It retains their original license. It also reads
the target `NSIG` definition. Hooks replace kernel allocation, process
lookup, locks, publication and callout dispatch. An adaptive-lock hook
models another operation running while the BKL is dropped. These are
deterministic host function tests, not a running kernel or an SMP stress
test. `-Wno-unused-parameter` matches the kernel's existing compile policy.

Set `USL_SOURCE` to an original source snapshot to run the first regression
against it. Pass `"$ROOT" detach` to stop after that regression. The original
source must compile and fail at the missing deferred notification assertion.
The runner preserves `fixture.c`, the input hash, and compile/run logs.

Four causal negative controls are available through `USL_MUTATION`:
`no-generation-check`, `no-retirement-pin-check`,
`no-timeout-deadline-check`, and `no-irq-pending-flag`. Each must compile and
then fail a runtime assertion. Do not count a compile error as rejection.

Run Cppcheck and GNU `-fanalyzer -c` on both extracted fixtures. Also compare
the complete target TU with its real include/define context. Keep all
diagnostics. The unnamed KNF prototypes and `KSI_INIT_EMPTY` macro can
produce Cppcheck style warnings; do not suppress source diagnostics.

## Compile the target TU

Use a completed real kernel compile log and its matching configured kernel
directory. This command changes only the source argument and the output
object. It keeps `-Werror`, the target compiler, and every recorded flag.
It does not run make or change the configured build directory.

```sh
ruby ember/tests/usl-sync-target.rb \
  /absolute/kernel-build.log /absolute/configured/GENERIC \
  "$PWD/sys/dev/wscons/wsdisplay_compat_usl.c" \
  /absolute/private-receipt/target
```

The receipt stores the original and substituted argv, source hash and
compiler log. Compare the original source and the candidate with the same
command. For target analysis, add `-fanalyzer` to a copy of the recorded
argv and use another private output path. For Cppcheck, preprocess with a
copy of that argv (`-c` to `-E`, private `.i` output), then check that `.i`.
This retains the actual target headers and defines.

The local source gate used GNU 16.2.0 with the recorded amd64 GENERIC
command from source epoch `15f27943f319`. The changed TU used the current
worktree. This is a target compile check against that configured header
epoch, not a claim that a new complete kernel was built or booted.

## Gates that remain closed

The host tests and target compile do not establish real hard-keyboard IRQ
execution, timeout scheduling, secure VT handoff, native GUI operation, or
physical-device acceptance. A prepared target test must run with the new
kernel in a separate VM acceptance step. Development tools stay on the
cross-build host.

The [lease ownership regression](usl-owner.md) checks owner/admin ACK and
reset, process-death tombstones, and exit-hook retirement separately.
Initial registration entitlement, exact-screen FD/session ancestry,
controller signal routing, and coordinated DRM/wscons revoke/reopen and
libinput/KWin lifecycle remain separate gates.
The inherited dead-owner detach result remains success. No native device
backend can be enabled on the strength of this patch alone.
