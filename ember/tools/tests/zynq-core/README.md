# A9 timer and Zynq UART contracts

Run the production attach, clock update and receive interrupt functions in a
host register model:

```sh
python3 ember/tools/tests/zynq-core/run.py
CFLAGS='-fsanitize=address,undefined' python3 ember/tools/tests/zynq-core/run.py
CC=gcc CFLAGS=-fanalyzer python3 ember/tools/tests/zynq-core/run.py --output /tmp/zynq-core-check
valgrind --leak-check=full --error-exitcode=1 /tmp/zynq-core-check/timer
valgrind --leak-check=full --error-exitcode=1 /tmp/zynq-core-check/uart
```

The runner extracts function bodies and ring macros from the selected source.
`--timer-source` and `--uart-source` accept a previous driver revision for a
causal regression check. `--compile-only` prepares analyzer inputs without
execution. The default temporary directory is removed after the run.

The timer cases check registration while a separate timer owns hardclock,
frequency discovery through the parent, deferred frequency discovery, and
frequency updates with and without global timer interrupts. Registration must
follow counter enable and must not recur when hardclock starts.

The UART cases enumerate ring capacity and wrap positions, retain unread bytes,
and require a full software ring to leave the interrupt handler despite unread
hardware FIFO data. A bounded register-read count turns an old infinite loop
into an assertion failure. The existing signed `cc`/unsigned high-water compare
remains a visible compiler warning; the contract does not alter driver types.

These are software contracts with mocked bus operations and kernel services.
They do not establish counter frequency, IRQ routing, UART wiring, physical
flow control, SMP scheduling, or execution on a board. Target builds use the
project's GCC16 toolchain separately.
