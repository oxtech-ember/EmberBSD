# A733 native PowerVR render interface

The experimental native driver registers the upstream DRM name `powervr`.
It exposes the measured GPU identity through `DRM_IOCTL_PVR_DEV_QUERY`.
It also allocates native DMA-backed GEM buffer objects with CPU mappings.
It does not provide Mesa rendering, GPU mappings, contexts, or job handles.

## Ownership and registration

The A733 consumer must first identify BVNC `36.56.104.183` on hardware.
`pvr_render_attach()` accepts only that supported profile.
The BXM-4-64 profile has one phantom. Its four-core limit is a different field.
The driver copies the measured BVNC. Queries do not access GPU registers.

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
Write `1` after a successful identification stage to register the device.
Reading returns `1` once registration succeeds. Repeated writes are idempotent.
An in-progress request returns `EBUSY`. Missing or unsupported identity returns
`ENODEV`. Writing `0` does not detach the device; it returns `EINVAL`.
The handler releases the sysctl tree lock before registration.

## Supported ABI

The local header retains a subset of the MIT-licensed PowerVR UAPI from
[Linux `7b63ef2`](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/include/uapi/drm/pvr_drm.h).
Both ioctl arguments and the GPU information structure are 16 bytes.
The BVNC occupies bits B:63..48, V:47..32, N:31..16, and C:15..0.

`GPU_INFO_GET` supports null-pointer size discovery.
The minimum object is 12 bytes, through `num_phantoms`, as specified by the
[pinned driver helper](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_drv.h).
Shorter objects return `EINVAL`. Reserved bytes and caller extensions are zeroed.
Inaccessible destinations return `EFAULT`. A failed copy may have written an
earlier output prefix; the caller must discard that output.
The argument size changes only after successful copying.

Other device queries return `EOPNOTSUPP`.
The driver advertises `DRIVER_RENDER` and `DRIVER_GEM`.
PRIME, syncobj, display modes, GPU mappings, and submission remain unsupported.

## Buffer objects and CPU mappings

`CREATE_BO` creates a per-file GEM handle for zeroed native DMA memory.
The request must have a nonzero 4096-byte-aligned size and zero reserved fields.
The allocator rounds backing memory to the host page size and clears it all.
It rejects arithmetic overflow and sizes outside the native allocator's range.
Physical allocation can fail with `ENOMEM`; no GPU address is assigned.

The three upstream flags retain their values. Unknown bits are rejected.
`PM_FW_PROTECT` cannot be combined with `ALLOW_CPU_USERSPACE_ACCESS`.
GPU cache and firmware protection flags are stored for future GPU mappings.
This stage does not claim that those mappings already exist.

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
```

The contract compiles the actual query and registration code with boundary
stubs. It checks short objects, output bounds, padding, copy faults, unsupported
queries, duplicate registration, and last-reference cleanup after failed attach.
These checks do not exercise the kernel DRM core, device nodes, or hardware.

Build the native acceptance utility with the selected GCC16 compiler and a
recorded EmberBSD sysroot. Do not substitute upstream NetBSD libraries.

```sh
CC=/path/to/aarch64--netbsd-gcc SYSROOT=/path/to/emberbsd-sysroot \
    sh ember/tools/pvr-render-check-build.sh /path/to/EmberBSD /new/output
```

After explicit registration, select the actual render device reported by the
native DRM platform record. Pass its path and the independently measured BVNC:

```sh
/path/to/pvr-render-check /dev/dri/renderD128 0x00240038006800b7
```

Keep the kernel revision, node identity, tool hash, and output with the board
receipt. A successful identity query does not establish GPU buffer execution.
Mesa initialization additionally needs real VM, GEM, synchronization, heap and
runtime queries. Accelerated Plasma also requires the display path.
The separate [bounded compute experiment](a733-gpu-fwcompute.md) is not a DRM
submission API.

## Physical identity result

On 2026-10-13, Zero 3W passed registration on kernel `e25e95dd908`.
The compute experiment first passed all 21 stages again after a fresh boot.
The registered nodes were `card0` and `renderD128`, with device major 180.
Both native platform records described `allwinner,sun60i-a733-gpu`.
The identity utility from `9ccdadbfe10` passed all 72 checks.
It verified BVNC `36.56.104.183`, bounded outputs, invalid pointers, unsupported
queries, and absent synchronization and modesetting capabilities.
A repeated registration request returned success without another device.
The installed libdrm 2.4.134nb2 also passed platform discovery and node folding.
Both its shared library and a diagnostic archive from the same build passed.
The [Ports checker](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/libdrm-platform-check.c)
accepts the explicit `allwinner,sun60i-a733-gpu` profile.

The original utility expected zero for the absent modesetting capability.
The native DRM core correctly returns `EOPNOTSUPP` without `DRIVER_MODESET`.
The corrected utility tests that error. The driver did not need this change.
No GEM allocation, DRM job submission, Vulkan, or display was accepted here.
