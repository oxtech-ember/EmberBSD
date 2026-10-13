# Native DRM synchronization objects

The common DRM layer provides binary and timeline synchronization objects.
A driver enables their capability bits only when its selected contract passes.
The PowerVR candidate enables both bits for CPU-issued synchronization.
This does not establish GPU job completion, Vulkan execution, or a desktop.

## Publication and callback lifetime

`drm_syncobj_find_fence()` retains its native syncobj reference while it waits
for publication. The wait entry initializes its mutex, condition variable,
and wakeup flag before registration. Cleanup removes the callback under the
syncobj lock before it releases the object or destroys local wait resources.
An interrupted wait releases a concurrently published fence and clears the
output. The lock order remains syncobj lock, then wait lock.

`DRM_IOCTL_SYNCOBJ_TRANSFER` can wait for publication. Its descriptor uses
`DRM_UNLOCKED`, as binary and timeline WAIT already do. A signal producer can
therefore enter DRM while the transfer sleeps. Unknown transfer flag bits
and nonzero padding return `EINVAL`. Only `WAIT_FOR_SUBMIT` is accepted.

The native fence-chain implementation arms its callback on the contained
fence. A zero return from `dma_fence_add_callback()` means that registration
succeeded. The callback retains the chain until deferred IRQ work completes.
A chain can therefore survive after its last external reference is closed.

## Source checks

Run these checks from the owning EmberBSD checkout:

```sh
sh ember/tools/drm-syncobj-find-contract.sh
sh ember/tools/drm-syncobj-dispatch-contract.sh
sh ember/tools/dma-fence-chain-contract.sh
sh ember/tools/drm-syncobj-wait-contract.sh
sh ember/tools/bcm2712-v3d-sync-contract.sh
```

Use `CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'` for the
instrumented host runs. These checks extract production C functions. Their
models replace only kernel boundaries, including allocation, locks, and IRQ
scheduling. They are deterministic regression checks, not SMP stress tests.
The kernel contract dispatcher also runs the new find and chain checks.

The find check covers 12 paths: immediate success, missing handles, missing
points, publication, handle closure during a wait, timeout, spurious wakeup,
interruption concurrent with publication, and already-completed points.
It detects the original implementation and six isolated incorrect changes.
The chain check passes 1,940 assertions. It covers pending predecessors,
callback races, closed external references, deferred completion, traversal,
and release. Both original callback defects fail independently.

GCC 16.2, Clang 22 Static Analyzer, and Cppcheck 2.22 analyze actual AArch64
translation units and compare them with the source before these changes.
Existing path limits and support-header diagnostics remain recorded.
Valgrind does not support the ARM64 macOS host or this EmberBSD kernel.
ASan/UBSan runs are separate evidence and are not called Valgrind checks.

## Native API acceptance

Build the userspace tool with the same source revision, target compiler, and
recorded sysroot as the candidate kernel:

```sh
CC=/path/to/aarch64--netbsd-gcc SYSROOT=/path/to/emberbsd-sysroot \
  sh ember/tools/drm-syncobj-check-build.sh /path/to/EmberBSD /new/output
```

Run `drm-syncobj-check /dev/dri/renderD128` on a driver that enables both
syncobj capabilities. The tool checks 256 timeline points, transfer flag
validation, retained timeline lifetime across opaque-fd export/import, and
32 delayed publication transfers between threads. It uses a 30-second
process alarm. The thread rendezvous does not prove the precise kernel wait
entry time; the source fixture covers that interleaving deterministically.

Timeline SIGNAL supplies completed CPU fences. This tool does not test a
pending GPU fence. GPU completion requires a separate driver's job test.
Physical acceptance of this candidate remains pending.
