# Fixed HVS frame memory preparation

This private helper prepares one unexposed 1920x1080 XRGB8888 frame. Its logical
size is 8294400 bytes and its pitch is 7680. It uses the existing native contiguous
`bcmv3d_bo_alloc` constructor. It does not change CREATE_BO's separate SG policy.
This is a source/host contract. No physical HVS DMA or scanout is accepted here.

## Private ownership contract

`bcmv3d_hvs_prepare(sc, consumer_dmat, out)` leaves `out` unchanged on error.
The caller uses process context, serializes access and holds the render/device
reference and consumer tag until destruction. The constructor supplies one GEM
reference. The helper keeps it and creates a separate one-segment consumer map
with `BUS_DMA_WRITE`. It checks the actual page-rounded backing size, GEM size,
original-map geometry, consumer mapsize and complete segment length. Size rounding
uses `PAGE_SIZE` explicitly, as the BO constructor does. BSD `round_page` uses
`PAGE_MASK`, whose meaning changes after Linux DRM headers; it is not valid here.
The complete
consumer span must fit 32 bits; base zero and exclusive end 2^32 are valid. This
also satisfies the conservative 36-bit HVS bound. No HVS address alignment is
invented. PAGE alignment belongs to the existing BO constructor.

The helper reads the address only from the new consumer map. It does not copy
`bo->gpu` or the original map address. Numeric addresses can coincide. The finite module explicitly
uses `sc->dmat`, the current GPU0 parent tag. This proves CPU/DMA-map preparation
only. It does not establish an HVS parent/tag, IOMMU configuration, accessibility,
clock, power, ownership or DMA-address-path contract.

The module fills and reads back a deterministic 32-bit XRGB pattern across the
whole backing allocation, including page-rounding padding. It then calls
`bcmv3d_hvs_prewrite` for all backing bytes. PREWRITE is cache synchronization,
not publication or drain. It checks reserved-byte accounting before and after
under the external prerequisite that no concurrent GPU clients allocate BOs.

`bcmv3d_hvs_destroy` unloads and destroys the separate map before exactly one
constructor GEM put. All cleanup APIs are ordinary void operations. The module
drops the render minor last. No job, descriptor, LPTR, fence, IRQ or callback sees
the frame. Calling this destruction API after any hardware publication attempt
is invalid. A future persistent display owner needs its own proved stop/drain
contract. No fake drain flag, POSTWRITE inference or fault registry is added.

## Explicit module

`bcmv3d_hvs_prepare` is built only on explicit request. It has no default kernel
configuration, automatic attachment or new kernel API. Its Makefile reuses the
selected configured kernel's actual DRM flags and headers. Set
`EMBER_KERNEL_OBJDIR` to that matching kernel object directory. Audit the new
frame-helper C/H, MMU4 backing C/H, table encoder C/H, MMU4 corevar header,
module and Makefile as source exceptions. Audit every selected-S include against
the selected kernel; its pathname alone does not prove compatibility. All existing native
interfaces, generated options and strong symbols must match the selected kernel.
The [sampled ABI helper](../tools/bcmv3d-hvs-prepare-abi.c) supplies an additional
configuration/layout comparison, not a complete compatibility proof.

The module requires `prepare-only=true` and an exact verified kernel buildinfo.
For example, after a coherent build and source/symbol audit:

```sh
: "${kernel_commit:?Set the verified selected kernel commit}"
modload -b prepare-only=true -s "expected-buildinfo=EmberBSD:${kernel_commit}" \
    ./bcmv3d_hvs_prepare.kmod
modunload bcmv3d_hvs_prepare
```

The normal INIT cycle is finite and leaves no frame behind. Wrong properties, interrupt context,
framebuffer/console presence or unverified absence, wrong render/GPU0 identity and
invalid display resources refuse before allocation. It rejects all display-probe
mode properties. The operation does not map display resources, access MMIO, query
or change clocks, invoke PHY/IOMMU owners, submit DMA, or expose a GEM handle.
No compatibility claim permits use of a retained PHY owner's references.

## Optional MMU4 backing cycle

Add `mmu4-prepare-only=true` and `expected-mmu4-debug` to the required properties
for an explicit MMU4 memory cycle. The supplied DEBUG is only an encoder profile.
It is not a live register sample, an access grant or an MMU ownership claim.
The module calls the [native backing helper](bcmv3d-mmu4-backing.md), checks its
cached completion, complete-page counts and five PREWRITE operations, and checks
BO accounting before and after destruction. It does not seal or program backing.
The plain pattern cycle remains the default when both added properties are absent.

```sh
: "${kernel_commit:?Set the verified selected kernel commit}"
: "${mmu4_debug:?Set the verified encoder profile in decimal}"
modload -b prepare-only=true -b mmu4-prepare-only=true \
    -i "expected-mmu4-debug=${mmu4_debug}" \
    -s "expected-buildinfo=EmberBSD:${kernel_commit}" \
    ./bcmv3d_hvs_prepare.kmod
```

Require the successful preparation and equal before/after accounting messages.
If private cleanup refuses, INIT retains the backing, render reference and module
code. Its return0 then preserves lifetime; it is not a successful preparation.
FINI returns `EBUSY`. There is no retry or force-unload recovery. Keep these
objects until controlled reboot. Constructor and clean pre-publication errors
otherwise release their owned data and return the original positive errno.
This cycle proves native allocation, PA/map correspondence and synchronization
only. It does not prove hardware translation, HVS DMA, cache accessibility,
firmware handoff, clocks, power, physical scanout or drain.

## Actual-source checks

Run `ruby ember/tools/bcmv3d-hvs-prepare-contract.rb /path/to/EmberBSD`.
The runner compiles the actual helper and module bodies and the unchanged native
BO constructor/free. It extracts the native BSD `round_page` and Linux
`PAGE_MASK` definitions to
keep their header interaction in the actual-helper regression. A causal mutant
restores the unsafe expression. It reuses the existing BO fixture services with
two explicit
adaptations: guarded PAGE_SIZE and a controlled DRM device shape for the module.
It links actual libfdt. AML, DRM lookup, bus DMA, allocation and reference services
remain controlled boundaries. Source hashes and compile commands can be saved
with `HVS_PREPARE_EVIDENCE` to a new directory. The tests use a consumer address
that differs from both original DMA address and V3D GPU VA.

Tests cover 4 KiB and caller-selected page rounding, complete32-bit span limits,
geometry, positive errno conversion, every allocation/map failure, output commit,
pattern/PREWRITE, exact map/GEM/minor cleanup, process/property/framebuffer guards
and focused causal mutants. Host sanitizers and static analyzers do not establish
physical HVS or DMA behavior. See the [HVS core](bcm2712-display-hvs-core.md) for
separate list, address-path and hardware-ownership boundaries.

The module fixture also controls only the new backing API boundary. It checks
mode/type rejection, constructor/prepare/status failures, complete geometry,
error precedence, BO cleanup and retained render/code lifetime. Retained cleanup
cases run in separate child processes without restoring uncertain map metadata.
The backing helper's own fixture tests its actual implementation separately.
