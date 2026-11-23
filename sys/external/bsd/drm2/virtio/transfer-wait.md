<!-- Origin: EmberBSD; AI-assisted explicit 3D transfer and WAIT ownership contract. -->
# Explicit 3D transfer and WAIT

The driver retains each explicit 3D transfer until its request phase has ended
and reports its exact acceptance error. WAIT observes tracked request errors
and completion, including work hidden by reservation replacement. These source
and native software contracts leave VIRGL disabled. A matched kernel and live
DMA/graphics qualification remain pending.

## Admission and one operation ledger

TO_HOST and FROM_HOST name exactly one BO. Handle lookup and charged metadata
allocation finish before the final attachment mutex. Under that mutex, admission
requires a live nonzero context and software key, a ready device, an acknowledged
attachment, the [loaded-map eligibility proof](dma-eligibility.md), and an OPEN
[backing lease](backing-lifetime.md). Closing or stopped contexts reject. Duplicate
handles share the existing attachment entry; removing the last entry and accepting
a transfer serialize on the same mutex. A GEM reference alone is not membership.

The lock order remains attachment, BO reservation, submit. Admission holds the
attachment mutex through queue acceptance, not through the new GPU completion.
It waits all earlier ledger, exclusive and shared dependencies under one 15*HZ
fence deadline. Reservation acquisition is interruptible but untimed; this is
not a hard ioctl wall-clock deadline. A dependency error or signal returns before
PRE without resetting an otherwise healthy device.

The C3 ledger is now `operation_members`/`operation_pending`, with typed immutable
EXEC, TO_HOST and FROM_HOST arrays. There is no second transfer list. An EXEC
still snapshots all context attachments; a transfer includes only its explicit
BO. Both see each other's outstanding records after an exclusive reservation
replacement. Only a positively typed EXEC fence can use the existing same-context
EXEC skip. Transfers never use that optimization, and their fences are not EXEC.

Array/member and dependency scratch allocations share the existing 16 MiB C3
budget. The charge/free helpers retain their `exec_` names for that common budget;
this does not create an independent transfer allowance. Array charges last through
delayed free. Scratch arithmetic is checked before charging or allocating. The
preexisting command cookie, BO/map and reservation shared list remain outside
this metadata budget; the generic reservation API is unchanged.

## Directional request phases

All command, array/member, fence and dependency storage exists before PRE.
Common prepare runs after producer admission and safe classic fence emission:

| Operation | Full-map PRE | Matching POST |
| --- | --- | --- |
| EXEC | PREREAD + PREWRITE | POSTREAD + POSTWRITE |
| 3D TO_HOST | PREWRITE | POSTWRITE |
| 3D FROM_HOST | PREREAD | POSTREAD |

Registration precedes reservation replacement/unlock. A rejected ENOSPC attempt
closes its own phase before the bounded pressure wait, preserving its durable
record and original fence for re-PRE. Common finish closes the phase on success,
local rejection, host error or reset cancellation before callback, wait completion,
BO release or fence publication. Successful enqueue may already have freed its
cookie; neither helper accesses it afterward. Allocation failures perform no PRE.
Both 3D helpers and ioctls return the real acceptance errno and balance the caller
fence reference on every path. The existing u32 UAPI offset remains unchanged.

A request POST does not close the persistent backing lease, transfer CPU ownership,
or prove query DONE. The admitted coherent ARM64 backend permits these separately
paired phases; arbitrary bus_dma backends are not certified. Reset still joins
producers/dequeue owners, recovers cookies, closes leases and publishes terminal
fences in that order. C2's zero-reference UNREF raw owner remains separate.

## Driver-local WAIT

WAIT rejects unknown flags. With a reservation held, it takes bounded temporary
references to the BO ledger, and checks both the exclusive fence and every shared
fence. The shared count has its own authority and is not limited by the native
ledger capacity. Reservation ownership stabilizes the lists; temporary references
protect each inspected fence. No DMA lock is held during waits or sync operations.

An initial scan checks every already-known error before blocking on another pending
fence. Native cookie errors behind the contiguous publication prefix and terminal
foreign errors are visible. Blocking waits share one interruptible 15*HZ deadline.
Local unfinished/timeout returns EBUSY; signals and stored fence errors remain
exact, including stored ETIMEDOUT at the budget boundary. WAIT does not reset a
healthy device on timeout. NOWAIT uses `GFP_NOWAIT` scratch and reservation trylock,
never waits for reservation ownership or GPU completion, and returns a real
allocation error or EBUSY when necessary. Native GFP_NOWAIT maps to KM_NOSLEEP.

WAIT establishes completion of its tracked request snapshot. It grants neither
exclusive access to an mmap nor query DONE, and does not retire an OPEN lease.

## Reproduction and evidence limits

```sh
sh ember/tools/virtgpu-transfer-contract.sh
TRANSFER_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/virtgpu-transfer-contract.sh
```

The fixture extracts production ioctls, both 3D helpers, common queue/finish,
operation phases and dependencies, membership/budget functions, native reservation
replacement/getters, fence status and publication. It uses the prior EXEC fixture;
`SUBMIT_SOURCE_ROOT` selects an immutable baseline. Current host checks pass 59
new groups, 204 affected prior groups and focused ASan/UBSan. The baseline compiled
and exposed the lost errno, missing FROM PRE, unpaired error POST and WAIT errors.

The same 59 new and 204 prior groups pass natively on NetBSD 11/AArch64 with
GCC 16.2. A clean export of `089544e22b2b02b5d51806392a7263d1fc0be8f5`
regenerated a VirtGPU trial configuration and dependencies and compiled fourteen fresh
driver objects with base GCC 12.5 and normal `-Werror`: 2.64 seconds, peak RSS
67068 KiB and zero swaps. The guarded run completed with status 0. It did not
link a kernel, install files or boot the changed driver; these objects must not
be linked with unrelated objects from the partial build directory.

Allocators, low-level locks, transport, scheduling, fence waiting and bus sync are
controlled seams. The old generic WAIT seam provides completed readiness, exposing
the caller's failure to inspect errors; it is not a replacement implementation.
The zero-reference case checks POST before the final array put; actual map/ID
retirement is covered separately by the backing contracts. These checks do not
prove native IRQ races, cache behavior or hardware execution.

The subsequent [controlled console and 2D stage](controlled-console.md) uses this
same ledger for 2D uploads, replaces legacy lifetime ATTACH phases, removes both
manual compensation POSTs and the success-only opcode helper, and rejects private
console GEM/context reachability. Its host qualification boundary remains explicit.
Full 3D format/mip/box/stride bounds, arbitrary user mmap exclusion, delayed query
completion, live host behavior and acceleration remain separate gates.
