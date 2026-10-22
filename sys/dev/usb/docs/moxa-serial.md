<!-- Origin: EmberBSD MOXA serial-core development guide, 2026-10-09. -->
# MOXA UPort 1150 preparation and G2 transports

EmberBSD provides preparatory C helpers for a shared four-port MOXA G2 USB
transport, plus an opt-in `ucom` backend that connects external transports to
the existing TTY lifecycle. They let developers exercise framing, completion,
queueing and TTY faults before attaching a device. The separate experimental `umoxa` USB parent supports the 1150 protocol below.
The G2 USB parent and Ethernet NPort implementation remain pending. Bounded 1150 RS-232 loopback is recorded below; broader physical serial validation remains pending. The isolated 1150
USB/firmware trial below does not establish G2 or native board support.

## Experimental UPort 1150 parent

`umoxa` binds only VID/PID `110a:1150` and attaches one legacy USB `ucom` child.
It is software preparation, not accepted device support. The independent
BSD protocol encoder was checked against the wire facts in MOXA's official
[UPort 1100 Linux driver v6.0](https://www.moxa.com/getmedia/c7a1d4ee-ff6f-46fe-b707-e6e2c6fcc152/moxa-uport-1100-series-linux-kernel-6.x-driver-v6.0.tgz),
`mxu11x0/driver/mxu11x0.{c,h}`. Its SHA512 matches the
[official catalog](https://www.moxa.com/en/support/product-support/software-and-documentation?psid=114473):

```text
B812A68A776EFC345FB34ACCFA57C928268278BBCCEA674D7A6ED24FDA674FD7A6064FDE23818CA34A760E97229C4F832E78B5C9F9AC273D4932583CF3E27DFE
```

Cold configuration 1 must have one firmware bulk-out endpoint; warm configuration
2 must have bulk-in, bulk-out and interrupt-in endpoints. Actual endpoint
addresses come from descriptors. The shapes were captured on the owner's physical
1150. On 2026-10-13, read-only cached descriptor capture on macOS confirmed
the owner's cold `110a:1150` (bcdDevice `0x0200`): configuration 1, interface
0, one bulk-out endpoint `0x01`, maximum packet 64. A later firmware trial confirmed a warm device descriptor with one configuration
whose value is 2: bulk-in `0x81`, bulk-out `0x01`, interrupt-in `0x83` (2 bytes).
The parent selects the state by configuration values, not their count.
The initial firmware trial completed 255 transfers (16287 bytes), but reset
returned an I/O error through macOS/QEMU. The repaired automatic transition
and its host requirements are recorded below. The driver rejects other
configurations and malformed status packets.
Data uses the existing one-port USB/TTY path, without G2 multiplexed framing.

Firmware is a separately supplied, appropriately licensed `moxa/moxa-1150.fw`
asset under a configured `hw.firmware.path` directory. It is not embedded or
redistributed here. The uploader skips the asset's three transport bytes,
pads payload to 16284 bytes with `0xff`, reconstructs length/checksum, and
requires complete 64-byte bulk transfers before reset. Missing, oversized,
short and failed firmware operations log an error and attach no TTY.
Firmware loading runs synchronously inside post-root attach, whose lifetime is
pinned by autoconf. Cold attach before root mount does no deferred work and
logs a request to reconnect after boot. This avoids holding the boot-time
configuration semaphore across a root-mount dependency. The cold xfer/pipe close
before reset. Reset acknowledgement is separate from complete bulk transport:
the parent checks configuration index 0/value 2 using at most five requests
(250ms timeout, 100ms delay), then selects the fresh configuration and validates
all endpoints before attaching ucom. A reset-related IOERROR/cancellation stops
probing the stale handle and leaves no TTY, awaiting normal warm enumeration.
Only a subsequent validated warm attach establishes readiness. Missing warm
state on an otherwise responding device remains a reported failure. No retry
or uncertain transfer replay occurs automatically.

Termios encodes 50..921600 baud, 5..8 data bits, no/odd/even parity, one/two
stop bits and RTS/CTS. `CDTRCTS` and `MDMBUF` are rejected before USB I/O.
`B0` hangup is not implemented and returns `EINVAL`, preserving modem lines.
Software XON/XOFF stays in the TTY layer; device offload is disabled.
The opt-in `ucom_methods.ucom_param_strict` serializes setting ioctls,
commits TTY settings after parent acceptance, retries a rejected identical
setting, and returns initial parameter failure from open. Other parents
leave the flag zero. Rebuild all affected drivers and rump components together.

The installed `<dev/usb/umoxa1150io.h>` provides `UMOXA1150_SET_MODE` with an
`int`: `UMOXA1150_MODE_RS232` (default), `UMOXA1150_MODE_RS485_2W`, or
`UMOXA1150_MODE_RS422_4W` (also the protocol's four-wire RS485 mode).
`UMOXA1150_GET_FAULT` returns the retained configuration/modem errno, cleared
by a successful new parent open. Legacy `ucom_set` cannot return errno for
DTR/RTS/break ioctls; failures are logged and retained, and do not establish
physical control success. Partial USB command acceptance is not rolled back.
Hardware flow, mode changes, carrier reporting and UART drain are unaccepted.

Run the 12 wire/lifecycle groups, 22 actual-parent groups, and the actual
`ucomparam`/initial-parameter-section regression on Linux/macOS:

```sh
sh ember/tools/umoxa1150-test.sh /absolute/new-1150-protocol-output
sh ember/tools/umoxa1150-driver-test.sh /absolute/new-1150-parent-output
sh ember/tools/ucom-strict-param-test.sh /absolute/new-strict-param-output
```

All three run plain and ASan/UBSan binaries. The parent compiles the actual
source with external USB/kernel doubles; the strict regression extracts the
unmodified parameter sections from `ucom.c`. Neither executes native USB/TTY
concurrency. The 33 prior G2/transport host groups remain separate.
With a clean whitespace-free source export and prepared fork tools:

```sh
sh ember/tools/umoxa1150-cross-check.sh /absolute/clean-source \
    /absolute/prepared-kernel-build /absolute/new-1150-cross-output
```

This creates fresh EMBER64 configuration headers and compiles two AArch64
objects using that configuration's real make flags. Compiler version is
recorded; the check does not imply GCC16, a full linked kernel, boot or USB
acceptance. The compatibility matrix now includes 18 parent method owners.
Separately, `6e5e17c99515787b3ce7eca03cb8c701698dbe7a` linked the complete
configured EMBER64 AArch64 kernel and image using prepared fork GCC12.5 tools.
The 21-object compatibility matrix passed. This configured build did not run
the owning full wrapper or build matched board modules/DTBs, boot the kernel,
or transfer firmware to the attached adapter.
First physical step is USB descriptor capture. Firmware/re-enumeration comes
next on an isolated test OS; a serial loopback or NPort peer follows only
once USB attachment is accepted. No jumper is needed for descriptor capture.

## G2 source and validation

- `sys/dev/usb/umoxa_frame.{c,h}` validates G2 data/event transfers and
  builds TX headers. Its host contract has eight named groups.
- `sys/dev/usb/ucom_transport_core.{c,h}` models one port's open, close,
  detach, fault and TX completion state. Its contract has eight named groups.
- `sys/dev/usb/ucom_transport_rx.{c,h}` provides a bounded per-port RX ring;
  six groups pin FIFO/wrap, capacity, atomic overflow, hysteresis and isolation.
- `sys/dev/usb/umoxa_g2_txq.{c,h}` schedules four copied TX jobs over one
  owned wire frame; eleven groups check credit, fairness, lifetime and faults.
- Host C99 builds use `-Wall -Wextra -Werror`; available ASan/UBSan builds
  run the same contracts. Host execution validates a sequential model.
  The real `ucom`/TTY path has a separate sixteen-group native rump fixture.
  Its USB stubs exercise no hardware. Physical USB behaviour and sustained
  operation still need validation before a device support claim.

Run from a clean source checkout, with an absolute, unused output directory
outside the source tree for each command:

```sh
sh ember/tools/umoxa-frame-test.sh /absolute/new-frame-output
sh ember/tools/umoxa-g2-txq-test.sh /absolute/new-txq-output
sh ember/tools/ucom-transport-core-test.sh /absolute/new-state-output
sh ember/tools/ucom-transport-rx-test.sh /absolute/new-rx-output
```

Each runner saves its compiler version, binaries, build/run logs and source
SHA256 hashes. It refuses to overwrite an existing output. `CC` selects a
single host compiler executable path; it is not a string of compiler flags.
Sanitizer compiler/linker unavailability is recorded as a skip; sanitizer
runtime/test failure fails the command. These runners execute host binaries,
so an AArch64 cross compiler is not a replacement for their host `CC`.

After preparing an EMBER64 cross build with the fork's
[wrapper](../../../../tools/docs/cross-build.md), compile the four conditional kernel helpers:

```sh
sh ember/tools/moxa-core-cross-check.sh /absolute/clean-source \
    /absolute/kernel-build /absolute/new-kernel-object-output
```

The output's parent must already exist. This checks four AArch64 kernel
objects using the generated `nbmake-evbarm` compiler and kernel headers;
it does not link the helpers into a kernel or execute target code. Keep
source revisions and header/toolchain provenance with the resulting receipt.

## Framing contract

RX data records use a four-byte header: big-endian 16-bit port followed by
big-endian 16-bit payload length. Ports are 0 through 3. Zero-length records
are consumed without callback delivery. Events are eight bytes: port, event
code and four opaque detail bytes. Unknown event codes remain opaque.
The decoder validates the complete transfer before invoking any callback;
a malformed suffix rejects a valid prefix too. Input must remain immutable
through callbacks, whose payload/detail pointers are borrowed for that call.

`umoxa_g2_rx_data` takes a payload limit from 1 through 65535. Empty input
may be NULL, but a callback is always required. Bad arguments return EINVAL;
invalid ports, truncated records, event tails and excessive record lengths
return EPROTO. Callers must provide spans covering their declared lengths.

`umoxa_g2_tx` accepts payload lengths 1 through 65535 and a SEND_NEXT value
of 0 or 1. SEND_NEXT sets bit 0x8000 in the TX port word; this bit is invalid
as an RX port. Insufficient capacity returns EMSGSIZE without changing the
output buffer. Payload and output may overlap; the written counter must not
overlap either. With a valid counter pointer, errors set it to zero.

Wire facts were inspected in MOXA's official
[UPort family Linux driver v6.2 archive](https://cdn-cms-frontdoor-dfc8ebanh6bkb3hs.a02.azurefd.net/getmedia/334908bb-fa9d-4fd8-8e18-db2512348451/moxa-uport-1200-1400-1600-1200-g2-1400-g2-1600-8-g2-series-linux-kernel-6.x-driver-v6.2.tar),
`mxuport/driver/mxuport/mx-uport.c` and `mx-uport.h`.
The archive's SHA512 matched the
[official catalog](https://www.moxa.com/en/support/product-support/software-and-documentation?psid=131128):

```text
B7A1FD7C2E1A23259E5696611FBE0CDEF50912E85200C70EBA6CF80DF86B90985ED774E69B8E7A24C1654985143F1C0F366C54CED164725374A1D0AD33F5A1BE
```

These are independent BSD-2-Clause helpers based on wire facts. Vendor GPL
code and firmware are not included. Device revision and actual descriptors
still need validation. SEND_NEXT scheduling is implemented below. Actual USB event delivery,
receive/purge barriers and UART drain semantics belong to the future parent.

## State contract

One initialized `ucom_transport_core` belongs to one port; the caller supplies
locking. No allocation, I/O or callbacks occur inside the state core. Output
counters must not alias the state; void functions require non-NULL state.
Other NULL arguments produce EINVAL or a rejected boolean query/completion.

- Open increments a lifetime epoch and resets its TX cookie. A pending TX
  gets a unique cookie. Open/begin return zero or a positive errno.
- Accept and done return 0 or 1. A stale or duplicate done returns 0 without
  changing state. A matching failed done returns 1, consumes zero bytes and
  faults the port. Full success consumes exactly the pending length once.
- Short, overlong, failed or negative-error completion faults without replay.
  A supplied positive errno is retained; otherwise the fault is EIO.
- Close permits a new lifetime after a fault. Detach is final. Epoch overflow
  leaves the port detached; cookie overflow faults it. Neither counter wraps.
- Each instance is independent. This does not prove isolation in a shared USB
  scheduler; the shared scheduler below is exercised in a software fixture,
  while actual USB stop/detach barriers remain to be built.

An epoch identifies host callbacks, not bytes still in a UART or device queue.
The parent must establish purge/enable and USB completion barriers on reopen.
The core leaves TTY output accounting to the bridge; it cannot infer whether
an errored USB transfer already reached the physical line.

## Shared G2 transmit scheduling

`umoxa_g2_txq` takes a frame budget5..1024 (including the four-byte header)
and a nonzero timeout in monotonic milliseconds. Each of four ports owns one
copied payload1..budget-4; all ports share one stable borrowed wire frame.
No allocation, I/O or child callback occurs inside the engine. Caller supplies
locking and physical USB abort/completion barriers. Output structs/masks and
input spans must not alias the engine or one another. Reinitialize only after
quiescence. Struct fields are caller storage, not mutable configuration.

Open requires a strictly increasing nonzero per-port epoch; submit requires
an increasing nonzero cookie. Closed/faulted/old submissions return errno.
Round-robin pick returns EBUSY for the shared active transfer, EAGAIN for no
eligible job, or a token/port/epoch/cookie/frame on success. Tokens never wrap.
Done consumes only a matching physical token; stale/duplicate tokens return0.
Its result delivers child acknowledgement only for the still-open matching job.
Payload accounting excludes the wire header. Close invalidates a port but
retains its active frame; reopen is EBUSY until physical completion/abort.

Following the pinned v6.2 ordering, when prior dispatched payload totals>=512,
the next dispatch sets SEND_NEXT and arms that port's credit deadline. USB done
and event2 are separate: early event2 clears the wait before done; an unarmed
credit is ignored. Only an armed wait resets the payload counter. A queued job
behind a wait does not block peers. The timeout is caller policy, not measured
MOXA latency. Pick/tick reject backward time. Token/deadline overflow faults
all open ports EOVERFLOW instead of wrapping.

Successful short/overlong completion faults its owner EIO/EPROTO. SEND_NEXT-only
expiry faults its owner ETIMEDOUT. USB error/watchdog or malformed shared event
batch faults all open ports. The event decoder validates every record before
admitting credit. Fault masks describe new faults; notify children outside the
parent lock. Fault clears queued jobs but retains an active frame until real
completion/abort; never replay an uncertain transfer automatically.

Wire credits have no epoch/cookie. A late old event during a new armed wait is
indistinguishable; the real parent must establish receive/purge barriers and
verify them on hardware. These helpers do not claim UART drain semantics.
The private rump parent uses a1024-byte frame/1020-byte payload budget and a
100ms synthetic clock policy. It aborts its software flight synchronously on
stop; that test operation is not proof of USB controller quiescence.

## External ucom backend

`ucom_methods.ucom_transport` is a trailing nullable pointer. NULL preserves
the existing per-child USB pipe path. Stack `ucom_attach_args` is unchanged.
Rebuild the kernel, all affected parent drivers and rump components from one
pinned source tree; old module binaries are not compatible by assumption.
No real MOXA parent or VID/PID binding is added by this backend.

An external parent supplies `ucom_param` and all `uct_*` methods in
`struct ucom_transport_methods` (`sys/dev/usb/ucomvar.h`). Legacy
open/close/read/write/set methods must be NULL. get_status/ioctl remain
available. Attach rejects an incomplete table, a payload buffer outside
1..65535 or a nonzero packet header length. Framing belongs to the parent.

| Parent method | Contract |
| --- | --- |
| uct_attach(arg, port, child) | Process context; publish child only on success; clean resources on error |
| uct_start(arg, port, epoch) | Process context; stop will run even if start fails partway |
| uct_stop(arg, port, epoch) | Quiescence barrier; no dispatch/callback/buffer use remains on return |
| uct_submit(arg, port, epoch, cookie, bytes, length) | Nonblocking softint; borrow payload until done/stop; error accepts nothing |
| uct_rx_flow(arg, port, epoch, paused) | Nonblocking softint; no synchronous child callback |
| uct_set(arg, port, which, on) | Process context; return errno for DTR/RTS/break rejection |
| uct_detach(arg, port) | Process context; release the child binding permanently |

No `sc_lock` or TTY lock is held across parent methods. A process mutex
serializes termios/modem/break setting ioctls and param/set, covering the
TTY commit after parent acceptance. Drain may wait while holding this mutex,
but progress commands (start/stop/flush) and queries remain available. Never
reenter these setting controls on the same port. Lock order is process
control, `sc_lock`, then TTY. t_oproc and
hwiflow only schedule work while holding the TTY lock. Submit, flow and fault
notification carry references that close waits for before calling stop.
Parents release their lock before `ucom_transport_input/done/fault`.

Input copies the entire supplied block or returns errno. Capacity is 8192
bytes per port, with pause/resume watermarks 6144/2048. These are software
limits, not measured throughput. Overflow or failed TTY admission faults
that port instead of silently dropping the remainder. A fault wakes blocked
TTY I/O, subsequent operations return its errno, and poll reports error/hangup.
Selector registration and fault admission share `sc_lock`; fault notification
wakes both read and write selectors. External read/write kqueue filters retain
native TTY readiness rules while healthy and report `EV_EOF` with the transport
errno in `fflags` on fault. They use an atomic error snapshot under the TTY
lock, avoiding the reverse TTY-to-`sc_lock` order. Closing invalidates the
epoch and waits for notification dispatch before clearing this snapshot.
On a revoked TTY vnode, read may report EOF; consumers must treat EOF as loss.

Done uses payload bytes, excluding the parent's framing. Full success removes
the pending payload exactly once; stale/duplicate done changes nothing.
Partial/errored TX faults without replay. A TTY flush during pending TX keeps
newly queued bytes intact when the old completion arrives. The parent
relinquishes the borrowed buffer before invoking done. USB acceptance does
not prove UART drain; real parent scheduling/drain semantics remain pending.

Initial termios/set/start failure aborts open. Rejected parameters leave the
TTY settings unchanged, and failed initial open restores its saved settings.
Errors from two-part modem control are returned; successfully changed bits
remain reflected in cached state. Hardware partial-control rollback belongs
to the parent, which must expose uncertainty/fault instead of claiming success.

## Cross build and native execution

Prefer building the fixture on the development host with the fork's prepared
AArch64 cross tools. Supply a separate, pinned target-runtime sysroot containing
`usr/include`, `usr/lib` and `lib` from the matching NetBSD 11 software lab.
It supplies TTY/VFS/rump and C runtime dependencies, not a claim of complete
EmberBSD userland coherence. The private component compiles kernel sources from
the fork; the runner records source/header/link-input and output hashes.
No target binary is executed on the development host.

```sh
sh ember/tools/ucom-transport-rump-cross-build.sh /absolute/clean-source \
    /absolute/prepared-kernel-build /absolute/pinned-target-runtime \
    /absolute/new-cross-output
```

Copy the resulting `test` to the matching disposable VM and run
`RUMP_NCPU=4 ./test`. Record the target runtime/library hashes and output.
The wrapper passes `MAKEOBJDIR` on the make command line and checks `.OBJDIR`
before compilation: generated files remain in the new private output.
The source/build/sysroot paths must have no whitespace; host `shasum` is
required. No libraries or kernel are installed by this check.

### Explicit native build fallback

On a matching NetBSD 11 development runtime with compiler, rump libraries and
the fork's clean source export (native make paths must have no whitespace):

```sh
sh ember/tools/ucom-transport-rump-test.sh /absolute/clean-source \
    /absolute/new-native-output
```

The runner builds a private component containing real ucom/core/RX and a
test-only parent, links to the runtime's TTY/VFS/rump libraries, and records
those library hashes. Sixteen groups exercise actual device nodes, data exchange,
stale completion/reopen, partial TX fault, RX pressure/overflow, flush,
rejected/serialized controls, drain/resume/flush progress, dispatch-close
ordering, blocked read/poll, poll-registration races, healthy/fault kqueue
faults, shared four-port scheduling/credit timeout/shared event faults and
detach/failed attach. Three attach warnings are intentional
invalid children. Raw rump syscall clients retry RUMP_ERESTART where ordinary
kernel/libc syscall handling would restart. This is bounded native software
acceptance, not a complete rebuilt OS, USB stack or physical-device test.

The focused compatibility matrix uses the prepared kernel's actual compiler
flags and generated headers. It cross-compiles ucom/core/RX plus all 17 legacy
method-table owners and retains dependency/object hashes:

```sh
sh ember/tools/ucom-transport-cross-check.sh /absolute/clean-source \
    /absolute/kernel-build /absolute/new-matrix-output
```

This matrix does not link a kernel; run the full kernel wrapper separately
from that same pinned source for the configured link and matched modules.
GitHub CI runs the 33 host groups on Linux/macOS; it does not run the native
fixture or kernel build. Keep those separate receipts with a published change.

## macOS USB trial boundary

The isolated QEMU 11.1.1 AArch64 trial booted the exact EmberBSD kernel at
`5ddc0e1d982343d217fa6baa88c7b1a5e2d44771` over the existing NetBSD 11 lab
userland. Kernel SHA256 was
`4989c280ac4651a47f7beed2bca986591bbbb93ea79afdfd9c2918dc231dd6bd`.
Module autoload was disabled; this is not full distribution acceptance.

Two host prerequisites were observed: run QEMU without `-daemonize` to avoid
Darwin Objective-C initialization after fork; configure the cold adapter to
USB configuration 1 before QEMU initializes its libusb context. QEMU skips
`libusb_set_configuration` for a single-configuration device, and an existing
Darwin libusb context retained stale configuration metadata after an external
configuration change. A fresh context then issued all firmware bulk writes.
These are host trial workarounds, not EmberBSD USB controller guarantees.

The external asset from linux-firmware tag `20250211` is 12462 bytes with SHA256
`5a7a87085584c4ccf8de63249934fb6b5129e2a42aa6efda2f740f0af2978f8b`.
Its [MOXA license](https://kernel.googlesource.com/pub/scm/linux/kernel/git/firmware/linux-firmware.git/+/refs/tags/20250211/LICENCE.moxa)
allows use on MOXA devices. The opaque file and license remain outside Git.

The corrected kernel at `f50dd3b10fe00501141fd1636951180eba37f329` also booted
with SHA256 `c6c4c0920b5866ee721ae931cc481158e7d38452327846aa4607016667de1bcb`.
After manual QEMU detach/restart and host configuration 2, the physical warm
adapter attached as `umoxa0` with `ucom0` and `/dev/dtyU0`. The warm trial
issued no firmware bulk writes. Reset acknowledgement and automatic transition
from cold to warm were unresolved in this initial trial. Carrier/modem events,
native concurrency and sustained operation remain pending.

For the next RS-232 loopback trial, connect DB9 pin 2 (RxD) to pin 3 (TxD),
using the connector's pin numbers. The supplied terminal block has a different
mapping; this instruction is for the DB9 connector. Pinout:
[MOXA UPort 1100 QIG, page 3](https://moxa.com/getmedia/f21cfe09-4f59-4241-a93a-aaa207e2f003/moxa-uport-1100-series-qig-v8.2.pdf).

With the owner-installed DB9 2–3 jumper, a physical RS-232 test on the corrected
trial kernel opened `/dev/dtyU0` and compared all 4096 binary bytes without a
mismatch: 2048 at 9600 baud and 2048 at 115200 baud, both 8N1 with software
and hardware flow disabled. All 256 byte values were covered; the retained
parent fault was 0. This is a bounded local loopback, not an independent peer,
flow-control/modem, RS-485, unplug/power-loss or sustained-use acceptance.

The native test source is `ember/tools/umoxa1150-loopback-test.c`; it uses
`<dev/usb/umoxa1150io.h>`. Compile it with the matching NetBSD/EmberBSD userland
compiler and headers, then run on the isolated target only after the DB9 jumper
is installed. It targets `/dev/dtyU0`, sets RS-232 mode, bounds reads with poll
deadlines and has a 30-second alarm. Hardware tests are not run by host CI.

### Automatic cold-to-warm trial

Kernel source `8bdb1d1da76ab20fb0c689591b9255769188ff3f` booted with SHA256
`f56e1967ccce145e740046cc48e7d5e22773eeff760dceb048e0ed21be664bf6`.
The owner power-cycled the adapter; its actual configuration index 0/value 1
was captured before the single post-root USB attachment. All 255 firmware
blocks completed, with the same 16287-byte wire SHA256
`3723acacfab187ef61bb33f7a34efcf5c630dbd1f5f74135dba2c3ad97de4c88`.
Reset still returned guest EIO, followed by device disconnect. The parent
reported waiting for warm USB attachment; automatic enumeration then validated
configuration 2 and attached `umoxa0 → ucom0`. No repeated stale-handle probes,
manual detach, VM restart, firmware replay or manual configuration 2 intervened.
The bounded RS-232 test then matched 4096 bytes at 9600/115200 8N1 with fault0.
The port closed normally after the test.

This complete trial required a process-local Darwin libusb configuration bridge
and `usb-host,guest-reset=off`. Stock
[QEMU 11.1.1](https://github.com/qemu/qemu/blob/v11.1.1/hw/usb/host-libusb.c)
skips `libusb_set_configuration` for a device reporting one configuration.
A separate stock-backend trial automatically enumerated the warm child but
left host configuration0: open succeeded and serial data timed out. The temporary
bridge selected the actual cold1/warm2 configuration on the host. QEMU's extra
bus reset also cleared that configuration, so it was disabled only for this
passed-through device; the driver's firmware reset command remained enabled.
The bridge is trial infrastructure outside this repository, not an EmberBSD
feature or an upstream QEMU/libusb fix. No system QEMU installation changed.
Bare-metal USB, repeated power cycles, serial modes/flow/modem and sustained
operation still need separate acceptance.

The experimental four-port USB parent is described in [moxa-g2.md](moxa-g2.md).
