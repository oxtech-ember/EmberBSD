# Precision Touchpad contract tests

The test compiles the real kernel HID parser and contact policy in userland.
Small include shims provide only allocation and kernel print interfaces.
The fixture is the unmodified 615-byte HID descriptor from a `347d:7640` device.
It has five parallel contacts in input report 4 and input mode in feature report 3.

Run on the selected build host with GCC 16:

```sh
CC=/path/to/gcc16 sys/dev/hid/tests/run-hidmt.sh
CFLAGS='-fsanitize=undefined -fsanitize-undefined-trap-on-error -fno-sanitize-recover=all' \
    CC=/path/to/gcc16 sys/dev/hid/tests/run-hidmt.sh
```

The tests cover descriptor ranges and report IDs, short taps, long holds,
pointer motion, contact order changes, disabled tapping, physical buttons,
palms, invalid lengths, duplicate identifiers, and invalid coordinates.
Enabling tapping during an existing contact must not synthesize a click.

The I2C transport must also compile against the selected kernel configuration.
These tests do not simulate I2C hardware, wscons queues, device detach,
kernel locking, or a compositor.
Valgrind does not support the NetBSD target.
The UBSan trap run is an arithmetic and bounds check, not a leak or race check.
Physical acceptance must test attach, movement, taps, close/open, suspend,
and persistence separately.
