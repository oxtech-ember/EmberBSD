# Native V3D binary synchronization

Status: native TFU/CSD binary synchronization passed physical CM5 acceptance
on 2026-10-11. This connects the [accepted QPU path](bcm2712-v3d-csd.md) to
standard DRM syncobj operations. Mesa execution is still a separate gate.

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

`DRIVER_SYNCOBJ` is enabled. Timeline syncobj, general CL, PRIME and KMS are
not enabled. `SUPPORTS_CSD` remains zero until the complete Mesa contract is
accepted. The queue is synchronous and bounded; it is not a general scheduler.

## Shared DRM wait repairs

The host contracts exposed a lost publication wakeup, a WAIT_ALL loop that
stopped consuming its timeout after partial completion, and an allocation
failure that left local wait resources initialized. Each regression failed
against the old implementation before passing with the correction.

The first physical run exposed subsecond truncation in `nsecs_to_jiffies64`:
with `hz=100`, a 100 ms wait expired about 80 ms before its deadline.
The conversion now preserves both whole seconds and the nanosecond remainder.
The next run reached the correct deadline but still could not receive both
signals. Native `drm_ioctl` held `drm_global_mutex` while waiting, blocking
the signal producer. Binary and timeline WAIT descriptors now use
`DRM_UNLOCKED`; other core ioctls keep their existing serialization.
An actual-dispatch regression reproduces the lock obstruction.

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

The kernel builder includes these contracts. The binary ownership model
passed 1,173 checks. Four wait scenarios cover progress, publication and
allocation cleanup. Tick conversion passed 20,551 comparisons with an
independent 128-bit arithmetic reference, including absolute DRM deadlines.
CSD passed 38,572 checks, 46 injected failures and nine causal mutants;
TFU passed 28,596 checks, 37 injected failures and eleven mutants.
Memory/undefined-behavior runs used
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

## Physical acceptance, 2026-10-11

CM5 Rev 1.0, BCM2712 D0, 4 GiB RAM and 64 GB eMMC, Waveshare CM5-NANO-B.
The existing UEFI was preserved; its source and carrier PCB revisions remain
unknown. GCC 16.2.0 cross-built the kernel and static C test on macOS.
The kernel source is public commit `2294dc3440654d5d42116e2f75c42ab0541ac764`.

At 12:52:54 UTC the check exited 0 in 1.11 seconds. It passed create, reset,
signal, finite WAIT_ALL/WAIT_FOR_SUBMIT, and opaque-fd export/import with
retained lifetime after original handles were destroyed. Sixteen rounds
alternated partial completion/timeouts and full completion from another thread.

The GPU portion passed 260 same-handle fence replacements and 64 alternating
CSD/TFU chains, followed by empty-input rejection and one recovered CSD submit.
There were 325 CSD submissions and 64 TFU copies. The first 324 CSD results
were checked word-for-word: 5,184 GPU-computed integers plus full guards.
The final recovery submission checked completion, without another data readback.
No CSD/TFU failure diagnostic appeared in the captured kernel log.
This 1.11-second check is neither a performance benchmark nor a soak test.

| Artifact | SHA256 |
| --- | --- |
| Kernel ELF | `3f892f70612c5347c068c6402307ad9944f57546458a418f6c20c3afe03196b8` |
| AArch64 Image | `d332b864f82e6accb26a4ecdb4234341473038da28129052d5da1860c0b771b2` |
| Static check | `f17ab177603ecc3f7af8fb3dfa9e47dd541070656774827706f2fe2c489a8b15` |
| Full kernel/test log | `d42275519ccee5f18cca4cd43ca95732444a672dd77072952815ff8a4f9230fc` |

ELF/Image, debuglink, CTF, complete source export, compiler provenance and
module symbols passed verification. The matched `if_cemac_acpi` module was
installed with the test kernel; the other three board modules were unchanged.
The tool's 74 base/compiler headers and five libc/pthread/CRT files match
previously recorded device ABI inputs. The installed base revision is still
unknown: this does not establish complete SDK or sysroot provenance.

## Source analysis and remaining boundaries

Cppcheck 2.22.0 analyzed the four changed native translation units after
preprocessing with actual AArch64 kernel headers, macros and flags. Compared
with `815f742ca1f`, warning counts stayed 17/17/19/21 for job/driver/syncobj/ioctl.
No new warning appeared. GCC 16.2 `-fanalyzer` reported no analyzer findings;
the ioctl unit retained an existing `-Wshadow` warning. Apple Clang 21.0.0 Static Analyzer
reported the same six job-unit warnings as the baseline. This is not a
warning-free claim. Valgrind was unavailable on the ARM64 macOS host and
EmberBSD target; ASan/UBSan model runs are recorded separately.

The OS owns inherited support-header findings: the `printk.h` ptrdiff format,
the extreme-HZ division in `sched.h` (not reached at CM5's `hz=100`), and the zero-input precondition in
`log2.h` require caller/impact analysis. Macro sentinel, sizeof and dead-store
reports also remain to be classified. No broad suppressions were added.

Review also identified an inherited, separate
`drm_syncobj_find_fence(WAIT_FOR_SUBMIT)` defect: the syncobj reference is
dropped before callback use, and native callback lock/wakeup fields are not
initialized. The accepted binary path does not call this helper with that
flag. Repair and test it before enabling consumers of that branch; timeline
capability remains off. Unlocking the timeline WAIT descriptor is not timeline
acceptance. Independent review found no further defect in the accepted path.

Mesa/Vulkan execution, general CL, PRIME, KMS, arbitrary-job recovery and an
accelerated desktop remain unverified. The ordinary kernel and matching
Ethernet module are restored after the trial; GPU support there remains a
passive observer. Restoration was verified at 12:55:15 UTC.
Continue with current Mesa through EmberBSD-Ports.
