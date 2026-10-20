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

`bcmv3d_scanout_prepare` retains the native GEM object and waits for its prior
writer outside the owner's mutexes. It publishes a real reader fence, stages
the actual HVS consumer map, performs PREWRITE and publishes the MMU4 range.
Repeated preparation of the current object retains its existing read lease.

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

## Reproduce the source checks

```sh
ruby ember/tools/bcmv3d-scanout-contract.rb
ruby ember/tools/bcmv3d-flip-contract.rb
ruby ember/tools/bcmv3d-vblank-native-contract.rb
ruby ember/tools/bcmv3d-mmu4-range-contract.rb
ruby ember/tools/bootfb-serial-handoff-contract.rb
```

The scanout test executes the actual owner, flip and list-encoder bodies with
controlled MMU4, DMA, reader and kernel services. It checks80 isolated boots,
53 display I/O fault positions, delayed callbacks, slow construction, same-BO
lifetimes and faults on both sides of the completion commit. Fifteen compiled
source controls must fail a CHECK. A crash or timeout is not a valid rejection.

The range test executes the actual native adapter, provider, BO and backing
code. Its controlled HVS proof tests identity refusal, successful retirement,
cache timeout retention and fresh-slot isolation. The scanout contract tests
the actual owner proof separately. This split does not claim physical DMA.

`CC`, `CFLAGS`, `TEST_RUNNER`, `SCANOUT_COMPILE_ONLY`, `SCANOUT_OUTPUT` and
`SCANOUT_EVIDENCE` support cross builds and retained evidence for the owner
test. `SCANOUT_SKIP_MUTANTS=1` selects its positive memory-check run. The GEM
staging test has its own executable and separate evidence directory.

DRM connector/CRTC/plane integration, real page-flip events, hotplug, DPMS and
physical accelerated KWin remain integration work. Passing a controlled source
test does not establish any of those user scenarios.
