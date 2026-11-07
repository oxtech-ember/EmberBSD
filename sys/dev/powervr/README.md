# PowerVR firmware metadata and MIPS preparation

<!-- Origin: EmberBSD PowerVR firmware preparation, AI-assisted. -->

The metadata, image and MIPS helpers parse bounded firmware input, prepare
ELF buffers and encode page tables and boot data without hardware access. The
shared firmware memory, initialization, start and boot owners additionally
prepare native DMA and perform an explicitly requested bounded MIPS handoff.
They do not provide graphics. The A733 platform wrappers retain their existing
strict feature policy and register address.
The platform supplies and validates the register physical address before
calling the common boot-data encoder. Before adding a hardware consumer,
the platform must establish the live register resource and verify owned RAM
ranges do not overlap MMIO; the encoder cannot establish either fact.

`PVR_GE8300_EXPERIMENTAL` selects an experimental preparation policy for
BVNC 22.102.54.38 and the exact `rogue_22.102.54.38_v1.fw` image from the
selected `powervr-firmware` source. Its SHA-256 is
`b571cdd90312c20fe87f14aae43484f7279859921ae3abe58e935412040c7f98`.
The matching image reports firmware ABI 1.1, build 6976702, six layouts,
and device array counts 1, 1, 2, and 22. A digest match identifies this
pinned input; it is not firmware authentication or hardware qualification.

Its raw BRN and ERN masks are `0x54000610` and `0x1a5`; feature words are
`0x9219dd57fdb6005b` and `0x4000000004`. Unknown BRN bits 26, 28 and 30,
ERN bit 8 and feature bit 102 remain visible in raw masks. They are not
claimed as implemented capabilities. The feature query accepts only feature
IDs known to the shared decoder. Unknown images, changed bytes, incompatible
BVNC and malformed buffers are rejected; errors leave output unchanged.

To exercise both actual firmware inputs and the GE CPU-only path, use the
existing `ember/tools/a733-gpu-fwinfo-contract.sh`,
`a733-gpu-fwimage-contract.sh` and `pvr-ge8300-fw-contract.sh` with the
selected host Clang and firmware files. Successful tests establish no GPU,
firmware execution, compute or rendering support.

## Queue failure diagnostics

The native DRM submit ioctl returns after validation and bounded queue admission.
The worker retains each context, VM mapping set, render target and dependency.
It signals completion only after firmware completion or a recorded error.
Closing a file cancels its queued work, including VMs with removed handles.
Waiting mapping changes prevent new holds until the queued work retires.
Completion waits and cleanup retries release the device gates between probes.

Timeline transfers flatten AND dependencies with a bounded work list. OR arrays
retain their original semantics. Fence containers release children through IRQ
work, which prevents recursive destruction of deeply nested payloads.
The compatibility layer drains this work before its fence state is destroyed.
Actual-source contracts cover submission, close, mapping barriers and nested
fence release. They do not establish device acceptance or module unload support.

A failed queue observation reports the snapshot as `PVR_RUNTIME_EIO` when
firmware events, faults or recovery flags fail validation. It also reports
pending FWCCB words and recorded hardware recovery data. Each diagnostic DMA
read acquires and releases only the selected firmware object.

## Single-context recovery

On GE8300 the DRM bridge opts the queue in to per-context recovery. A
`CONTEXT_RESET_NOTIFICATION` for one context with a guilty or innocent lockup
or overrun reason is then a valid event: the service consumes it, prints
`PVR_CONTEXT_RESET`, records the server context ID and keeps the queue usable.
A job of the reset context that is still running fails with `EIO`; if the
report arrives only after that job completed, the next job fails instead.
The context refuses later jobs and its VM accepts no new submissions. Its
CCCBs must drain within one second; the normal acknowledged `CLEANUP` path
then retires it, and a failed cleanup retains only that VM. If firmware does
not drain it, the service freezes as before. `HWR_RESET_IN_PROGRESS` alone is
waited out with bounded 1 ms busy polls, 250 per observation. All-context
resets, firmware fault or watchdog reasons, unknown flags, firmware faults and
the other HWR flags keep the previous fail-stop behavior. A733 is unchanged.
Linux likewise continues after this notification; it has no host action.

For a context reset with a page fault, `PVR_HWR_JOB` matches the firmware
context, job ID and page catalogue against 16 recent submission slots.
The type is the DRM job type, including fragment transfers. A missing match
or an unavailable device gate produces `unknown`. A closed VM can retain
a matching job type after its mapping information becomes unavailable.

For a matching live VM, `PVR_HWR_VM` reports the mapping at notification time:
GPU start and exclusive end, BO offset and size, or `unmapped`. It also reports
the CPU copy of the leaf PTE and its present bit for the fault address.
These records do not sample GPU TLB contents or identify the intended surface.

The native DRM submit path reports copied geometry or fragment command words
as `PVR_SUBMIT_ERROR_CMD` before retaining a failed VM. A failed paired geometry
job also reports its fragment command. The dump never follows GPU addresses.
Successful submissions produce no command dump. These records identify failed
work; they do not prove that the GPU stopped or permit VM release.
