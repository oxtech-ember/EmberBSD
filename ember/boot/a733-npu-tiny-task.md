# A733 fixed native NN task

This interface selects one trusted NN profile. It does not accept user tensors,
commands, device addresses, or a model file. The source and host tests define the
contract. The fixed INT8 profile has bounded
[physical acceptance](#physical-acceptance).

## Before a run

Use a matched kernel, modules, and device tree. Start from a cold boot. Complete
the normal NPU identification stages and the retained CCU/PCK checks first. The
observer identity is PID `0x1000003b`, revision `0x9202`. The retained PID name
reports the vendor CID, also named CUSTOMER_ID at register `0x030`. It is not the
PRODUCT_ID register. TASK admission checks the full identity below.

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
checks fresh clock/power ownership and cold NPU state. It reads model `0x9000`
twice before any product or ECO register access. It then captures two complete
identity samples. Each sample starts and ends with the model register.

Only this exact identity admits the fixed INT8/non-BLT profile:

| Field | Register | Required value |
| --- | --- | --- |
| Model | `0x020` | `0x9000` |
| Revision | `0x024` | `0x9202` |
| Product | `0x0a8` | `0x05090009` |
| ECO | `0x0e8` | `0x08000000` |
| Customer | `0x030` | `0x1000003b` |

Every read must succeed. Both samples and their model brackets must be stable.
Unknown identities refuse before DMA publication. No revision mask or default
HWDB entry is used. The source-pinned `vip9000nano-di` entry supplies eight INT8
cores and no BLT engine. These are HWDB capabilities, not register samples.
Read masks retain successful partial reads after a failure. `task_identity_valid`
means that two full identity samples were stable. It does not mean that the
identity was accepted. `task_profile_valid` is the separate capability decision.

The driver also samples raw minor feature 0 at `0x034`. Its MORE bit guards raw
minor feature 5 at `0x0a0`. With MORE clear, minor5 is not read and its raw validity
stays clear. With MORE set, both minor5 samples must be stable and BLT bit31 must
be clear. A raw register does not replace the exact identity requirement.

The 144-byte stream uses only the fixed image. It retains the native context,
DMA maps, and immutable heap references before the first cache sync. The
commands, image, and numerical oracle do not depend on a model file.

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
- `task_identity_valid=1`, `task_profile_valid=1`, `task_capability_source=1`,
  `task_int8_cores=8`, and `task_blt=0`. Source 1 means the pinned HWDB entry.
- Both `task_identity_read_maskN` values are `0x3f`. The six fields in each
  `task_identity_sN_*` sample are model, revision, product, ECO, customer, and
  `model_after`. Both model fields are `0x9000`.
- Both `task_feature_read_maskN` values are 3 when MORE is clear, or 7 when MORE
  is set. Bits 0, 1, and 2 record successful raw model, minor0, and minor5 reads.
  `task_features_valid` keeps its raw-completeness meaning. It is zero with MORE
  clear. A cached minor5 zero with bit2 clear is unread, not a measurement.
- Comparison validity/pass, final attempt/validity, and `task_model_pass` are set.
  Final ACK is zero.
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

## Physical acceptance

The [acceptance record](a733-npu-tiny-task-acceptance.json) reports one fixed
INT8 task on an A733 board with GCC 16.2.0 `EMBER64` kernel `8735c9333e50`.
The 144-byte stream uses six retained DMA buffers. Native comparison verifies
all 512 numerical output bytes and the full 4096-byte output page, including
its 3584-byte poison tail. It also checks every read-only buffer and tail and
both complete MMU table references. All six mismatch counts are zero.

The returned task reaches step 13 with error zero, IRQ count 1 from baseline 0,
and exactly event 4. Comparison validity/pass, final validity and model pass
are set. Terminal and final idle samples are `0x7fffffff`. The final IRQ masks
and ACK are zero. The context and DMA remain retained until reboot.

Two complete stable identity samples match all five required values above.
Both read masks are `0x3f`. The pinned HWDB entry supplies eight INT8 cores and
no BLT engine. Raw minor0 is zero, so MORE is clear. Raw minor5 is **unread**;
`task_features_valid=0` reports incomplete raw features, not failed identity.
The cached minor5 zero is not a measured register value.

GPU/display peer fields and display cached data remain unchanged through
this task. One GPU Compute after NN completion then verifies all 16 words on
the same boot. The
NPU resources remain retained. All 133 TASK nodes, GPU and display cached
state, and the eight cohort and four system-file hashes remain unchanged.
This check does not run NN and GPU workloads simultaneously.

The NN acceptance window lasts 31.389 seconds. It includes host checks and is
not an NN execution-time benchmark. The maximum sampled temperature is
53.196 degrees Celsius for that window and 54.002 degrees Celsius for the
subsequent GPU check. These are sampled values, not continuous measurements.
The PCB revision and bootloader firmware version were not recorded.

This result accepts only the fixed profile. It does not accept arbitrary
models, model frameworks, a general inference API, fault injection, sustained
stress, physical HDMI or a complete Plasma session.

## Source checks

`python3 ember/tools/a733-npu-task-contract.py` checks the actual MMU, image,
DMA, FE, run, and TASK sources with a simulated device. The simulated device is
part of the test only. The native driver never copies expected output into the
DMA output page. `python3 ember/tools/a733-npu-fe-contract.py` also checks actual
consumer selection, cached nodes, permanent legacy revocation, and the old FE
profile. `python3 ember/tests/a733-npu-observe.py` runs the complete NPU source
control group. These host checks do not establish a physical NN result.

## Capability provenance

The register locations and identity reads use the
[pinned vendor hardware source](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/npu/aw_nna_galcore/kernel/arch/gc_hal_kernel_hardware.c#L178).
The finite capability facts use the
[exact HWDB entry](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/npu/aw_nna_galcore/kernel/inc/gc_feature_database.h#L320993).
Its full source SHA256 is `e067acd20f09b3fc50f742d1e86ec07687d20c702ad59d73559a896abce3a6ea`.
Both files permit the MIT license. The native admission does not use the
vendor query's informal-release revision wildcard. Host tests prove this
closed software contract. The [physical acceptance](#physical-acceptance)
separately records complete product/ECO capture and the fixed numerical result.
