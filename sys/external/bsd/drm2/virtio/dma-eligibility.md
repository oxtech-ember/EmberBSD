<!-- Origin: EmberBSD; AI-assisted owned ARM64 DMA eligibility contract. -->
# Owned backing eligibility

VIRGL remains disabled. This driver-local gate prepares admission of owned
backing for a future coherent 3D device. It does not enable negotiation,
prove live topology, or implement context-wide DMA synchronization.

`virtgpu_dma_arm64.c` checks an already loaded linear map. It accepts only
standard native ARM DMA create/destroy/load/unload and both sync methods,
with no may-bounce hook or IOMMU. Every tag range must be coherent,
unambiguous and free of address overflow. A preallocated idle bounce cookie
is allowed; actual bouncing is rejected. Other architectures and unknown
backends return EOPNOTSUPP when eligibility is required.

The predicate independently walks KVA through native pmap extraction and
owned shmem pages. It checks every page/range/segment boundary against the
loaded bus-address stream. Static address translation and adjacent/coalesced
ranges are allowed when the entire stream matches. `_ds_paddr` is not used:
the standard loader can store an already translated address in that field.
Segment capacity is checked against the actual create capacity before array
access; sizes, lengths, overflow and complete tails must match exactly.

## Owned aliases and admission

Before any container conversion, the wrapper checks the core object,
VirtGPU device, exact object functions, absence of imported backing and
native shmem pager. It then requires the retained pin and vmap, the exact
owned vmap address, and the native shmem page array. The current helper uses
PAGE_KERNEL and its pager creates normal cached mappings. A false pmap
coherency result is a consistency check of this owned policy; it is not a
generic cache-attribute classifier. NC kernel aliases cannot qualify by
setting the map's coherent bit. Changing these owned mapping operations
requires revisiting this contract.

Object creation passes `params->virgl || vgdev->has_virgl_3d` to backing
attachment. Thus a future 3D device also checks dumb/2D objects. The current
2D-only device preserves its ordinary backing and PRIME behavior. The
predicate runs after load, before PRE, wire submission or publication of
`obj->pages`. Geometry, wire-size checks and entry allocation share this
pre-PRE boundary. A local rejection unloads/destroys the unpublished map,
unmaps, frees SG storage and unpins exactly once, without a POST.

An ACKed CREATE still owns its host ID even if backing was never submitted.
Its existing UNREF/reset retirement releases the ID. After PRE, all errors
retain the published map/pin until that retirement; they never use the early
local unwind. `sg_free_table` owns DMA-map destruction only after the map is
published in the SG table. Qualified backing now follows the separate
[lease contract](backing-lifetime.md). Its request metadata allocation also
precedes PRE. Eligibility becomes true only after successful
backing ATTACH and is cleared by detach. No supported API reloads this
backing; future reload support needs an explicit generation contract.

GEM open checks retained eligibility before both duplicate-handle counting
and new CTX_ATTACH. Same-device PRIME and flink handle creation use this
callback; existing PRIME cache hits reuse an already admitted handle.
Foreign backing remains unsupported. EXEC's optional BO list is not the
complete set of reachable backing and is not used as an admission substitute.

## Checks and remaining gates

From the repository root:

```sh
sh ember/tools/virtgpu-dma-contract.sh
sh ember/tools/virtgpu-dma-integration-contract.sh
DMA_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/virtgpu-dma-contract.sh
DMA_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/virtgpu-dma-integration-contract.sh
```

The MD fixture extracts actual predicate bodies and native ARM structure
layouts. Native method addresses, pmap extraction and page objects are
controlled seams. Fifty-six vectors cover accepted translations/coalescing,
idle bounce, overflow, overlap, holes, crossing, capacity, tails, methods,
IOMMU/bounce, cached-alias consistency and independent physical-page identity.

The integration fixture shares the resource contract and extracts actual
owned wrappers, backing attachment/unwind, object creation, GEM open and
core handle/PRIME paths. Its MD result is a controlled boundary: arithmetic
is tested separately by the actual predicate, not duplicated in this model.
Eleven groups cover gate rejection before PRE/ATTACH/CTX_ATTACH, required=false,
future 3D dumb backing, duplicate/PRIME admission, early wire failures,
ACKed CREATE retirement, after-PRE errors and retained-map publication.
Allocation/map/pin/reference and PRE/POST counters verify conservation.

`RESOURCE_SOURCE_ROOT=/absolute/baseline-export` selects older production
integration bodies. The unchanged baseline fails seven integration groups;
the new wrapper-only group is explicitly skipped because that helper did not
exist. Host GREEN and ASan/UBSan pass the MD and integration groups.

The committed export also passes all 56 MD vectors and 11 integration groups
on NetBSD/AArch64 with GCC 16.2, together with the 97 existing driver groups.
A regenerated VirtGPU trial configuration and dependencies and thirteen fresh VirtGPU
objects pass with the base GCC 12.5 and the existing `-Werror` policy.
This is native contract/compilation evidence: no complete kernel was linked,
installed or booted, and actual native loaded-map inspection remains pending.

Firmware coherence flags do not prove physical topology. Before feature-on,
additional work must establish immutable negotiation/probe policy, inspect
actual maps, synchronize all context-reachable backing (including encoded
EXEC/query/copy/attach-detach access), and verify no-clobber and live results.
The [completion foundation](completion-lifetime.md) and this gate do not
establish those DMA phases or GPU acceleration.
