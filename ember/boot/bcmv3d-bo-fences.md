# Native BO reservation helper

The [helper](../../sys/external/bsd/drm2/bcmv3d/bcmv3d_resv.c) provides a
checked reservation and producer-fence contract for up to 64 native GEM BOs.
It does not access GPU or display registers. Renderer submission does not use
this helper yet. TFU/CSD/CL still publish only their existing explicit syncobj
fences. This source slice does not add implicit BO synchronization or KMS.

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
execution. Native renderer integration needs its own tests and board acceptance.

`CC` and `CFLAGS` select compiler and target options. `TEST_RUNNER` supplies an
execution prefix. `RESV_COMPILE_ONLY=1` builds without execution. `RESV_OUTPUT`
preserves the binary. `RESV_EVIDENCE` must name a new directory.
`RESV_SKIP_MUTANTS=1` and `RESV_SKIP_HARNESS=1` select positive sanitizer runs.
Host ASan/UBSan execution is not a native kernel memory test.
