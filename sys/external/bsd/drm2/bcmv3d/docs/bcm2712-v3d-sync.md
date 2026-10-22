# Native V3D binary synchronization

Native TFU/CSD submissions use binary DRM syncobj operations.
The [BO reservation contract](bcmv3d-bo-fences.md) attaches a producer fence
to admitted TFU/CSD/CL BOs, including jobs without an output syncobj.
Hardware applicability and evidence belong to [DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/bcm2712-cm5/diagnostics.md).

## Ownership and completion

`bcmv3d_job.c` uses the existing DRM syncobj and native `dma_fence` APIs.
It snapshots the input fence before replacing the output, including when
both handles are equal. The output object has its own retained reference;
closing its handle during a dependency wait cannot invalidate the object.
Input waits are interruptible, bounded to one second and outside the engine
mutex. Missing handles, empty inputs and dependency errors reject admission.
Validation and allocation failures preserve the previous output fence.

An admitted job publishes a new fence under the serialized engine lock.
Successful completion is signalled only after cache cleaning, PTE revocation
and DMA ownership transfer. Uncertain execution errors signal an error fence,
poison the shared engine and retain GPU-owned BOs until reboot. Fence context,
sequence and lock belong to the device; hot removal remains unsupported.

`DRIVER_SYNCOBJ` is enabled. The later [native CL gate](bcm2712-v3d-gl.md)
adds serialized bin/render and retains both input fences before either wait.
This contract covers binary synchronization. Timeline support, PRIME and KMS
have separate interfaces and qualification. The submission queue is bounded
and synchronous; it is not a general scheduler.

## Shared DRM wait repairs

The common wait contracts require publication wakeups, timeout consumption
after partial WAIT_ALL completion, and allocation-error cleanup.
`nsecs_to_jiffies64` preserves whole seconds and the nanosecond remainder.
Binary and timeline WAIT descriptors use `DRM_UNLOCKED` so a blocked waiter
does not hold `drm_global_mutex` against its signal producer. Other core
ioctls retain their serialization. The dispatch regression checks this boundary.

## Reproduce the checks

From the owning source checkout, run:

```sh
sh ember/tools/bcm2712-v3d-sync-contract.sh
sh ember/tools/drm-syncobj-wait-contract.sh
sh ember/tools/drm-syncobj-dispatch-contract.sh
sh ember/tools/drm-jiffies-contract.sh
sh ember/tools/bcm2712-v3d-csd-contract.sh
sh ember/tools/bcm2712-v3d-job-contract.sh
```

The kernel builder includes these contracts. The wait models cover progress,
publication and allocation cleanup. Tick conversion uses an independent
128-bit arithmetic reference, including absolute DRM deadlines.
Memory/undefined-behavior runs use
`CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'`;
set `V3D_SKIP_MUTANTS=1` for instrumented CSD/TFU runs.
These models exercise source behavior, not a physical cache or GPU.

Cross-build `EMBERV3DRENDER` from pinned EmberBSD sources. Build the static
acceptance tool with the same compiler, UAPI and recorded target ABI inputs
as [the render check](bcm2712-v3d-render.md#reproduce-the-userspace-check):

```sh
"$CC" --sysroot="$SYSROOT" -std=c11 -O2 -Wall -Wextra -Werror \
    -pthread -static -I sys/external/bsd/drm2/dist/include/uapi/drm \
    ember/tools/bcm2712-v3d-sync-check.c -o "$OUTPUT/sync-check"
```

On the matched CM5 kernel, run `./sync-check /dev/dri/renderD128`.
Preserve and verify the ordinary kernel and its matching modules before
installation, then restore both after the experiment.

## Hardware applicability


Current hardware evidence and its applicability belong to the [DevBoards profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/bcm2712-cm5/graphics.md).
Finite waits, imported handle lifetime, same-handle replacement and CSD/TFU dependency chains have distinct contracts. Completion without data readback is not output-integrity evidence.

## Source analysis and remaining boundaries

Static analysis and executable model checks cover different boundaries.
Host ASan/UBSan does not qualify physical execution or replace a target memory checker.

The OS owns inherited support-header findings: the `printk.h` ptrdiff format,
the extreme-HZ division in `sched.h` (not reached at CM5's `hz=100`), and the zero-input precondition in
`log2.h` require caller/impact analysis. Macro sentinel, sizeof and dead-store
reports also remain to be classified. No broad suppressions were added.

The [common DRM synchronization contract](../../drm/docs/drm-syncobj.md) retains the
`drm_syncobj_find_fence(WAIT_FOR_SUBMIT)` object and initializes its callback
state. It also repairs native fence-chain callback registration. The binary
CM5 acceptance above does not call the find helper with this flag and does
not establish timeline support for V3D. The V3D capability remains off.

Binary synchronization alone does not qualify Mesa/Vulkan execution, PRIME,
KMS, arbitrary-job recovery or an accelerated desktop. Follow the
[current graphics profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/bcm2712-cm5/graphics.md) for hardware scope.
Continue with current Mesa through EmberBSD-Ports.
