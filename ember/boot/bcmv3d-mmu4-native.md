# Private native MMU4 display owner

This source-only component combines native backing, the fixed MMU4 sequencer
and the shared ICAC executor. Default profiles do not call it. The manual
`EMBERV3DCONSOLE` profile calls it after its same-owner dual-pipeline stop.
Completion does not prove physical translation, scanout, display ownership or AXI drain.

The canonical BCMV3D ACPI attachment passes its actual `aa_memt` through the sole
`bcmv3d_render_attach` call. The renderer stores that tag and one private owner
slot. The added fields and attach signature require a coherent kernel, renderer
and client rebuild. They are not an overlay on an older kit. Ordinary configs
stay unchanged. The adapter, backing and frame-helper TUs require all three
existing selectors: `bcmv3drender`, `bcm2712_display_components` and
`bcm2712iommuc_acpi`. The provider alone has no DRM dependency.

The opt-in `EMBERV3DHDMI` source-selection profile includes `EMBERV3DRENDER`,
attaches the ICAC software owner and selects `WSDISPLAY_BOOTFB_DEFER`.
It links the three adapter translation units with the cold framebuffer issuer.
It is an integration profile. It does not select an exclusive console path,
close readers, issue firmware handoff or start MMU4, scanout or a desktop.
The ICAC attachment validates its descriptor without mapping registers.
Use a coherent GCC16 kernel and matching modules. The ordinary `EMBER64` and
`EMBERV3DRENDER` profiles keep their existing options and attachments.

The private renderer attach returns the actual registered `bcmv3d_device *` only
on success. An error preserves output storage. The ACPI owner stores this borrowed
boot-lifetime object. A read-only validator compares its device, bus-space tag,
DMA tag, ACPI handle and opaque boot owner. No global render-minor lookup or copied
status bit can replace that exact tuple. The signature change needs a coherent
kernel and module rebuild.

The manual profile keeps normal active resident devices. It has no competing
modesetting driver, suspend, detach or manual display probe. After both PV/HVS
pipelines stop, its fixed continuation rechecks the exact issued CLOSED lease,
complete CPU close/drain, actual held serial route, successful firmware result,
both per-pipe stop results and all four held clock records. Under Giant, it marks
one attempt before any map or allocation. It releases the finalizer's entire
Giant recursion across native calls, then restores that exact depth before
publishing the terminal result. It holds no DRM or ICAC lock across those calls.

MOP and MOPlet share MMU4. Disabled HVS channels alone do not prove writeback idle.
The owner retains fixed resource7 `0x107c500000/0x28` and resource8
`0x107c501000/0x20` mappings through its real bus-space tag. It reads each
TXP_DST_CTRL at `+0x0c`. GO bit0 or BUSY bit1 refuses the attempt before native
create, ICAC observation or local MMU4 access. There is no ABORT, reset or write
through these mappings. A read failure is terminal. A map or raw sample is not
an independent access grant.

The same owner then calls real native create, prepare and program in that order.
It queries the actual cached native status after prepare/program, including errors.
A diagnostic error cannot replace an earlier operation error. Success requires
completed native and sequencer stages, actual prepared/sealed backing, completed
shared flush and retained MMU4 lease. Every outcome retains the owner, renderer,
clock providers, mappings and allocated backing until reboot. There is no retry,
rollback, unmap or fallback after CLOSED. This stage does not enable HVS/PV, publish
a display list, allocate HVS SRAM/UPM/COB ranges or start KMS. The same selected
profile then enters the separately guarded [fixed HVS/PV0 publication](bcmv3d-hvs-publish.md).
The separate [retained range mechanism](bcmv3d-mmu4-range.md) can publish and
unmap the three never-HVS-latched private GEM windows. It does not unmap the
initial frame or establish active scanout retirement. No KMS caller uses it yet.

The joined actual-source check is:

```sh
ruby ember/tools/bootfb-serial-handoff-contract.rb "$PWD"
SKIP_MUTANTS=1 CFLAGS='-DNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer' \
    ruby ember/tools/bootfb-serial-handoff-contract.rb "$PWD"
```

It composes actual close, firmware, clock, dual-stop, native MMU4, ICAC executor,
backing, frame helper and encoder bodies. The real renderer attach/output/tuple
body is checked by `bootfb-claim-job-contract.rb`. The joined fixture uses controlled
renderer storage and a tuple service; it does not establish native ABI or hardware
identity. ACPI, DRM, heap, DMA/pmap, scheduling, mailbox and bus services are
controlled. Explicit fixture reboots free only fake allocations between independent
boots. No production cleanup API is introduced. Tests cover prerequisite refusals
before map, both writeback masters, map/read faults, reentry, exact operation order,
status failures and retained first errors. Compiled causal controls must reach a
named CHECK; `NDEBUG` does not disable it. ASan/UBSan is host execution, not Valgrind
or a kernel memory checker. The coherent GCC16 kernel and four matched modules
linked successfully. Paired actual target-TU Cppcheck, GCC analyzer and Clang Static
Analyzer checks found no new safety defect. Existing style, inter-TU unused-symbol
and inherited analyzer findings remain classified separately. The complete manual
owner completed MMU4 programming and a fixed HVS test frame on BCM2712 hardware.
The retained range check also completed with active initial HVS output.
These results do not establish mutable HVS submission, retirement or DRM/KMS.

Writeback facts derive from pinned
[vc4_txp.c](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_txp.c#L140-L144)
and [BCM2712 shared-MMU4 masters](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/arch/arm64/boot/dts/broadcom/bcm2712-ds.dtsi#L284-L290).
Only register and ordering facts inform this independent BSD implementation.

`bcmv3d_mmu4_native_create(sc, out)` validates native device/ACPI attachment,
registered DRM/render identity, canonical GPU0, the full v1 resource contract,
typed ICAC dependency and actual attached ICAC provider. It pins a DRM memory
reference; this does not prove hardware liveness. A short renderer accounting
lock publishes one owner slot. A losing creator frees only fresh unexposed
metadata and its reference. Create does no map/MMIO/backing allocation.
Outputs need accessible disjoint storage and stay unchanged on error.

BEFORE prepare, establish normal-active SoC fabric access, real held clock
lifetimes, console/client handoff and exclusion of all firmware, MMU2/4/5/cache
writers and local MMU4 clients. Keep those conditions for the entire retained
lifetime, including errors. They precede the FIRST ICAC or MMU4 map/read.
Missing clock properties, no framebuffer, DRM references and returned samples
do not prove them. HVS global ENABLE with disabled channels is not handoff.

Prepare consumes one attempt. It obtains real ICAC cached status. Only a
pristine uninitialized/unconsumed provider performs a new one-shot CONTROL read.
It then requires COMPLETE, mapped/valid data, exactly one attempted/completed
read, zero error and FLUSHING clear. All other raw bits remain diagnostics.
An existing accepted prefix
gets no new read. Reuse needs the same externally continuous access/exclusion
lifetime since that sample. Unknown continuity forbids prepare. Returned
EALREADY is failure even if a following snapshot is accepted. Failed, partial
or consumed prefixes never map the local MMU.

After that prefix, prepare maps GPU0 resource4 at `0x1000005200/0x80`, reads
DEBUG+0x38 once and constructs actual backing/config. Its frame, four control
allocations and large plan/image are allocated outside ICAC. No backing
PREWRITE or register program occurs here. Failure consumes the attempt and
keeps the published owner plus remaining resources.

Program rechecks cached admission before irreversible seal. This guard does
not re-observe CONTROL or DEBUG. The actual sequencer later performs its own
mandatory fresh DEBUG/CONTROL/SHOOT_DOWN preflight. Samples grant no permission.
Separate cached calls are not a cross-call lock. Trusted callers exclude
competing operations; the provider rechecks admission under its own mutex.

Lock order is local owner then ICAC. The fixed synchronous callback uses a
local actual sequencer. Init, program, status and any proved untouched fini
stay inside the executor. Actual backing prepare validates mappings, encodes,
zeros/copies and completes all five full PREWRITE syncs without allocation.

The closed backend uses only these resource4 keys:

|Key|Offset|Direction|
|---|---:|---|
|DEBUG|0x38|Read|
|CONTROL|0x00|Read/write|
|SHOOT_DOWN|0x18|Read|
|MISC|0x24|Read/write|
|CAP|0x14|Write|
|BYPASS_START|0x1c|Write|
|BYPASS_END|0x20|Write|
|PT_BASE|0x04|Write|
|ILLEGAL_ADR|0x30|Write|

Each access checks direction, alignment and the full four-byte span. Before
a local write the owner latches its attempt and checks seal, running sequencer
and actual retained shared MMU4 lease. A whole-window READ|WRITE barrier follows
each write. The pure core supplies values/order. Native void accesses can stall
or abort; the pure poll limit cannot recover them.

Native and actual-core faults stop later backend IO. Held-lock protocol faults
are sticky; unlocked misuse does not mutate owner state. Public prepare/program
also refuse a prior sticky fault before new work. Repeats preserve their original
result first. No retry, fault reset or replacement owner exists.

Sequencer status is copied BEFORE untouched fini. Primary and close errors stay
separate. After the synchronous executor and ICAC snapshot, the owner refreshes
actual backing status on success and failure. The cached prepared, sealed and
first-error fields therefore describe the completed attempt. A failed backing
snapshot clears `backing_valid`. Its error fills an empty `status_error` and
uses the existing sticky fault path. It replaces the operation result only when
no earlier operation error exists.
Never close retained, mutated, faulted or unknown state. Shared status failure
is an operation error if no earlier error exists. Only value snapshots escape.
Borrowed stack/core pointers are cleared before return. The shared lease survives
the local object's scope. Success and errors keep the owner, remaining maps,
backing, callback text and render/device/tag references until reboot. No free,
disable, reset, restore or detach API exists. Completion is protocol only.

Run the actual-source host contract from a complete checkout:

```sh
ruby ember/tools/bcmv3d-mmu4-native-contract.rb
```

The fixture reuses complete actual descriptor/provider/cache/sequencer bodies.
Actual backing/helper/encoder TUs use the existing controlled native services.
The create bridge checks the adapter device argument, then uses that fixture's
controlled renderer/DMA/pmap model. The prepare wrapper calls unchanged actual
prepare and checks no allocation plus all five syncs. Cached/observation wrappers
inject scoped errors/races with real provider calls. A fini wrapper introduces
a retained lease for close-error tests. Kernel namespace/device/mutex/DRM/bus/heap
services are controlled. This is not hardware or a kernel mutex stress test.
Each child models a fresh boot. Mutants must compile and fail CHECK with SIGABRT.
The parent propagates that signal without manufacturing a CHECK. Child SIGSEGV,
abort without CHECK and setup failures do not count. Bounded process-group
timeouts reject hangs and keep raw outputs. These harness limits do not bound
native bus access.

Use CC/CFLAGS and MMU4_NATIVE_COMPILE_ONLY=1 for GCC16 target compilation.
TEST_RUNNER runs target fixtures on a supported host. MMU4_NATIVE_EVIDENCE keeps
inputs/commands/raw outputs. MMU4_NATIVE_SKIP_MUTANTS=1 skips source mutations.
All four harness controls still run. They intentionally exercise signals and
timeouts; record the sanitizer verdict for the positive separately.
MMU4_NATIVE_MUTANTS selects source controls. Compile-only is not execution. Native TU
checks need selected-kernel per-file architecture/DRM flags and real headers.

See the [backing](bcmv3d-mmu4-backing.md),
[sequencer](bcm2712-mmu4-init-core.md) and
[ICAC owner](bcm2712-iommuc-native.md) contracts. Register facts derive from
[Raspberry Pi Linux bcm2712-iommu.c](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/iommu/bcm2712-iommu.c).
Its lack of clock/reset/power requests is prior art, not an always-on ACPI
guarantee. No GPL implementation is copied. Only the explicit manual profile
contains the bounded same-owner boot continuation described above.
