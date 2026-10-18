# Explicit retained DISP16 clock gate

`bcm2712_disp_probe` is an opt-in clock-only module. It uses the single
[shared clock core](bcm2712-display-clock-core.md) source with six private
function names. It does not replace an installed kernel or its HSM/BVB adapter.
There is no attachment, MMIO, reset, IRQ, frame DMA or HVS activation.
This source contract is not physical DISP16 or scanout acceptance.

GPU0 ACPIv1 declares HSM13. DISP16 is a fixed known SoC firmware clock ID,
not a new ACPI capability property. Each reply still requires validation.
DISP16 is one global HVS firmware clock. Its two lease slots are logical clients,
not HDMI ports. CORE4 being enabled does not establish DISP16 state or power.
Neither a firmware clock reply nor an IOMMU snapshot is a clock lease, power
operation, mapping, exclusion of another writer or a consumer-drain proof.

## Mechanism

The typed API is in
[`bcm2712_display_disp.h`](../../sys/arch/arm/broadcom/bcm2712_display_disp.h).
Create validates canonical GPU0 resources and reads clock16 STATE, RATE, MAX,
MIN. It performs no SET and requires `0 < MIN <= MAX`. A zero or unsupported
minimum fails; no rate is substituted. Initial off/zero is valid before acquire.
The firmware minimum is cached as the floor for the owner lifetime.

Acquire/update/release use `max(floor, active logical-client minima)`.
They query the current state/rate/maximum, set rate before enabling an off clock,
and verify rounded state/rate before committing the lease. Rounded rate must be
at least the requested minimum and no greater than the current maximum.
A first operation can reduce an inherited high rate. The caller must exclude
firmware and unregistered consumers before that operation. No SET_RATE0 is sent.
No BCM2711/GEN5 formula or guessed 75/120/500MHz floor is used for DISP16.

Last release requests the floor and leaves the clock enabled. Destroy requires
no leases or fault. It restores off only if this owner enabled an initially off
clock. An initially on clock remains on. Unacquired off destroy performs no SET.
Rate is not restored to an inherited zero or a previous high rate.

A failed SET or its verification latches fault before lease commit. The provider,
callback, mutex and module must remain resident. No mutation or destroy retries
an uncertain SET. A failed GET before the operation's first SET leaves its lease
unchanged without a mutation fault. Cached status sends no firmware query and
reports floor, both minima, last verified state/rate, leases, fault and mutation.
Failed create/status preserve output. The fixed owner is checked before any
provider dereference. Hard/soft interrupt calls are refused before sleeping.

## Finite module policy

Require `disp-mode=hold`, `cold-headless=true` and exact `expected-buildinfo`.
Reject clock/DVP/HD/HPD/DDC/PHY/IOMMU mode properties before effects.
Keep the framebuffer/FDT absence and registered native render/GPU0 guards.
Create the DISP owner and acquire logical slot0 at its positive firmware floor.
Keep the render reference, owner, mutex and module after verified hold.
INIT returns0; FINI returns `EBUSY` until reboot. A separate finite read-only
[IOMMU observer](bcm2712-iommu-observe.md) can then query the clock while this
real lease exists. This gate contains no automatic release or observation.

A known error before SET with verified zero leases/fault/mutation destroys the
provider, destroys the unlocked mutex with no callers/waiters, releases the
render reference, and returns the original positive errno. The adapter's private
init/fini hooks make clean load failure unloadable without leaking LOCKDEBUG
storage. A later clean retry starts with fresh context and a new mutex.

An uncertain SET, status, owner or cleanup result instead returns INIT0 with an
explicit retained-fault/no-success diagnostic and FINI `EBUSY`. It does not
release, restore, destroy the mutex or let callback text unload. Source tests
exercise normal release/destroy separately; the physical hold mode never calls
that path after acquisition. Future HVS users must keep the clock owner until
a separate reviewed stop/drain policy permits release. A retained static PHY
experiment can coexist under external writer exclusion. It is never unloaded
or acquired through this gate.

## Selected-kernel build and checks

Keep the selected installed kernel's `S`, generated headers and DRM flags.
Build only the explicit module Makefile with GCC16. Its six namespace macros
compile the same actual shared engine; no forked clock implementation exists.
Audit the exact current clock C/header, new DISP C/headers and Makefile as source
exceptions. The exact clockvar preinclude supplies the appended private profile.
No broad source include override, weak fallback or files.bcm2835 edit is used.
The old kernel native HSM/BVB slots and their mutex remain unchanged. The new
DISP-only lock serializes this module. Kernel mailbox transport serializes
packets; cross-ID atomic transactions are not promised. External writer exclusion
and module residency are caller obligations.

Require six defined `bcm2712_disp_private_*` symbols, no old common-clock import
or duplicate definition, typed DISP exports/hooks, and the exact kernel's strong
symbol/source/generated-header closure before load. Use
[`bcm2712-disp-probe-abi.c`](../tools/bcm2712-disp-probe-abi.c) for the selected
kernel/module layout comparison. A buildinfo string alone is insufficient.

Run `sh ember/tools/bcm2712-disp-probe-contract.sh` from a complete source tree.
The fixture executes the actual shared core, typed adapter, hold module and
bundled libfdt. It reuses the existing common-clock contract's firmware wire
service; kernel/device/property/resource/mailbox/mutex boundaries are controlled.
A harness-only reboot resets retained host storage between cases. It is never
reported as successful native fault cleanup. `CC`, `CFLAGS`, `TEST_RUNNER`,
`DISP_PROBE_COMPILE_ONLY=1` and `DISP_PROBE_OUTPUT` support cross checks.
Use `DISP_PROBE_SKIP_MUTANTS=1` for a host ASan/UBSan positive run.
Old frozen HSM transcript/layout checks remain intact; their unknown profile
input moves from2 to3 because2 is now DISP.

Pinned primary facts:
[DISP minimize/noncritical versus BVB critical](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/clk/bcm/clk-raspberrypi.c#L157-L170),
[firmware minimum/maximum](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/clk/bcm/clk-raspberrypi.c#L375-L393),
[rounding and minimum requests](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/clk/bcm/clk-raspberrypi.c#L267-L289),
and [enable/unprepare](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/clk/bcm/clk-raspberrypi.c#L292-L337).
Positive MIN, lazy activation, rate-before-enable and restoring only an owned
initial off-state are explicit BSD policy. GPL implementation is not copied.
