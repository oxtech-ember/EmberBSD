# Private native MMU4 display owner

This source-only component combines native backing, the fixed MMU4 sequencer
and the shared ICAC executor. It has no default caller or attachment action.
It does not establish physical access, translation, scanout, handoff or drain.

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
read, zero error, known bits and FLUSHING clear. An existing accepted prefix
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
guarantee. No GPL implementation is copied. No current boot call is authorized.
