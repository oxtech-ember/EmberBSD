<!-- Origin: EmberBSD native experimental VirtGPU integration, 2026-10-06. -->
# Experimental native VirtGPU

`EMBERGPU` replaces the existing framebuffer-only `viogpu` attachment with
native `virtiodrm` DRM/KMS. The ordinary `EMBER64` configuration is unchanged.
This is an experimental 2D integration, not a claim of a validated graphics
session. On 2026-10-06, revision `67a611acb9b` booted under UTM 4.7.5 with
virtio-ramfb and GL disabled. Native PCI discovery, 32 cross-process dumb
GEM/PRIME mapping lifetimes (one page and 8 MiB), malformed size/handle
rejection and visible 800x600 KMS color bars passed. labwc/Pixman displayed
a native Kate window without Xorg. The subsequent Ports input adaptation
passed physical pointer, keyboard and file-save checks documented in the
[runtime probes](https://github.com/oxtech-ember/EmberBSD-Examples/tree/main/desktop/wayland-utm).
This does not establish VirGL, reset stress or physical-board support.
Retest against each exact kernel and
virtual hardware configuration.
The [complete cross build and isolated QEMU boot](kernel-boot.md) on
2026-10-07 covers revision `c6aba7d1e240`, current libdrm PCI enumeration and
32 GEM/PRIME lifetime cycles. It excludes the firmware console handoff,
the UTM desktop and 3D acceleration.
The configuration disables default module autoload. Its VirtGPU, DRM,
Linux compatibility and VirtIO dependencies are built into the kernel;
do not load modules from a different build during the experiment.

The attachment requires modern VirtIO and negotiates no optional GPU
features. VIRGL, EDID, blob resources and context-init remain disabled.
The standard VirtGPU ioctl numbers and permission flags are retained;
unsupported 3D requests fail through their existing feature checks.
The retained 3D code passes native contracts and object compilation for
[asynchronous EXECBUFFER errors and private fence descriptors](submit-ownership.md).
The [explicit 3D transfer and WAIT contract](transfer-wait.md) adds directional
request phases and exact tracked errors. Its 59 new and 204 prior contract groups
pass on NetBSD/AArch64 with GCC 16.2; fourteen fresh driver objects compile with
base GCC 12.5 and normal `-Werror`. The linked
[controlled console and finite 2D contract](controlled-console.md) now pairs
legacy ATTACH/uploads and orders private kernel copies. Private GETFB refusal
precedes handle/VMA publication. All 57 new and 268 existing contract groups
pass natively with GCC 16.2, and fourteen fresh objects pass with base GCC 12.5
and normal `-Werror`. Focused host ASan/UBSan also passes. The complete kernel
now links and boots in the bounded [isolated check](kernel-boot.md).
Stable writable ordinary host RAM must
be qualified separately;
retained host bounce mappings remain unsupported. Complete 3D request bounds
and live qualification remain required before VIRGL can be enabled and tested.

## Provenance

The existing `dist/drm/virtio` import matches Linux `v5.6` after removing
NetBSD RCS/cdefs metadata and blank lines, except the original object file.
That file used a plain integer in place of upstream's atomic resource-ID
counter and omitted cached-map initialization. Native IDA allocation and
cached UVM mapping now supply those contracts.

The comparison covered Kconfig, Makefile, debugfs, display, driver, header,
fence, GEM, ioctl, KMS, plane, PRIME, queue and object sources. Original
licences, authors and NetBSD identifiers remain in place. Adaptations are
marked with an Origin line; this is not a new BSD licence on imported code.

The added `include/linux/virtio_gpu.h` is the BSD-licensed
[Linux v5.6 protocol header](https://github.com/torvalds/linux/blob/v5.6/include/uapi/linux/virtio_gpu.h).
It has only an additional Origin comment. The original file's SHA256 is
`1183f4b9eea4a8048ec704bfb8c578c00ab26d9e8157d799c88a1d6e161b9da6`.
The native GEM/UVM helper and local wait/limit adapters are newly written
BSD-licensed code. They do not import Linux's GPL shmem helper.

## Native ownership

The UVM helper wires native GEM pages until the final host-resource release.
Its pager takes ordinary GEM references and maps those pages. Native
`drm_gem_put_pages` unwires pages but does not free the page-pointer array;
the helper separately frees that allocation, including its error unwind.

Backing entries come from a retained linear `bus_dmamap_load` map's
`dm_segs`, rather than raw physical addresses. Finite ATTACH and each 2D upload pair PREWRITE
with POSTWRITE; they do not invalidate or copy back CPU memory with POSTREAD.
The DMA map and wire outlive a GPU-fenced RESOURCE_UNREF response. A failed
UNREF allocation/submission or invalid response resets the device before
local backing release. Disabling submissions alone never permits release. The
[owned ARM64 backing gate](dma-eligibility.md) checks loaded maps before PRE
when 3D admission is required; early local failures perform no POST.
For accepted backing, the [lease and retirement contract](backing-lifetime.md)
adds distinct bidirectional persistent and ATTACH/UNREF phases. Its host and
native contracts preserve backing through cookie drain and reset retirement;
fourteen fresh native driver objects compile with the normal warnings.
This source gate leaves feature negotiation disabled. The subsequent
[whole-context EXEC contract](exec-ownership.md) snapshots actual attachments,
retains independent operation ledgers and bounds dependency/storage ownership.
The [classic EXEC framing gate](exec-framing.md) rejects misaligned/truncated
copied packets before PRE, submission or output-fd publication. Its 28 new
and 218 affected prior groups pass natively with GCC 16.2; a fresh ioctl object
passes base GCC 12.5 with normal `-Werror`. Focused host ASan/UBSan also passes.
Format-aware access and truthful
host errors remain [activation obligations](exec-framing.md#host-obligations-before-activation).
The whole-context EXEC contract's 47 new and 132 prior groups pass natively
with GCC 16.2; fourteen fresh driver objects compile with base GCC 12.5 and normal
`-Werror`. Full kernel linkage now passes; live DMA and graphics qualification
remain pending beyond the isolated memory/ABI check.

All synchronous control operations request and validate a GPU fence,
including create, attach, transfer, scanout and flush. A returned descriptor
without the requested fence is an error, not proof that the GPU retired the
command. This follows the
[VirtIO 1.1 GPU completion contract](https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.pdf).
Cursor requests retain their GEM object until cursor-ring completion.

Primary-node mappings use native `drm_gem_mmap_object`. The direct native
PRIME hook accepts an object-relative byte offset and acquires exactly one
pager reference. It bypasses the generic PRIME helper's fake-offset hook.
Same-device fd/handle import uses the DRM core self-import path;
cross-device SG imports explicitly fail with ENODEV.

Native system work executes on one ordered worker. Control/cursor ACKs
therefore use a separate completion workqueue, and fatal cleanup has its
own workqueue. Reset seals admission and stops transport DMA before cleanup
joins producers and dequeue workers, then cancels outstanding cookies.
Qualified backing leases close after that drain. Terminal fences publish
only after lease retirement, before console/config/object
worker joins. The bounded per-cookie timeline and teardown contract is
documented in [completion lifetime](completion-lifetime.md). This foundation
does not qualify host memory mappings or live graphics. The
[controlled console contract](controlled-console.md) covers source ownership of
legacy 2D and private copies subject to that host qualification boundary.
Normal live detach is rejected
with EBUSY.

Classic wire fence IDs are bounded to `1..INT32_MAX`. The next allocation
seals the device and uses normal reset retirement without wrapping IDs.
The [range and exhaustion contract](classic-fence-range.md) passes 25 new
and 179 prior groups on the host and NetBSD/AArch64 with GCC 16.2. Fourteen
fresh objects compile with base GCC 12.5 and normal `-Werror`; full kernel
linkage now passes, while live reset qualification remains pending.

## Classic capset queries

Discovery and GET_CAPS use the existing fenced synchronous control path.
Discovery runs during initialization; cache requests run in ioctl threads,
never on the ordered completion worker. Initialization propagates discovery
errors and uses the ordinary deinit/reset/drain path before freeing its table.
The reset interlock publishes the retained pointer/count together and checks
the latched error before publishing the usable count. A reset during allocation
discards only the still-local table. No partially validated table is published.
Duplicate or zero IDs, zero or
oversized payloads and invalid response lengths/types/fences fail the query.
Unknown IDs remain discovery metadata; only classic VIRGL/VIRGL2 IDs 1/2
can be requested. A negotiated classic renderer with no classic capset fails
initialization. This does not enable feature negotiation or new context types.

The native local limits are 64 discovery records, 64 KiB per payload,
1 MiB of persistent cache including entry metadata, and 128 cache entries.
These are defensive support limits, not VirtIO specification constants.
Response sizes must also fit the transport's actual maximum request.
Unsigned 32-bit versions retain their ABI meaning; a large host maximum
cannot bypass the cache budget through repeated distinct user queries.
An exhausted budget returns ENOSPC; existing successful entries remain usable.

The response-wait mutex owns lookup, allocation budget and each result state.
Once published, a cache entry has one pending operation and a terminal success
or errno shared by concurrent callers. Failures before entry allocation leave
no shared operation. Later allocation/submission failures persist in the entry.
Timeout and reset wake waiters; late callbacks cannot overwrite a terminal
failure. Successful bytes become visible through the same mutex and then stay
immutable. Entries, including failed entries, live until deinit has stopped
transport and drained callbacks. GET_CAPS checks the helper result before
accessing a pointer and preserves the standard minimum-size copyout ABI.

`sh ember/tools/virtgpu-capsets-contract.sh` compiles the actual discovery,
cache, callback, ioctl, response-validation and synchronous-wait functions.
Allocation/transport fault injection and real pthread waiters cover malformed
host data, duplicate queries, reset, timeout/late completion, byte copying,
copyout failure and both aggregate limits. Host checks pass, including address
and undefined-behavior sanitizers. The five capset groups and nine existing
VirtGPU groups also pass on NetBSD 11/aarch64. The three affected kernel
objects compile with native GCC 12.5 and the normal `-Werror` flags; the KMS
object was rebuilt after the discovery/reset fix. A full clean kernel build
and isolated 2D boot now pass; live host capset checks remain pending.
These checks do not prove 3D
support or permit linking the partially updated object directory.

## Classic context lifetime

File open publishes its private context only after a successful CTX_CREATE
response with the requested GPU fence. Each command returns its own status;
an unrelated sticky submission error cannot report success or failure for it.
The ID allocator's exclusive upper bound keeps the wire ID and signed return
value representable. Command allocation failure releases the unsubmitted ID.
An error after entering the synchronous create path completes transport reset
before releasing the ID, conservatively including wait/fence allocation errors.

Postclose waits for fenced CTX_DESTROY. Failure completes reset before releasing
the ID and private state. The native reset mutex also joins an already running
reset; observing `vqs_ready=false` alone does not establish host retirement.
Open and postclose run in DRM file threads without a VirtGPU submission or
response lock held. The submission lock is released before response waiting. Completion
and reset cleanup retain their separate workers; these calls do not wait from
the ordered response worker or synchronously drain that worker.

`sh ember/tools/virtgpu-context-contract.sh` extracts the actual context commands,
open/postclose, synchronous waits, response validation, stop and native reset.
Host checks pass with address/undefined-behavior sanitizers. Allocation and
transport seams cover errors, malformed replies, concurrent ID reuse, delayed
publication, timeout/late cookies and reset overlapping ID retirement. They
model GPU execution and hardware reset. All six context groups, the five
capset groups and nine existing VirtGPU groups also pass on NetBSD 11/aarch64.
The changed KMS/VQ objects compile with native GCC 12.5 and normal `-Werror`
flags. A complete matched kernel build and live context checks remain pending.
GEM attachment and resource creation now have separate
[source ownership contracts](resource-lifetime.md), including duplicate handles
within one DRM file. VIRGL remains disabled; these checks do not establish 3D
acceleration.

## Build and checks

On a native NetBSD/aarch64 build host with its normal tool PATH:

```sh
cd sys/arch/evbarm/conf
config EMBERGPU
cd ../compile/EMBERGPU
make depend
make -j8
```

Run `sh ember/tools/virtgpu-contract.sh` and
`sh ember/tools/virtio-transport-contract.sh` from the source root.
The VirtGPU regressions compile production function bodies against
deterministic native-API models. They cover queue errors/pressure, actual
GPU-fence response validation, reset/UNREF lifetime, PRIME offsets/references,
page-array release, cursor failures before fence emission, and stale bounce
copyback. They do not prove actual DMA coherency, UVM/MMU behavior, SMP
interrupt safety, visible scanout or an accelerated userland session.

## Ordinary console recovery

EMBERGPU alone enables `VIRTGPU_CONSOLE` and the native `virtiodrmfb`
child. The selected FDT simple framebuffer postpones its final wsdisplay
attachment until autoconfiguration finishes; the early ARM renderer stays
available. A selected PCI VirtGPU can reserve it and prepare a private
XRGB8888 framebuffer through the native DRM helper. Failure before the first native
SET_SCANOUT submission leaves the firmware fallback to attach once. The regular
EMBER64 configuration does not enable this selection.

The child uses drmfb/genfb/wsdisplay with the existing keyboard and tty
ownership. Its CPU shadow is separate from the retained wired GEM backing
and DMA map. Optional genfb raster notifications only mark atomic damage.
A dedicated 20-ms upload worker consumes damage before copying, transfers
with PREWRITE/POSTWRITE and a GPU fence, then waits for fenced FLUSH.
Concurrent drawing can transiently tear a copied snapshot; its trailing
damage notification guarantees a later upload. Drawing never waits for a
GPU fence or writes the DMA buffer. wsdisplay mmap is unavailable because
untracked mappings would defeat this contract; the DRM GEM mmap ABI remains.

DRM master and wsdisplay modes serialize against the worker. A graphics
owner suppresses console restore and upload. Returning to MODE_EMUL marks
a full restore, and lastclose also redraws the active terminal. Raster
notifications after that redraw retain the final pixels. Reset stops
periodic requeue before a separate cleanup worker drains uploads, after
waking fence/response waiters. A stopped console retains its framebuffer
and pages rather than submitting a stale host resource.

`sh ember/tools/virtgpu-console-contract.sh` compiles production selection,
geometry, upload, ownership and stop functions against API models. Native
builds and these models do not prove visible login, live VT switching or
compositor-crash recovery; those need VM acceptance. Panic/DDB and reset
reinitialization are unsupported. There is no live unload or VirGL claim.

The initial commit suppresses empty SET_SCANOUT commands while firmware
still owns the display. A failed transfer prerequisite stops the plane
update before SET_SCANOUT/FLUSH. The first potentially submitted native
SET_SCANOUT is a conservative takeover boundary: even a failed reply may
mean that the host changed output. An error after this boundary retains the
native helper, framebuffer and final wsdisplay; it never claims firmware
rollback. The uploader retries ENOMEM/EAGAIN/ENOSPC at one-second intervals,
with a full transfer, explicit nonzero SET_SCANOUT and FLUSH. It clears the
console error only after that sequence succeeds. A new graphics master is
rejected while recovery has an error or the transport is stopped. Permanent
host/protocol failure stops uploads and retains resources; device-reset
reinitialization and revival of firmware output remain unsupported.

The production plane/command regression injects failed transfer allocation,
failed transfer, SET_SCANOUT allocation failure, uncertain SET_SCANOUT
completion and failed FLUSH after successful SET_SCANOUT. The worker test
checks retry, master rejection during recovery, and successful resumption.
The console currently requires exactly one host scanout.
