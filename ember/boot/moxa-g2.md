# Experimental UPort 1450-G2 USB parent

The `umoxag` driver binds only MOXA `110a:1452` and exposes four `ucom`
children using the existing opt-in external transport. It is an independently
written BSD-2-Clause proposal. Hardware acceptance remains pending; the legacy
1450, isolated 1450I-G2 and other G2 products do not match this profile.

## Protocol provenance

Wire facts were traced in MOXA's official Linux 6.x v6.2 reference package,
not copied as implementation. The package SHA512 is
`b7a1fd7c2e1a23259e5696611fbe0cdef50912e85200c70eba6cf80df86b90985ed774e69b8e7a24c1654985143f1c0f366c54ced164725374a1d0ad33f5a1be`.
The [official software catalog](https://www.moxa.com/en/support/product-support/software-and-documentation?psid=131128)
listed this version/hash when checked on 13 October 2026.

Expected interface shape: one vendor interface, first bulk-IN descriptor for
framed data, second bulk-IN for eight-byte events, one bulk-OUT. Actual endpoint
addresses are read, never guessed. Data and OUT packet sizes must agree; event
packet size must not exceed OUT. Unknown shapes fail before any TTY is attached.
This is a conservative source-derived profile, not a measured device descriptor.

## Firmware and attachment

Firmware already running in SRAM with a valid version needs no file or upload.
For a cold ROM/XIP device, attach after root mount requires an appropriately
licensed external `/libdata/firmware/moxa/moxa-1450-g2.fw` (subject to the normal
firmload search path). No binary is bundled or extracted by this patch. The
vendor header mentions `MOXA-SOFTWARE-NOTICE`, absent from the inspected archive;
that reference does not authorize redistribution of its bytes.

The upload uses RAM START, bounded 64-byte DATA requests and STOP. It issues no
flash erase/write commands. Complete data transfer is recorded separately from
STOP acknowledgement. Fresh version and SRAM-remap queries must confirm the
uploaded image before child attachment. If STOP/probe disconnects, the empty
parent waits for a fresh USB attachment; an I/O error alone never establishes
readiness. Missing/invalid images, short data or failed queries create no TTY.
Cold pre-root attachment reports the need to reconnect after root mount.

## TTY and fault contract

- Defaults: 9600 8N1 RS232, DTR/RTS logically asserted. Supported software
  settings are baud 50..921600, 5..8 bits, none/odd/even parity, one/two stop bits.
  These bounds and modes still need physical acceptance. Initial MSR/DCD is
  published at start, including blocking dial-in open without a new event. B0, CDTRCTS and MDMBUF
  are rejected before any control operation; TTY owns XON/XOFF processing.
- A private per-device workqueue owns synchronous transfers on all three pipes.
  Submission copies at most `OUT packet size - 4` bytes and schedules work;
  soft-context RX-flow updates only flags. Neither path blocks or calls children.
  Polling is nominally 20ms, read timeouts 10ms, TX/credit deadlines 1000ms.
  Latency, throughput and host-controller behavior are unmeasured.
  Explicit synchronous transfers avoid the bulk helper's hidden CLEAR_FEATURE
  on empty polls; STALL faults the stream rather than silently recovering.
- A complete USB write retires a payload once. Short/failed shared writes and
  malformed whole RX/event batches fault all open ports, with no uncertain
  replay. Credit timeout, local RX overflow or per-port control failure faults
  only that owner and preserves peer jobs. The first fault remains queryable.
- The worker validates a whole received batch before publishing any prefix.
  Modem events use detail byte2 high bits; line events use detail byte0 status.
  Child callbacks run outside the parent's state mutex.
- Process controls gate and wait for the worker. Close first invalidates the
  owner's queue epoch; USB resources and child pointers remain alive through
  physical USB completion. Detach aborts pipes, waits for work, then releases children.
- Reopen purges the owner and drains **both** shared RX/event streams while its
  queue is closed. At most eight paired reads forward valid peer traffic and
  discard old-owner data/credits. Continuous traffic returns EBUSY. Untagged
  wire credit cannot be assigned a software epoch; this drain assumption must
  be tested on real hardware, including delayed credits after the empty reads.

The installed `<dev/usb/umoxa_g2io.h>` provides `UMOXA_G2_GET_FAULT` (int) and
`UMOXA_G2_SET_MODE` (int: 0 RS232, 1 RS485 two-wire, 2 RS422, 3 RS485 four-wire).
Select mode immediately after opening an idle TTY, before transmitting bytes.
Mode change after queued/sent data returns EBUSY, including bytes still in
the TTY queue and previous transmissions after credit reset; close and reopen before trying
again. A mode persists across reopen. RTS/CTS is retained in RS232; selecting a
non-RS232 mode while RTS/CTS remains enabled returns EINVAL. Initial logical DTR/RTS setup is cached
until start, allowing non-RS232 opens; explicit modem controls while open in
non-RS232 mode return EOPNOTSUPP. RTS-off under RTS/CTS returns EBUSY.
Bias, termination, automatic direction timing and electrical wiring are not
configured or validated by these ioctls.

## Reproducible software checks

From a clean source checkout, use new absolute output directories outside it:

```sh
sh ember/tools/umoxa-g2-protocol-test.sh /tmp/g2-protocol
sh ember/tools/umoxa-g2-driver-test.sh /tmp/g2-parent
sh ember/tools/umoxa-g2-txq-test.sh /tmp/g2-queue
```

The parent runner compiles the actual production C with test-only USB/kernel
boundaries, synthetic firmware, plain execution and ASan/UBSan. Its tests cover
four-child attach, RAM readiness, short/failing requests, resource cleanup,
framed multiport exchange, local/shared faults, credit deadline, copied data,
flow deferral, cached settings, modem/mode controls, reopen drain and queued
detach. It does **not** prove native workqueue concurrency, real USB execution,
firmware licensing or electrical support. The separate native fixture in `tests/dev/usb/umoxa_g2` compiles the actual
parent, worker, controls, ucom and TTY against target headers and libraries;
only USB/firmload and the attachment root are doubled. Eight groups exercise
initial DCD, four concurrent ports/credits, mode reopening, TTY bytes queued
before dispatch, retained RTS/CTS, close waiting for an active worker, fault
query through ioctl admission and detach aborting an active worker.

```sh
sh ember/tools/umoxa-g2-rump-test.sh /absolute/source /tmp/g2-native
# Or cross-build, then execute test on the matching NetBSD 11 runtime:
sh ember/tools/umoxa-g2-rump-cross-build.sh /absolute/source /absolute/build /absolute/target-runtime /tmp/g2-cross
RUMP_NCPU=4 /tmp/g2-cross/test
```

The native fixture uses real kernel scheduling/locking and TTY operations in
rump; it does not prove the USB controller or DMA boundary. Existing native
real-ucom/TTY regressions protect the shared foundation separately.

The ucom private kernel methods add an optional **read-only** fault-ioctl
callback. Only GET_FAULT is admitted by this parent after fault; mutating and
normal TTY operations retain their original error. For opt-in transports,
private parent ioctls are tried before TTY compatibility hooks; legacy USB
parents retain their original routing. A new non-blocking idle query includes
TTY output not yet submitted to the parent. Rebuild kernel and affected
modules together; this is not a binary ABI guarantee for old modules.

## Physical handoff and recovery

Connect a 1450-G2 when software review/build is complete. Record complete cold
and warm descriptors, query version/remap, obtain an appropriately licensed
firmware asset if needed, and exercise four ports, cross-port isolation, flow,
mode changes, unplug/close/detach during I/O and delayed credit after reopen.
Use RS232 loopback or an independent RS232 peer; a bare TTL adapter is not an
RS232 electrical peer. Keep the previous kernel and module set for rollback.
Power-cycling clears RAM firmware; unknown device shapes remain unclaimed.

See [the MOXA foundation and 1150 trial](moxa-serial.md). The proposal depends
on that unmerged foundation; source/CI and hardware acceptance are distinct.
