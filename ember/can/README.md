# CAN FD for application development

EmberBSD extends NetBSD's raw CAN stack with CAN FD records and a software
`canlo` interface. Applications can develop and test 64-byte exchanges without
a controller, alongside existing Classical CAN applications. This repository
owns the socket ABI, kernel implementation, `canconfig`, and regression tests.

The verified scope is kernel code running in a native NetBSD 11/AArch64 rump
process. A physical CAN FD driver, data-phase bit timing, electrical bus
behavior, error frames, ISO-TP and CAN XL are outside this implementation.
No new kernel was booted for this validation.

## Application contract

| Property | Classical CAN | CAN FD |
| --- | --- | --- |
| Record | `struct can_frame`, 16 bytes | `struct canfd_frame`, 72 bytes |
| Payload field | `can_dlc`, 0–8 bytes | `len`, 0–64 bytes |
| Payload offset | 8 | 8 |
| Socket opt-in | Existing default | `CAN_RAW_FD_FRAMES` at `SOL_CAN_RAW`, nonzero integer |
| Interface | MTU 16 or 72 | FD capability and enabled mode, MTU 72 |

The FD option defaults to off. An opted-in socket receives both formats;
a legacy socket never receives a 72-byte record. Receive into a buffer large
enough for `struct canfd_frame` and branch on the returned record length.
Existing identifier filters, wildcard interface reception, loopback and
`CAN_RAW_RECV_OWN_MSGS` also apply to FD traffic.

FD sends require exactly 72 bytes, even for an empty payload. The allowed
flags are `CANFD_BRS`, `CANFD_ESI` and `CANFD_FDF`. The stack sets `CANFD_FDF`
and clears reserved bytes before transmission and on reception. Unknown
flags, FD RTR/error frames, and a payload length above 64 are rejected.
The byte count is not a wire DLC. Software loopback accepts every length
from 0 through 64; a future physical driver must handle controller DLC rules.
BRS and ESI are carried as metadata without simulating physical signaling.

Classical CAN's structure, socket address and existing option numbers remain
unchanged. Valid short Classical records remain supported: an 8-byte header
and at least the declared payload, up to 16 bytes. An RTR header may request
data without carrying it. Incomplete headers/payloads and DLC above 8 are
rejected. New applications should use the full structure.

See [can(4)](../../share/man/man4/can.4),
[canloop(4)](../../share/man/man4/canloop.4), and
[canconfig(8)](../../sbin/canconfig/canconfig.8) for interfaces and commands.

## Configure a system built with this extension

These commands require a matching EmberBSD kernel with CAN and `canloop`,
the updated headers, and the updated `canconfig`. An existing NetBSD 11
installation does not acquire the extension just by building the tests.

```sh
ifconfig canlo0 create
canconfig canlo0 down
canconfig canlo0 fd
canconfig canlo0 up
canconfig canlo0
```

Expect MTU 72 and FD in the mode and capability flags. The interface has no
physical clock; `canconfig` reports this and rejects timing commands.
Each application must additionally enable the FD socket option.

To restore Classical mode, use separate `down`, `-fd`, and `up` invocations.
`canconfig` applies interface up/down flags at the end of each invocation;
`down fd` in one call therefore cannot reconfigure an active interface.
Direct `ifconfig` MTU changes accept only 16 or 72 and keep FD mode consistent.
Changes of mode or MTU require a down interface. Existing physical drivers
do not advertise FD and cannot transmit oversized FD records.

## Reproduce the software checks

Use a clean checkout of the desired EmberBSD commit on NetBSD 11 with the
matching userland, base compiler, rump libraries, ATF and `/usr/share/mk`.
The complete source checkout supplies kernel headers and rump build files;
do not combine these with an unrelated source tree.

```sh
sh ember/tools/canfd-rump-check.sh /absolute/EmberBSD /new/absolute/canfd-check
sh ember/tools/canfd-ownership-check.sh /absolute/EmberBSD
```

The native runner builds a private rump CAN library, the real socket tests,
and `canconfig`. It verifies library selection with `ldd` before running ATF.
It records source, dependency, binary and runtime-library hashes plus build
and test logs. Nothing is installed globally and the running kernel is not
changed. Keep the evidence you need, then remove the chosen work directory.

The CAN FD executable and its optional debug companion are listed in the
system release sets. Check their packaging separately:

```sh
sh ember/tools/canfd-sets-contract.sh /absolute/EmberBSD /new/absolute/set-check
```

This runner uses the checkout's real make install metadata and `makeflist`
in 11 configurations, including ATF/rump/debug switches and compat builds.
It also verifies that the compat build traversal excludes CAN tests.
On a non-NetBSD host, set `SETLIST_MAKE` to an absolute path to a compatible
`bmake`; the default is `/usr/bin/make`. A complete source checkout is required.

For AArch64 with and without debug files, the runner checks a minimal
`DESTDIR` using the real `checkflist -m`. Removing the CAN FD set entries
reproduces the extra-file failure; both configurations pass with the entries.
The `-m` option permits unrelated missing files, so this is a scoped
packaging regression, not a complete release check. A full release check
requires the complete installed `DESTDIR`. No socket or hardware test runs here.

Validation on 2026-10-07 used NetBSD 11.0/AArch64 and GCC 12.5.0:

- 15 existing CAN/filter cases pass both before and after the extension.
- 11 new [rump cases](../../tests/net/can/t_canfd.c) exercise the actual raw
  socket and `canlo` path: all 65 payload lengths and flag combinations,
  malformed records, opt-in and mixed reception, filters, loopback, wildcard
  addresses, mode/MTU transitions, and repeated close/destroy cycles.
- The new option test fails with `ENOPROTOOPT` against the original library.
- 20 [CLI fixtures](../../sbin/canconfig/tests/check.sh) execute production
  command handling against controlled ioctl responses. Real kernel mode and
  MTU ioctls are exercised separately by the rump tests.
- A [fault contract](../tools/canfd-ownership-check.sh) extracts production
  packet-handling functions and forces allocation, pullup and queue failures.
  It checks exactly-once packet disposal and sender-reference release.
  This is a controlled fault fixture, not simulated bus interoperability.

Rump validation compiles and executes the changed CAN kernel code with
diagnostic assertions. It does not replace a complete kernel build/boot,
physical-controller tests, sustained load or real-time measurements.
Existing UDS/ISO-TP probes in EmberBSD-Ports retain their separately documented
transport limits; they are not automatically CAN FD transports.

## Provenance

The implementation extends `sys/netcan` and `canloop` from NetBSD, retaining
their source identifiers, copyrights and licenses. EmberBSD's changes and
tests were developed with AI assistance; they have not been submitted to or
accepted by NetBSD. The FD record layout and flag values follow the published
[SocketCAN interface](https://docs.kernel.org/networking/can.html) and
[Linux UAPI definitions](https://github.com/torvalds/linux/blob/master/include/uapi/linux/can.h).
No Linux kernel implementation is copied into this stack.
