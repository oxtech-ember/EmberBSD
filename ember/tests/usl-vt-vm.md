# Prepared wscons VT ownership acceptance runner

`usl-vt-vm.c` is a libc-only target C program. Build it on the cross host.
Copy only the executable and its non-secret receipt into an explicitly
provisioned, disposable QEMU VM. Do not install target development tools.
The current gate is source, host-function tests and a static target ELF.
No VM, hardware, keyboard IRQ or desktop acceptance is implied.

## Fixture contract

Only the fixture owner can authorize execution. The required environment is:

- A private, quiescent VM with the tested kernel and a recovery snapshot.
- Independent serial or SSH control. Do not run from either selected VT.
- Two existing idle text screens on wsdisplay unit 0. Disable their gettys
  and greeters before the test. Provision them before running the program.
- The return screen is initially active. Both screens initially use
  `VT_AUTO` and `WSDISPLAYIO_MODE_EMUL`.
- An existing positive non-root UID. Its primary group must exist. Use
  local account lookup in the fixture. The runner does not create users.
- A root-owned regular fixture record with mode `0600`, one hard link,
  no symlink and the first line `USL_VT_VM_FIXTURE_V1`.
  Keep the VM identity, kernel SHA256, source commit and provisioning
  receipt with that record. This record must contain no credentials.

The explicit `--vm-fixture` option is an operator guard. It does not prove
that the machine is a VM. Root ownership of the record does not prove the
loaded kernel hash. The provisioning and boot receipts supply that evidence.

`VT_OPENQRY` does not reserve a VT. The runner does not use it as a lock.
`TIOCEXCL` does not exclude a privileged opener and is not used as proof.
Before opening either screen, the runner queries `VT_GETSTATE` through
`/dev/ttyEcfg`. It requires both selected open bits to be clear and the
return VT to be active. These are observations, not an atomic reservation.
An open bit is Boolean. It cannot count other opens after our own open.
Fixture isolation remains a required condition throughout the test.

The runner accepts canonical `/dev/ttyE0` through `/dev/ttyE14` only.
This restriction follows the representable `vt_stat.v_state` mask: bit 0
is unused and the remaining 15 bits identify VT1 through VT15. It is not
a guess about the number of configured kernel screens. The runner obtains
the wsdisplay character major from `kern.drivers`. It checks each FD's
actual type and device number against the current unit-0 minor ABI.

## Build on the host

Use a new receipt path and a frozen, genuine EmberBSD sysroot. The OS
provenance argument is retained and hashed; the helper does not interpret
it as proof that a complete distribution passed.

```sh
ruby ember/tests/usl-vt-vm-build.rb "$PWD" \
  /absolute/application-toolchain/bin/x86_64--netbsd-gcc \
  /absolute/frozen-os-dest /absolute/gcc-runtime-stage/usr/pkg/gcc16 \
  /absolute/os-provenance.json /absolute/new-runner-receipt
```

The helper requires GNU 16.2.0 and `x86_64--netbsd`. It uses the selected
OS headers with `-nostdinc`, OS `crt0/crti/crtn` and libc, and the audited
GCC16 static CRT/libgcc. It verifies the link map and the NetBSD ABI note.
The output must be ELF64 amd64 with no interpreter or dynamic dependencies.
It checks all used header and selected library/tool hashes before and after
the link. It never runs the target executable. No host prefix is an RPATH.

Rebuild from the final frozen sysroot before execution. A program linked
from a partially built destination tree is a compile check only. Archive
its input hashes and diagnostics, but do not treat it as accepted media.

## Execute only after fixture authorization

This command is a target command. Do not run it on the cross host.
The example screen numbers and UID must match the actual provisioned VM.

```sh
./usl-vt-vm --vm-fixture --fixture-record /root/vt-fixture.record \
  --screen /dev/ttyE4 --return-screen /dev/ttyE0 --uid 1000 \
  > /root/vt-normal.log 2>&1
```

The root supervisor opens the screens once. It sends the same open-file
description through `SCM_RIGHTS` to non-root workers. The owner worker also
forks a same-UID child with the exact inherited FD. The test covers:

- Owner registration and the existing duplicate-`VT_PROCESS` `EBUSY` rule.
- Readable `VT_GETMODE` for another FD holder.
- Foreign same-UID peer and fork-child rejection for acquire/release ACK
  and release denial, without consuming the owner's pending request.
- Foreign automatic-mode reset rejection, followed by successful owner
  ACK/deny/reset and administrative ACK/reset.
- Real ordinary-ioctl acquire and release transitions between the two VTs.
- Owner exit while another process retains the transferred FD, deferred
  removal, reaping and a fresh lease. An old FD holder cannot control it.

The owner blocks its two notification signals and uses `sigtimedwait`.
No ioctl runs in an asynchronous signal handler. Commands, signal waits,
active-VT observations and child reaping have finite userspace deadlines.
Cleanup tries both child PIDs independently, then restores automatic mode,
the original active VT and text mode. It reports any restoration failure.
It sends signals only to recorded positive child PIDs. There is no device
creation, raw-keyboard change, permission change, revoke or VT text output.

`VT_GETMODE` exposes no owner PID or incarnation. Matching mode/signals
cannot identify a lease against a root actor that replaces it. Only fixture
isolation authorizes cleanup of that expected lease. An unexpected mode,
active third VT, reset error or failed restoration returns failure. Do not
run this program where another user or greeter can claim the screen.

SIGKILL, a VM power loss or a kernel hang can prevent cleanup. A userspace
deadline cannot bound a hung kernel syscall. Retain the independent control
channel and VM-level watchdog. Do not report failure recovery as proven
until the real VM exercises it. The host suite models the restoration
ioctl boundaries; it does not execute this whole target scenario.

## Host checks and causal controls

```sh
HOST_CC=gcc USL_VM_WORK=/absolute/new-host-receipt \
  ruby ember/tests/usl-vt-vm-test.rb "$PWD"
HOST_CC=clang \
  HOST_CFLAGS='-O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer' \
  USL_VM_WORK=/absolute/new-sanitizer-receipt \
  ruby ember/tests/usl-vt-vm-test.rb "$PWD"
```

These checks execute the real parser, metadata guard, descriptor transfer,
deadline and child-reaping functions. They extract the real restoration
function with controlled ioctl boundaries. They cover successful cleanup,
already-restored state, unknown modes, an occupied return lease, reset
failure, an unexpected third VT, activation failure, completion failure and
wrong text mode. The VT constants come from the actual source headers.
The host does not open a wscons device.

`USL_VM_MUTATION` accepts `no-vm-guard`, `loose-fixture-mode`,
`no-wait-deadline`, `no-child-reap`, `ignore-reset-failure` and
`unknown-vt-activation`. Each must compile, then fail a runtime assertion.
Keep compiler, Cppcheck, GCC analyzer and ASan/UBSan receipts. Compare full
target diagnostics using the same sysroot and flags as the ELF build.

## Separate acceptance gates

Ordinary `VT_ACTIVATE` calls `wsdisplay_switch(..., 1)` in
`wsdisplay_compat_usl.c`. This runner qualifies only that path when run.
The keyboard command in `wskbd.c` calls `wsdisplay_switch(..., 0)`.
A later keyboard phase needs explicit READY markers and bounded waits,
external authorized QMP key events, and their host receipt. A user-space
signal or another ordinary ioctl cannot prove keyboard hard-IRQ execution.

The runtime exit test does not force PID or `struct proc` address reuse.
That causal case remains in [the source-function tests](usl-owner.md).
This runner does not prove first-claim TTY/session ancestry, device-FD
revocation, same-PID multi-session signal routing, SMP stress or native
KWin/libinput lifecycle. The ConsoleKit device backend remains `none`.
