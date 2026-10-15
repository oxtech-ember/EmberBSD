# A733 native PowerVR execution

The native driver now has an experimental synchronous compute submission path.
It connects firmware memory, per-file VMs, execution contexts, free lists, and syncobjs.
Production-source contracts pass with native DMA and firmware boundary models.
Three physical Vulkan compute lifecycles now pass on Zero 3W.
The diagnostic compute packet and ICD enumeration remain separate checks.
Graphics submission and a complete accelerated desktop are not accepted.

## Activation and ownership

Use the measured BXM profile `36.56.104.183` and firmware `1.1.6976702`.
First complete [firmware startup](a733-gpu-fwprobe.md) through stage 17,
or complete the [diagnostic compute check](a733-gpu-fwcompute.md) through stage 21.
Then register the [native render device](a733-pvr-render.md).
Activate execution explicitly:

```sh
sysctl -w hw.sun60igpuid0.exec_attach=1
```

Activation adopts the running firmware and its prepared MIPS address space.
It does not reset or restart the GPU. Diagnostic mutations then return `EBUSY`.
The service allocates coherent native DMA objects in a separate firmware arena.
It preserves the diagnostic heap and checks each allocation's ownership.
Mapping changes require an acknowledged firmware MMUCACHE command before reuse.
An uncertain command or firmware failure stops execution until reboot.
There is no accepted GPU reset or hot-removal path.

The device gate serializes firmware work. The platform gate protects its registers.
Dependency waits release both gates and retain their contexts and syncobjs.
A file can remove a handle without invalidating a retained operation.
Unknown completion retains affected firmware objects, VMs, tables, and GEM references.
The driver does not free memory that the GPU could still access.

## Supported operations

`CREATE_CONTEXT` accepts the fixed-profile render, compute, and transfer contexts.
Mesa needs these contexts even when an application only uses compute.
Static state is copied before decoding. Unsupported layouts fail explicitly.
`DESTROY_CONTEXT` removes the handle and waits for firmware cleanup when required.
A busy cleanup has a bounded retry. Uncertain cleanup retains the owner until reboot.

`CREATE_FREE_LIST` allocates native 4 KiB PM pages and fills the protected backing BO.
One firmware free list can own a BO at a time, including aliases through other VMs.
The owning mapping cannot be removed while the free list is live.
The firmware's maximum page count remains the allocation limit; allocation may fail.
Free-list growth and reconstruction requests are not implemented in this slice.
Such requests stop the firmware service instead of reporting false completion.

`SUBMIT_JOBS` supports only the fixed BXM compute stream.
It requires the current 48-byte job and 16-byte sync-operation layouts.
Unsupported stream extensions and job types return `EOPNOTSUPP`.
HWRT datasets, geometry, fragment, and transfer jobs remain unsupported.
All job streams and synchronization records are copied before firmware execution.
The driver acquires context references and prepares timeline storage before submission.

Each job waits at most ten seconds for dependencies.
The driver then publishes pending page-table changes and synchronizes mapped buffers.
It submits a compute command followed by a real firmware UFO update.
KICK accepts a zero return word or `CMD_EXECUTED`; unknown return bits fail.
Neither word proves job completion. CLEANUP keeps its synchronous return-word check.
Success requires queue consumption, the expected UFO value, and fresh firmware health.
Only that success publishes a completed binary fence or timeline point.
A failure leaves the current and later signal operations unsignaled.
`jobs.count` reports the failing job index, or the completed count on success.
An interrupted wait returns `EINTR`; it must not restart an already partially completed batch.

## VM and DMA changes

A firmware memory context holds the native page-catalogue address.
Published VMs retain removed page tables until TLB invalidation completes.
GPU completion precedes POST synchronization and exposure of buffer results.
The last VM reference first quiesces its firmware use, then releases its tables and BOs.
A failed invalidation retains those objects for reboot recovery.
See the [VM ownership contract](a733-pvr-vm.md).

## Source checks

Run the production-source contracts with a supported host sanitizer:

```sh
for name in a733-gpu-mips a733-gpu-fwprobe a733-gpu-fwservice \
    a733-gpu-fwbridge pvr-stream pvr-context pvr-freelist pvr-exec \
    pvr-submit pvr-vm pvr-file pvr-bo pvr-render; do
    CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
        sh "ember/tools/$name-contract.sh"
done
```

The checks cover actual encoders, queue wrapping, completion order, DMA ownership,
allocation failures, interrupted waits, handle removal, and bounded cleanup.
KICK checks cover both return words, missing UFO or CCCB completion, fresh health failure,
and invalid return bits. A zero CLEANUP return word remains an error.
They do not establish physical DMA behavior or sustained hardware stability.
GCC16 and Clang target analysis and Cppcheck supplement these executable checks.
Recorded fixture path-budget limits and inherited header diagnostics remain separate.
Valgrind does not support the macOS ARM64 test host; ASan/UBSan is a different check.

Ports owns the [Mesa consumer and package](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-graphics/cross/powervr.md).
A board receipt must include the kernel and module hashes, firmware, DTB,
consumer and Mesa hashes, result bytes, queue completion, and fresh fault counters.
Keep Mesa's `PVR_I_WANT_A_BROKEN_VULKAN_DRIVER=1` experimental gate.
A successful compute check does not establish Vulkan conformance or rendering.

## Physical compute boundary

GCC16 kernel `e876bf6d2ce282ae7770078af8f3455866c940cb` passes on A733 Zero 3W with 4 GiB RAM.
The PCB revision was not recorded. Firmware is `1.1.6976702`, BVNC `36.56.104.183`.
The explicit PLL600 preparation and all 21 diagnostic stages pass before native activation.
The accepted kernel ELF SHA256 is `33389891548cca65e87283804f2a08f05d71eaffd4bab3935f91970e4591070a`.
Matched modules and DTBs come from the same cross-build.

Mesa `26.2.4nb5` and libdrm `2.4.134nb2` use the native render node.
The Ports consumer SHA256 is `438a0dc7191abde38a3639f6562a16375ad3ccf5a475902affc3f08861f3ef84`.
Three consecutive create/dispatch/destroy lifecycles pass within one boot.
Each checks all 16 output words, a completed Vulkan fence, and host readback.
The execution service stays enabled after cleanup; SSH and boot identity stay unchanged.
These are bounded functional runs, not an endurance or performance result.

The prior kernel returned `EIO` on the first KICK before its completion checks.
Mesa mapped that errno to `VK_ERROR_OUT_OF_DEVICE_MEMORY`; this did not establish allocation failure.
The corrected path accepts the asynchronous zero return word and checks actual completion.
`ktrace` confirms a successful `SUBMIT_JOBS` ioctl on the corrected kernel.
The upstream experimental gate stays enabled. No Vulkan conformance is claimed.

## Remaining rendering path

Zink needs working graphics jobs and HWRT datasets in addition to compute.
Native A733 display support also needs DE/TCON/HDMI, KMS, and shared-buffer ownership.
Accelerated KWin and a complete Plasma Desktop session require separate physical acceptance.
