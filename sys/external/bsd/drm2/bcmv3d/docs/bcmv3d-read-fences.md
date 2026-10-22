# BO consumer read leases

The [read helper](../bcmv3d_read.c) retains a
BO's original prior writer and publishes a shared pending reader fence. It retains
an actual GEM reference. Each read has a distinct context, including context zero.
A later canceled or failed read cannot replace an older reader in dma_resv.
The native HVS owner uses this helper. DRM/KMS integration remains separate.
This helper does not establish hardware retirement.

Use private zeroed `bcmv3d_read` storage. Keep its address stable. Serialize
operations on each lease. Supply an actual validated live GEM reference.
Use process context without engine/native hardware-owner or BO reservation
locks. DRM modeset locks follow their core rules. The positive tick timeout
bounds the prior-writer wait, not lock acquisition or allocation. A positive
wait result still requires successful fence status. Errors use positive DRM
errno, including internal `ERESTARTSYS`.

`bcmv3d_read_reserve` captures the exclusive writer under the BO reservation
lock. It publishes its shared reader without waiting for rendering.
`bcmv3d_read_wait` waits only for that retained writer. A newer reservation
snapshot cannot replace this dependency. A successful wait drops the prior
fence reference. A failed wait keeps it for retry or known cancellation.
Wait does not acquire the reservation lock or allocate storage.

`bcmv3d_read_begin` combines reserve and wait. On a writer error, it cancels its
never-started reader. A signalled shared fence can remain in the reservation
until its next update. This fence does not hold the canceled reader's GEM ref.
After successful wait, prepare the actual consumer DMA data and hardware state.
Call `bcmv3d_read_end` only after real hardware retirement and consumer DMA/map
cleanup, or after a known never-started cancellation. It signals without
reacquiring the BO reservation. A GPU writer may already hold that reservation
while it waits for this read. A copied lease cannot wait on or release it.

`bcmv3d_read_poison` signals an error but retains the BO, fence and lease until
reboot. End refuses poisoned storage. The actual consumer must also retain its
mappings and hardware resources. Its admission policy must bound active leases.
The embedded fence lock is destroyed after an RCU grace period, with the CV and
allocation. It cannot outlive its coallocated storage.

Run `ruby ember/tools/bcmv3d-read-contract.rb`. It executes actual read and
producer helpers, the common GEM WW lock/unlock bodies, and native shared/
exclusive publication functions. GEM, WW, fence, DMA-free and RCU services are
controlled. Cases check overlapping readers, copied leases, poison retention,
writer timeout/error/restart, allocation/lock/reservation failures, deferred
lock destruction and closed caller references. A joined sequence completes a
reader while the writer holds the actual WW reservation tuple, publishes a new
producer and starts the next read. Deferred reserve cases check the original
writer after reservation replacement, timeout, error, retry and cancellation.
Fifteen controls compile before exact named
failures reject them. This is not a native scheduler, RCU or hardware DMA test.

`CC`, `CFLAGS`, `TEST_RUNNER`, `READ_COMPILE_ONLY` and `READ_OUTPUT` select the
cross-build and execution path. `READ_EVIDENCE` preserves exact inputs and
commands in a new directory. Use `READ_SKIP_MUTANTS=1` for a positive sanitizer
or supported Valgrind run. Host ASan/UBSan does not execute kernel memory code.

The [producer contract](bcmv3d-bo-fences.md), consumer map synchronization,
HVS list lifetime and DRM/KMS commit events remain distinct contracts.
