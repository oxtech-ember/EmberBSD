# BCM2712 native render interface

Status: implemented, cross-built and under physical acceptance. The accepted
kernel-only DMA, IRQ and single-fault probes are prerequisites. This stage
establishes a DRM/GEM ownership boundary and a bounded userspace TFU operation
before general Mesa submissions. It does not yet establish Mesa rendering.

## Design

Use the existing EmberBSD DRM core and native bus_dma/GEM integration.
A private character-device protocol would force a separate Mesa backend;
importing the full Linux driver would require unimplemented platform and
scheduler interfaces. Neither is needed for this first native path.

`EMBERV3DRENDER` is opt-in and excludes every boot-time job probe. The
existing ACPI observer verifies the board resources and completes takeover
before registering a render-capable DRM device. The ordinary kernel keeps
its current passive observer. Display modesetting is a separate driver.

Buffer objects use native contiguous DMA storage and the existing CMA UVM
pager. Each has a separately allocated GPU virtual range; GPU addresses
are not physical addresses. GEM handles, mappings and submitted jobs own
references. The last reference releases the range and storage. Limit one
BO to 16 MiB and total BO storage to 64 MiB during this stage. The engine's
4 MiB page table and three scratch pages also count against that total.

A device mutex serializes jobs. The MMU maps only the two referenced BOs:
source read-only, destination writable. The first admitted payload is the
accepted 64x64 R32F TFU copy. Validate every register, flag, reserved field,
handle and range before touching the GPU. Unsupported formats, extensions,
sync objects and CL/CSD submissions fail explicitly. General TFU support
is not advertised to Mesa until its full contract is implemented.

Each submission programs all TFU input registers and requires exactly one
completion, idle TFU, one counter increment and clean MMU/HUB state. The
HUB handler masks itself and records evidence; it never touches GEM
objects. Controller masking and a barrier drain it before final checks.
Before releasing job references, remove the PTEs and complete the bounded
MMU flush. DMA ownership returns to the CPU only after verified retirement.

After an uncertain submission, timeout, fault or failed unmap/flush, poison
the engine and retain the submitted BO references until reboot. New jobs
fail. There is no unbounded retry or freeing of GPU-accessible storage.
The fixed-payload fault probe does not establish recovery for arbitrary
userspace submissions. Engine page tables and scratch remain allocated
for the device lifetime; hot removal is unsupported.

## Interfaces and ownership

The UAPI follows the Broadcom-licensed header at Linux
`a5ebb76233b79db01e82e061173cb73c6d2b5c6b`, blob
`d9b01f4c3a047eac90899901cc603838ab5c9090`, SHA256
`eafcaa1f22beedd040baa27912b506732bc32fed01b23a4b581148a267afbac3`.
Preserve the license and existing ABI fields. V3D 7.1 TFU adds `v71.ioc`;
the old fork header cannot represent that payload. Header availability
does not imply that all declared ioctls are implemented.

The new driver lives in `sys/external/bsd/drm2/bcmv3d/`:

- `bcmv3d_drv.c`: registration, capability queries and DRM ioctl routing.
- `bcmv3d_bo.c`: DMA/GEM allocation, virtual ranges, mapping and release.
- `bcmv3d_job.c`: serialized MMU/TFU execution and retained-failure ownership.
- `bcmv3d_drv.h`: shared native structures and interfaces.

`CREATE_BO`, `GET_BO_OFFSET`, `MMAP_BO`, `WAIT_BO` and the bounded
`SUBMIT_TFU` use the standard V3D structures. BO lookup always uses the
calling DRM file. `GET_PARAM` reports the observed hardware identifiers;
unsupported capabilities remain zero. PRIME, general synchronization,
shaders, KMS and a desktop are outside this initial acceptance.

## Implementation and acceptance plan

Execute inline in the current task. Use the existing source checkout and
cross compiler; keep build products outside Git. Preserve the recorded
ordinary kernel and compare matching modules before each physical install.

1. Add actual-source contracts for UAPI layout, BO validation and lifetime,
   job validation, ordered DMA ownership, completion and retained failures.
   Observe each new test fail before implementing its owner. Add causal
   mutants for skipped range checks, early release and false completion.
2. Implement registration and BO lifetime. Exercise handle-creation and
   mapping failures, zeroed allocations, cross-file access rejection,
   close while mapped, virtual-range reuse and quota exhaustion/recovery.
3. Implement one serialized TFU path. Check register-write failures, timeout,
   duplicate IRQs, MMU errors, failed PTE revocation and counter wrap.
   A failed engine must retain references and refuse subsequent submissions.
4. Integrate the configuration and contracts into the normal kernel builder.
   Cross-build with the selected GCC16 prefix, inspect provenance and all
   artifacts, and obtain an independent review before hardware execution.
5. Run the C userspace acceptance tool on CM5: identity, BO/mmap lifecycle,
   unauthorized access failures, repeated verified TFU copies and concurrent
   submit/close cases. Record source, firmware limits, duration, hashes and
   exact unsupported features. Restore the ordinary kernel after the trial.
6. Update this document, the central README, board page and knowledge base
   from observed results. Only then expand the admitted submission contract
   toward the current Mesa V3D driver through EmberBSD-Ports.

Tests must distinguish a registered DRM node, mapped CPU memory, real GPU
execution and Mesa rendering. None is an alias for the next level.

## Reproduce the userspace check

Build `EMBERV3DRENDER` with the normal kernel builder and a pinned EmberBSD
checkout. Preserve the tested ordinary kernel and matching module bundle.
This configuration owns GPU0 until reboot; it cannot coexist with boot probes.

Cross-compile the check using the same checkout's V3D UAPI, a selected cross
compiler and an EmberBSD sysroot whose used headers, libc, pthread and CRT
provenance are recorded:

```sh
"$CC" --sysroot="$SYSROOT" -std=c11 -O2 -Wall -Wextra -Werror \
    -pthread -static -I sys/external/bsd/drm2/dist/include/uapi/drm \
    ember/tools/bcm2712-v3d-render-check.c -o "$OUTPUT/render-check"
```

On the CM5 running that kernel, select the render node registered as `v3d`
and run `./render-check /dev/dri/renderD128` with access to that node.
The check validates per-file handles and mmap authorization, zeroed storage,
388 repeated/concurrent TFU copies, guard pages, conversion-counter wrap,
mapping lifetime after handle close and 64 submit/close races on a shared fd.
It reports successful submissions, lookup races returning `ENOENT`, and
observed busy BOs separately. Each successful operation verifies every word.
Record the kernel and test hashes, output, duration and kernel log.

Host contracts compile the actual production sources with modeled services.
The job contract exercises 37 register failures, DMA ownership and PTE revoke
ordering, retained failure references, close during execution, wait boundaries,
counter wrap and completion at the final wait slice. Eleven causal mutants must
fail, including missing DMA sync and premature return of ownership. These
checks do not emulate a physical V3D or establish cache coherency on hardware.

The first physical trial registered DRM and passed buffer/mmap isolation but
timed out on the first TFU completion. ARM PIC `intr_mask`/`intr_unmask` uses
a nesting counter. Masking in both initialization and submission left the
line disabled after one unmask. The driver now tracks its controller-mask
ownership; the contract models the native counter and rejects the original
double-mask behavior. A pending physical repeat must validate this change.
