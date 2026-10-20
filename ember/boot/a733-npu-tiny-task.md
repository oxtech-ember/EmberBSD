# A733 fixed native NN task

This interface selects one trusted NN profile. It does not accept user tensors,
commands, device addresses, or a model file. The source and host tests define the
contract. A physical numerical NN result is not accepted yet.

## Before a run

Use a matched kernel, modules, and device tree. Start from a cold boot. Complete
the normal NPU identification stages and the retained CCU/PCK checks first. The
accepted identity is PID `0x1000003b`, revision `0x9202`.

Do not prepare or run the old `engine_stage` profile on this boot. Its prepare
step selects FE. A TASK prepare selects TASK. The selected profile cannot change
until reboot. A failed selection also remains selected. Native requests cannot
change the clocks, power, or memory claims of a peer GPU or display consumer.

The nodes below use the actual NPU device name, for example `hw.sun60inpuid0`.
Only the normal sysctl root policy permits writes to `task_stage`. All other
TASK nodes are read-only. Reads copy cached values. They do not read live NPU
registers or DMA data.

## Prepare and run once

Write `task_stage=1` once. This constructs the fixed image and allocates six
buffers. It does not set up an IRQ, publish DMA, or access NPU registers. The
first TASK attempt permanently closes legacy `READ_REG`, including on failure.
It does not restore the old readiness flag. Nonzero observer stages also refuse
requests after a native profile is selected.

A valid prepare has `native_profile=2`, `task_attempted=1`,
`task_prepared=1`, `task_result=0`, and `task_release_error=0`. The run,
comparison, final proof, and model success fields remain zero. The fixed counts
are 144 command bytes, 864 constant bytes, 256 input bytes, and a 4096-byte output
page. Their device VAs are `0x01001000`, `0x01002000`, `0x01003000`, and
`0x01004000`. The root table, STLB, and four data pages occupy 36 KiB. Each loaded
DMA map has one non-bounce segment below 4 GiB. All 15 physical buffer pairs are
disjoint. The command PA must differ from its VA.

Write `task_stage=2` once, only after prepare succeeds. The closed coordinator
checks fresh clock/power ownership and cold NPU state. It reads the model twice,
then the MORE feature bit, then minor feature 5. The samples must be stable.
Model `0x9000`, MORE set, and BLT clear admit the selected command tail. The
144-byte stream uses only the fixed image. It retains the native context, DMA
maps, and immutable heap references before the first cache sync.

A transport event alone does not prove NN success. Completion must bind the
exact run, DMA context, and immutable references. Only this capability permits
POSTREAD. The comparison checks all 512 numerical output bytes, the 3584-byte
poison tail, every read-only buffer and tail, and both full MMU table references.
The existing MMU builder defines those references.

After comparison, a final fresh proof reads IRQ mask, idle, command DMA, idle,
command DMA, IRQ mask, then ACK. These reads and `model_pass` publication have
one IRQ lock. A late error must fail even when the IRQ handler is no longer
armed. The consumer never derives model success from `compare_pass` alone.

## Required terminal evidence

Require all of the following in the same returned run:

- `task_result=0`, `task_run_error=0`, and `task_run_step=13`.
- Publication, retention, start, terminal, and raw-snapshot validity are set.
- A new IRQ contains exactly event 4. IRQ fault and terminal ACK are zero.
- Terminal and final idle samples are `0x7fffffff`. Each DMA pair is equal and
  points inside the owned command VA page. Both final IRQ masks are zero.
- Feature validity, comparison validity/pass, final attempt/validity, and
  `task_model_pass` are set. Final ACK is zero.
- All six `task_mismatchN` fields are zero. Each `task_firstN` has the bit pattern
  `UINT32_MAX`. An INT sysctl can print this value as `-1`.

DMA addresses and `task_run_irq_count` / `task_run_irq_baseline` use 64-bit QUAD
nodes. VAs, fixed byte counts, features, raw samples, and comparison/final fields
use 32-bit INT nodes. The run IRQ count must exceed its baseline.

A valid raw snapshot or comparison does not by itself prove model success.
`task_stage=0` is a dry request. It does not reset a stage, profile, error
quarantine, or retained context. A repeated run refuses before hardware access.
Do not retry, reset, disable clocks, free buffers, or select FE after a TASK run.
All published assets remain retained until reboot, including after failure or
timeout. A failed constructor cleanup preserves its context pointer and cached
release error; the consumer cannot run that context.

## Source checks

`python3 ember/tools/a733-npu-task-contract.py` checks the actual MMU, image,
DMA, FE, run, and TASK sources with a simulated device. The simulated device is
part of the test only. The native driver never copies expected output into the
DMA output page. `python3 ember/tools/a733-npu-fe-contract.py` also checks actual
consumer selection, cached nodes, permanent legacy revocation, and the old FE
profile. `python3 ember/tests/a733-npu-observe.py` runs the complete NPU source
control group. These host checks do not establish a physical NN result.
