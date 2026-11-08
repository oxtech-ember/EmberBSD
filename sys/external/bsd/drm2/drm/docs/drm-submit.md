# Synchronous batches on the DRM scheduler

`drm_sched_batch` is the native batch interface of `drmkms_sched`.
It runs on the existing scheduler's thread and entity queue, without a separate
workqueue. PowerVR uses it for compute, transfer and render submission.
The ordinary scheduler interface and its existing consumers retain their behavior.

## Ownership and admission

The common layer owns admission, byte accounting, syncobj references, producer
lookup, dependency capture, output fences, publication, waits, cancellation and
drain. Policy permits eight reserved batches per queue, 256 items per batch and
1 MiB charged memory per batch, including retained syncs, potential timeline
chains, output fences and the scheduler fence. A shared budget of 1,048,576 search
steps bounds producer lookup, including empty items. These are software policy
limits, independent of command-stream or firmware sizes.

The driver checks its allocation with `drm_sched_batch_size()`, reserves a slot
and allocates zeroed storage. Each successful reservation is released exactly
once: by the caller on rejection or by the common scheduler after retirement.
`drm_sched_batch_init()` binds an embedded batch and a strided array of embedded
items. Storage remains live through the release callback. Errors are positive
BSD errno values.

`sync_alloc()` allocates bounded normalized operations. The driver supplies
handle, value, binary/timeline and signal/wait after validating its UAPI flags.
`prepare()` retains syncobjs, allocates timeline nodes and creates independent
output fences. `capture()` retains all inputs before any output can replace a
binary payload. Every fallible hardware preflight and VM hold also precedes commit.

`commit()` serializes stopping checks, scheduler-job initialization, all output
publication and entity insertion under one sleeping mutex. Allocation failure
publishes nothing. No fallible operation remains after the first publication.
Success transfers ownership; the caller must no longer access the batch.
Rejection calls `cleanup()`, releases driver resources and unreserves.
The release callback cleans accepted batches without unreserving: the common
layer drops the reservation after the callback returns.

## Dependencies and execution

Items identify the first item of their contiguous atomic execution unit.
Units execute in publication order on one entity, with one synchronous batch in
flight. The worker waits for every member's dependencies before calling the
backend once for the first item. Success completes every output in that unit.
An error completes that unit and remaining batch outputs with the error;
previously completed outputs remain successful.

Producer lookup selects the latest earlier matching batch signal. Unpublished
self/forward dependencies return `EDEADLK`; an existing published payload may have
an external producer and is retained. A driver callback can declare an exact
matching signal from an earlier member of the same unit to be hardware-internal.
It cannot waive a mismatching timeline value or a dependency between units.

Strict completion/error waits use the
[fence-chain payload status](../../linux/docs/dma-fence-chain.md).
They never substitute scheduled for finished or skip a same-scheduler dependency.
The ordinary scheduler dependency callback returns no dependencies for batches;
the common batch worker performs these stricter waits. Synchronous backend results
become `NULL` or `ERR_PTR(-errno)` for the scheduler. Asynchronous scheduler
consumers retain their existing behavior.

Publication and completion waits have a ten-second deadline per item. Publication
polling also has a finite poll budget. Completion waits probe cancellation at short
intervals. The shared deadline helper supplies the same policy for driver resource
barriers. Interrupted admission returns `EINTR`. The driver's cancellation
callback observes retained state safely without execution gates.

Output fences have independent contexts because preparations can finish out of
order. Their names and release operations retain no queue pointer; users may keep
them after queue destruction. Signals occur outside driver gates, which execute
callbacks must release before returning.

## Stop and hardware retirement

`queue_stop()` closes admission. Admission waiters observe it on the next one-tick
probe; queued work observes it before execution or during dependency waits.
The owner guarantees queue lifetime for every API call, including the interval
before reserve registers its caller. Before `queue_destroy()`, exclude new calls
and quiesce entrants that have not yet entered reserve. Destroy then joins both
blocked admission callers and all reservations, including preparation and sleeping
release. It finalizes the empty entity and joins the scheduler thread before
freeing the queue. No job reaches entity destruction's fence-callback kill path.
Destroy runs outside the scheduler thread, without driver gates.
The queue name remains live through destruction.

Executing callbacks retire or quarantine hardware resources before returning.
The interface disables automatic timeout replay and never invokes scheduler
fault/resubmit recovery. An error fence is not proof that DMA stopped.

PowerVR owns validation, firmware encoding, context/HWRT/VM references, mapping
holds and quarantine. Its atomic geometry/fragment pair and exact `FENCE_PR`
exception are hardware declarations. A final fragment is a separate unit.
The callback repeats close/backend/VM checks under execution gates before DMA.
VM holds continue to exclude mapping changes until retirement.

## Checks and UI consumers

`ember/tools/drm-scheduler-contract.sh` executes real `sched_entity.c`,
`sched_main.c` and `sched_fence.c` with modeled host kernel boundaries.
`ember/tools/pvr-submit-contract.sh` executes these together with the real batch
layer, PVR submission and command decoder. The kernel contract runner invokes both.

The PVR syncobj UAPI used by Mesa render submission is preserved. Render,
composition and presentation consumers continue to wait on output completion
fences and receive execution errors. Admission count means accepted work, not
rendered or presented frames. This refactoring does not measure UI speed or qualify
fallback selection or presentation correctness. Firmware, DMA, compositor timing
and sustained-operation acceptance require target hardware tests.
