# Typed shared IOMMU-cache mechanism

This source-only BSD core implements the fixed shared-cache enable/flush
transaction. The [private native owner](bcm2712-iommuc-native.md) supplies its
callbacks. The core has no mapping, attachment, MMU programming or power operation. The separate [ICAC resource contract](bcm2712-iommuc-resources.md)
describes geometry and dependencies. Neither contract grants hardware access.

The caller serializes all operations across MMU2, MMU4 and MMU5. These physical
member IDs share one cache. Logical lease bits are not hardware ownership.
The caller keeps the core, callbacks, callback argument and underlying authority
alive. It must not replace a retained or faulted core with fresh storage.

## Lifetime and transaction

Initialize fresh zeroed storage. Callback storage may alias `core.io`.
Reserve a typed member without I/O. Mark it retained before the first local-MMU
or shared-cache write. Only an untouched reservation can be released.
A retained member cannot be released, closed or reinitialized, including after
a successful flush. There is no source-backed shared disable/write0 operation.

A retained member can request one transaction:

1. Read fixed CONTROL+0. Reject FLUSHING=1. Preserve all raw bits for diagnostics.
2. Mark the shared write attempt before calling the write callback with3.
3. Order a successful write with the mandatory barrier.
4. Poll CONTROL at most1024 times. Completion tests only FLUSHING=0.
   Neither ENABLE nor FLUSH readback is required. Other bits remain diagnostics.

The maximum is1025 reads, including preflight, one write and one barrier.
This is an operation-count bound. Callback latency, native bus stalls and
aborts have no recovery or deadline guarantee. The command is exactly3, without
read-modify-write or echoing diagnostic bits. The pinned protocol polls only
FLUSHING. It does not define the meaning or write behavior of other returned bits.
The initial busy refusal remains a local exclusion guard.

Every valid transaction error latches the first global fault and retains all
member obligations. No subsequent operation performs I/O, retry, restore or
cleanup write. A preflight failure cannot authorize unwind: the caller might
already have changed a local MMU. Invalid arguments do not create a fault.
Status is a cached copy. Raw validity clears before each read and becomes true
only after success. Write failure can retain a valid preflight sample.
`mutation_attempted` counts shared-cache write attempts, not local-MMU writes.
`flush_complete` is protocol evidence, never an AXI-drain or free predicate.

## Checks and integration boundary

Run `sh ember/tools/bcm2712-iommuc-core-contract.sh`. The fixture compiles the
actual C source and header as separate translation units. Literal3/bit/poll
oracles are independent of production constants. It checks all member leases,
init aliasing, closure, re-entry rejection, completion at polls1/1024, timeout,
every injected callback failure, global fault retention and zero post-fault I/O.
High bits, including raw80000000, remain intact through preflight and polling.
All four low-bit states with FLUSHING clear complete. Compiled controls reintroduce
the old full-word/ENABLE guards and verify that unknown bits never reach writes.
Assertions in the fixture remain fatal when `NDEBUG` is set.

`CC`, `CFLAGS`, `TEST_RUNNER`, `IOMMUC_COMPILE_ONLY=1`, `IOMMUC_OUTPUT` and
`IOMMUC_EVIDENCE` support cross checks. `IOMMUC_SKIP_MUTANTS=1` runs the positive;
`IOMMUC_MUTANTS` selects compiled behavioral controls. Use the common GCC16
cross compiler and an EmberBSD sysroot for target objects. Execute source tests
on a supported host. Source tests do not accept physical cache or display use.

A future native owner must establish actual cache/interconnect accessibility,
canonical ICAC identity, common serialization and native/firmware exclusion.
It must prepare and synchronize the real MMU tables and default pages before
mutation. Default pages remain required for the proposed bypass path. Shared
flush completion does not complete local TLB work, transfer HVS ownership or
drain DMA. No suspend, detach, table release or shared disable protocol is
provided. Missing clock/power properties are not an always-on guarantee.

Pinned primary facts are Raspberry Pi Linux
`a1a601d4857978d810175365550c4536701df4ed`:
[cache command and bounded polling](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/iommu/bcm2712-iommu-cache.c#L15-L77),
[shared MMU relationships](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/arch/arm64/boot/dts/broadcom/bcm2712-ds.dtsi#L274-L305),
and [MMU table initialization](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/iommu/bcm2712-iommu.c#L205-L232).
Only register and protocol facts inform this independent BSD implementation.
