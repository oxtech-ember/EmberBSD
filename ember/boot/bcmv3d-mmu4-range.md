# Private retained MMU4 ranges

This mechanism publishes and unmaps three fixed translation windows in the
same native MMU4 owner. It does not publish an HVS list, add a KMS caller,
complete a producer fence or establish active scanout retirement. The current
HVS publication API accepts only the initial IOVA 0. The private windows at
8, 16 and 24 MiB have never been passed to that API.

The caller first uses `bcmv3d_mmu4_native_stage_gem`. The caller holds a live
actual BCMV3D GEM reference. Staging proves full SG DMA and pmap correspondence
and prepares exactly 2025 read-only 4 KiB leaves. It does not change live PTEs.

`bcmv3d_mmu4_native_publish_staged_gem(native, slot)` requires the same
successful, sealed initial native owner. It revalidates actual mappings and
owned table contents. It uses the existing local mutex, then the retained
ICAC executor. It never resets the initial observation, execution or program
guards. The retained executor does not initialize a new core or change leases.
It keeps the initial execution result. A live callback or protocol failure
latches a provider fault and prevents later mutation.

Before a first top PTE store, the backing retains the actual BO, consumer map
and both L2 allocations. It uses aligned 32-bit little-endian atomic stores.
Only the selected top pair changes. The full candidate top image is not copied
because it also contains other staged slots. The initial config, plan, image,
frame, default page and L2 pages keep their original contracts.

The new L2 allocations receive full native-page PREWRITE. The top receives
POSTWRITE before the CPU update and full native-page PREWRITE after it. The
same pattern applies to reachable L2 updates. Native control padding stays
zero. Both 4096 and 65536 native page sizes use 4096-byte hardware tables.

The shared cache completes its existing CONTROL 3 protocol first. Completion
tests only FLUSHING bit 2. The local MMU then receives CONTROL `0x04090807`:
ENABLE and the established abort/statistics policy remain set, with TLB_CLEAR.
A native bus-space barrier precedes the local poll. The local poll tests
TLB_CLEARING bit 7 and the established control/error fields. Both shared and
local polls allow at most 1024 returned samples. Exhaustion returns ETIMEDOUT.
A native bus stall or abort cannot be bounded by this software poll.

The numeric protocol comes from Raspberry Pi Linux
`a1a601d4857978d810175365550c4536701df4ed`,
`drivers/iommu/bcm2712-iommu.c` and `bcm2712-iommu-cache.c`.
This BSD mechanism uses full local invalidation for each fixed window.
The Linux 16 MiB choice between full clear and 16 KiB shootdown lines is a
performance heuristic. No Linux implementation is copied here.

`bcmv3d_mmu4_native_unmap_gem(native, slot)` clears that slot's leaves and
completes both invalidations. It then releases the actual consumer map and GEM
reference outside the ICAC mutex. The six L2 allocations remain resident after
their first linking: 24 KiB at native 4 KiB pages or 384 KiB at native 64 KiB
pages. Their top links remain valid. A later stage reuses those exact
allocations and maps. It does not allocate another L2 pair per frame.

Discard still releases only never-published resources. It cannot free a live
slot or resident L2. Discard of a new, unreachable BO staged in an empty
resident pair releases that BO and consumer map, then keeps the table pair.
Initial sealed backing remains retained until reboot.

Any post-exposure failure keeps the actual BO, maps and L2 allocations alive.
This includes a shared/local timeout, provider postcondition failure or final
status snapshot failure. The first operation error takes precedence over a
later diagnostic error. Cached range diagnostics record the operation, slot,
PTE stores, syncs, polls, barriers and returned control value. These fields do
not grant access or prove display readiness.

This unmap API applies only to these never-HVS-latched windows. An active or
pending HVS frame needs a separately implemented commit lifetime and retirement
mechanism. A changed active-list value, vblank sample, empty channel or TLB
completion alone is not a retirement predicate. A caller boolean cannot supply
that missing mechanism. Producer fences and consumer data cache sync also
remain part of future HVS submission, not table publication.

Run the actual-source joined contract:

```sh
ruby ember/tools/bcmv3d-mmu4-range-contract.rb
```

The contract uses complete native/provider/cache/sequencer source bodies and
an actual BO/backing/helper/table TU. Its controlled kernel service bridge is
the native renderer boundary; it is not a physical device. It verifies two
cycles for all three slots, stable L2 allocation identity, real BO reference
release, independent PTE contents at PREWRITE, exposure latches before stores,
initial backing preservation, provider gates, error precedence and retention.
The initial native owner and resident tables survive each isolated boot.

CC/CFLAGS select the host compiler and sanitizers. RANGE_PAGES selects native
page sizes. RANGE_EVIDENCE records inputs and commands. RANGE_SKIP_MUTANTS=1
runs positives only. Each source mutant compiles before it must fail through
CHECK and SIGABRT. Separate crash, bare-abort and timeout controls cannot count
as source rejection. Host contracts do not establish hardware translation,
visible pixels, display latency, HVS retirement or Plasma/KMS operation.

## Manual boot check

`EMBERV3DCONSOLE` runs `bcmv3d_mmu4_check_ranges` after successful fixed HVS
publication and its two progress samples. The same actual renderer and native
owner remain held. Giant stays fully released for this sleepable check.
The check stages three new contiguous native BOs, publishes all three private
windows, then unmaps them. It repeats that cycle once. Caller references are
released after staging. Successful completion requires six BO reservations,
six releases and unchanged reserved bytes and failure count. The six resident
L2 allocations are not BO quota. Their identity is checked by the joined
range contract. No userspace or competing BO allocator may run during this
manual boot check.

A first allocation, stage, publish or unmap error stops further publication.
Discard runs only for slots successfully staged by this check. Existing backing
rules refuse release of exposed or uncertain storage. A check failure does not
rewrite the separate fixed HVS publication result. The serial diagnostic starts
with `boot MMU4 unused ranges`. It reports actual return codes and BO accounting.
It does not prove an HVS fetch through these private windows or scanout retirement.
Default profiles do not select this check TU or call it.

Run the actual check caller with controlled BO/native services:

```sh
ruby ember/tools/bcmv3d-mmu4-check-contract.rb
```

The caller contract covers all 24 failure positions, reference transfer,
post-exposure retention, one-shot use and four independent accounting defects.
Nine source controls must compile and fail through CHECK/SIGABRT. The separate
`bootfb-serial-handoff-contract.rb` composes the actual boot owner and checks
HVS success, complete Giant release, exact owner arguments, one call and error
isolation. Its native check service is controlled. The full range contract
executes the actual provider/backing/BO implementation separately.

`CC` and `CFLAGS` select the compiler and target options. `TEST_RUNNER` selects
an execution prefix. `MMU4_CHECK_COMPILE_ONLY=1` builds without execution.
`MMU4_CHECK_OUTPUT` preserves the target binary. `MMU4_CHECK_EVIDENCE` must name
a new directory. `MMU4_CHECK_SKIP_MUTANTS=1` runs only the positive contract.
ASan/UBSan checks are host execution. They are not a native kernel memory test.
Hardware acceptance used source `9afdceeb7f093a60cd5de7d517588ea727ed4637`.
The manual profile completed two cycles with six publications and six unmaps.
All six BO references and reservations were released. The six L2 tables stayed
resident. The initial HVS color frame remained correct. Four native V3D
regression programs passed with unchanged reserved BO bytes and no failures.
This checks the unused-window protocol with active initial HVS output.
It does not prove HVS fetch through these windows or active scanout retirement.

The separate [BO reservation helper](bcmv3d-bo-fences.md) provides checked
lock, wait and fence publication primitives. Renderer submission does not use
it yet. It does not change this range lifetime contract.
