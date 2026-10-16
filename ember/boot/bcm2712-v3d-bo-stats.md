# Native V3D BO allocation diagnostics

The native renderer exposes a read-only allocation snapshot at
`hw.<device>.bo_stats`. The device name is the ACPI renderer's `device_xname`,
for example `bcmv3d0`. An absent node means this diagnostic is unavailable.
The interface does not change quotas, allocate GPU storage, or read MMIO.

## Meaning and ownership

`reserved_bytes` is the current device-wide budget reservation. It includes
internal page tables and scratch storage, userspace BOs, and allocations still
inside bus_dma. `highwater_bytes` is the largest reservation since attachment.
Neither value is a measurement of physical residency or process memory.

The existing limits remain 16 MiB per BO and 64 MiB per device. The internal
4 MiB page table and three scratch pages consume this budget. Reservations
start after GPU virtual-range allocation. They end on allocation rollback or
final GEM release. A closed GEM handle can leave a mapping, PRIME reference,
cache entry, or submitted job alive. A handle trace cannot replace this snapshot.

`reservations` and `releases` count these transitions. `failures` counts each
failed allocation or handle publication once. `failures_by_stage` identifies
request validation, quota, GPU VA, DMA storage, CPU mapping, DMA map creation,
DMA map loading, segment geometry, or GEM handle publication. Counters saturate
at `UINT64_MAX`. They never wrap or reset through this interface.

The last-failure fields contain the requested and rounded byte counts, positive
native errno, stage, and current reservation when the failure was recorded.
For post-reservation failures this includes the allocation that will unwind.
A later snapshot can have lower current usage. Concurrent later failures can
replace the last-failure record; compare stage counters around a bounded test.
No PID or physical address is exposed.

The allocator mutex protects every counter and the complete snapshot. The
sysctl handler releases that mutex before user copyout. It registers only after
successful DRM registration. A partial sysctl registration removes its nodes.
The existing driver has no hot removal: the device and diagnostic owner live
until reboot. A future detach implementation must remove and drain sysctl nodes
before it destroys the mutex or frees the owner. This patch does not add detach.

## Read the snapshot

The v1 native-endian structure is declared in
[`bcmv3d_bo_stats.h`](../../sys/external/bsd/drm2/bcmv3d/bcmv3d_bo_stats.h).
Its length is 192 bytes. Readers must validate the version, returned length,
structure length, stage range, and reserved fields. Do not decode it as text
or assume that an arbitrary future version has the same layout.

Cross-compile the reader against a recorded EmberBSD sysroot:

```sh
"$CC" --sysroot="$SYSROOT" -std=c11 -O2 -Wall -Wextra -Werror -static \
    ember/tools/bcm2712-v3d-bo-stats.c -o "$OUTPUT/bo-stats"
```

On a kernel with the diagnostic, use the registered device name:

```sh
./bo-stats hw.bcmv3d0.bo_stats
```

Record one snapshot before and one after the owned workload. Correlate failed
CREATE_BO requests with changes in stage counts. `quota` proves a budget refusal;
`dma_alloc` identifies native DMA allocation failure. Neither a low final value
nor a userspace close proves that usage was low at the earlier failure.

## Source checks

Run `ruby ember/tools/bcm2712-v3d-bo-stats-contract.rb`. It compiles the actual
allocator and sysctl adapter with modeled kernel services. It checks every
allocation-failure stage, unwind, shared quota, extra GEM references, read-only
copyout, registration failure, counter saturation and concurrent snapshots.
The actual reader rejects missing, truncated and incompatible snapshots.
Causal controls remove accounting, lock, write guard or teardown and must fail.

Run the existing BO and SG contracts as well. ASan/UBSan can check the executable
host models where Valgrind is unavailable. Host models and target compilation
do not establish a physical allocation cause or accept a complete desktop.
