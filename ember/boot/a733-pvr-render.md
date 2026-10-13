# A733 native PowerVR render interface

The experimental native driver registers the upstream DRM name `powervr`.
It exposes the measured GPU identity through `DRM_IOCTL_PVR_DEV_QUERY`.
It also allocates native DMA-backed GEM buffer objects with CPU mappings.
It builds CPU-owned GPU VM tables and reports heap and static-area layouts.
It derives runtime limits and compatibility lists from prepared firmware metadata.
It does not provide Mesa rendering, execution contexts, or job handles.

## Ownership and registration

The A733 consumer must first identify BVNC `36.56.104.183` on hardware.
`pvr_render_attach()` also requires the prepared firmware metadata from stage 13.
It accepts BVNC `36.56.104.183` and firmware `1.1.6976702`.
The driver copies an immutable [query profile](a733-pvr-queries.md).
The BXM-4-64 profile has one phantom. Its four-core limit is a different field.
Queries do not access GPU registers or reload firmware.

Call registration from a serialized sleepable context without the sysctl tree
lock. Registration creates the native DRM platform metadata and acquires that
lock internally. Do not hold the consumer run lock across registration either.
Another sysctl handler can hold the tree lock while it waits for the run lock.
Reserve registration under the run lock, then release it before the DRM call.

The caller retains the returned device reference until reboot.
The driver does not implement hot removal. Private state survives any open
reference acquired during registration failure. The last device reference
releases that state and the DRM device. Registration does not power on the GPU.

The existing native DRM platform record identifies the parent device and
`allwinner,sun60i-a733-gpu`. The common libdrm platform-enumeration adapter can
read this record. No BCM-specific enumeration alias is required.

The integration exposes `hw.sun60igpuid0.render_attach`.
Write `1` after successful firmware preparation, stage 13, to register the device.
Reading returns `1` once registration succeeds. Repeated writes are idempotent.
An in-progress request or missing prepared firmware returns `EBUSY`.
Missing or unsupported identity returns
`ENODEV`. Writing `0` does not detach the device; it returns `EINVAL`.
The handler releases the sysctl tree lock before registration.

## Supported ABI

The local header retains a subset of the MIT-licensed PowerVR UAPI from
[Linux `7b63ef2`](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/include/uapi/drm/pvr_drm.h).
The DEV_QUERY arguments and GPU information structure are 16 bytes.
The BVNC occupies bits B:63..48, V:47..32, N:31..16, and C:15..0.

`GPU_INFO_GET` supports null-pointer size discovery.
The minimum object is 12 bytes, through `num_phantoms`, as specified by the
[pinned driver helper](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_drv.h).
Shorter objects return `EINVAL`. Reserved bytes and caller extensions are zeroed.
Inaccessible destinations return `EFAULT`. A failed copy may have written an
earlier output prefix; the caller must discard that output.
The argument size changes only after successful copying.

Heap and static-area queries follow the [native VM contract](a733-pvr-vm.md).
Runtime, quirk, and enhancement queries follow the [firmware-derived contract](a733-pvr-queries.md).
Unknown device queries return `EOPNOTSUPP`.
The driver advertises `DRIVER_RENDER`, `DRIVER_GEM`, and binary and timeline syncobjs.
The [native syncobj check](drm-syncobj.md#native-api-acceptance) passes on Zero 3W.
These capabilities cover CPU-issued synchronization; GPU job fences remain unimplemented.
GPU job fences, PRIME, display modes, and submission remain unsupported.

## Buffer objects and CPU mappings

`CREATE_BO` creates a per-file GEM handle for zeroed native DMA memory.
The request must have a nonzero 4096-byte-aligned size and zero reserved fields.
The allocator rounds backing memory to the host page size and clears it all.
It rejects arithmetic overflow and sizes outside the native allocator's range.
Physical allocation can fail with `ENOMEM`; no GPU address is assigned.

The three upstream flags retain their values. Unknown bits are rejected.
`PM_FW_PROTECT` cannot be combined with `ALLOW_CPU_USERSPACE_ACCESS`.
VM mapping translates cache and firmware protection flags into Rogue PTE bits.
No execution context publishes these tables to firmware yet.

`GET_BO_MMAP_OFFSET` returns a real GEM VMA offset for a handle owned by the file.
CPU `mmap` requires `ALLOW_CPU_USERSPACE_ACCESS` and the object's starting offset.
A shorter page-aligned mapping is allowed. Oversized and executable mappings
are rejected. Another file cannot use the offset without an allowed handle.
Closing the handle and file does not invalidate a live CPU mapping.
The last reference releases the GEM object, DMA memory, and device reference.
Native `GEM_CLOSE` provides handle release; there is no private close ioctl.

## Source checks and board acceptance

Run the production-source contract:

```sh
CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
    sh ember/tools/pvr-render-contract.sh
CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
    sh ember/tools/pvr-bo-contract.sh
CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
    sh ember/tools/drm-mmap-contract.sh
```

The contract compiles the actual query and registration code with boundary
stubs. It checks short objects, output bounds, padding, copy faults, unsupported
queries, duplicate registration, and last-reference cleanup after failed attach.
These host checks do not exercise the running kernel, device nodes, or hardware.
The BO contract also compiles the real GEM close and pager reference helpers.
It checks 4 KiB and 16 KiB host-page cases and allocation failure cleanup.
The mmap contract rejects a missing driver callback before changing outputs.

Build the native acceptance utility with the selected GCC16 compiler and a
recorded EmberBSD sysroot. Do not substitute upstream NetBSD libraries.

```sh
CC=/path/to/aarch64--netbsd-gcc SYSROOT=/path/to/emberbsd-sysroot \
    sh ember/tools/pvr-render-check-build.sh /path/to/EmberBSD /new/output
CC=/path/to/aarch64--netbsd-gcc SYSROOT=/path/to/emberbsd-sysroot \
    sh ember/tools/pvr-bo-check-build.sh /path/to/EmberBSD /new/bo-output
```

After explicit registration, select the actual render device reported by the
native DRM platform record. Pass its path and the independently measured BVNC:

```sh
/path/to/pvr-render-check /dev/dri/renderD128 0x00240038006800b7
/path/to/pvr-bo-check /dev/dri/renderD128
```

Keep the kernel revision, node identity, tool hash, and output with the board
receipt. A successful identity query does not establish GPU buffer execution.
Mesa initialization additionally needs GPU VM, synchronization, heap and
runtime queries and real execution contexts. Accelerated Plasma also requires the display path.
The separate [bounded compute experiment](a733-gpu-fwcompute.md) is not a DRM
submission API.

## Physical result

On 2026-10-13, Zero 3W passed registration on kernel
`0e89a6ffb4d726c64cf402049758f960d29a9655` (`EMBER64 #12`, GCC 16.2).
The kernel and four companion modules came from the same clean source export.
The accepted DTB and firmware were retained. The physical board revision
was not recorded; this result applies to the tested Zero 3W unit.
The compute experiment first passed all 21 stages again after a fresh boot.
The registered nodes were `card0` and `renderD128`, with device major 180.
Both native platform records described `allwinner,sun60i-a733-gpu`.
The identity utility from the same commit passed all 115 checks.
It verified BVNC `36.56.104.183`, firmware-derived runtime and compatibility
queries, bounded outputs, invalid pointers, and unsupported requests.
Binary and timeline syncobj capabilities were present; modesetting was absent.
Registration before stage 13 correctly returned `EBUSY`.
The separate native syncobj utility passed 1,712 checks in the same boot.
A repeated registration request returned success without another device.
The earlier acceptance on kernel `2b8bd199f829` also checked the installed
libdrm 2.4.134nb2 for platform discovery and node folding.
Both its shared library and a diagnostic archive from the same build passed.
The [Ports checker](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/libdrm-platform-check.c)
accepts the explicit `allwinner,sun60i-a733-gpu` profile.

The BO utility passed 2,105,411 checks, including an 8 MiB allocation.
It checked zeroed pages, six valid flag combinations, invalid requests,
foreign-file rejection, stable offsets, and complete and prefix mappings.
Both mappings stayed accessible after closing the handle and both files.
The prefix remained writable after unmapping the complete buffer.
The final unmap completed without a fault or watchdog reset.

These results accept native GEM and CPU mappings. The
[VM acceptance](a733-pvr-vm.md#physical-result) adds CPU-owned GPU tables,
per-file VM operations, and heap queries. All 586 VM checks passed again
on kernel `0e89a6ffb4d7`. These short checks do not establish sustained GPU operation.
GPU access to these BOs, DRM job submission, Vulkan, display, and Plasma remain unaccepted.

## Next Mesa contracts

The Mesa 26.2.4 PowerVR winsys creates a VM before it reports a physical device.
It queries heap and static-area layouts, then maps three internal BOs.
The [native VM interface](a733-pvr-vm.md) supplies per-file ownership,
nonoverlapping mappings, page-table updates, and BO references through unmap.
It is separate from the diagnostic compute VM. Firmware publication, TLB
invalidation, and GPU ownership during execution remain missing.
The new [runtime and compatibility queries](a733-pvr-queries.md) derive their values
from the parsed metadata of the prepared firmware image. These queries describe
hardware limits; they do not implement free lists or GPU execution contexts.

Device creation also allocates a global free list and creates compute, render,
and transfer contexts, even for a compute-only application.
The current Mesa null-job path calls `drmSyncobjTransfer` for binary waits.
The native DRM implementation requires timeline support for that ioctl.
Native timeline lookup, transfer, and chain lifetime fixes pass source contracts
and the physical [CPU-issued syncobj check](drm-syncobj.md#native-api-acceptance).
These synchronization operations do not connect GPU completions to job fences yet.

Mesa includes BVNC `36.56.104.183` but excludes it from its conformant allowlist.
Retain its explicit `PVR_I_WANT_A_BROKEN_VULKAN_DRIVER=1` experimental gate.
The display path additionally needs native A733 DE/TCON/HDMI support and KMS.
Neither a render node nor the accepted compute packet establishes that path.
