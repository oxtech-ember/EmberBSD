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
With `HIDMT_TAP_DRAG` a one-finger tap holds the left button: a new touch
within `HIDMT_DRAG_US` drags, its lift releases, a short second touch adds
one click, a second finger ends the drag (a physical click with two
contacts stays right only), and `hidmt_drag_expire()` releases an unused
hold.
With `HIDMT_DRAG3` three contacts arm a drag and keep the pointer still.
After `HIDMT_DRAG3_SETTLE_US`, travel of their mean beyond
`HIDMT_DRAG3_START` presses the left button. Lifting fingers stops the
pointer, the last lift releases the button. A sequence with four contacts,
a scroll, or a third contact later than `HIDMT_DRAG3_LAND_US` never arms;
a new set of three contacts re-arms; frames of a drag never scroll or start
a gesture; the stall timer gives a resting drag `HIDMT_DRAG_STALL_US`.
A drag with the mechanical button (`t_hidmt_bdrag.c`): one finger presses
the switch as the left button and stays on it, a second finger moves the
pointer by its own increments from where it landed (no jump, also after the
report order changes or a new second finger lands). In each frame the contact
that moved more moves the pointer, so the pressing finger still drags while
another finger rests. Lifting either finger keeps the button down while the
switch stays closed; opening the switch ends the drag. A press with two
contacts stays the clickfinger right button, with three the middle one. Such
frames never scroll or start a gesture; two fingers without the switch still
scroll.

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
