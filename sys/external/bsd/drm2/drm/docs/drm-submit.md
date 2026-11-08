# Shared submission boundary

PowerVR currently uses a private serial workqueue in `powervr/pvr_submit.c`.
Its admission limits, syncobj producer search, dependency capture, waits, cancellation and drain remain driver-local policy.
Moving the fence-chain status operation to the common layer does not complete this scheduler transition.

The existing `drmkms_sched` implementation is the shared scheduler owner.
A direct queue replacement cannot preserve the current submission contract:

| Existing scheduler behavior | Required contract for this consumer |
| --- | --- |
| `drm_sched_entity_add_dependency_cb()` skips dependencies from the same entity and substitutes scheduled for finished fences | Preserve strict completion dependencies and their errors before execution |
| `drm_sched_process_job()` signals finished without copying the hardware fence error | Propagate execution errors and cancel dependent batch outputs |
| `drm_sched_entity_push_job()` requires common serialization with job initialization | Preserve publication order when preparation completes out of order |
| Entity destruction can call `free_job` from a fence callback | Run sleeping context cleanup outside fence locks |
| `drm_sched_fini()` stops the scheduler thread and destroys its locks | Close admission, quiesce submitters, cancel queued work and drain active execution |
| Timeout recovery can resubmit jobs | Require explicit backend recovery; do not replay commands with uncertain DMA completion |

Extend this existing owner with explicit contracts before switching the consumer.
Keep its current clients' dependency optimizations unchanged unless they select the new semantics.
Common code must own bounded admission, byte accounting, producer lookup and all-input-before-output capture.
It must also own dependency deadlines, cancellation and drain through backend callbacks.

PowerVR retains command validation, firmware encoding and the exact `FENCE_PR` pairing rule.
Its execution units are compute, transfer, an atomic geometry/fragment pair, and an optional final fragment.
Context, HWRT and VM references remain retained through retirement.
VM submission holds continue to exclude mapping changes until that work retires.
An error fence does not prove DMA stopped. Existing quarantine retains uncertain tables, mappings and BOs.

## Next software acceptance

Build a host contract around the real `sched_entity.c`, `sched_main.c` and `sched_fence.c` with a modeled backend.
Then connect the existing `pvr-submit-contract` to that common implementation.
Required cases include:

- Pending and failed completion dependencies within and across entities of one scheduler.
- Binary WAIT+SIGNAL preserving the previous payload, and malformed final jobs publishing nothing.
- Preparation outside queue order, concurrent publication and exact `FENCE_PR` pairing.
- Independent final-fragment completion after a paired partial render.
- Allocation failure, interruption and timeout releasing each reservation exactly once.
- Close during an external wait cancelling work before DMA; MAP/UNMAP barriers preserving ownership.
- Stop during active DMA retaining uncertain memory and completing every published output with a defined status.
- Cancellation and free callbacks performing sleeping cleanup outside fence locks.

Switch PVR, its tests and kernel/module dependencies together, then remove the replaced private queue and policy paths.
Physical firmware, DMA and sustained-operation acceptance remain separate from these software contracts.
