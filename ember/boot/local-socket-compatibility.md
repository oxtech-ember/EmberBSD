# Local socket compatibility for Wayland

EmberBSD fixes two inherited local-socket behaviors required by the current
Wayland libraries. The kernel changes belong to this OS repository; the
[Wayland 1.26 port and installed-package tests](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/wayland.md)
belong to Ports. These are IPC fixes, not GPU acceleration.

## Per-call nonblocking send

When a socket's send buffer was full, `sosend()` recognized `MSG_NBIO` and
descriptor-level `O_NONBLOCK`, but ignored `MSG_DONTWAIT`. A caller using
the latter could block indefinitely. The buffer-space check now honors
both per-call flags without changing descriptor flags.

The [regression](../tools/socket-nonblock-contract.c) fills a local stream
socket and exercises `send`, `sendto` and `sendmsg`. Each call tests both
per-call flags, descriptor nonblocking mode, and ordinary blocking mode
with a delayed reader. It checks preserved descriptor flags and successful
transmission after draining the buffer. An alarm detects accidental blocking.

## Socketpair peer identity

`LOCAL_PEEREID` previously failed for connected local stream and sequenced
packet socketpairs because `unp_connect2()` did not save the connection
identity. It now records the creating process's PID and effective UID/GID
before publishing the connected state, using the existing `unp_connid()`
helper under the socket lock. Named connections retain their existing behavior.

The [identity regression](../tools/socket-peercred-contract.c) checks both
endpoints, inherited endpoints after `fork`, and changed child credentials.
It also checks the identity of a new pair created by that child, ordinary
named connections, and rejection for unconnected and datagram sockets.
The changed-credential cases need root; they use a disposable child and do
not create a user account. Non-root runs report those cases as skipped.

## Reproduce and interpret

On EmberBSD with a C compiler:

```sh
sh ember/tools/socket-nonblock-contract.sh
sh ember/tools/socket-peercred-contract.sh
```

For cross compilation, compile the same C files with the target compiler
and `-std=c11 -D_NETBSD_SOURCE -Wall -Wextra -Werror`, then execute the
binaries on the target. The shell entry points require a NetBSD target.

On 2026-10-08 the same GCC16.2-built binaries were run before and after
installing a complete `EMBER64` kernel on Orange Pi Zero 3W (Allwinner
A733, vendor boot0/U-Boot). Before the update, 3 of 12 send cases and
16 of 25 identity cases failed. The updated kernel passed all 37 cases,
with no skips. Matching kernel, DTB and modules were installed and checked
by SHA256. All 26 enabled upstream Wayland 1.26.0 test invocations then
passed against the installed Ports `wayland-1.26.0nb1` package.

These checks cover local stream/seqpacket contracts and the stated negative
controls. They do not establish TCP/UDP behavior, concurrent writers,
partial-send semantics, a compositor session, or a sustained stress run.
Origin: EmberBSD, AI-assisted fixes and regressions; not submitted upstream.
