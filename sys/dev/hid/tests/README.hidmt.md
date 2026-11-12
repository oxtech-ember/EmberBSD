# Precision Touchpad contract tests

The test compiles the real kernel HID parser and contact policy in userland.
Small include shims provide only allocation and kernel print interfaces.
The fixture is the unmodified 615-byte HID descriptor from a `347d:7640` device.
It has five parallel contacts in input report 4 and input mode in feature report 3.

Run with the selected native SDK compiler (Clang or GCC):

```sh
CC=/path/to/selected/compiler sys/dev/hid/tests/run-hidmt.sh
CFLAGS='-fsanitize=undefined -fsanitize-undefined-trap-on-error -fno-sanitize-recover=all' \
    CC=/path/to/selected/compiler sys/dev/hid/tests/run-hidmt.sh
```

The tests cover descriptor ranges and report IDs, short taps, long holds,
pointer motion, contact order changes, one- and two-finger taps, disabled
tapping, physical buttons, palms, invalid lengths, duplicate identifiers,
and invalid coordinates. `HIDMT_TAP_LEFT` synthesizes left click from one
finger; `HIDMT_TAP_RIGHT` synthesizes right click from two fingers; three
contacts are not a tap. Right-only mode rejects one-finger taps and still
accepts two-finger taps. Both asynchronous two-finger release orders produce one right tap only after
the last contact leaves; a repeated release produces no additional tap. A
physical click with two contacts is right; with three contacts it is middle;
clickfinger works with all tap flags off.
Enabling tapping during an existing contact must not synthesize a click.

The I2C transport must also compile against the selected kernel configuration.
These tests do not simulate I2C hardware, wscons queues, device detach,
kernel locking, or a compositor.
Valgrind does not support the NetBSD target.
The UBSan trap run is an arithmetic and bounds check, not a leak or race check.
Physical acceptance must test attach, movement, taps, close/open, suspend,
and persistence separately.

## Review Dev Bots #472: lifecycle (`t_hidmt_lifecycle.c`)

`hidmt_release()`, `hidmt_reset()`, `hidmt_live()` and `hidmt_stall()` (the
timer rule: an incomplete hybrid frame is dropped after
`HIDMT_HYBRID_TIMEOUT_US`, silent contacts are released after
`HIDMT_STALL_US`, a swipe in progress gets CANCEL, no tap is synthesised),
the release after a dropped frame (a Contact Count 0 report without a touching
contact is a release, with one it is the tail of a hybrid frame) and
`hidmt_gesture_update()` with gestures off. Run by `run-hidmt.sh`;
`HIDMT_SRC_SYS=<old sys/>` plus `-DLEGACY_API` gives the red run against the
pre-review sources.

Assumption to confirm with a `hw.imt0.rawlog` capture on the hardware: only
the first report of a frame carries Contact Count N, the following ones carry
0 and the same Scan Time (Microsoft Precision Touchpad); a touchpad keeps
reporting while a finger rests. Only the ELAN descriptor is available so
far, no raw sequences with touches.
