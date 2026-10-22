# Native BO reservation helper

The [helper](../bcmv3d_resv.c) provides a
checked reservation and producer-fence contract for up to 64 native GEM BOs.
The helper does not access GPU or display registers. Native TFU/CSD/CL
submission now uses it for implicit BO producer ordering. Every admitted job
creates a native producer fence, including when `out_sync` is zero. The same
fence is attached to every listed BO and to the optional output syncobj.
This source integration does not add KMS.

The caller keeps actual GEM references and an unchanged pointer array alive.
Objects must have unique non-null reservations. Start with zeroed private
`bcmv3d_resv` storage. Use process context. The lock order is BO reservations,
then the engine. Never call prepare while the engine lock is held.

`bcmv3d_resv_prepare(state, objects, count, timeout)` calls the existing
`drm_gem_lock_reservations`. It retains all acquired locks on success.
It finalizes the WW acquire context on a lock error. It captures referenced
exclusive and shared fences through `dma_resv_get_fences_rcu`. It waits for
both groups. It checks each completed fence status. A positive wait result
alone cannot hide a negative fence status. All waits share the returned
remaining tick budget. Lock acquisition itself is interruptible, without
that timeout. Snapshot arrays and fence references are released on every path.
A failed prepare releases all locks and its acquire context.
Errors use positive DRM errno. The internal `ERESTARTSYS` code is preserved.
The existing DRM ioctl adapter converts it to the native syscall restart result.
A non-DRM caller must provide the same translation.

`bcmv3d_resv_publish(state, fence)` requires one pending actual producer fence.
The caller keeps its reference and controls its producer. Before the first
reservation change, the helper validates all held reservations and their exact
WW acquire-context identity. A copied context is refused. It attaches the same
exclusive fence to every BO. This conservatively treats all listed BOs as
writers. It performs no submission, completion or register operation.

Call `bcmv3d_resv_unlock` before starting the producer. Completion and failure
must signal the actual fence without reservation locks. Existing signaled
fences can remain referenced by a BO until the next reservation update or BO
release. The helper owns no GEM reference. Direct CPU stores are outside this
contract. Consumer read fences, pixel DMA sync, HVS submission and hardware
retirement remain separate requirements. A signaled error does not prove DMA
drain or authorize release of uncertain storage.

Run the actual-source contract:

```sh
ruby ember/tools/bcmv3d-resv-contract.rb
```

The contract executes the complete helper and actual common GEM lock/unlock
bodies. WW mutex, reservation snapshot and fence services are controlled.
It checks 64 BOs, all 1152 injected lock/snapshot/wait fault positions, empty
and single-group snapshots, remaining budget, failed fence status, references,
complete ownership preflight and error cleanup. Each source control compiles
before a named CHECK and SIGABRT reject it. Crash, bare abort and timeout controls
cannot count as source rejection. This does not prove kernel scheduling or GPU
execution. Native renderer integration has a separate actual-source contract. Board
acceptance remains separate from these source and compilation checks.

`CC` and `CFLAGS` select compiler and target options. `TEST_RUNNER` supplies an
execution prefix. `RESV_COMPILE_ONLY=1` builds without execution. `RESV_OUTPUT`
preserves the binary. `RESV_EVIDENCE` must name a new directory.
`RESV_SKIP_MUTANTS=1` and `RESV_SKIP_HARNESS=1` select positive sanitizer runs.
Host ASan/UBSan execution is not a native kernel memory test.

## Renderer integration

The [submission source](../bcmv3d_job.c) first
retains all explicit dependencies and BO references. It waits for explicit
inputs, then prepares the BO reservations outside the engine lock. Under the
engine lock, it checks admission and publishes one shared-timeline producer
fence to the reservations. It unlocks the reservations before starting GPU
work. Completion signals the fence after the existing cache, PTE revocation
and DMA checks. A failure signals an error. Uncertain execution retains the
existing poisoned engine and BO references until reboot.

TFU still rejects the same object as source and destination. CSD/CL still
reject duplicate objects. Objects with a shared reservation are also rejected.
All job BOs are conservatively treated as writers. This can serialize readers.
It does not add asynchronous scheduling or change the render ioctl ABI.

Run `ruby ember/tools/bcmv3d-job-resv-contract.rb` for 51 actual caller cases
across TFU/CSD/CL. The fixture executes actual BO, job, reservation and common
GEM lock bodies. Controlled services check that every hardware start sees the
same pending fence on both BOs with all reservations released. Cases include
no output syncobj, consecutive producers, lock/snapshot errors at both BOs,
prior reader/writer failure, timeout, interruption, `ERESTARTSYS`, allocation
failure, sequence exhaustion, invalid payload and poisoned ownership.
Ten compiled source controls require exact named failures. Host ASan/UBSan
checks execution. These are not a native scheduler or hardware test.

`V3D_RESV_EVIDENCE` preserves raw inputs and commands in a new directory.
`CC`, `CFLAGS`, `TEST_RUNNER`, `V3D_COMPILE_ONLY` and `V3D_OUTPUT` select the
cross-build and execution path. The existing job/CSD/CL/sync/wait shell
contracts also support that cross-build path and retain their previous tests.
Use `V3D_SKIP_MUTANTS=1` for positive instrumented execution.

The [shared GEM framebuffer allocation contract](../../drm/docs/drm-gem-framebuffer.md)
checks unused plane initialization and matching allocation cleanup before KMS
uses that helper. Producer ordering alone does not establish scanout lifetime.

The [consumer read helper](bcmv3d-read-fences.md) provides a separately tested
shared-fence lease. Its HVS/KMS binding and actual retirement remain pending.
