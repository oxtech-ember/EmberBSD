# BCM2712 native render interface

Status: the bounded userspace path passed physical CM5 acceptance on
2026-10-11. Native DRM/GEM buffers, mmap isolation and repeated TFU copies
work in the opt-in kernel. The subsequent [native CSD acceptance](bcm2712-v3d-csd.md)
adds verified QPU computation. The later [CL and Mesa acceptance](bcm2712-v3d-gl.md)
adds hardware OpenGL through the common Ports package. Each stage has its own limits.

## Design

Use the existing EmberBSD DRM core and native bus_dma/GEM integration.
A private character-device protocol would force a separate Mesa backend;
importing the full Linux driver would require unimplemented platform and
scheduler interfaces. Neither is needed for this first native path.

`EMBERV3DRENDER` is opt-in and excludes every boot-time job probe. The
existing ACPI observer verifies the board resources and completes takeover
before registering a render-capable DRM device. The ordinary kernel keeps
its current passive observer. Display modesetting is a separate driver.

Userspace buffer objects use native scatter/gather DMA storage and a private
UVM pager. The allocator admits at most one DMA segment per page. It validates
each loaded segment for page alignment, length and the 36-bit address limit.
CPU faults use allocation segments; GPU PTEs use translated DMA map segments.
Each BO has a separately allocated GPU virtual range; GPU addresses
are not physical addresses. GEM handles, mappings and submitted jobs own
references. The last reference releases the range and storage. One BO can use
at most the existing 256 MiB resource ceiling. Every allocation must also fit
the remaining device budget. Limit total BO storage to 1/16 of physical memory,
with a 64 MiB floor and a 256 MiB cap. A full Plasma session needs more than
64 MiB. The engine's 4 MiB page table and three scratch pages also count against that total.
They retain their strict contiguous allocation contract. Quota and GPU VA
reservation precede segment-list allocation. The fixed-mode HVS consumer keeps
its separate 16 MiB storage limit. The shared CMA helper is unchanged. PRIME self-import and UVM reference ownership remain unchanged.

The scatter/gather change passed physical CM5 checks on 2026-10-16 with
kernel source `93dc0e3167a2e1381c9c4971cf8e4ee00f282685`. The unchanged
BO probe passed single allocations through 16 MiB and three simultaneous
buffers totaling 17,969,152 bytes. The previous kernel failed the 7,475,200-byte
allocation. Existing TFU, CSD, binary-sync and CL programs also passed, including
325 CL pairs and 1,331,200 checked pixels. Kernel and boot time remained unchanged.
These bounded checks do not accept the full desktop or a long stress run.
Allocation, mapping and rendering retain separate acceptance boundaries.
Fragmentation, DMA ranges and the unchanged
budget can still reject an allocation. A successful allocation does not prove
that global memory use is bounded or that no memory leaks exist.

The [read-only BO snapshot](bcm2712-v3d-bo-stats.md) distinguishes quota,
DMA and publication failures without changing these limits. Its counters include
in-flight reservations and retained GEM ownership.

The [CL/CSD object-list contract](bcmv3d-submit-lists.md) replaces fixed stack
arrays with bounded heap ownership. Its 4096-entry software policy is separate
from the BO storage budget and physical rendering acceptance.

A device mutex serializes jobs. For TFU, the MMU maps only two referenced BOs:
source read-only, destination writable. The first admitted payload is the
accepted 64x64 R32F TFU copy. Validate every register, flag, reserved field,
handle and range before touching the GPU. Unsupported formats, extensions,
sync objects and CL submissions failed explicitly in the initial TFU stage.
The later [syncobj](bcm2712-v3d-sync.md) and [CL](bcm2712-v3d-gl.md) stages
add binary dependencies and bin/render execution. The subsequent
[CSD path](bcm2712-v3d-csd.md) shares the serialized engine and maps only its
referenced BOs. General TFU support
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

CL failure reports read the actual PCS register at `0x130`, CT1 status,
current/end addresses, and HUB MMU control. Earlier diagnostics mislabeled
CORE_IDENT2 at `0x08` as PCS. A `read-failed` bit marks an unavailable value:
bits 0 through 5 correspond to IRQ, PCS, CT1CS, CT1CA, CT1EA, and MMU.
These finite reads run only after both IRQ paths are masked and drained.
They do not acknowledge faults, reset the engine, or release retained BOs.


## CL rejection receipts

The native device reports at most eight rejected `SUBMIT_CL` admissions per
boot. A private per-device CAS counter saturates at eight. A receipt requires
`EINVAL`; successful jobs and other errors do not consume this budget.
The ioctl result, admission rules, BO limit, waits and GPU execution are unchanged.
There is no reset interface.

`reason` identifies `header`, `duplicate`, `sync-bcl`, `sync-rcl`, `wait-bcl`,
`wait-rcl`, `reservation`, `range`, `fence-publish` or `resv-publish`.
A reason identifies the failing boundary. It does not assert that each helper
can currently return `EINVAL` for an otherwise valid request. After admission,
execution failures use the existing retained-fault diagnostic instead.
Receipts after BO lookup run after the original reference and lock unwind.
Header rejection runs before copying the BO handle array.

Each receipt records its sample number, saved errno, requested BO handle count,
flags, pad, perfmon ID, extension presence, BCL/RCL GPU endpoints, QMA/QMS/QTS,
and input/output sync IDs. It does not read MMIO or report user pointers,
BO handle arrays, or BO contents. Concurrent receipts can appear out of sample
order. These receipts can locate a rejected Mesa request; they do not establish
that a particular BO count caused a reported draw failure.

Run `ruby ember/tools/bcmv3d-cl-reject-contract.rb` to test the actual UAPI,
private job/sync structures, range helper, bounded counter, receipt and submit
body with controlled kernel/DRM services. The test checks every header and
range branch, duplicate references, helper error routing, post-unwind lifetime,
the execution poison path, device separation and concurrent saturation.
Injected helper failures establish routing, not hardware or helper reachability.
The existing CL, sync, TFU and CSD contracts test their actual helper bodies.
Set `CL_REJECT_EVIDENCE` to a new directory to retain input hashes, extracted
bodies, exact commands and compiled causal-control results. `CC`, `CFLAGS`
and `TEST_RUNNER` select the compiler, instrumentation and execution runner.

## TFU rejection receipts

TFU has a separate per-device budget of eight admission receipts per boot.
Its CAS counter saturates and has no reset interface. Any admission errno can
produce a receipt. Successful jobs and execution failures do not consume this
budget. The existing payload whitelist, errno, waits and retained-fault path
remain unchanged.

`reason` identifies `dst-lookup`, `src-lookup`, `payload`, `sync`, `reservation`,
`poisoned`, `fence-publish` or `resv-publish`. A receipt records the saved errno,
all scalar TFU configuration words and coefficients, flags, pad, extension
presence, sync IDs and the number of nonzero inline BO handles. It records
size and GPU base only for the source and destination BOs already found by
the submission. `src-known=0` or `dst-known=0` means that lookup did not obtain
that BO. Its metadata fields remain zero. Extra handles are counted but not
looked up for diagnostics.

BO metadata is copied while the existing GEM reference is live. Receipts run
after the original reference and lock unwind. The logger does not read MMIO,
user pointers, extension memory or BO contents. Concurrent sample lines can
appear out of order. These receipts locate a rejection boundary; they do not
establish compatibility with the rejected payload or a working Plasma session.

Run `ruby ember/tools/bcmv3d-tfu-reject-contract.rb` for the actual payload,
submission, private structures, snapshot and bounded logger contract.
`TFU_REJECT_EVIDENCE` retains source pins, commands and causal controls in a new
directory. `CC`, `CFLAGS` and `TEST_RUNNER` select compilation and execution.
Controlled helper errors prove routing, not hardware or helper reachability.

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
- `bcmv3d_job.c`: serialized MMU/TFU/CSD execution and retained-failure ownership.
- `bcmv3d_drv.h`: shared native structures and interfaces.

`CREATE_BO`, `GET_BO_OFFSET`, `MMAP_BO`, `WAIT_BO` and the bounded
`SUBMIT_TFU` use the standard V3D structures. BO lookup always uses the
calling DRM file. `GET_PARAM` reports the observed hardware identifiers;
unsupported capabilities remain zero. PRIME, general synchronization,
KMS and a desktop are outside this initial TFU acceptance. Shader submission
has its own subsequent [CSD acceptance](bcm2712-v3d-csd.md).
Binary TFU/CSD syncobj and finite waits passed a later
[physical synchronization check](bcm2712-v3d-sync.md).

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
Run `ruby ember/tools/bcmv3d-bo-size-contract.rb` for the actual allocator,
whole-BO PRIME mmap, final-page pager and renderer PTE bounds. The contract
accepts 16 MiB plus padding through the inclusive 256 MiB ceiling. It rejects
zero, overflow-sized requests, quota exhaustion and injected allocation errors.
It checks size/accounting/ref cleanup with 4 KiB and 64 KiB allocator pages.
Engine PTE coverage is checked only with its existing 4 KiB page contract.
The 4 MiB renderer table contains 1,048,576 PTEs. The existing GPU VA arena
reserves the first and last 1 MiB of that 4 GiB range. A larger software limit
does not prove that physical allocation, GPU execution or Plasma succeeds.

Run `sh ember/tools/bcm2712-v3d-sg-contract.sh` for fragmented allocation,
error unwinding, mapped lifetime, and PTE coverage. The TFU test crosses a
segment boundary and stops within the next segment at its 16 KiB limit.
Negative controls reject one-segment allocation/mmap/free, invalid segment
acceptance and contiguous-PFN assumptions. These are modeled service tests,
not a UVM concurrency or hardware cache-coherency test.

CL, TFU and CSD completion share an IRQ-safe sleep interlock.
A spin mutex at `IPL_VM` protects event publication and waiter enqueue.
The submission mutex precedes that interlock. The ISR takes only the spin mutex.
The waiter drops the interlock before masking and draining the parent IRQs.
Spurious wakeups preserve the 500 ms deadline, including tick-counter wrap.

Run `sh ember/tools/bcm2712-v3d-wait-contract.sh` for the actual-source regression.
It injects completion between the event check and sleep enqueue for all three engines.
It also checks early completion, spurious wakeups, wrong IRQs, timeout, errors,
last-slice completion and retained faulted buffers at `hz=100` and `hz=1000`.
Negative controls restore `tsleep` or unlock before enqueue; both must fail.
`CC`, `CFLAGS`, `V3D_COMPILE_ONLY=1`, `V3D_OUTPUT` and `TEST_RUNNER` support
cross compilation and a selected execution checker. Physical latency remains unmeasured.

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
double-mask behavior. The corrected kernel passed the physical repeat below.

## Physical acceptance, 2026-10-11

The board was CM5 Rev 1.0, BCM2712 D0, 4 GiB RAM and 64 GB eMMC on
Waveshare CM5-NANO-B. Existing UEFI was preserved; its source revision and
carrier PCB revision are unknown. The cross-build used GCC 16.2.0 on macOS.

The C check completed in 0.18 seconds with exit status 0: zeroed GEM storage,
per-file handle/mmap denial, mappings surviving handle close, 388 TFU copies,
full data/guard verification, counter wrap and two concurrent clients.
The 64 shared-file submit/close races reported 32 successful jobs, 32
`ENOENT` lookup races and 32 observations of a busy BO. Thus 420 GPU copies
completed in total. This short check is not a throughput benchmark or soak.

| Artifact | SHA256 |
| --- | --- |
| `netbsd-EMBERV3DRENDER` | `0d2bcf8e764023f13c4689a1e7624d5e9a464fe5edc7edbe268bb183df28d76c` |
| AArch64 Image | `82a84e9ee38113bd5e8384f438183647564d861bb2430b6ff1e71a731683d000` |
| Check from `878c7f1c2fc4776b9dc3f8e231c5e07450fcb493` | `f027f65db5712b05f8aef9599c7d1bf21613d86cafc9c608ee88c63ca656eb7c` |
| Captured kernel/test log | `cce678aa28feeb012e7600e55d27ea7a0acfc75606bbf2408bfacfd46a22db2c` |

ELF/Image, debuglink, CTF, source/compiler provenance and matching module
symbols passed. All four board modules matched the accepted ordinary kernel.
The check's 67 used base headers, libc, pthread and CRT matched the installed
CM5 bytes. This is device ABI evidence, not complete sysroot or SDK acceptance.
The ordinary kernel is restored after the trial. Shader submission, Mesa,
Vulkan, display modesetting, recovery of arbitrary jobs and a desktop remain
separate acceptance steps.

## Source reconstruction

The isolated render source starts from the complete tree
`63ba4c53e3fc9397e5a1a5730e9b9b32f95ff49c`, reconstructed from public commits
as described in [the fault receipt](bcm2712-v3d-fault-plan.md).
Apply, in order, the changes from public commits `d5e5a378957`,
`fe7a2ad5783`, `7eb32a28867`, `878c7f1c2fc`, `404efe26b34` and `9315242ee76`.
The complete resulting tree is `cf53cace35b196521e1ccb1617115cbab78d5ef1`;
this reconstruction was checked using an independent Git index.
The build records `BUILDINFO=EmberBSD:635b0902c5624fd5a1aeaab27bb9e72b884efc42`.
That local commit is not a separate public branch. The tree and public changes
identify all sources; artifact hashes identify the actual physical trial.
