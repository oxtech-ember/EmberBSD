# A733 native PowerVR VM ownership

The native render driver creates per-file GPU address spaces.
Each space has real DMA-backed Rogue page tables and retained GEM references.
The interface also reports the BXM-4-64 heap and static-area layouts.
These operations prepare memory for a later execution interface.
They do not submit jobs or make Mesa Vulkan usable yet.

## Source and ABI

The UAPI and heap definitions retain the MIT alternative from
[Linux `7b63ef2`](https://github.com/torvalds/linux/tree/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination).
EmberBSD supplies native DMA, GEM lifetime, file isolation, and table ownership.
The implementation and regression fixtures are AI-assisted.

`CREATE_VM_CONTEXT` returns a nonzero handle owned by the open DRM file.
`DESTROY_VM_CONTEXT` removes that handle and releases its mappings.
Reserved fields must be zero. Unknown and foreign VM handles return `EINVAL`.
The current table profile requires 4 KiB host pages.
Other host page sizes return `EOPNOTSUPP` when creating a VM.

`VM_MAP` retains a buffer owned by the same file.
The GPU address, buffer offset, and size must be host-page aligned.
The size must be nonzero. The complete address range must fit one active heap.
The buffer offset and size must fit the allocated object.
Unknown flags, reserved fields, and invalid ranges return `EINVAL`.
A missing or foreign GEM handle returns `ENOENT`.

The first native implementation rejects overlapping mappings with `EEXIST`.
Linux supports replacement and splitting; those operations are not implemented.
`VM_UNMAP` requires the exact address and size of one complete mapping.
An absent or partial range returns `ENOENT`.
Different VMs can map the same buffer at the same address.

## DMA tables and lifetime

The Rogue profile uses 40-bit GPU addresses and 36-bit DMA addresses.
It has a 1024-entry catalogue and 512-entry directories and leaf tables.
Each table occupies one coherent 4 KiB native DMA allocation.
The allocator validates the loaded DMA span, alignment, and cache attributes.
It rejects bounce mappings. It does not use CPU addresses as DMA addresses.

Mapping allocates all missing tables before changing existing entries.
Allocation failure releases provisional tables and preserves existing mappings.
Successful mapping synchronizes leaves, directories, and then the catalogue.
Unmapping clears parent references before releasing empty child tables.
Tables are allocated only for used ranges. No diagnostic window limit applies.

Each mapping owns a GEM reference independently of the userspace handle.
Closing a GEM handle therefore does not invalidate its VM mappings.
The file mutex serializes mapping, unmapping, and VM handle removal.
DRM closes GEM handles before the driver's `postclose` callback.
That callback releases remaining VM tables and their retained buffer references.
Existing CPU mappings can survive both file closure and VM destruction.

`BYPASS_DEVICE_CACHE` and `PM_FW_PROTECT` select their corresponding PTE bits.
They do not prove GPU cache coherency or firmware access control in execution.
The buffer allocator still uses one contiguous native DMA segment.
Physical fragmentation can cause allocation failure.

## Heap queries

`HEAP_INFO_GET` reports all six upstream heap indexes.
The general, PDS code/data, USC code, visibility-test, and transfer heaps are active.
The RGNHDR heap has size zero because this BVNC does not require BRN63142.
Reported sizes reserve GPU address ranges, not physical memory.

`STATIC_DATA_AREAS_GET` reports five upstream static-area locations.
The query does not populate them. Mesa must allocate and initialize their BOs.
Both queries support null-pointer size and array-count discovery.
The minimum query size is 16 bytes. Heap and static strides are 24 and 16 bytes.
Longer output strides are zero-filled. Nonzero unknown input extensions return `E2BIG`.
The returned array count cannot exceed the supported layout.
Bad user pointers return `EFAULT`; failed copies can leave a written prefix.

## Reproducible checks

Run the real-source fixtures with host memory sanitizers:

```sh
for name in pvr-vm pvr-heap pvr-file pvr-bo pvr-render; do
    CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
        sh "ember/tools/$name-contract.sh"
done
```

The VM fixture checks actual table bytes, sparse mappings, and boundary crossings.
It injects allocation failures and validates synchronization before table retirement.
The file fixture checks references, isolation, cleanup, and concurrent map/destroy.
The heap fixture checks array strides, bounds, extensions, and copy failures.
Boundary models do not establish hardware DMA behavior.
Valgrind does not run on the macOS ARM64 test host; ASan/UBSan are separate checks.

Build the board utility from a clean commit with the common GCC16 compiler:

```sh
CC=/path/to/aarch64--netbsd-gcc SYSROOT=/path/to/emberbsd-sysroot \
    sh ember/tools/pvr-vm-check-build.sh /path/to/EmberBSD /new/vm-output
```

After [explicit render registration](a733-pvr-render.md), run:

```sh
/path/to/pvr-vm-check /dev/dri/renderD128
```

The utility checks 128 additional VMs and an 8 MiB GEM buffer.
It maps across 2 MiB and 1 GiB table boundaries and tests every active heap.
It closes the GEM handle before unmapping and leaves mappings for `postclose`.
The final CPU mapping must remain writable after both files close.
Record the source revision, tool hash, kernel hash, and output.

## Physical result

On 2026-10-13, Zero 3W passed 586 VM checks on kernel `9b2ded0e10b2`.
The board used BVNC `36.56.104.183`, 4 KiB host pages, and the existing DTB.
The VM, identity, and BO tools completed between 13:40:26 and 13:40:27 UTC.
The identity tool passed 72 checks. The BO tool passed 2,105,411 checks.
The separate compute probe also passed all 21 stages after this kernel boot.
It wrote `0x13579bdf`, reached queue 184/184 and timeline 1, with no fresh fault/HWR.
The watchdog remained disarmed after the checks; SSH remained available.

The VM utility hash is `728c3685b565bbc3db0b9b6275994d62a1266a6c10ab878523033c794b20f1db`.
The kernel hash is `4d2746be9afbee9ee874fe734a1c444884df0a6dc18019bd59ed96dfba1d7503`.
The boot-image hash is `d73cd563dc8038b59e082aa46a67d3a80830c551959967de3d9c25db5fbf272f`.
The matching module set came from the same clean GCC16 build.
The utility used the recorded EmberBSD libc sysroot; this is not a complete SDK audit.

Source contracts passed 30,564 table checks, 596 file checks, and 3,688 heap checks.
The unsupported 16 KiB VM profile passed 101 file checks.
Twelve table, nine file, and seven heap causal mutations failed as expected.
GCC16, Clang22, and Cppcheck2.22 analysis found no new confirmed safety defect.
Path-budget limits and inherited header/model diagnostics remain recorded separately.
These short checks do not establish long-duration stability or GPU access to the VMs.

## Execution boundary

All tables remain CPU-owned and unpublished to firmware.
The future execution interface must add firmware memory contexts and TLB invalidation.
It must retain VMs and buffers until jobs complete or recovery quiesces the GPU.
Do not reuse the current destruction path while hardware owns a table.
Runtime queries, synchronization, execution contexts, and DRM submission remain required.
The separate compute probe does not execute these userspace VM mappings.
HDMI/KMS and accelerated Plasma remain separate, unaccepted paths.
