<!-- Origin: EmberBSD; AI-assisted finite 2D phases and private console contract. -->
# Controlled console and legacy 2D

This source stage pairs finite ATTACH and 2D upload phases and orders kernel
console copies after prior operation POST. It does not enable VIRGL or certify
portable DMA, a live host mapping, or a graphics session. Native object checks,
a matched kernel and runtime mapping qualification are separate gates.

## One owner for each phase

A non-required ATTACH cookie owns full-map PREWRITE/POSTWRITE, including local
rejection, host error, ENOSPC attempts and joined reset cancellation. ATTACH
allocates command, BO array, wait and fence before PRE. Ending that finite token
retains the ordinary pins, vmap, DMA map and host addresses. It creates no OPEN
C2 lease. The former lifetime PRE/final POST and successful-opcode POST are gone.

Every 2D upload has a typed TO_HOST member in the existing durable operation
ledger and uses full-map PREWRITE/POSTWRITE. Array/member and dependency storage
share the existing budget. Command, wait, fence and scratch exist before PRE.
With reservation ownership held, it inspects all prior ledger, exclusive and
shared dependencies under the existing 15*HZ dependency deadline. Replacing the
exclusive reservation slot does not hide a pending member. No same-context
EXEC optimization applies to uploads.

Prepare registers the member before publishing/unlocking the reservation. An
ENOSPC rejection POSTs its attempt before pressure waiting; the durable member
and fence survive for another PRE. Finish owns exactly one POST per prepared
attempt on success, rejection or cancellation, before callbacks, wait completion,
terminal fence readiness and BO puts. Both former caller compensation POSTs
have been removed. Neither WAIT nor a completed upload excludes arbitrary
userspace mmap access or establishes query DONE.

All existing callers allocate a TO_HOST operation array. The command helper
consumes that array on every exit. A supplied fence means the caller has already
locked its reservation and retains its own fence reference. That route never
passes a NULL fence to an implicitly relocking synchronous helper. The NULL
route allocates its own fence and acquires the reservation once. Errors preserve
their errno; cursor code pings only after successful synchronous upload and has
no redundant unbounded fence wait. Cursor-ring queuing performs no CPU copy and
opens no artificial backing phase.

## Resource and zero-reference retirement

Before possible CREATE exposure, object creation registers a producer and
publishes `dma_resource_retained` in the existing backing registry. This is
host-resource/ID ownership, not a DMA PRE or C2 lease. The producer spans CREATE,
ATTACH and failure unwind. Even CREATE uncertainty or ATTACH metadata OOM followed
by UNREF metadata failure cannot free or reuse a possibly retained host ID.
Local backing setup failures still release never-exposed local maps without POST.

A C2 ATTACH transfers that same registry record to its OPEN lease; it does not
publish a second node. Legacy backing retains the resource guard with lease NONE.
Successful fenced UNREF clears the guard. A failed allocation, exhausted classic
timeline or invalid response instead retains it for reset. Reset joins producers,
dequeue/POST owners and cancellation cookies before clearing resource guards or
closing C2 leases. Registry removal uses a temporary raw retirement pin; concurrent
zero-GEM-ref release records release_pending and cannot resurrect or free the BO.
Map unload, ID release and final free require no resource guard, operation/member,
retirement pin or OPEN/CLOSING lease. No special cleanup fence ID exists.

## Kernel-private console

The [firmware framebuffer contract](../../../../../ember/boot/firmware-framebuffer.md)
keeps the existing boot reserve/commit API. Descriptor queries and cancellation
apply only to an uncommitted reservation. They do not change runtime ownership.

Only the console probe sets the immutable `private_console` creation parameter,
before host backing exposure. Object attach clears the initial mapped BO, including
page padding, after vmap and before ATTACH PRE. The probe never writes exposed BO
pixels afterward. Ordinary userspace dumb and cursor BOs do not inherit privacy.

The VirtGPU framebuffer create_handle callback refuses an owned private BO before
calling the generic helper, so GETFB cannot insert an IDR entry or grant VMA access.
Generic core grants those rights before invoking GEM open; a concurrent MAP and
native mmap could otherwise retain a mapping after the later refusal/master drop.
GETFB2 is not wired in this source tree. GEM open still refuses the private BO
before the non-VIRGL fast return as defense for new handles and same-device PRIME.
The private test first whitelists the owned core object before container
conversion. Context attach, DMA admission and whole-context snapshots also reject
it. Privacy excludes encoded/query
reachability; it does not remove C1 qualification or the C2 bidirectional lease
for a VirGL-capable dumb BO, nor extend C1 overlap to arbitrary bus_dma backends.

Rasops still draws into its separate shadow; wsdisplay mmap remains absent. The
worker holds the console/master transition mutex and consumes damage before
upload. The controlled copy helper allocates all metadata, retains a BO reference,
acquires its reservation and completes dependencies before copying. A registered
producer protects copy-to-queue against reset; a readiness-only wake is not
permission to copy. The BO reference and reservation survive through acceptance
or rejection. Bounds, master/mode serialization and dirty retry remain in force.
Concurrent shadow drawing may tear one snapshot; its final damage notification
ensures another upload. This narrowly guards private framebuffer publication and
does not reorder generic DRM handle creation or provide arbitrary userspace mmap exclusion.

## Pinned host qualification boundary

The source proof covers QEMU `37ba092d59aff24900dfd0d5e01d4ed68441ba07`
(UTM 4.7.5 dependency) and `6601422e1fff2da1376faafb1e4c2c5cdb2d8003`
(UTM 5.0.6 beta dependency). Both software and GL device classes dispatch
CREATE_2D even when the guest has not negotiated VIRGL. The GL class uses its
renderer texture path; absence of VIRGL does not select the software class.
Renderer references are the UTM `dc039d9ecd74fc671a85bfbe7c4e4bc552b7b855`
and `5d26f605f50f8e22002ec6db5fb775e1992d4e96` source snapshots used by the same pinned dependency audits.

Both classes map backing entries with TO_DEVICE. For non-direct mappings,
address_space_map allocates a host BounceBuffer and reads guest pixels at ATTACH.
Later uploads read that retained buffer without remapping or refreshing it.
Guest PRE/POST cannot update a retained host bounce snapshot. The negative stale
bounce model remains unsupported for both classes.

A working path requires every complete DMA segment to remain stable writable
ordinary guest RAM, with the same bytes as the CPU/UVM mapping across all splits.
Host bounce, MMIO/RAM-device, ROMD, translated apertures and lifetime remapping
are excluded. Guest C1 does not prove the host FlatView; no guest feature bit
establishes that property. Launch observations of virt/HVF, 4 GiB, virtio-ramfb
and GL off are configuration facts only. Live FlatView/segment correlation is a
later safe runtime gate. Migration, hotplug and backend changes require preserved
mapping invariants or separate qualification.

Pinned primary paths: [software dispatch/backing](https://github.com/utmapp/qemu/blob/37ba092d59aff24900dfd0d5e01d4ed68441ba07/hw/display/virtio-gpu.c),
[GL dispatch](https://github.com/utmapp/qemu/blob/37ba092d59aff24900dfd0d5e01d4ed68441ba07/hw/display/virtio-gpu-gl.c),
[DMA direction](https://github.com/utmapp/qemu/blob/37ba092d59aff24900dfd0d5e01d4ed68441ba07/include/system/dma.h),
[direct/bounce mapping](https://github.com/utmapp/qemu/blob/37ba092d59aff24900dfd0d5e01d4ed68441ba07/system/physmem.c),
[RAM/ROMD distinction](https://github.com/utmapp/qemu/blob/37ba092d59aff24900dfd0d5e01d4ed68441ba07/include/exec/memory.h).
The beta paths at the second exact commit preserve these inspected distinctions.
This is source provenance, not installed-binary or live-mapping verification.

## Reproduction and evidence

```sh
sh ember/tools/virtgpu-controlled-2d-contract.sh
sh ember/tools/virtgpu-console-private-contract.sh
sh ember/tools/virtgpu-framebuffer-publication-contract.sh
CONTROLLED_2D_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/virtgpu-controlled-2d-contract.sh
CONTROLLED_2D_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/virtgpu-console-private-contract.sh
PUBLICATION_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/virtgpu-framebuffer-publication-contract.sh
```

The original 51 focused host groups cover actual production copy/upload, queue,
dependency, ledger, finite ATTACH/reset/free, GEM/PRIME/context/snapshot refusal and console
probe functions. Six additional groups execute actual framebuffer init/GETFB,
create_handle, core publication, MAP and native locked mmap functions, with an
interleaving at VMA allow. Private refusal precedes any IDR/VMA side effect in both
feature modes; ordinary and non-master behavior remains intact. The same fixture
also runs eight existing full callback groups. Earlier console/master/dirty retry
and cursor status cases remain
in the affected existing contracts. Allocators, transport, host-class map reads,
fence waits, low-level locks and bus sync remain controlled seams. Host checks and
focused ASan/UBSan pass. The accepted C7a baseline compiles and exposes the absent
ledger/private ownership, premature PRE and zero-reference retirement defects;
its console probe separately fails the creation-flag assertion. The pre-guard C7b
source returns EACCES but still allows transient IDR/VMA publication and persistent
native GEM/UVM mapping in both modes; the new six-group regression fails on it.

These checks do not establish native IRQ/SMP behavior, QEMU mappings, bus DMA,
a complete kernel, live graphics or physical hardware. Full 3D packet, format,
mip/box/stride validation and host error forwarding remain separate stages.

## Native contract and object gate

On 2026-10-07, source `647875172ceb243903994f48aef24f9cd5a0c78b`
(finite phases plus the prepublication privacy repair) passed all 325 distinct
groups on NetBSD 11/AArch64 with GCC 16.2: 57 new and 268 existing. The publication
wrapper includes eight existing resource groups; they were not counted or run
twice. Configuration and dependencies were regenerated, and fourteen fresh driver
objects compiled with base GCC 12.5 and normal `-Werror` in 3.33 seconds,
with 68,228 KiB maximum RSS and zero swaps. The bounded native guard returned 0.

The exact 379-path source export SHA256 is
`9196dee689eea6f9a15b315e52a1aecd44fdd287ba4f12767785e590054b851f`.
Native contract/object log SHA256:
`a35046a427d32ebbd14fb4d2a925df69002bd56a3a00af93edbf4a145c2e107c`.
Receipt SHA256:
`c9cf9f17b745e6fbff023eb002d46d9bbf91a9133936944fec8c62b7f120d179`.
Source and the focused privacy repair passed independent review. These are native
software/object checks: the partial object directory is not a matched kernel.
No kernel was linked, installed or booted; VIRGL remains disabled. Actual SMP/IRQ,
host mappings and the restored visible console still need runtime acceptance.
