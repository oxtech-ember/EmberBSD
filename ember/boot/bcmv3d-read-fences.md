# BO consumer read leases

The [read helper](../../sys/external/bsd/drm2/bcmv3d/bcmv3d_read.c) waits for a
BO's prior writer and publishes a shared pending reader fence. It retains an
actual GEM reference. Each read has a distinct context, including context zero.
A later canceled or failed read cannot replace an older reader in dma_resv.
This helper is not connected to HVS/KMS yet. It does not establish retirement.

Use private zeroed `bcmv3d_read` storage. Keep its address stable. Serialize
operations on each lease. Supply an actual validated live GEM reference.
Use process context without engine/native hardware-owner or BO reservation
locks. DRM modeset locks follow their core rules. The positive tick timeout
bounds the prior-writer wait, not lock acquisition or allocation. A positive
wait result still requires successful fence status. Errors use positive DRM
errno, including internal `ERESTARTSYS`.

After successful `bcmv3d_read_begin`, prepare the actual consumer DMA map and
hardware state. Call `bcmv3d_read_end` only after real hardware retirement and
consumer DMA/map cleanup, or after a known never-started cancellation. It signals
without reacquiring the BO reservation. A GPU writer may already hold that
reservation while waiting for this read. A copied lease cannot release it.

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
producer and starts the next read. Thirteen controls compile before exact named
failures reject them. This is not a native scheduler, RCU or hardware DMA test.

`CC`, `CFLAGS`, `TEST_RUNNER`, `READ_COMPILE_ONLY` and `READ_OUTPUT` select the
cross-build and execution path. `READ_EVIDENCE` preserves exact inputs and
commands in a new directory. Use `READ_SKIP_MUTANTS=1` for a positive sanitizer
or supported Valgrind run. Host ASan/UBSan does not execute kernel memory code.

The [producer contract](bcmv3d-bo-fences.md), consumer map synchronization,
HVS list lifetime and DRM/KMS commit events remain distinct contracts.
