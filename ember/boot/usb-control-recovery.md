# USB control request recovery

The xHCI driver recovers a stalled control ring in process context.
Queued transfers started by completion use the USB task queue.
One restart owner protects the ring while controller commands release the lock.
The transfer becomes active before recovery can sleep, so cancellation can complete it.

## Source regression

Run from the source root. Each output directory must be new.

```sh
sh ember/tests/xhci-control-recovery.sh sys/dev/usb/xhci.c /absolute/work/xhci-check
CC=clang CFLAGS='-O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined' \
    sh ember/tests/xhci-control-recovery.sh sys/dev/usb/xhci.c /absolute/work/xhci-asan
```

The test extracts the actual driver functions and supplies pthread-based kernel doubles.
It checks process and soft interrupt starts, deferred recovery, concurrent restart,
and cancellation or timeout while Reset Endpoint or Set TR Dequeue is pending.
The earlier implementation fails the soft interrupt and cancellation interleavings.
These tests verify synchronization. They do not model controller hardware or USB timing.

## Abort contract

`USB_ABORT_REQUEST` requires a writable endpoint-zero descriptor.
It cancels the active default-pipe request and preserves queued requests.
Concurrent abort callers wait for the current abort to finish.
The call succeeds without work when no request is active.
`USB_DO_REQUEST` accepts only `USBD_SHORT_XFER_OK` in `ucr_flags`.
Other bits return `EINVAL`; kernel transfer flags are not a user interface.

```sh
sh ember/tests/usb-abort-request.sh sys/dev/usb/usbdi.c sys/dev/usb/ugen.c /absolute/work/abort-check
```

This regression extracts the actual abort functions and ioctl validation.
It covers access checks, request flags, queued requests, concurrent cancellation,
and serialization with the existing whole-pipe abort operation.

## Physical control probe

Build `ember/tools/ugen-control-probe.c` with the matching EmberBSD headers and libc.
Select the endpoint-zero node of the intended device with `usbdevs -v`.
The probe prints its vendor, product and bus before measurement.

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror ember/tools/ugen-control-probe.c -o ugen-control-probe
./ugen-control-probe /dev/ugen0.00 read 1024
./ugen-control-probe /dev/ugen0.00 stall 1024
```

`read` validates the standard device descriptor on every request.
`stall` requests undefined descriptor type 255 and requires `EPIPE` each time.
Both modes require a valid device descriptor before and after the loop.
The probe sends no device-specific write, port reset, or firmware image.
Its one-second timeout bounds each request; counts must be between 1 and 10000.
Use only a device whose unsupported descriptor request is known to stall.

Record the kernel revision, build configuration, firmware, device identity,
request count, elapsed time, and final descriptor result with each hardware run.
Successful control recovery does not establish bulk, isochronous, power-management,
or phone boot support.
