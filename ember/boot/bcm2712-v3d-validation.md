# BCM2712 V3D acceptance guards

The CM5 experiments are separate, opt-in kernels. The ordinary kernel only
observes the device. A successful host contract does not establish physical
GPU support, and the accepted hardware logs apply to their recorded kernels.

The 2026-10-11 review reproduced false success in the interrupt and fault
experiments. It also found that the combined DMA/queue configuration continued
after DMA failure. The guards and regressions below are required before using
these experiments as evidence for a DRM or Mesa implementation.

## Stop after a failed stage

The observer's actual-source contract covers the finalizer as well as register
observation. It exercises takeover, DMA, queue, IRQ and fault configurations.
An observation or takeover failure must not start an experiment. A DMA failure
must not start the queue: a timed-out TFU job can still own the current MMU.
Repeated finalization must not rerun any stage.

The compiler rejects probe options without takeover, a queue without DMA,
and polling/IRQ/fault combinations that would share hardware ownership.

## Interrupt completion and DMA integrity

An unsuccessful wait for binning completion must stop before render submission.
The interrupt experiment checks each completion event, TFU idle state and the
conversion counter. Duplicate or out-of-phase events cannot establish success.
Wait intervals are converted from milliseconds to scheduler ticks; a zero
tick timeout is never used.

Verification covers the TFU payload, both sources, the alias destination,
padding, scratch and the render output. It also checks the command lists
against CPU-side snapshots. A correct output image does not excuse writes
outside its permitted destination.

Teardown masks each established interrupt at the controller, drains any
in-flight handler with a normal-priority cross-call barrier, then removes
the handlers. The final verdict is checked after that drain. Regressions
inject delayed completion and handler errors at the barrier; neither may
produce a successful result.

## Fault identity and recovery

The deliberately invalid destination mapping must cause a fault without a
TFU completion. The VIO client and decoded address must identify TFU accessing
that alias range. A fault flag alone is insufficient. The following valid
job must complete once, with an idle TFU, correct data and intact protected
buffers. DMA synchronization separates device ownership from CPU inspection
and page-table changes.

Failure diagnostics preserve raw MMU control, TFU state, violation identity
and violation address, including register-read failures. They help distinguish
a stuck engine, repeated fault and bad mapping on the next hardware run.
They do not establish the cause of the earlier physical failure.

The first physical fault-recovery boot remains **unaccepted**. Its recorded
interrupt mask `0x30` denotes PTI plus WRV (bits 4 and 5), not PTI plus CAP.
CAP is bit 3. That correction does not identify why the following job failed.

## Repeat the checks

From a clean EmberBSD checkout, run:

```sh
sh ember/tools/bcm2712-v3d-contract.sh
sh ember/tools/bcm2712-v3d-takeover-contract.sh
sh ember/tools/bcm2712-v3d-dma-contract.sh
sh ember/tools/bcm2712-v3d-queue-contract.sh
sh ember/tools/bcm2712-v3d-irq-contract.sh
sh ember/tools/bcm2712-v3d-fault-contract.sh
sh ember/tests/build-kernel.sh
```

The fixtures compile the production bodies with fake host services. Negative
cases inject failed stages, extra or missing events and corrupted DMA memory.
Causal mutants must compile and then fail a behavioral assertion. Host
sanitizers can be enabled with `CFLAGS='-fsanitize=address,undefined
-fno-omit-frame-pointer'`. The kernel wrapper runs the contracts before
publishing outputs, including when cross-compiling.

Then cross-build `EMBER64`, `EMBERV3DQUEUE`, `EMBERV3DIRQ` and `EMBERV3DFAULT`
from a recorded clean commit. Preserve matching modules, source provenance
and artifact hashes. A later physical run must use the corresponding recovery
bundle and record its own result; previous acceptance cannot certify the
changed guards or MMU recovery.
