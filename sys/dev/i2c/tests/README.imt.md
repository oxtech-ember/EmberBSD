# Precision Touchpad power transition contracts

Run with GCC 16 and Python 3 on the selected build host:

```sh
CC=/path/to/gcc16 python3 sys/dev/i2c/tests/run-imt.py
CFLAGS='-fsanitize=undefined -fsanitize-undefined-trap-on-error -fno-sanitize-recover=all' \
    CC=/path/to/gcc16 python3 sys/dev/i2c/tests/run-imt.py
CFLAGS=-fanalyzer CC=/path/to/gcc16 python3 sys/dev/i2c/tests/run-imt.py
```

`BUILD_DIR` retains generated sources, preprocessed contexts, and executables
for analysis. Without it, the runner removes its temporary directory.

The first contract compiles every function from the real `imt.c` adapter.
Userland doubles replace kernel includes and external boundaries.
The transport double models feature report reads, writes, and parent resets.
The test exercises the registered PMF callbacks after the parent reset.
It checks mode restoration, polling, open reference ownership, failed restore
and retry, closed devices, input suppression, and stale tap and button release.

The second contract extracts the real report functions and command dispatcher
from `ihidev.c`. The I2C boundary checks acquisition, transfers, and release flags,
including the report identifier mismatch path. Existing report APIs must keep
non-polling behavior. The dispatcher retains its kernel sign-comparison policy.

These tests do not exercise PMF traversal, actual I2C hardware, kernel locking,
wscons queues, or sleep firmware. Check those on a coherent kernel separately.
Compile both drivers against the selected kernel configuration as well.
The contact decoder and pointer policy have separate tests in
`sys/dev/hid/tests/run-hidmt.sh`.

## Review Dev Bots #472: gestures, lifecycle, timer

`run-imt-gesture.sh` (any C23 compiler, no GCC 16) builds the real `imt.c`
adapter together with the real `hid.c` and `hidmt.c`, feeds it reports in the
format of the ELAN 04f3:31fd descriptor and checks: CANCEL on disable,
suspend and resume in the middle of a swipe; gestures or hybrid switched off
and on (candidate and assembler are dropped); a lost report, a frame without
continuation and a silent device (timer); lost `wsmouse_gesture()` events
(END/CANCEL retried, BEGIN lost silences the swipe, UPDATE deltas carried);
`WSMOUSEIO_GGESTURE`; the defaults with all sysctls off (no timer, no
traffic). `imt_doubles.h` is shared with `t_imt.c`.

```sh
CC=clang sh sys/dev/i2c/tests/run-imt-gesture.sh
# red run against the pre-review sources (a checkout of the sys/ tree):
LEGACY_SYS=/path/to/old/sys sh sys/dev/i2c/tests/run-imt-gesture.sh
```
