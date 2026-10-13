# A733 native PowerVR render identity

The experimental native driver registers the upstream DRM name `powervr`.
It exposes the measured GPU identity through `DRM_IOCTL_PVR_DEV_QUERY`.
This is a render-node identity interface. It does not provide Mesa rendering.
It does not allocate GPU buffers, virtual machines, contexts, or job handles.

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

Other device queries return `EOPNOTSUPP`. No other PowerVR ioctl is registered.
The driver advertises `DRIVER_RENDER` only. GEM, PRIME, syncobj, display modes,
GPU mappings, and submission remain unsupported.

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
