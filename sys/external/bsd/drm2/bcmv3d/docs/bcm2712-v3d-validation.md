# BCM2712 V3D acceptance guards

The opt-in experiments have separate hardware ownership and completion guards.
Host contracts exercise source behavior and do not establish physical GPU support.
Current hardware applicability belongs to [DevBoards diagnostics](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/bcm2712-cm5/diagnostics.md)
and the [graphics profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/bcm2712-cm5/graphics.md).

## Stop after a failed stage

The observer's actual-source contract covers the finalizer as well as register
observation. It exercises takeover, DMA, queue, IRQ, fault and render configurations.
An observation or takeover failure must not start an experiment. A DMA failure
must not start the queue: a timed-out TFU job can still own the current MMU.
Repeated finalization must not rerun any stage.

The compiler rejects probe options without takeover, a queue without DMA,
and polling/IRQ/fault/render combinations that would share hardware ownership.

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

The deliberately invalid destination mapping must cause a real MMU fault.
The VIO client and decoded address must identify TFU accessing
that alias range. A fault flag alone is insufficient. The following valid
job must complete once, with an idle TFU, correct data and intact protected
buffers. DMA synchronization separates device ownership from CPU inspection
and page-table changes.

V3D 7.1 uses the low seven AXI client ID bits; TFU is `0x7c` or `0x7d`.
The older 4.1 map is invalid here. The regression covers all 256 low-byte
values and a high-bit variant. PTI and WRV are bits 4 and 5; CAP is bit 3.
Failure diagnostics preserve raw MMU control, TFU state, violation identity
and address, including register-read failures. Diagnostics alone do not identify a cause.

### Retirement and terminal event guards


V3D 7.1 counts a failed conversion retirement. Require one retirement after the fault and two after the following good job, including eight-bit wrap.
A retirement counter alone never establishes successful work. A faulted conversion can also signal terminal TFUC.
The terminal TFUC must appear in its first ISR or masked status within a bounded wait before rearm.
Masking and the cross-call barrier freeze the first-job baseline. Fault identity, unchanged buffers, idle TFU and one retirement remain mandatory.
Source MMU W1C precedes the final owned HUB acknowledgment. The post-fixup flush must not acknowledge a new fault.
Before job 2, HUB and MMU must be clean. Exactly one new ISR/TFUC and a second retirement must accompany checked output and protected memory.
Regressions retain the observed HUB_STS 0x32, delayed terminal indication, new faults, changed identity, extra ISRs and teardown races.

### Fault write sink

For the fixed invalid-destination payload, the write sink receives the final
128 source bytes. The contract checks that exact pattern and surrounding canaries.

The sink behavior is documented in pinned Linux
[v3d_drv.h](https://github.com/torvalds/linux/blob/a5ebb76233b79db01e82e061173cb73c6d2b5c6b/drivers/gpu/drm/v3d/v3d_drv.h):
invalid MMU reads and writes are redirected to scratch. The sink must not be required to remain zero after a deliberately invalid write.

The experiment surrounds the sink with two nonzero canary pages.
For this payload only, the first 32 sink words must equal the final 32
source words; every other sink word remains zero. Different fault write
patterns are unsupported. The sink is reset only after
successful fault verification and MMU recovery, while CPU-owned. The good
job must leave the reset sink and both canaries intact. This is a bounded
payload check, not a general guarantee about MMU burst size or DMA isolation.

Reissue all eleven TFU input fields before each start, matching the pinned
Linux `v3d_tfu_job_run()` sequence. The fixture decodes submitted registers
and rejects incomplete fresh input. Resubmission write failures are covered.

### Bounded recovery scope


Current hardware evidence and its applicability belong to the [DevBoards profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/bcm2712-cm5/graphics.md).
One invalid-destination fault followed by one fixed TFU copy does not qualify repeated/concurrent recovery, bin/render faults or endurance. The opt-in probe retains allocations until reboot.

### IRQ and queue scope


Current hardware evidence and its applicability belong to the [DevBoards profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/bcm2712-cm5/graphics.md).
Successful IRQ and DMA-to-queue chains do not substitute for injected duplicate/error-race contracts or arbitrary failure recovery.

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
