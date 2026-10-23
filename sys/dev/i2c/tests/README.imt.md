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
