# Native HVS scanout owner

The explicit BCM2712 serial-handoff profile creates one retained native HVS
consumer after CPU console close, firmware acknowledgement, four clock leases,
dual-pipeline stop, initial MMU4/HVS publication and the real PV0 IRQ route.
The default render profile does not select this consumer.

This mechanism prepares native GEM objects and publishes HVS lists. It does not
register DRM/KMS objects or start a physical Plasma session. Kernel linking,
controlled source contracts and physical display acceptance are separate checks.

## Resources and format

The canonical boot owner reserves SRAM words32 through72 and UPM words0
through62463, slots0 through3. The initial IOVA0/list32 stays pinned. Three
mutable slots use IOVA8/16/24MiB, heads42/52/62 and disjoint UPM ranges. Head72
is a permanent empty list with black background. Empty output keeps timing
running; this is not complete connector DPMS.

The admitted plane is fixed1920x1080, XRGB8888, linear, pitch7680 and offset0.
Capacity and actual native ownership are validated. Resource metadata, an empty
framebuffer registry or a disabled-channel bit cannot grant ownership.

## Prepare, submit and complete

`bcmv3d_scanout_reserve` retains the native GEM object and its original writer.
It publishes a real reader fence without waiting for rendering. It allocates
the actual HVS consumer map and publishes unexposed MMU4 translations. It leaves
the slot RESERVED. It does not perform consumer PREWRITE or access HVS.

`bcmv3d_scanout_finalize` waits for the captured original writer outside owner
mutexes. It checks the writer's fence status, rechecks sticky owner faults,
performs consumer PREWRITE and changes the slot to READY. It allocates no storage.
Call this phase from a commit worker that can wait for rendering. A failed
finalize leaves its software reference with the caller. Put cancels a known
never-exposed RESERVED or READY slot after successful translation removal.
A failed or uncertain cleanup retains the reader and mapping.

`bcmv3d_scanout_prepare` combines reserve and finalize with the existing bounded
writer wait. Repeated reservation of a RESERVED, READY, PENDING or CURRENT object
retains its original lease before reservation locking. An in-progress reservation
or a retiring instance of that object returns EBUSY before reservation locking.
This prevents a cycle with a writer holding the BO reservation while it waits
for the old reader. A reused RESERVED object keeps its original dependency.

Submission exposes the consumer before the first HVS write. It writes and
verifies only the inactive list, then publishes its pointer. Actual active-head
snapshots establish the latch. The same-current submission uses the captured
native event sequence, so a delayed older callback cannot complete it.
Callback binding and its initial snapshot share the native PV0 mutex. Allocation
delays cannot make that baseline stale before the first callback.

The previous list retires only after another observed HVS frame progression.
A process worker must claim that exact retired frame. The real native MMU4
path validates its identity and state before translation removal. It completes
cache invalidation, consumer POSTWRITE, DMA unload and map reference release
before the reader fence can signal success. Linked L2 tables stay resident.
No caller-supplied completion boolean authorizes an exposed range.

After successful map cleanup, the worker commits read completion under the
state mutex. A fault before that commit retains and poisons the lease. A later
fault cannot revoke the completed retirement of an already detached BO.
Only that committed lease may finish outside the IRQ mutex. Current and pending
leases remain quarantined. The fault remains sticky and prevents reuse or
further display MMIO. No timeout authorizes retirement or recovery writes.

The frame-count rule follows the pinned Linux reference described in the
[flip protocol](bcmv3d-flip.md). It is not an independent AXI-drain measurement.
Mappings, clock leases, callback storage and hardware owners survive to reboot.
There is no detach or reset API.

`bcmv3d_scanout_wait_retired` waits for an old GEM reader without holding
native, engine or reservation locks. The caller keeps its frame reference.
After the fence completes, a process-lock recheck confirms FREE state,
committed completion and fully cleared reader/map ownership. This also covers
a fence signal before the worker finishes reclaim. It consumes no caller ref.
CURRENT/PENDING frames are refused. Linux restart status is translated to BSD
EINTR; invalid error values become EIO. Timeout never authorizes cleanup.
The API does not cover the reserved empty-list slot.

`bcmv3d_scanout_wait_blank_ready` checks that reserved slot separately.
READY or the same CURRENT blank can be submitted when no publication is
pending. RETIRING, RETIRED and RECLAIMING wait for the worker. The worker
broadcasts readiness or fault under the same process-lock interlock.
No native IRQ lock is held during the condition-variable sleep. One bounded
tick budget spans spurious wakeups and counter wrap. BSD ERESTART becomes
EINTR; timeout becomes ETIME. The API changes no BO lease or mapping.
It provides readiness only. The caller must serialize the later submit.

## Reproduce the source checks


```sh
ruby ember/tools/bcmv3d-scanout-contract.rb
ruby ember/tools/bcmv3d-flip-contract.rb
ruby ember/tools/bcmv3d-vblank-native-contract.rb
ruby ember/tools/bcmv3d-mmu4-range-contract.rb
ruby ember/tools/bootfb-serial-handoff-contract.rb
```

The scanout test executes the actual owner, flip and list-encoder bodies with
controlled MMU4, DMA, reader and kernel services. It checks123 isolated boots,
53 display I/O fault positions, delayed callbacks, slow construction, same-BO
lifetimes, deferred writer completion, cancellation of prepublished translations
and faults on both sides of the completion commit. Retirement waits check
lock release, worker recheck, fence lifetime and target errno boundaries.
The reserved-slot tests cover CV lifetime, its interlock, wakeups, one timeout
budget, signal errors and fault delivery. Forty-two compiled source controls
must fail a CHECK. A crash or timeout is not a valid rejection.

The range test executes the actual native adapter, provider, BO and backing
code. Its controlled HVS proof tests identity refusal, successful retirement,
cache timeout retention and fresh-slot isolation. The scanout contract tests
the actual owner proof separately. This split does not claim physical DMA.

`CC`, `CFLAGS`, `TEST_RUNNER`, `SCANOUT_COMPILE_ONLY`, `SCANOUT_OUTPUT` and
`SCANOUT_EVIDENCE` support cross builds and retained evidence for the owner
test. `SCANOUT_SKIP_MUTANTS=1` selects its positive memory-check run. The GEM
staging test has its own executable and separate evidence directory.

The optional [KMS layer](bcmv3d-kms.md) passes physical modeset and repeated
double-buffer page flips. Physical accelerated KWin, full hotplug and DPMS
remain separate gates. Controlled source tests do not establish those scenarios.

The separate [explicit cycle probe](bcmv3d-scanout-probe.md) exercises three
native BOs through the existing owner. It preserves the final CURRENT BO and
reports strict cached lifetime and allocation accounting. Its host contract
and module build do not establish physical display or desktop acceptance.
