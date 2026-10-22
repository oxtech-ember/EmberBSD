# Fixed MMU4 initialization mechanism

This pure BSD sequencer implements one full-translation initialization profile.
`BCM2712_DISPLAY_COMPONENTS` links it when `bcmv3d_acpi` is selected. It has
no native caller, MMIO mapping, allocator, provider, automatic attachment
or power operation. The [fixed table encoder](bcm2712-mmu4-table-core.md) defines
four images and 2025 read-only frame pages, with aperture [0, 256 MiB) and frame
IOVA 0. The [shared-cache core](bcm2712-iommuc-core.md) supplies the actual typed
MMU4 reservation, retention and enable/flush mechanism. Neither grants access.

## Preparation and lifetime

Initialize fresh zeroed core storage. The small cfg contains expected DEBUG and
top/two L2/default PAs. It uses the encoder's fixed DEBUG/36-bit full-page policy.
The caller keeps the complete plan, image and preallocated native backing in
separate stable storage. No large object is copied onto the core's stack.
Init takes alias-safe cfg/io snapshots. Reinit is EBUSY in every initialized
state. Init/status perform no I/O and preserve output on error.

The required prepare_pages callback must validate its full plan against cfg,
use the actual encoder, copy/zero owned backing and allocation padding, and
complete full PREWRITE for table/default/frame storage before any MMIO read.
It must not allocate, publish pointers, perform MMIO or change a lease. Partial
preparation on error stays unpublished. A returned 0 or cached prepared flag is
not proof of native PA authority, synchronization, power or hardware readiness.
A future native adapter must implement and qualify those obligations.

The caller serializes the core and shared MMU2/4/5 operations. No callback may
reenter mutation, change shared leases or invalidate storage/arguments between
status, prepare and retain. Keep callback text, arguments, pages, mappings and
underlying owners alive. This core implements no global lock or registration.

## Fixed one-shot program

Program first reads cached shared status and reserves MMU4 through the actual
cache core. It never adopts a preexisting reservation. Prepare completes next.
Read DEBUG, CONTROL and SHOOT_DOWN. Require matching DEBUG, CONTROL == 0 and
SHOOTING bit 31 clear. These are conservative initial compatibility checks, not
firmware exclusion, client inactivity or a drain proof. Retain actual member 4
before the first local write.

Closed register keys select only the following fixed actions. They are not a
public arbitrary-offset interface. Each successful local write has a mandatory
barrier. Read/write/prepare errors are positive BSD errno values.

|Step|Offset|Action|
|---:|---:|---|
|1|0x00|write 0x0810100c; poll TLB_CLEARING bit 7 at most 1024 reads|
|2|0x24|fresh MISC read; clear SINGLE_TABLE bit 31, preserve every other bit|
|3|0x14|write 0x80000000: enabled 256 MiB exclusive cap|
|4|0x1c|write 0: bypass start/invert disabled|
|5|0x20|write 0: bypass end disabled|
|6|0x04|write top_PA>>12: aperture bias 0|
|7|0x30|write 0x80000000 OR default_PA>>12|
|8|shared cache|actual enable_flush(MMU4), including its own barrier/poll|
|9|0x00|write 0x04090803; read CONTROL for named postconditions|

The poll rejects ENABLE/BYPASS/sticky fields with 0x08101101. Completion requires
bit 7 clear. Command bits 2/3 are ignored. The bounded init poll is extra BSD
policy, based on the source's named range-sync poll. Final readback requires
`(raw & 0x070f0f03)==0x04090803` and `(raw & 0x08101080)==0`.
Other fields remain raw diagnostics. No whole-register, PT/CAP/default or
unsupported lock readback is assumed. Shared flush precedes final ENABLE.

Success uses eight local writes/barriers and 6..1029 local reads. The shared core
adds at most 1025 reads, one write of 3 and one barrier: at most 2054 reads and 9 writes/
barriers in total. These are operation-count bounds. Callback latency, MMIO
stalls and bus aborts have no wall-clock return or recovery guarantee.

## Error and closure rules

Program is one-shot even after a pre-write refusal. Changed DEBUG returns EAGAIN;
nonzero initial CONTROL or SHOOTING bit 31 returns EBUSY. Poll/postcondition failure
returns EIO and exhausted polls return ETIMEDOUT. Callback/cache errno is preserved.
Status caches the last stage/step, counts, poll, raw register/validity, first_error,
fault, close_error, preparation, owned reservation, retention and completion.
Raw validity clears before each read and is set only on success. Mark every
local write attempt before its callback. No barrier follows a failed write.

Before retain, first_error records refusal, not sticky fault. Clean fini can
release only this core's own untouched reservation despite first_error. It does
not close the shared cache or release foreign leases. A release failure records
close_error separately, preserves first_error and retains storage/obligations.

After retain, every error is sticky and success also retains owners. No later
I/O, barrier, retry, reinit, restore or cleanup write follows a fault. A local
fault does not fabricate shared global fault, but member 4 remains retained.
The future native authority must block inappropriate other-member activity.
Fini is EBUSY after retention, mutation, fault or during a callback. There is no
disable/write0 or owner/page-release protocol. COMPLETE is a sequence result,
not working translation, HVS publication, stop/drain or permission to free.

## Source checks and acceptance boundary

Run `sh ember/tools/bcm2712-mmu4-core-contract.sh`. It compiles unmodified real
sequencer, cache core, encoder and fixture as separate translation units with
real headers. The fixture uses the actual encoder and copies fake backing at
the preparation boundary; it does not implement native bus_dma synchronization.
Tests cover literal order/words/barriers, retain before writes, no foreign-lease
adoption, every fallible callback, pre-retain clean closure, separate close error,
cache error/no-final-ENABLE, first/last poll, both timeout bounds, fresh MISC,
named-bit rejection/ignored fields, PA/config bounds, init aliasing, one-shot,
reentry defenses, cached status and no post-fault I/O. Each case is a fresh fake
world, not recovery of a retained native owner. CHECK is fatal under NDEBUG.

`CC`, `CFLAGS`, `TEST_RUNNER`, `MMU4_CORE_COMPILE_ONLY=1`, `MMU4_CORE_OUTPUT` and
`MMU4_CORE_EVIDENCE` support cross checks. `MMU4_CORE_SKIP_MUTANTS=1` runs the
positive; `MMU4_CORE_MUTANTS` selects compiled controls. Use GCC16 for target
objects. ASan/UBSan source tests are not Valgrind or physical acceptance.

A native owner still needs PA/DMA correspondence, full allocation/sync geometry,
shared cache accessibility/exclusion, every MMU4 master, firmware handoff, actual
power/interconnect and stop/drain. GPU0 resources, clocks, ICAC_DEP, CONTROL == 0 and
disabled HVS channels establish none of these. Source success authorizes no MMIO.

Pinned facts are Raspberry Pi Linux `a1a601d4857978d810175365550c4536701df4ed`:
[named register fields](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/iommu/bcm2712-iommu.c#L24-L85),
[initialization/default/cache-before-enable](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/iommu/bcm2712-iommu.c#L147-L232),
and [bounded TLB range-sync poll](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/iommu/bcm2712-iommu.c#L398-L445).
Only numeric/protocol facts inform this independent BSD implementation.
