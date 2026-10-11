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

The deliberately invalid destination mapping must cause a real MMU fault.
The VIO client and decoded address must identify TFU accessing
that alias range. A fault flag alone is insufficient. The following valid
job must complete once, with an idle TFU, correct data and intact protected
buffers. DMA synchronization separates device ownership from CPU inspection
and page-table changes.

CM5 uses the V3D 7.1 AXI client map: low seven ID bits, TFU `0x7c`/`0x7d`.
The initial guard incorrectly used the older 4.1 map. A regression now
replays the physical `0x7d` result, checks all 256 low-byte values and a
high-bit variant. The old guard fails this fixture. The corrected contract
initially passed 337 cases, 645 checks, 20 causal mutants and ASan/UBSan. This corrects
fault identification; it does not establish recovery of the next GPU job.

Failure diagnostics preserve raw MMU control, TFU state, violation identity
and violation address, including register-read failures. They help distinguish
a stuck engine, repeated fault and bad mapping on the next hardware run.
They do not establish the cause of the earlier physical failure.

The first physical fault-recovery boot remains **unaccepted**. Its recorded
interrupt mask `0x30` denotes PTI plus WRV (bits 4 and 5), not PTI plus CAP.
CAP is bit 3. That correction does not identify why the following job failed.

### Physical guard check on 2026-10-11

Source `f7317164ae5ecd8a306ece0bada643771b36400b`, cross-built with GCC 16.2.0,
booted on the same CM5 with kernel SHA256
`0f9eb0e4f90a206e84dc6dee3f4cbe6e49dc9c86e4930ac0b27007bb5faa7342`.
The new client/address guard accepted TFU `0x7d` inside the destination alias.
There was one fault event (PTI+WRV), no TFUC and an idle TFU, but CVTCT
advanced from 0 to 1. The original counter guard therefore stopped before
recovery. The saved capture SHA256 is
`88949e69a035d5079fcf82e6acdd10df64e79a8b3487eed6fb843c4be9ba20f0`.
The board was returned to its tested ordinary kernel with Wi-Fi recovery.

V3D 7.1 counts this failed conversion too. The corrected guard requires
one retirement after the fault, then a total of two after the good job,
including eight-bit wrap. A counter alone never establishes successful work.

A diagnostic follow-up on the same network-driver baseline, source
`5a9111e68aa41cc1b5469d8dc038778184596b84`, recorded `HUB_STS=0x32`:
the fault ISR received PTI+WRV (`0x30`), then TFUC appeared under the mask.
TFU was idle, CVTCT advanced once and only one ISR ran. This disproves the
earlier assumption that a faulted conversion cannot signal terminal TFUC.
Kernel SHA256:
`fb5b28df57fd1c7fe2141363e5f366d58430ac8709b09fc2869901c3692a749e`;
capture SHA256:
`8a975671dada9cbf1ea2748cd737bda7660db5a93e13a498c058d23710975ba8`.
The board returned to its tested ordinary kernel after this failed probe.

The corrected contract requires the failed job's terminal TFUC before rearm,
either in its first ISR or in masked status within a bounded wait. The
controller mask and cross-call barrier freeze the first-job event baseline.
Only this initial phase permits the recorded fault bits and an undelivered
TFUC. The fault identity, unchanged buffers, idle TFU and one retirement
are still mandatory. Source MMU W1C precedes the final owned HUB acknowledge.
The post-fixup flush cannot acknowledge a new fault. Before job 2, HUB and MMU
must be clean; afterwards exactly one new ISR/TFUC and a second retirement
must accompany verified output and intact protected memory.

Regressions cover the physical `0x32`, delayed terminal indication, new faults,
changed violation identity, extra ISR deliveries and baseline/teardown races.
350 cases, 674 checks, 24 causal mutants and ASan/UBSan pass. Physical
acceptance of recovery with this revised retirement model remains pending.

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

## Recorded host validation

On 2026-10-11, clean source commit
`be475f24bb0ac4fe07beda01d73b642d9a417142` passed the portable contracts
and cross-builds on macOS/ARM64 with GCC 16.2.0. The IRQ contract passed
81 cases, 339 checks and 17 causal mutants; the fault contract passed
81 cases, 134 checks and 20 causal mutants. Both also passed ASan/UBSan.
The build-wrapper regression verified GPU contract dispatch and refusal
to publish artifacts after a contract failure.

| Configuration | ELF kernel SHA256 |
| --- | --- |
| `EMBERV3DQUEUE` | `eb6697b859c5ab0cfa2a68b858d179fc384d53abe84123138a91b9dcf60c31f5` |
| `EMBERV3DIRQ` | `226675f7e58d63f885398cbae5e4e8650ff5746126f7df2b8ad08e88b0f75d7c` |
| `EMBERV3DFAULT` | `c83216b46969b5a5ebd69cfe95b099989801cc0aa9232462be0fd5f708f97947` |
| `EMBER64` | `00b056c8b3bc1b7800ff5c2e0fca692970ad6bdedeef6c4a0010a17a14638587` |

All four builds passed artifact, matching-module and source/compiler
provenance checks. The ordinary kernel contains the observer and no
experimental probe entry points. These corrected kernels have not yet
been accepted on the physical CM5; MMU recovery remains unaccepted.
