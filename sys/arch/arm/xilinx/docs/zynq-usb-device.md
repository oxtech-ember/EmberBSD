# Native Zynq USB peripheral

This driver supplies controller mechanics and a copied function interface.
It does not implement IIO policy, ADC DMA sharing, or RF transmission.
Source and host checks do not establish physical USB operation or throughput.

## Selection and resources

Enable `zynqudc* at fdt?` in the kernel configuration. The driver matches
`xlnx,zynq-usb-2.20a` only with `dr_mode = "peripheral"`.
The host attachment accepts `host` or an absent role. It rejects other roles.
Dynamic OTG role switching is unsupported.

The node must supply `phy_type = "ulpi"`, a clock at index zero,
a decoded interrupt, and a register range of at least 512 bytes.
Hardware must report device support and at least three endpoint numbers.
The driver checks the ULPI viewport with finite timeouts. It clears host
VBUS drive, charge, and discharge controls before a function can connect.
Device initialization sets OTGSC termination and retains the ULPI D- pulldown.
It clears the ULPI D+ pulldown and prepares the full-speed transceiver.
TermSelect stays clear while stopped. The controller owns D+ attachment
through RUN after EP0 and interrupts are ready. High-speed negotiation
remains a controller operation after host reset.
The existing EHCI path retains its host configuration.

The hard interrupt disables controller interrupts and schedules a USB soft
interrupt. Core state uses an adaptive lock at `IPL_SOFTSERIAL`.
No copyin, copyout, or condition wait runs under a hard interrupt lock.

## Function ABI

The installed header is `dev/usb/usbperiph.h`, ABI version 1.
The device is `/dev/usbperiph0`, character major 369, minor 0.
Run `sh MAKEDEV usbperiph0` from `/dev` to create an owner-only node.
Open it with `O_RDWR`.
One file descriptor owns the controller. Threads can transfer different
endpoint directions concurrently through that descriptor.

`USBPERIPH_IOC_CONFIGURE` installs bounded device, configuration, and string
descriptors while disconnected. Supply an identity you can use.
The driver supplies no default vendor or product identity.
Configurations use one configuration value, contiguous interface numbers,
alternate setting zero, and bulk endpoints. Endpoint addresses are unique.
The supplied endpoint packet sizes are 512 bytes. Enumeration adjusts them
to 64 bytes for full speed and supplies the other-speed configuration.

`USBPERIPH_IOC_CONNECT` starts enumeration. Standard EP0 requests stay in
the controller. `SET_ADDRESS` changes the address after its status stage.
Reset invalidates the active configuration and advances the generation.
A new setup drains the previous EP0 transfer before installing its reply.

`USBPERIPH_IOC_GET_EVENT` returns configuration, reset, suspend, disconnect,
or vendor setup events. The latest event replaces an unread older event.
Generation and sequence fields let the service reject obsolete work.
The call waits at most one second and can return `EWOULDBLOCK`.
Vendor interface requests 0, 1, and 2 with no data stage are forwarded.
The service must return `USBPERIPH_IOC_REPLY_SETUP` with the event generation
and sequence. Set `stall` to reject a request. Replies expire after two
one-second timer callbacks. Other vendor requests stall explicitly.
These requests support the control boundary used by libiio 0.25. The
service owns reset/open/close policy and the `IIO` interface string.

`USBPERIPH_IOC_XFER` accepts an endpoint address, a copied user buffer,
a length up to 16384 bytes, and a timeout from 1 to 60000 milliseconds.
The user address has a fixed-width 64-bit field. The kernel validates it
against its pointer range. No user address enters a hardware descriptor.
A zero generation accepts the current generation. A nonzero stale value
returns `ESTALE`. A successful ioctl returns actual bytes and the request generation.
Short OUT packets return their actual length. An active descriptor is not
retired merely because an interrupt occurred. `CANCEL` wakes its waiter.
`DISCONNECT`, reset, and close retire all directions and wake waiters.
Unrelated interrupts cannot extend a transfer's absolute deadline.

`USBPERIPH_IOC_ENDPOINT_GATE` is additive to ABI version 1. Its 16-byte
argument contains `uint64_t generation`, `uint32_t endpoint`, and
`uint32_t enable`. Zero generation accepts the current bus generation.
A nonzero value must match under the controller lock, or the call returns
`ESTALE`. Addresses must name a nonzero bulk direction in the installed
configuration descriptor. Thus a function can close gates before CONNECT.
EP0 and unadvertised endpoint addresses are rejected.

With enable zero, the driver closes admission under its lock before canceling
and draining that direction. A blocked transfer wakes. Future XFER calls and
requests returning from user copies cannot prime a closed direction.
They return `EPIPE`. Reopening a gate does not revive a request whose copy
crossed the close barrier; its changed cancellation epoch returns `ESTALE`.
A canceled active request returns `ECANCELED`. Drain failure retains DMA and
blocks reopening with `EIO`, as for existing cancellation failures.

Enable one reopens the direction. Gates stay closed across disconnect and
bus reset until explicitly reopened. A new exclusive open resets gates to
open, preserving the generic counter and loopback behavior. A service must
retire its old producer threads before reopening a new pipe session.
Initialize a fresh XFER argument for each call. On any ioctl error, discard
the payload and ignore all output fields, including actual and generation.
The syscall copies the output structure back only when the driver succeeds.
An error can therefore leave old field values in a reused user structure.
If a close crosses copyout, discard the already copied data on that error.

## Read-only diagnostics

`USBPERIPH_IOC_SNAPSHOT` returns a fixed 144-byte version-1 snapshot on the
owning descriptor. Reserved fields and padding are zero. The driver takes
its existing lock and reads twelve controller registers without writes,
read-to-clear registers, endpoint DMA contents, or ULPI transactions.
The result contains lifecycle state, IRQ/setup/reset/disconnect counters,
last handled IRQ and setup, endpoint ownership masks, and register state.
Endpoint masks use bit `2 * endpoint_number + direction_in`.

The PHY fields are cached initialization readbacks taken before RUN.
`USBPERIPH_SNAPSHOT_PHY_INITIAL_VALID` marks them valid. They are not live
PHY state and cannot prove the post-negotiation line configuration.
As for other output ioctls, ignore the entire result if the ioctl fails.

The diagnostic function prints snapshots before/after CONNECT, for at most
four enumeration timeouts, and after configuration. It flushes each line
so redirected logs remain visible while enumeration is incomplete.

## DMA ownership

The controller owns one contiguous 2048-byte queue-head region and a
32-byte-aligned descriptor per direction. Data buffers use distinct native
`bus_dma` maps. DMA addresses and descriptor bounds must fit 32 bits.
Each transfer fits within the controller's five page pointers.
One request per direction is active. No running descriptor chain is patched,
so the add-descriptor tripwire append algorithm is unnecessary.
The setup tripwire protects the setup packet snapshot.

Cancellation proves flush, prime, and active endpoint bits clear before
returning ownership. Failed drain marks the controller faulted. Its DMA
maps stay allocated and cannot be reused, copied into, or restarted.
There is no detach path. All controller allocations remain until a proven reset.
PMF shutdown blocks new admission, drains all endpoint directions, and stops
the timer. A separate final shutdown hook runs without sleeping or hardclock
polling. It requires the PMF drain certificate, an available lock, inactive
requests, retained mappings without inflight DMA, and idle hardware registers.
It blocks platform reset if that proof is absent, including panic or cold
paths that skipped PMF. Failure to register either hook disables admission.
Queued soft interrupts cannot prime descriptors after disconnect or fault.
Reconnect clears old setup and completion work before a distinct generation.

## Source audit

The register layout and device sequences were checked against AMD USBPS,
commit `f8809afec39a7048597f34f39372169be20735cf`:

- `XilinxProcessorIPLib/drivers/usbps/src/xusbps_hw.h`
- `XilinxProcessorIPLib/drivers/usbps/src/xusbps_endpoint.c`

The initial full-speed ULPI setting follows Table 5-1 (ULPI 1.1 Table 40)
of the [USB3320 data sheet](https://ww1.microchip.com/downloads/aemDocuments/documents/UNG/ProductDocuments/DataSheets/USB3320-Data-Sheet-DS00001792.pdf).
An operational OTG full-speed peripheral uses XcvrSelect=01, TermSelect=1,
OpMode=00, D+ pulldown disabled, and D- pulldown enabled. HS selection with
TermSelect=1 and normal OpMode is not a defined attachment state.
However, software must not assert TermSelect during attachment preparation.
[UG585 device initialization](https://docs.amd.com/r/en-US/ug585-zynq-7000-SoC-TRM/Device-Controller-Initialization-Overview)
requires D+ pull-up to remain inactive until RUN. The controller starts
attachment only after queue heads and interrupt delivery are prepared.

Both donor files declare MIT. The endpoint file credits Xilinx, 2010–2021,
and Advanced Micro Devices, 2023–2026. The implementation here is original
native code. No donor C source, standalone BSP, cache API, or board stack
is imported. The donor setup tripwire, queue-head fields, page pointers,
prime, and flush sequences informed this code.

The existing NetBSD-derived Zynq EHCI files remain the host implementation.
FreeBSD's DWC OTG device path, OpenBSD's FDT EHCI, and DragonFly's EHCI
implementations were checked as subsystem references. They do not supply this Zynq peripheral function ABI.

## Verification

Run the actual-source controller and cdev tests with an explicitly selected
native GCC16 and the host's declared ABI inputs:

```sh
CC=/absolute/native/gcc16 CFLAGS='host ABI flags' \
    sh ember/tools/tests/zynq-usb-device/run.sh
```

Use `-fsanitize=address,undefined -fno-omit-frame-pointer` for sanitizer checks.
Set `ANALYZE=yes CPPCHECK=/absolute/cppcheck` for Cppcheck and GCC analysis.
The runner replaces MMIO, DMA cache services, and kernel waits. It executes
the real controller and cdev bodies. It does not simulate a USB host or PHY.

`ember/tools/tests/zynq-usb-device/function.c` is a target test function.
Build it against this header and an EmberBSD sysroot. Its arguments are
`device counter|loopback owned-vid owned-pid bytes`. The counter is a
little-endian 32-bit increasing word sequence. Its product string is
`EmberBSD USB diagnostic`; it sets no default VID or PID. Loopback echoes actual OUT
bytes. It prints transferred bytes and exits on transport errors.

Physical acceptance still requires enumeration, high-speed operation,
12.288 MB/s ordered payload, a short run, a 30-minute run, reset, repeated
start/stop, and cable removal. Use a RAM candidate and preserve rollback.
