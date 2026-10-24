# ADI AXI DMAC finite-transfer contract

Run from the source root:

```sh
sh ember/tools/tests/adi-axi-dmac/run.sh
ANALYZE=yes MEMCHECK=yes sh ember/tools/tests/adi-axi-dmac/run.sh
RACECHECK=yes sh ember/tools/tests/adi-axi-dmac/run.sh
CFLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all' \
    sh ember/tools/tests/adi-axi-dmac/run.sh
```

The host harness compiles the actual driver through its interrupt handler.
It substitutes register, memory, interrupt and clock services. It checks
byte-width decoding, FIFO and AXIS TX, FIFO RX, matching IRQ completion,
memory ownership, allocation errors and failed-drain quarantine.
The DMA model tracks each map's PRE/POST direction, length and ownership.
It rejects duplicate PRE operations, unpaired POST operations, undrained
submitted ranges, and CPU copies or unmapping before ownership returns.
The CV model checks completion before the first wait and EOT while asleep.
It checks enqueue-before-unlock and requires the IRQ interlock at each signal.
Unrelated IRQs and DONE without EOT must not wake a waiter. Spurious wakes
must consume one fixed timeout budget. Tick wrap and an EOT racing with
timeout return are covered.

The pthread cases use real mutexes and condition variables. They hold one
transfer at IRQ wait while another thread fills an inactive buffer. They
check active-buffer rejection, serialized transfers, stop, failed-drain
quarantine, and copy ordering against transfer/free/channel release.
A barrier after drain checks that ownership lasts until result publication.
A process alarm bounds deadlock regressions.
CPU access after shutdown is rejected. Kernel scheduling is outside this
host test.

TX preparation is optional. A prepared transfer consumes the earlier PREWRITE
and skips cache preparation at submission. Tests cover repeated preparation,
length changes, read/write/free cancellation, invalid calls, unchanged RX,
timeouts, unknown engine ownership, and failed-drain retention.
A pthread case prepares and cancels buffer B while buffer A is active.
It then checks that A's completion drain preserves B for the next transfer.
Explicit stop and shutdown cancel unused preparation after successful drain.

The large transfer case uses 2,080,000 bytes with a 24-bit DMA length field.
The AXIS TX case requires TLAST; FIFO TX does not use a packet marker.

`ANALYZE=yes` runs Cppcheck and GCC `-fanalyzer`. `MEMCHECK=yes` uses Valgrind
Memcheck. `RACECHECK=yes` selects Valgrind Helgrind instead.
`CC` and `CFLAGS` select the host compiler. The host must support pthreads.
This is a host test; the kernel must also be cross-built for its target. FDT attachment and real AXI timing,
cache coherency, downstream output and hardware drain remain board checks.

The consumer must supply positive AXI drain acknowledgement after CONTROL
is disabled. EOT proves finite DMA completion, not downstream analog output.
No test replaces that consumer's hardware drain implementation.

For exact no-SG version 4.5.c, the driver requires CONTROL and SUBMIT zero
and DBG_STATUS bits 11:0 equal to 0x701. This reset-manager state proves
that outstanding memory responses have drained. Equal transfer IDs do not
prove drain after disable: hardware resets both IDs immediately. The host
fixture models that early reset and delayed reset-manager completion.
A stripped debug register, SG state, stalled clock or deadline expiry fails
closed. Other versions still require the external drain provider.

Shutdown can retain a converter guard after a failed initialization. For exact
4.5.c, PMF can record a separate reset-only drain certificate after disabling
submission and observing the hardware witness. This does not return CPU
ownership or free any guard, mapping or buffer. The final hook checks that
certificate and the live hardware again. Quarantine, pending operations,
nonzero buffer ownership or unavailable locks still prevent reset.
The polling budget also expires when hardclock interrupts have stopped.
The fixture covers retained ownership, rejected teardown, frozen ticks,
PMF lock contention and final-hook fault cases in separate child processes.
